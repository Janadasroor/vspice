#include "sim_manager.h"
#include "../core/raw_data_parser.h"
#include "../../schematic/items/schematic_item.h"
#include "../../schematic/items/smart_signal_item.h"
#include "../../core/simulation_manager.h"
#include "../../schematic/analysis/spice_netlist_generator.h"
#include <QDebug>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <QDir>

SimManager& SimManager::instance() {
    static SimManager inst;
    return inst;
}

SimManager::SimManager(QObject* parent) : QObject(parent) {}

void SimManager::runDCOP(QGraphicsScene* scene, NetManager* netMgr) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    runNgspiceSimulation(scene, netMgr, config);
}

void SimManager::runTransient(QGraphicsScene* scene, NetManager* netMgr, double tStop, double tStep) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStop = tStop;
    config.tStep = tStep;
    runNgspiceSimulation(scene, netMgr, config);
}

void SimManager::runAC(QGraphicsScene* scene, NetManager* netMgr, double fStart, double fStop, int points) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = fStart;
    config.fStop = fStop;
    config.fPoints = points;
    runNgspiceSimulation(scene, netMgr, config);
}

void SimManager::runMonteCarlo(QGraphicsScene* scene, NetManager* netMgr, int runs) {
    // Ngspice support for Monte Carlo usually involves a control script loop.
    // For now, we will just run a basic OP analysis as a placeholder,
    // or we could implement a script generator.
    emit logMessage("Monte Carlo via Ngspice not fully implemented yet, running OP.");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    runNgspiceSimulation(scene, netMgr, config);
}

void SimManager::runParametricSweep(QGraphicsScene* scene, NetManager* netMgr, const QString& component, const QString& param, double start, double stop, int steps) {
    // Requires .control script loop
    emit logMessage("Parametric Sweep via Ngspice not fully implemented yet, running OP.");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    runNgspiceSimulation(scene, netMgr, config);
}

void SimManager::runSensitivity(QGraphicsScene* scene, NetManager* netMgr, const QString& targetSignal) {
     emit logMessage("Sensitivity analysis via Ngspice not fully implemented yet.");
     // Fallback to OP
     SimAnalysisConfig config;
     config.type = SimAnalysisType::OP;
     runNgspiceSimulation(scene, netMgr, config);
}

void SimManager::runNetlistText(const QString& netlistContent) {
    if (m_control) {
        emit logMessage("A simulation is already running.");
        return;
    }
    emit simulationStarted();
    emit logMessage("Running ngspice netlist...");
    startNgspiceWithNetlist(netlistContent);
}

void SimManager::runNgspiceSimulation(QGraphicsScene* scene, NetManager* netMgr, const SimAnalysisConfig& config) {
    if (m_control) {
        emit logMessage("A simulation is already running.");
        return;
    }

    emit simulationStarted();
    emit logMessage(QString("Generating ngspice netlist (Analysis: %1)...").arg(static_cast<int>(config.type)));

    // Map SimAnalysisConfig to SpiceNetlistGenerator::SimulationParams
    SpiceNetlistGenerator::SimulationParams params;
    switch (config.type) {
        case SimAnalysisType::Transient:
            params.type = SpiceNetlistGenerator::Transient;
            params.start = "0";
            params.stop = QString::number(config.tStop);
            params.step = QString::number(config.tStep);
            break;
        case SimAnalysisType::AC:
            params.type = SpiceNetlistGenerator::AC;
            params.start = QString::number(config.fStart > 0.0 ? config.fStart : 10.0, 'g', 12);
            params.stop = QString::number(config.fStop > 0.0 ? config.fStop : 1e6, 'g', 12);
            params.step = QString::number(config.fPoints > 0 ? config.fPoints : 10);
            break;
        case SimAnalysisType::OP:
        default:
            params.type = SpiceNetlistGenerator::OP;
            break;
    }

    QString netlistContent = SpiceNetlistGenerator::generate(scene, "", netMgr, params);
    startNgspiceWithNetlist(netlistContent);
}

void SimManager::startNgspiceWithNetlist(const QString& netlistContent) {
    // Create a temporary file that auto-deletes when the object is destroyed.
    // However, we need it to persist until simulation load.
    // We'll manage it via a member or just use a transient one and pass path.
    auto* tempFile = new QTemporaryFile(this);
    tempFile->setAutoRemove(false);
    if (tempFile->open()) {
        QTextStream out(tempFile);
        out << netlistContent;
        tempFile->close();
    } else {
        emit errorOccurred("Failed to create temporary netlist file.");
        delete tempFile;
        return;
    }

    m_control = new SimControl();
    m_paused = false;

    auto& sm = SimulationManager::instance();
    
    // Disconnect previous connections to avoid duplicates
    sm.disconnect(this);

    // Connect signals
    connect(&sm, &SimulationManager::outputReceived, this, &SimManager::logMessage, Qt::UniqueConnection);

    connect(&sm, &SimulationManager::realTimeDataBatchReceived, this, &SimManager::realTimeDataBatchReceived, Qt::UniqueConnection);
    
    QPointer<QTemporaryFile> safeTempFile(tempFile);
    
    // Connect RAW results ready signal - this is the "happy path" for data
    connect(&sm, &SimulationManager::realTimeDataBatchReceived, this, [this](const std::vector<double>& times, const std::vector<std::vector<double>>& values, const QStringList& names) {
        emit realTimeDataBatchReceived(times, values, names);
    });
    
    connect(&sm, &SimulationManager::rawResultsReady, this, [this, safeTempFile](const QString& path) {
        auto* watcher = new QFutureWatcher<std::pair<bool, SimResults>>(this);
        connect(watcher, &QFutureWatcher<std::pair<bool, SimResults>>::finished, this, [this, watcher, safeTempFile]() {
            auto result = watcher->result();
            watcher->deleteLater();
            
            if (result.first) {
                emit simulationFinished(result.second);
            } else {
                emit logMessage("Ngspice: Data parse error or empty results.");
            }
            
            if (safeTempFile) safeTempFile->deleteLater();
            cleanupSimulation();
        });

        watcher->setFuture(QtConcurrent::run([path]() {
            RawData rd;
            QString err;
            if (RawDataParser::loadRawAscii(path, &rd, &err)) {
                return std::make_pair(true, rd.toSimResults());
            }
            qDebug() << "RawDataParser error:" << err;
            return std::make_pair(false, SimResults());
        }));
    });
    
    // Handle the simulation engine finishing (it fires regardless of result parsing)
    connect(&sm, &SimulationManager::simulationFinished, this, [this, safeTempFile]() {
        // If we don't have results coming (e.g. error), we must clean up here.
        // If results ARE coming, rawResultsReady watcher will handle it.
        // We use a small delay or a state check to be safe.
        QTimer::singleShot(500, this, [this, safeTempFile]() {
            if (m_control) { // Still running? Then no results were ready or they failed
                if (safeTempFile) safeTempFile->deleteLater();
                cleanupSimulation();
            }
        });
    });

    sm.runSimulation(tempFile->fileName(), m_control);
}

void SimManager::cleanupSimulation() {
    if (m_control) {
        delete m_control;
        m_control = nullptr;
    }
}

void SimManager::runRealTime(QGraphicsScene* scene, NetManager* netMgr, int intervalMs) {
    // Stub for now or implement via repeated OP
    emit logMessage("Real-time simulation via Ngspice not yet optimized. Running single-shot OP.");
    runDCOP(scene, netMgr);
}

void SimManager::stopRealTime() {
    if (m_rtTimer) {
        m_rtTimer->stop();
        delete m_rtTimer;
        m_rtTimer = nullptr;
    }
    m_rtScene = nullptr;
}

void SimManager::onInteractiveStateChanged() {
    // Interactive changes handling
}

void SimManager::onRealTimeTick() {
    // Real-time tick handling
}

QStringList SimManager::preflightCheck(QGraphicsScene* scene, NetManager* netMgr, SimNetlist& outNetlist) {
    // Generate netlist via bridge just to check structure/connectivity
    outNetlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    outNetlist.flatten();
    
    QStringList diag;
    for (const auto& d : outNetlist.diagnostics()) {
        QString qd = QString::fromStdString(d);
        if (!qd.trimmed().isEmpty()) diag << qd;
    }
    return diag;
}

void SimManager::runWithNetlist(const SimNetlist& netlist) {
     emit errorOccurred("Direct SimNetlist execution not supported with Ngspice backend yet. Use UI.");
}

void SimManager::stopAll() {
    SimulationManager::instance().stopSimulation();
    cleanupSimulation();
    emit logMessage("Simulation stopped.");
}

void SimManager::pauseSimulation(bool pause) {
    // Ngspice bg_halt / bg_resume could be used
    emit logMessage("Pause not fully implemented for Ngspice.");
}
