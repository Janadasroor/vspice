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
#include <QRegularExpression>
#include <utility>

namespace {
    /**
     * Resolves a file path case-insensitively.
     * This is needed because ngspice lowercases paths, but Linux filesystems are case-sensitive.
     * If the exact path doesn't exist, we scan the directory for a case-insensitive match.
     */
    QString resolveCaseInsensitiveFilePath(const QString& path) {
        QFileInfo fi(path);
        // If the file exists exactly as specified, return it immediately.
        if (fi.exists()) return path;
        
        QDir dir(fi.absolutePath());
        if (!dir.exists()) return path;
        
        QString target = fi.fileName().toLower();
        // Scan directory for a matching filename (case-insensitive)
        const auto entries = dir.entryList(QDir::Files);
        for (const QString& entry : entries) {
            if (entry.toLower() == target) {
                return dir.absoluteFilePath(entry);
            }
        }
        return path; // Return original if not found
    }

    QString normalizeStreamVectorName(const QString& rawName) {
        const QString q = rawName.trimmed();
        if (q.isEmpty()) return rawName;

        static const QRegularExpression branchRe(
            "^\\s*([A-Za-z0-9_.$:+-]+)\\s*#\\s*branch\\s*$",
            QRegularExpression::CaseInsensitiveOption);
        if (const auto m = branchRe.match(q); m.hasMatch()) {
            return QString("I(%1)").arg(m.captured(1).toUpper());
        }

        static const QRegularExpression deviceCurrentRe(
            "^@\\s*([A-Za-z0-9_.$:+-]+)\\s*\\[\\s*i[a-z]*\\s*\\]$",
            QRegularExpression::CaseInsensitiveOption);
        if (const auto m = deviceCurrentRe.match(q); m.hasMatch()) {
            return QString("I(%1)").arg(m.captured(1).toUpper());
        }

        static const QRegularExpression wrapperRe(
            "^(v|i)\\s*\\(\\s*(.+)\\s*\\)$",
            QRegularExpression::CaseInsensitiveOption);
        if (const auto m = wrapperRe.match(q); m.hasMatch()) {
            return QString("%1(%2)")
                .arg(m.captured(1).toUpper(), m.captured(2).trimmed().toUpper());
        }

        return q;
    }
}

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
    // Set environment variables for ngspice to find spinit and code models
    // This must be done BEFORE ngSpice_Init
    QString scriptsPath = "/home/jnd/.ngspice";
    if (qEnvironmentVariableIsEmpty("SPICE_SCRIPTS")) {
        qputenv("SPICE_SCRIPTS", scriptsPath.toUtf8());
    }
    if (qEnvironmentVariableIsEmpty("SPICE_LIB_DIR")) {
        qputenv("SPICE_LIB_DIR", scriptsPath.toUtf8());
    }

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
    m_stopRequested = false;
    m_pauseRequested = false;

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
    if (control) {
        m_bufferTimer->start();
    } else {
        m_bufferTimer->stop();
    }

    QString error;
    const bool loaded = loadNetlistInternal(netlist, true, &error);
    if (!loaded) {
        m_bufferTimer->stop();
        if (!error.isEmpty()) Q_EMIT errorOccurred(error);
        return;
    }

    Q_EMIT simulationStarted();
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
    auto emitNumberedNetlist = [this](const QStringList& numberedLines, const QString& header) {
        Q_EMIT outputReceived(header);
        for (int i = 0; i < numberedLines.size(); ++i) {
            Q_EMIT outputReceived(QString("%1: %2").arg(i + 1, 4).arg(numberedLines.at(i)));
        }
    };

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

        // Fix WAV file paths: ngspice lowercases paths which breaks case-sensitive filesystems (Linux).
        // We find WAVEFILE entries and resolve the correct case from the disk before passing to ngspice.
        QDir baseDir = QFileInfo(netlist).absoluteDir();
        QRegularExpression wavRe(R"REGEX(WAVEFILE\s*=?\s*"([^"]+)")REGEX", QRegularExpression::CaseInsensitiveOption);

        for (int i = 0; i < lines.size(); ++i) {
            auto match = wavRe.match(lines[i]);
            if (match.hasMatch()) {
                QString rawPath = match.captured(1);
                QString fullPath = QFileInfo(rawPath).isAbsolute() ? rawPath : baseDir.absoluteFilePath(rawPath);
                QString resolved = resolveCaseInsensitiveFilePath(fullPath);
                
                // If we found a case-insensitive match that differs from the input, replace it in the line
                if (resolved != fullPath) {
                    lines[i].replace(rawPath, resolved);
                    qInfo() << "[SimulationManager] Auto-corrected WAV path case:" << resolved;
                }
            }
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
            const QString reason = m_lastErrorMessage.isEmpty()
                ? QString("rc=%1").arg(rc)
                : QString("rc=%1, last error: %2").arg(rc).arg(m_lastErrorMessage);
            Q_EMIT outputReceived(QString("Ngspice: failed to load circuit via ngSpice_Circ (%1), falling back to source.").arg(reason));
            emitNumberedNetlist(lines, "[SIM_DEBUG] Shared-ngspice numbered netlist:");
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
            emitNumberedNetlist(lines, "[SIM_DEBUG] File-source numbered netlist after load failure:");
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
    m_stopRequested = true;
    m_pauseRequested = false;
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
    if (command == "bg_halt") {
        m_pauseRequested = true;
        m_stopRequested = false;
        QMetaObject::invokeMethod(m_bufferTimer, "stop", Qt::QueuedConnection);
    } else if (command == "bg_resume") {
        m_pauseRequested = false;
        m_stopRequested = false;
        if (m_streamingControl) {
            QMetaObject::invokeMethod(m_bufferTimer, "start", Qt::QueuedConnection);
        }
    }
    ngSpice_Command(command.toLatin1().data());
#endif
}

// --- Real-Time Switch Control ---

void SimulationManager::alterSwitch(const QString& switchRef, bool open, double vt, double vh) {
#ifdef HAVE_NGSPICE
    if (!m_isInitialized) return;

    // VioSpice implements switches as resistors, not voltage-controlled switches.
    // Netlist generator creates: R<ref> n1 n2 <value>
    //   Open state:  1e12 ohms (high resistance)
    //   Closed state: 0.001 ohms (low resistance)
    QString rName = switchRef;
    if (!rName.startsWith("R", Qt::CaseInsensitive)) rName = "R" + rName;

    double resistance = open ? 1e12 : 0.001;
    alterSwitchResistance(rName, resistance);
#endif
}

void SimulationManager::alterSwitchResistance(const QString& resistorName, double resistance) {
#ifdef HAVE_NGSPICE
    if (!m_isInitialized) return;

    // Set flag to prevent simulationFinished() from being emitted
    // when the background thread stops due to bg_halt
    m_switchToggleInProgress = true;

    // 1. Halt the simulation (pauses, doesn't fully stop)
    sendInternalCommand("bg_halt");

    // Small delay to ensure simulation has stopped
    QThread::msleep(20);

    // 2. Alter the resistor value
    // ngspice syntax: alter Rname R=value
    QString cmd = QString("alter %1 R=%2").arg(resistorName, QString::number(resistance, 'g', 12));
    ngSpice_Command(cmd.toLatin1().data());

    // 3. Resume simulation (bg_resume continues from paused state, bg_run would restart)
    sendInternalCommand("bg_resume");

    // Clear flag after resume
    m_switchToggleInProgress = false;
#endif
}

void SimulationManager::alterSwitchVoltage(const QString& controlSourceName, double voltage) {
#ifdef HAVE_NGSPICE
    if (!m_isInitialized) return;

    // Use bg_halt -> alter -> bg_run cycle for voltage-controlled switches
    // (for advanced users who use .model SW with VSWITCH)

    // 1. Halt the simulation
    sendInternalCommand("bg_halt");

    // Small delay to ensure simulation has stopped
    QThread::msleep(10);

    // 2. Alter the control voltage source
    QString cmd = QString("alter %1 DC=%2").arg(controlSourceName, QString::number(voltage, 'g', 12));
    ngSpice_Command(cmd.toLatin1().data());

    // 3. Resume simulation
    sendInternalCommand("bg_resume");
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
    for (const auto& vector : m_vectorMap) {
        if (vector.isScale) continue;
        names << vector.name;
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

        {
            std::lock_guard<std::mutex> lock(self->m_logMutex);
            const bool isError =
                lower.contains("no circuit loaded") ||
                lower.contains("there is no circuit") ||
                lower.contains("error on line") ||
                lower.contains("unknown model type") ||
                lower.contains("unable to find definition of model") ||
                lower.contains("mif-error") ||
                lower.contains("circuit not parsed") ||
                lower.contains("ngspice.dll cannot recover") ||
                lower.contains("could not find include file");
            const bool isWarning = lower.contains("warning");

            if (isError) {
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

            if (isError || isWarning) {
                qWarning() << "[Ngspice]" << msg.trimmed();
            }
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
    Q_UNUSED(numStructs);
    Q_UNUSED(id);

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

    if (!ctrl || vecArray->veccount <= 1 || !vecArray->vecsa) {
        return 0;
    }

    if (++self->m_streamingCounter % self->m_skipFactor != 0) {
        return 0;
    }

    std::vector<double> sampleValues;
    sampleValues.reserve(static_cast<size_t>(vecArray->veccount - 1));
    double timeValue = 0.0;
    bool haveScale = false;

    for (int i = 0; i < vecArray->veccount; ++i) {
        pvecvalues value = vecArray->vecsa[i];
        if (!value) continue;
        if (value->is_scale && !haveScale) {
            timeValue = value->creal;
            haveScale = true;
            continue;
        }
        sampleValues.push_back(value->creal);
    }

    if (!haveScale || sampleValues.empty()) {
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock(self->m_bufferMutex);
        self->m_simBuffer.push_back({timeValue, std::move(sampleValues)});
        if (self->m_simBuffer.size() > 2000) {
            self->m_simBuffer.erase(self->m_simBuffer.begin(),
                                    self->m_simBuffer.begin() + static_cast<std::ptrdiff_t>(self->m_simBuffer.size() - 2000));
        }
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
        vm.name = normalizeStreamVectorName(QString::fromLatin1(v->vecname));
        vm.isVoltage = (v->is_real && !vm.name.toLower().startsWith("i("));
        vm.isScale = (v->pdvec != nullptr && v->pdvec == v->pdvecscale);
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
        // If we're in the middle of a switch toggle, don't emit simulationFinished.
        // The simulation will be resumed immediately via bg_run.
        if (self->m_switchToggleInProgress) {
            QMetaObject::invokeMethod(self->m_bufferTimer, "stop", Qt::QueuedConnection);
            QMetaObject::invokeMethod(self, "processBufferedData", Qt::QueuedConnection);
            return 0;
        }
        // Normal completion or stop: write raw file and emit signals
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
    m_stopRequested = false;
    m_pauseRequested = false;
    Q_EMIT simulationFinished();
}
