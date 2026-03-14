#include "sim_manager.h"
#include "../../schematic/items/schematic_item.h"
#include "../../schematic/items/smart_signal_item.h"
#include <QDebug>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>

SimManager& SimManager::instance() {
    static SimManager inst;
    return inst;
}

SimManager::SimManager(QObject* parent) : QObject(parent) {}

void SimManager::runDCOP(QGraphicsScene* scene, NetManager* netMgr) {
    emit simulationStarted();
    emit logMessage("Building netlist from schematic...");

    SimNetlist netlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    emit logMessage(QString("Simulating circuit with %1 nodes and %2 components...")
                    .arg(netlist.nodeCount())
                    .arg(netlist.components().size()));

    SimEngine engine;
    SimResults results = engine.run(netlist);

    if (results.nodeVoltages.empty() && netlist.nodeCount() > 1) {
        if (!results.fixSuggestions.empty()) {
            emit logMessage(QString("Convergence hint: %1").arg(QString::fromStdString(results.fixSuggestions.front())));
        }
        emit errorOccurred("Simulation failed to converge or matrix is singular.");
        return;
    }

    emit logMessage("Simulation successful.");
    emit simulationFinished(results);
}

void SimManager::runTransient(QGraphicsScene* scene, NetManager* netMgr, double tStop, double tStep) {
    if (m_control) {
        emit logMessage("A simulation is already running.");
        return;
    }

    emit simulationStarted();
    emit logMessage(QString("Building netlist for transient analysis (Stop: %1s, Step: %2s)...").arg(tStop).arg(tStep));

    m_control = new SimControl();
    m_paused = false;

    // Offload to background thread (including netlist building)
    QFuture<SimResults> future = QtConcurrent::run([scene, netMgr, tStop, tStep, this]() {
        SimNetlist netlist = SimSchematicBridge::buildNetlist(scene, netMgr);
        
        SimAnalysisConfig config;
        config.type = SimAnalysisType::Transient;
        config.tStart = 0;
        config.tStop = tStop;
        config.tStep = tStep;
        config.transientStorageMode = SimTransientStorageMode::AutoDecimate;
        config.transientMaxStoredPoints = 50000;
        netlist.setAnalysis(config);

        SimEngine engine;
        return engine.run(netlist, m_control);
    });

    auto* watcher = new QFutureWatcher<SimResults>(this);
    connect(watcher, &QFutureWatcher<SimResults>::finished, this, [this, watcher]() {
        SimResults results = watcher->result();
        
        delete m_control;
        m_control = nullptr;
        m_paused = false;
        emit simulationPaused(false);

        if (results.waveforms.empty()) {
            if (!results.fixSuggestions.empty()) {
                emit logMessage(QString("Convergence hint: %1").arg(QString::fromStdString(results.fixSuggestions.front())));
            }
            emit errorOccurred("Transient simulation failed or stopped.");
        } else {
            emit logMessage(QString("Simulation finished with %1 waveforms.").arg(results.waveforms.size()));
            emit simulationFinished(results);
        }
        
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void SimManager::runAC(QGraphicsScene* scene, NetManager* netMgr, double fStart, double fStop, int points) {
    emit simulationStarted();
    emit logMessage(QString("Building netlist for AC analysis (%1Hz to %2Hz, %3 pts)...").arg(fStart).arg(fStop).arg(points));

    SimNetlist netlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = fStart;
    config.fStop = fStop;
    config.fPoints = points;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    if (results.waveforms.empty()) {
        emit errorOccurred("AC simulation failed (no data generated).");
        return;
    }

    emit logMessage(QString("Simulation finished with %1 waveforms.").arg(results.waveforms.size()));
    emit simulationFinished(results);
}

void SimManager::runMonteCarlo(QGraphicsScene* scene, NetManager* netMgr, int runs) {
    emit simulationStarted();
    emit logMessage(QString("Building netlist for Monte Carlo analysis (%1 runs)...").arg(runs));

    SimNetlist netlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    
    // Add default tolerances if none exist
    auto& comps = netlist.mutableComponents();
    for (auto& comp : comps) {
        if (comp.type == SimComponentType::Resistor) {
            if (!comp.tolerances.count("resistance")) comp.tolerances["resistance"] = { 0.05, ToleranceDistribution::Uniform };
        } else if (comp.type == SimComponentType::Capacitor) {
            if (!comp.tolerances.count("capacitance")) comp.tolerances["capacitance"] = { 0.10, ToleranceDistribution::Uniform };
        }
    }

    SimAnalysisConfig config;
    config.type = SimAnalysisType::MonteCarlo;
    config.mcRuns = runs;
    config.mcBaseAnalysis = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    emit logMessage(QString("Monte Carlo finished with %1 traces.").arg(results.waveforms.size()));
    emit simulationFinished(results);
}

void SimManager::runParametricSweep(QGraphicsScene* scene, NetManager* netMgr, const QString& component, const QString& param, double start, double stop, int steps) {
    emit simulationStarted();
    emit logMessage(QString("Building netlist for Parametric Sweep (%1: %2 from %3 to %4)...").arg(component).arg(param).arg(start).arg(stop));

    SimNetlist netlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    SimAnalysisConfig config;
    config.type = SimAnalysisType::ParametricSweep;
    config.sweepParam = component.toStdString() + "." + param.toStdString();
    config.sweepStart = start;
    config.sweepStop = stop;
    config.sweepPoints = std::max(1, steps);
    config.sweepParallelism = 0; // auto
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    if (results.waveforms.empty()) {
        emit errorOccurred(QString("Parametric Sweep Error: no traces generated for '%1'.").arg(component));
        return;
    }

    emit logMessage(QString("Parametric Sweep finished with %1 traces.").arg(results.waveforms.size()));
    emit simulationFinished(results);
}

#include <QTimer>

void SimManager::runSensitivity(QGraphicsScene* scene, NetManager* netMgr, const QString& targetSignal) {
    emit simulationStarted();
    emit logMessage(QString("Building netlist for Sensitivity analysis (Target: %1)...").arg(targetSignal));

    SimNetlist netlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Sensitivity;
    config.sensitivityTargetSignal = targetSignal.toStdString();
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    emit logMessage(QString("Sensitivity finished."));
    emit simulationFinished(results);
}

void SimManager::runRealTime(QGraphicsScene* scene, NetManager* netMgr, int intervalMs) {
    stopRealTime();
    
    m_rtScene = scene;
    m_rtNetMgr = netMgr;
    m_rtCurrentTime = 0.0;
    
    emit simulationStarted();
    emit logMessage(QString("Starting real-time interactive simulation (%1ms interval)...").arg(intervalMs));

    m_rtTimer = new QTimer(this);
    m_rtTimer->setObjectName("RealTimeTimer");
    
    connect(m_rtTimer, &QTimer::timeout, this, &SimManager::onRealTimeTick);

    // Hook into interactive items
    for (auto* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->isInteractive()) {
                connect(si, &SchematicItem::interactiveStateChanged, this, &SimManager::onInteractiveStateChanged, Qt::UniqueConnection);
            }
        }
    }

    m_rtTimer->start(intervalMs); 
    m_rtPending = true; // Initial run
}

void SimManager::onRealTimeTick() {
    if (!m_rtScene) return;
    
    // For RealTime mode, we usually simulate a small time step even if no interaction occurred
    // to allow for dynamic effects (if any).
    m_rtCurrentTime += 0.05; // 50ms simulated step
    
    if (m_rtPending) {
        m_rtPending = false;
        
        SimNetlist netlist = SimSchematicBridge::buildNetlist(m_rtScene, m_rtNetMgr);
        SimAnalysisConfig config;
        config.type = SimAnalysisType::OP;
        netlist.setAnalysis(config);

        SimEngine engine;
        SimResults results = engine.run(netlist);
        results.analysisType = SimAnalysisType::RealTime;
        results.xAxisName = "time_s"; 

        // Inject single points into waveforms so oscilloscope can scroll
        for (const auto& [node, val] : results.nodeVoltages) {
            SimWaveform w;
            w.name = "V(" + node + ")";
            w.xData = { m_rtCurrentTime };
            w.yData = { val };
            results.waveforms.push_back(w);
        }
        
        emit simulationFinished(results);
    }
}

QStringList SimManager::preflightCheck(QGraphicsScene* scene, NetManager* netMgr, SimNetlist& outNetlist) {
    outNetlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    outNetlist.flatten();
    
    QStringList diag;
    for (const auto& d : outNetlist.diagnostics()) {
        QString qd = QString::fromStdString(d);
        if (!qd.trimmed().isEmpty()) diag << qd;
    }

    // Heuristics: check for missing ground
    bool hasGnd = false;
    for (const auto& comp : outNetlist.components()) {
        for (int node : comp.nodes) if (node == 0) { hasGnd = true; break; }
        if (hasGnd) break;
    }
    if (!hasGnd && outNetlist.nodeCount() > 1) {
        diag << "[error] No ground (node 0) found in circuit. Please add a GND symbol.";
    }

    return diag;
}

void SimManager::runWithNetlist(const SimNetlist& netlist) {
    if (m_control) {
        emit logMessage("A simulation is already running.");
        return;
    }

    emit simulationStarted();
    
    m_control = new SimControl();
    m_paused = false;

    QFuture<SimResults> future = QtConcurrent::run([netlist, this]() {
        SimEngine engine;
        return engine.run(netlist, m_control);
    });

    auto* watcher = new QFutureWatcher<SimResults>(this);
    connect(watcher, &QFutureWatcher<SimResults>::finished, this, [this, watcher]() {
        SimResults results = watcher->result();
        
        delete m_control;
        m_control = nullptr;
        m_paused = false;
        emit simulationPaused(false);

        if (results.waveforms.empty() && results.nodeVoltages.empty()) {
            if (!results.fixSuggestions.empty()) {
                emit logMessage(QString("Convergence hint: %1").arg(QString::fromStdString(results.fixSuggestions.front())));
            }
            emit errorOccurred("Simulation failed or stopped.");
        } else {
            if (results.analysisType == SimAnalysisType::OP) {
                emit logMessage("DCOP simulation successful.");
            } else {
                emit logMessage(QString("Simulation finished with %1 waveforms.").arg(results.waveforms.size()));
            }
            emit simulationFinished(results);
        }
        
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

void SimManager::stopRealTime() {
    if (m_rtTimer) {
        m_rtTimer->stop();
        m_rtTimer->deleteLater();
        m_rtTimer = nullptr;
    }
    
    if (m_rtScene) {
        for (auto* item : m_rtScene->items()) {
            if (auto* si = dynamic_cast<SchematicItem*>(item)) {
                si->disconnect(this);
            }
        }
    }
    
    m_rtScene = nullptr;
    m_rtNetMgr = nullptr;
    m_rtPending = false;
}

void SimManager::onInteractiveStateChanged() {
    m_rtPending = true;
    // If the timer is active, we could wait for next tick, 
    // but for best 'Live' feel, we should trigger immediately if not already busy.
    // However, buildNetlist can be slow, so we rely on m_rtPending flag 
    // which the timer will pick up within RT interval (e.g. 50ms).
}

void SimManager::stopAll() {
    stopRealTime();
    if (m_control) {
        m_control->stopRequested = true;
        m_control->pauseRequested = false; // Unpause so it can see stop request
        emit logMessage("Stopping active simulation...");
    } else {
        emit logMessage("Stop requested. Real-time simulation was stopped if active.");
    }
}

void SimManager::pauseSimulation(bool pause) {
    if (m_control) {
        m_control->pauseRequested = pause;
        m_paused = pause;
        emit simulationPaused(pause);
        emit logMessage(pause ? "Simulation paused." : "Simulation resumed.");
    }
}
