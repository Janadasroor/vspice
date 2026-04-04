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
#include <utility>

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

QString SimulationManager::lastErrorMessage() const {
    std::lock_guard<std::mutex> lock(const_cast<SimulationManager*>(this)->m_logMutex);
    return m_lastErrorMessage;
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
    ngSpice_Command((char*)"set ngbehavior=ltps");
    ngSpice_Command((char*)"set filetype=binary");
#else
    qWarning() << "Ngspice not available (HAVE_NGSPICE not defined)";
#endif
}

void SimulationManager::runSimulation(const QString& netlist, SimControl* control) {
    if (!isAvailable()) {
        Q_EMIT errorOccurred("Simulation engine not installed.");
        return;
    }
    if (!m_isInitialized) initialize();

#ifdef HAVE_NGSPICE
    m_currentNetlist = netlist;
    {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        m_streamingControl = control;
    }
    m_vectorMap.clear();
    m_lastLoadFailed = false;
    m_bgRunIssued = false;

    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        m_simBuffer.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_logBuffer.clear();
    }
    m_streamingCounter = 0;
    m_skipFactor = 1;

    QString error;
    const bool loaded = loadNetlistInternal(netlist, true, &error);
    if (!loaded) {
        m_bufferTimer->stop();
        if (!error.isEmpty()) Q_EMIT errorOccurred(error);
        return;
    }

    Q_EMIT simulationStarted();
    qDebug() << "SimulationManager: Starting bg_run with post-run RAW capture";
    ngSpice_Command(const_cast<char*>("set filetype=binary"));
    const int rc = ngSpice_Command((char*)"bg_run");
    if (rc != 0 || m_lastLoadFailed) {
        m_bufferTimer->stop();
        m_bgRunIssued = false;
        QString finalErr = "Ngspice failed to start simulation.";
        if (!m_lastErrorMessage.isEmpty()) finalErr = m_lastErrorMessage;
        Q_EMIT errorOccurred(finalErr);
        Q_EMIT simulationFinished();
        return;
    }
    m_bgRunIssued = true;
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
    {
        std::lock_guard<std::mutex> lock(m_logMutex);
        m_lastLoadFailed = false;
        m_lastErrorMessage.clear();
    }

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
            Q_EMIT outputReceived(QString("Ngspice: failed to load circuit via ngSpice_Circ (rc=%1), falling back to source.").arg(rc));
            if (m_lastLoadFailed) {
                if (errorOut) {
                    *errorOut = m_lastErrorMessage.isEmpty() ? "Ngspice rejected the netlist during parse/model load." : m_lastErrorMessage;
                }
                if (!keepStorage) { m_circStorage.clear(); m_circPtrs.clear(); }
                return false;
            }
        }
    }

    if (!loaded) {
        QTemporaryFile temp(QDir::tempPath() + "/viospice_netlist_XXXXXX.cir");
        temp.setAutoRemove(false);
        QString sourcePath = netlist;
        if (temp.open()) {
            QTextStream out(&temp);
            for (const QString& line : lines) out << line << "\n";
            out.flush();
            temp.close();
            sourcePath = temp.fileName();
        }
        m_lastLoadFailed = false;
        QString cmd = "source \"" + sourcePath + "\"";
        const int rc = ngSpice_Command(cmd.toLatin1().data());
        QFile::remove(sourcePath);
        if (rc != 0) {
            if (errorOut) {
                *errorOut = m_lastErrorMessage.isEmpty() ? "Failed to load netlist into ngspice." : m_lastErrorMessage;
            }
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
        Q_EMIT outputReceived("Ngspice: no circuits loaded after load attempt.");
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
    {
        std::lock_guard<std::mutex> lock(m_controlMutex);
        m_streamingControl = nullptr;
    }
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

double SimulationManager::getVectorValue(const QString& name) {
#ifdef HAVE_NGSPICE
    if (!m_isInitialized) return 0.0;
    
    // We use ngGet_Vec_Info which is safe to call during simulation 
    // if ngspice is compiled with shared library support.
    pvector_info info = ngGet_Vec_Info(name.toLatin1().data());
    if (info && info->v_realdata) {
        // For transient/DC it's usually 1 point if we are paused, 
        // or we get the "current" point (the last one).
        if (info->v_length > 0) {
            return info->v_realdata[info->v_length - 1];
        }
    }
#endif
    return 0.0;
}

void SimulationManager::setParameter(const QString& name, double value) {
#ifdef HAVE_NGSPICE
    if (!m_isInitialized) return;
    
    // Use 'alter' or 'set' command depending on what it is.
    // For component values (e.g. R1), use 'alter'. 
    // For global params, use 'set'.
    QString cmd;
    if (name.contains('.')) {
        // Component parameter: R1.R = 10k
        QStringList parts = name.split('.');
        cmd = QString("alter %1 %2 = %3").arg(parts[0], parts[1], QString::number(value, 'g', 12));
    } else {
        cmd = QString("set %1=%2").arg(name, QString::number(value, 'g', 12));
    }
    
    ngSpice_Command(cmd.toLatin1().data());
#endif
}

void SimulationManager::sendInternalCommand(const QString& command) {
#ifdef HAVE_NGSPICE
    if (!m_isInitialized) return;
    ngSpice_Command(command.toLatin1().data());
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
        Q_EMIT outputReceived(msg);
    }

    std::vector<double> times;
    std::vector<std::vector<double>> valueRows;
    times.reserve(batch.size());
    valueRows.reserve(batch.size());

    for (const auto& p : batch) {
        times.push_back(p.time);
        valueRows.push_back(p.values);
    }

    QStringList names;
    for (size_t i = 1; i < m_vectorMap.size(); ++i) {
        names << m_vectorMap[i].name;
    }

    Q_EMIT realTimeDataBatchReceived(times, valueRows, names);
}

// --- Callbacks ---

int SimulationManager::cbSendChar(char* output, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && output) {
        QString msg = QString::fromLatin1(output);
        const QString lower = msg.toLower();
        // Clean up stderr/stdout distinction if needed
        if (msg.startsWith("stderr ")) msg.remove(0, 7);
        else if (msg.startsWith("stdout ")) msg.remove(0, 7);

        qDebug() << "[Ngspice]" << msg.trimmed();
        {
            std::lock_guard<std::mutex> lock(self->m_logMutex);
            if (lower.contains("no circuit loaded") ||
                lower.contains("there is no circuit") ||
                lower.contains("error on line") ||
                lower.contains("unknown model type") ||
                lower.contains("unable to find definition of model") ||
                lower.contains("mif-error") ||
                lower.contains("circuit not parsed") ||
                lower.contains("ngspice.dll cannot recover") ||
                lower.contains("could not find include file")) {
                self->m_lastLoadFailed = true;
                if (self->m_lastErrorMessage.isEmpty()) {
                    self->m_lastErrorMessage = msg.trimmed();
                }
            }
            
            // Capture specific "Error:" prefix even if not in the known failure list
            if (self->m_lastErrorMessage.isEmpty() && (msg.startsWith("Error:", Qt::CaseInsensitive) || msg.startsWith("Fatal error:", Qt::CaseInsensitive))) {
                self->m_lastErrorMessage = msg.trimmed();
            }
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
    SimControl* ctrl = nullptr;
    {
        std::lock_guard<std::mutex> lock(self->m_controlMutex);
        ctrl = self->m_streamingControl;
    }

    if (ctrl && ctrl->stopRequested) {
        ngSpice_Command((char*)"bg_halt");
        return 0;
    }


    Q_UNUSED(numStructs);
    Q_UNUSED(vecArray);
    // Standard schematic runs rely on the post-run RAW file. Avoid touching
    // ngspice's live data structures here; that callback path has been unstable
    // and is not required for correctness.
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
    if (self && finished && self->m_bgRunIssued) {
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
    if (m_bgRunIssued && !m_lastLoadFailed && !rawPath.isEmpty()) {
        ngSpice_Command((char*)"set filetype=binary");
        const QString writeCmd = "write " + rawPath;
        ngSpice_Command(writeCmd.toLatin1().data());
        Q_EMIT rawResultsReady(rawPath);
    }
#endif
    m_bgRunIssued = false;
    Q_EMIT simulationFinished();
}
