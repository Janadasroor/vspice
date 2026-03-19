#ifndef SIM_MANAGER_QT_H
#define SIM_MANAGER_QT_H

#include <QObject>
#include "../core/sim_results.h"
#include "sim_schematic_bridge.h"

/**
 * @brief Thread-safe (future) Manager for the simulation module.
 */
class SimManager : public QObject {
    Q_OBJECT
public:
    static SimManager& instance();

    void runDCOP(QGraphicsScene* scene, NetManager* netMgr);
    void runTransient(QGraphicsScene* scene, NetManager* netMgr, double tStop, double tStep);
    void runAC(QGraphicsScene* scene, NetManager* netMgr, double fStart, double fStop, int points);
    void runMonteCarlo(QGraphicsScene* scene, NetManager* netMgr, int runs);
    void runParametricSweep(QGraphicsScene* scene, NetManager* netMgr, const QString& component, const QString& param, double start, double stop, int steps);
    void runSensitivity(QGraphicsScene* scene, NetManager* netMgr, const QString& targetSignal);
    void runNetlistText(const QString& netlistContent);
    
    // Ngspice Integration
    void runNgspiceSimulation(QGraphicsScene* scene, NetManager* netMgr, const SimAnalysisConfig& config);

    // Debugger / Pre-flight check
    QStringList preflightCheck(QGraphicsScene* scene, NetManager* netMgr, SimNetlist& outNetlist);
    void runWithNetlist(const SimNetlist& netlist);
    
    void runRealTime(QGraphicsScene* scene, NetManager* netMgr, int intervalMs = 100);
    void stopRealTime();
    
    void stopAll();
    void pauseSimulation(bool pause);
    bool isPaused() const { return m_paused; }

signals:
    void simulationStarted();
    void simulationFinished(const SimResults& results);
    void simulationPaused(bool paused);
    void realTimePointReceived(double t, const std::vector<double>& values);
    void realTimeDataBatchReceived(const std::vector<double>& times, const std::vector<std::vector<double>>& values);
    void errorOccurred(const QString& msg);
    void logMessage(const QString& msg);

private slots:
    void onInteractiveStateChanged();
    void onRealTimeTick();
    void cleanupSimulation();

private:
    explicit SimManager(QObject* parent = nullptr);
    void startNgspiceWithNetlist(const QString& netlistContent);

    SimControl* m_control = nullptr;
    bool m_paused = false;

    QGraphicsScene* m_rtScene = nullptr;
    NetManager* m_rtNetMgr = nullptr;
    bool m_rtPending = false;
    double m_rtCurrentTime = 0.0;
    class QTimer* m_rtTimer = nullptr;
};

#endif // SIM_MANAGER_QT_H
