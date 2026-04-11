#include "SpiceWorkerThread.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QRegularExpression>

namespace {
bool isDigitalVectorName(const QString& name) {
    const QString lower = name.trimmed().toLower();
    return lower.startsWith("d(") || lower.startsWith("digital") || lower.contains("#branch_digital");
}
}

SpiceWorkerThread::SpiceWorkerThread(QObject* parent)
    : QThread(parent) {
}

SpiceWorkerThread::~SpiceWorkerThread() {
    stopSimulation();
    wait();
}

void SpiceWorkerThread::submitNetlist(const QString& netlistText) {
    {
        QMutexLocker locker(&m_commandMutex);
        m_pendingNetlist = netlistText;
        m_netlistPending.store(true);
    }
    if (!isRunning()) {
        start();
    }
    m_commandReady.wakeOne();
}

void SpiceWorkerThread::stopSimulation() {
    m_stopRequested.store(true);
#ifdef HAVE_NGSPICE
    ngSpice_Command(const_cast<char*>("bg_halt"));
#endif
    m_commandReady.wakeOne();
}

void SpiceWorkerThread::run() {
#ifdef HAVE_NGSPICE
    ngSpice_Init(cbSendChar,
                 cbSendStat,
                 cbControlledExit,
                 cbSendData,
                 cbSendInitData,
                 cbBGThreadRunning,
                 this);
    ngSpice_Command(const_cast<char*>("set filetype=ascii"));

    while (!isInterruptionRequested()) {
        QString netlistText;
        {
            QMutexLocker locker(&m_commandMutex);
            if (!m_netlistPending.load() && !m_stopRequested.load()) {
                m_commandReady.wait(&m_commandMutex, 50);
            }
            if (m_stopRequested.load()) {
                break;
            }
            if (!m_netlistPending.load()) {
                flushLogs();
                flushDigitalStates();
                continue;
            }

            netlistText = m_pendingNetlist;
            m_pendingNetlist.clear();
            m_netlistPending.store(false);
        }

        resetRunState();
        Q_EMIT simulationStarted();

        if (!loadCircuit(netlistText)) {
            Q_EMIT simulationError(QStringLiteral("Failed to load netlist into ngspice."));
            continue;
        }

        m_ngspiceRunning.store(true);
        m_wallStartMs = QDateTime::currentMSecsSinceEpoch();
        ngSpice_Command(const_cast<char*>("bg_run"));

        while (m_ngspiceRunning.load() && !m_stopRequested.load()) {
            flushLogs();
            flushDigitalStates();
            msleep(16);
        }

        flushLogs();
        flushDigitalStates();
        Q_EMIT simulationStopped();

        if (m_stopRequested.load()) {
            break;
        }
    }
#else
    Q_EMIT simulationError(QStringLiteral("Ngspice shared library support is not available in this build."));
#endif
}

void SpiceWorkerThread::resetRunState() {
    QMutexLocker locker(&m_stateMutex);
    m_digitalStates.clear();
    m_logQueue.clear();
    m_latestSimTime = 0.0;
    m_stopRequested.store(false);
}

void SpiceWorkerThread::wallClockThrottle(double simulationTimeSeconds) {
    m_latestSimTime = simulationTimeSeconds;
    const qint64 targetElapsedMs = static_cast<qint64>(simulationTimeSeconds * 1000.0);
    const qint64 actualElapsedMs = QDateTime::currentMSecsSinceEpoch() - m_wallStartMs;
    if (targetElapsedMs > actualElapsedMs) {
        msleep(static_cast<unsigned long>(targetElapsedMs - actualElapsedMs));
    }
}

void SpiceWorkerThread::flushDigitalStates() {
    QVector<DigitalNodeUpdate> batch;
    {
        QMutexLocker locker(&m_stateMutex);
        batch.reserve(m_digitalStates.size());
        for (auto it = m_digitalStates.cbegin(); it != m_digitalStates.cend(); ++it) {
            batch.push_back({it.key(), it.value().state, it.value().simulationTime});
        }
        m_digitalStates.clear();
    }

    if (!batch.isEmpty()) {
        Q_EMIT digitalStatesReady(batch);
    }
}

void SpiceWorkerThread::flushLogs() {
    QStringList logs;
    {
        QMutexLocker locker(&m_stateMutex);
        while (!m_logQueue.isEmpty()) {
            logs << m_logQueue.dequeue();
        }
    }

    for (const QString& line : logs) {
        Q_EMIT logMessage(line);
    }
}

bool SpiceWorkerThread::loadCircuit(const QString& netlistText) {
#ifdef HAVE_NGSPICE
    ngSpice_Command(const_cast<char*>("reset"));

    // ngSpice_Circ expects a char** where each entry is a single line,
    // and the last entry is NULL.
    QString processed = netlistText;
    processed.remove('\r');
    QStringList lines = processed.split('\n');
    m_lineStorage.clear();
    m_rawLines.clear();

    for (const QString& line : lines) {
        m_lineStorage.append(line.toUtf8());
    }
    for (int i = 0; i < m_lineStorage.size(); ++i) {
        m_rawLines.push_back(m_lineStorage[i].data());
    }
    m_rawLines.push_back(nullptr);

    const int rc = ngSpice_Circ(m_rawLines.data());
    return rc == 0;
#else
    Q_UNUSED(netlistText);
    return false;
#endif
}

void SpiceWorkerThread::publishDigitalValue(const QString& nodeName, double value, double simulationTime) {
    QMutexLocker locker(&m_stateMutex);
    m_digitalStates.insert(nodeName, BufferedState{decodeDigitalValue(value), simulationTime});
}

SpiceWorkerThread::DigitalState SpiceWorkerThread::decodeDigitalValue(double value) {
    if (value <= 0.25) {
        return DigitalState::Low;
    }
    if (value >= 0.75) {
        return DigitalState::High;
    }
    return DigitalState::Unknown;
}

int SpiceWorkerThread::cbSendChar(char* output, int, void* userData) {
    auto* self = static_cast<SpiceWorkerThread*>(userData);
    if (!self || !output) {
        return 0;
    }

    QMutexLocker locker(&self->m_stateMutex);
    self->m_logQueue.enqueue(QString::fromLocal8Bit(output).trimmed());
    return 0;
}

int SpiceWorkerThread::cbSendStat(char* stat, int, void* userData) {
    auto* self = static_cast<SpiceWorkerThread*>(userData);
    if (!self || !stat) {
        return 0;
    }

    const QString line = QString::fromLocal8Bit(stat);
    QRegularExpression timePattern(QStringLiteral("time\\s*=\\s*([0-9eE+\\-.]+)"));
    const QRegularExpressionMatch match = timePattern.match(line);
    if (match.hasMatch()) {
        self->wallClockThrottle(match.captured(1).toDouble());
    }
    return 0;
}

int SpiceWorkerThread::cbControlledExit(int, bool, bool, int, void* userData) {
    auto* self = static_cast<SpiceWorkerThread*>(userData);
    if (self) {
        self->m_ngspiceRunning.store(false);
    }
    return 0;
}

#ifdef HAVE_NGSPICE
int SpiceWorkerThread::cbSendData(pvecvaluesall vecArray, int numStructs, int, void* userData) {
    auto* self = static_cast<SpiceWorkerThread*>(userData);
    if (!self || !vecArray || numStructs <= 0) {
        return 0;
    }

    double simulationTime = 0.0;
    if (vecArray->vecsa[0]) {
        simulationTime = vecArray->vecsa[0]->creal;
    }
    self->wallClockThrottle(simulationTime);

    for (int i = 1; i < numStructs; ++i) {
        const pvecvalues value = vecArray->vecsa[i];
        if (!value || !value->name) {
            continue;
        }

        const QString vectorName = QString::fromLatin1(value->name);
        if (!isDigitalVectorName(vectorName)) {
            continue;
        }

        self->publishDigitalValue(vectorName, value->creal, simulationTime);
    }
    return 0;
}

int SpiceWorkerThread::cbSendInitData(pvecinfoall, int, void* userData) {
    Q_UNUSED(userData);
    return 0;
}
#else
int SpiceWorkerThread::cbSendData(void*, int, int, void*) {
    return 0;
}

int SpiceWorkerThread::cbSendInitData(void*, int, void*) {
    return 0;
}
#endif

int SpiceWorkerThread::cbBGThreadRunning(bool finished, int, void* userData) {
    auto* self = static_cast<SpiceWorkerThread*>(userData);
    if (self && finished) {
        self->m_ngspiceRunning.store(false);
    }
    return 0;
}
