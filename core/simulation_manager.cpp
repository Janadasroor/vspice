#include "simulation_manager.h"
#include "simulator/core/sim_results.h"
#include <QDebug>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include <QTextStream>
#include <QMetaObject>

SimulationManager& SimulationManager::instance() {
    static SimulationManager instance;
    return instance;
}

SimulationManager::SimulationManager(QObject* parent)
    : QObject(parent), m_isInitialized(false) {
    m_bufferTimer = new QTimer(this);
    m_bufferTimer->setInterval(33); // ~30 FPS
    connect(m_bufferTimer, &QTimer::timeout, this, &SimulationManager::processBufferedData);
}

SimulationManager::~SimulationManager() {
}

bool SimulationManager::isAvailable() const {
#ifdef HAVE_NGSPICE
    return true;
#else
    return false;
#endif
}

void SimulationManager::initialize() {
    if (m_isInitialized) return;

#ifdef HAVE_NGSPICE
    // Initialize ngspice with our callbacks
    // we pass 'this' as userData to route callbacks back to the instance
    ngSpice_Init(
        cbSendChar,
        cbSendStat,
        cbControlledExit,
        cbSendData,
        cbSendInitData,
        cbBGThreadRunning,
        this
    );
    m_isInitialized = true;
    qDebug() << "Ngspice initialized";
    ngSpice_Command((char*)"set filetype=ascii");
#else
    qWarning() << "Ngspice not available (HAVE_NGSPICE not defined)";
#endif
}

void SimulationManager::runSimulation(const QString& netlist, SimControl* control) {
    if (!isAvailable()) {
        emit errorOccurred("Simulation engine not installed.");
        return;
    }
    if (!m_isInitialized) initialize();

#ifdef HAVE_NGSPICE
    m_currentNetlist = netlist;
    m_streamingControl = control;
    m_vectorMap.clear();

    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_simBuffer.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logBuffer.clear();
    }
    m_bufferTimer->start();

    emit simulationStarted();

    QString error;
    const bool loaded = loadNetlistInternal(netlist, true, &error);
    if (!loaded) {
        if (!error.isEmpty()) emit errorOccurred(error);
        return;
    }
    ngSpice_Command(const_cast<char*>("set filetype=binary"));
    ngSpice_Command((char*)"bg_run");
#endif
}

bool SimulationManager::validateNetlist(const QString& netlist, QString* errorOut) {
    if (!isAvailable()) {
        if (errorOut) *errorOut = "Simulation engine not installed.";
        return false;
    }
    if (!m_isInitialized) initialize();
#ifdef HAVE_NGSPICE
    m_currentNetlist = netlist;
    QString error;
    const bool loaded = loadNetlistInternal(netlist, false, &error);
    if (!loaded && errorOut) *errorOut = error;
    return loaded;
#else
    if (errorOut) *errorOut = "Ngspice not available in this build.";
    return false;
#endif
}

bool SimulationManager::loadNetlistInternal(const QString& netlist, bool keepStorage, QString* errorOut) {
#ifdef HAVE_NGSPICE
    ngSpice_Command((char*)"reset");
    m_lastLoadFailed = false;

    bool loaded = false;
    QFile file(netlist);
    QStringList lines;
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            lines << in.readLine();
        }
        file.close();

        if (!lines.isEmpty() && lines.first().startsWith(QChar(0xFEFF))) {
            lines.first().remove(0, 1);
        }

        int firstNonEmpty = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (!lines.at(i).trimmed().isEmpty()) { firstNonEmpty = i; break; }
        }
        if (firstNonEmpty >= 0) {
            const QString head = lines.at(firstNonEmpty).trimmed();
            if (head.startsWith(".") || head.startsWith("*")) {
                lines.insert(firstNonEmpty, "Viospice Netlist");
            }
        } else {
            lines << "Viospice Netlist";
        }

        QStringList filtered;
        bool inControl = false;
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed().toLower();
            if (trimmed.startsWith(".control")) { inControl = true; continue; }
            if (inControl) {
                if (trimmed.startsWith(".endc")) inControl = false;
                continue;
            }
            filtered << line;
        }
        lines = filtered;

        bool hasEnd = false;
        for (int i = lines.size() - 1; i >= 0; --i) {
            const QString trimmed = lines.at(i).trimmed();
            if (trimmed.isEmpty()) continue;
            hasEnd = trimmed.toLower().startsWith(".end");
            break;
        }
        if (!hasEnd) lines << ".end";

        m_circStorage.clear();
        m_circPtrs.clear();
        m_circStorage.reserve(static_cast<size_t>(lines.size() + 1));
        m_circPtrs.reserve(static_cast<size_t>(lines.size() + 1));
        for (const QString& line : lines) {
            m_circStorage.push_back(line.toLatin1());
            m_circPtrs.push_back(m_circStorage.back().data());
        }
        m_circPtrs.push_back(nullptr);

        const int rc = ngSpice_Circ(m_circPtrs.data());
        loaded = (rc == 0 && !m_lastLoadFailed);
        if (!loaded) {
            emit outputReceived(QString("Ngspice: failed to load circuit via ngSpice_Circ (rc=%1), falling back to source.").arg(rc));
        }
    }

    if (!loaded) {
        QTemporaryFile temp(QDir::tempPath() + "/viospice_netlist_XXXXXX.cir");
        QString sourcePath = netlist;
        if (temp.open()) {
            QTextStream out(&temp);
            for (const QString& line : lines) out << line << "\n";
            out.flush();
            sourcePath = temp.fileName();
        }
        m_lastLoadFailed = false;
        QString cmd = "source \"" + sourcePath + "\"";
        const int rc = ngSpice_Command(cmd.toLatin1().data());
        if (rc != 0) {
            emit outputReceived(QString("Ngspice: source command failed (rc=%1).").arg(rc));
            if (errorOut) *errorOut = "Failed to load netlist into ngspice.";
            if (!keepStorage) { m_circStorage.clear(); m_circPtrs.clear(); }
            return false;
        }
        loaded = (rc == 0 && !m_lastLoadFailed);
    }

    if (!loaded && !file.exists()) {
        if (errorOut) *errorOut = "Netlist file not found.";
        if (!keepStorage) { m_circStorage.clear(); m_circPtrs.clear(); }
        return false;
    }

    if (!loaded) {
        emit outputReceived("Ngspice: no circuits loaded after load attempt.");
        if (errorOut) *errorOut = "No circuits loaded. Check netlist syntax.";
        if (!keepStorage) { m_circStorage.clear(); m_circPtrs.clear(); }
        return false;
    }

    if (!keepStorage) {
        m_circStorage.clear();
        m_circPtrs.clear();
    }
    return true;
#else
    if (errorOut) *errorOut = "Ngspice not available in this build.";
    return false;
#endif
}

void SimulationManager::stopSimulation() {
#ifdef HAVE_NGSPICE
    ngSpice_Command((char*)"bg_halt");
    QMetaObject::invokeMethod(m_bufferTimer, "stop", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "processBufferedData", Qt::QueuedConnection);
#endif
}

void SimulationManager::shutdown() {
#ifdef HAVE_NGSPICE
    ngSpice_Command((char*)"bg_halt");
    ngSpice_Command((char*)"quit");
    QMetaObject::invokeMethod(m_bufferTimer, "stop", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "processBufferedData", Qt::QueuedConnection);
    m_isInitialized = false;
#endif
}

void SimulationManager::processBufferedData() {
    std::vector<SimDataPoint> batch;
    std::vector<QString> logBatch;
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (!m_simBuffer.empty()) {
            m_simBuffer.swap(batch);
        }
    }
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        if (!m_logBuffer.empty()) {
            m_logBuffer.swap(logBatch);
        }
    }

    for (const QString& msg : logBatch) {
        emit outputReceived(msg);
    }

    if (batch.empty()) return;

    std::vector<double> times;
    std::vector<std::vector<double>> valueRows;
    times.reserve(batch.size());
    valueRows.reserve(batch.size());

    for (const auto& p : batch) {
        times.push_back(p.time);
        valueRows.push_back(p.values);
    }

    emit realTimeDataBatchReceived(times, valueRows);
}

// --- Callbacks ---

int SimulationManager::cbSendChar(char* output, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && output) {
        QString msg = QString::fromLatin1(output);
        // Clean up stderr/stdout distinction if needed
        if (msg.startsWith("stderr ")) msg.remove(0, 7);
        else if (msg.startsWith("stdout ")) msg.remove(0, 7);

        const QString lower = msg.toLower();
        if (lower.contains("no circuit loaded") || lower.contains("there is no circuit")) {
            self->m_lastLoadFailed = true;
        }
        
        qDebug() << "[Ngspice]" << msg.trimmed();
        {
            std::lock_guard<std::mutex> lock(self->m_logMutex);
            self->m_logBuffer.push_back(msg);
        }
    }
    return 0;
}

int SimulationManager::cbSendStat(char* stat, int id, void* userData) {
    // Progress updates - Throttled
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && stat) {
        static int throttleCounter = 0;
        if (++throttleCounter % 20 != 0) return 0; // Skip 95% of stats

        QString msg = QString::fromLatin1(stat);
        // Usually contains "% complete" or time info
        {
            std::lock_guard<std::mutex> lock(self->m_logMutex);
            self->m_logBuffer.push_back(msg);
        }
    }
    return 0;
}

int SimulationManager::cbControlledExit(int status, bool immediate, bool quit, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self) {
        qDebug() << "Ngspice exit request:" << status << " Thread:" << QThread::currentThreadId();
    }
    return 0;
}

#ifdef HAVE_NGSPICE
int SimulationManager::cbSendData(pvecvaluesall vecArray, int numStructs, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (!self || !vecArray) return 0;

    // Check for user abort
    if (self->m_streamingControl && self->m_streamingControl->stopRequested) {
        ngSpice_Command((char*)"bg_halt");
        return 0;
    }


    double time = 0.0;
    std::vector<double> values;
    values.reserve(numStructs);

    for (int i = 0; i < numStructs; ++i) {
        pvecvalues v = vecArray->vecsa[i];
        if (!v) continue;

        double val = v->creal;
        
        // The first vector in transient analysis is usually 'time'
        if (i == 0) {
            time = val;
        } else {
            values.push_back(val);
        }
    }

    // Forward to internal callback if registered
    if (self->m_streamingControl && self->m_streamingControl->streamingCallback) {
        self->m_streamingControl->streamingCallback(time, values);
    }

    // Buffer for UI/Oscilloscope
    {
        std::lock_guard<std::mutex> lock(self->m_bufferMutex);
        self->m_simBuffer.push_back({time, values});
    }

    return 0;
}

int SimulationManager::cbSendInitData(pvecinfoall initData, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (!self || !initData) return 0;

    self->m_vectorMap.clear();
    for (int i = 0; i < initData->veccount; ++i) {
        pvecinfo v = initData->vecs[i];
        if (!v) continue;
        
        VectorMap vm;
        vm.index = i;
        vm.name = QString::fromLatin1(v->vecname);
        vm.isVoltage = (v->is_real && !vm.name.toLower().startsWith("i("));
        self->m_vectorMap.push_back(vm);
    }
    
    qDebug() << "Ngspice: streaming initialized with" << initData->veccount << "vectors.";
    return 0;
}
#else
int SimulationManager::cbSendData(void* vecArray, int numStructs, int id, void* userData) {
    return 0;
}

int SimulationManager::cbSendInitData(void* initData, int id, void* userData) {
    return 0;
}
#endif

int SimulationManager::cbBGThreadRunning(bool finished, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && finished) {
        QFileInfo info(self->m_currentNetlist);
        const QString rawPath = info.absolutePath() + "/" + info.completeBaseName() + ".raw";
        QMetaObject::invokeMethod(self, "handleSimulationFinished", Qt::QueuedConnection, Q_ARG(QString, rawPath));
    }
    return 0;
}

void SimulationManager::handleSimulationFinished(const QString& rawPath) {
    m_bufferTimer->stop();
    processBufferedData(); // Flush remaining
#ifdef HAVE_NGSPICE
    if (!rawPath.isEmpty()) {
        const QString writeCmd = "write " + rawPath;
        ngSpice_Command(writeCmd.toLatin1().data());
        emit rawResultsReady(rawPath);
    }
#endif
    emit simulationFinished();
}
