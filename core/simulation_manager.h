#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>
#include <functional>
#include <vector>
#include <mutex>
#include <map>

#ifdef HAVE_NGSPICE
#include <ngspice/sharedspice.h>
#endif

class SimControl;

/**
 * @brief Manages interaction with the Ngspice simulation engine
 */
class SimulationManager : public QObject {
    Q_OBJECT

public:
    static SimulationManager& instance();

    bool isAvailable() const;
    void initialize();
    void runSimulation(const QString& netlist, SimControl* control = nullptr);
    bool validateNetlist(const QString& netlist, QString* errorOut = nullptr);
    QString lastErrorMessage() const;
    void stopSimulation();
    void shutdown();

    // --- Dynamic Interaction API ---
    double getVectorValue(const QString& name);
    void setParameter(const QString& name, double value);
    void sendInternalCommand(const QString& command);

    // --- Real-Time Switch Control ---
    // Phase 1: Toggle switch mid-simulation using bg_halt/alter/bg_resume cycle
    void alterSwitch(const QString& switchRef, bool open, double vt = 0.5, double vh = 0.1);
    void alterSwitchResistance(const QString& resistorName, double resistance);
    void alterSwitchVoltage(const QString& controlSourceName, double voltage);

    // Phase 2: Real-time callback registration (zero latency, no pause)
    // Registers the GetSwitchData callback with ngspice.
    // After this, switch clicks instantly affect simulation at every timestep.
    void registerSwitchCallback();

    // Update switch resistance in real-time (Phase 2)
    // Thread-safe: can be called from GUI thread while simulation runs
    void setSwitchResistance(const QString& name, double resistance);
    double getSwitchResistance(const QString& name) const;


Q_SIGNALS:
    void outputReceived(const QString& text);
    void simulationFinished();
    void rawResultsReady(const QString& rawPath);
    void simulationStarted();
    void errorOccurred(const QString& error);
    void realTimeDataBatchReceived(const std::vector<double>& times, const std::vector<std::vector<double>>& values, const QStringList& names);

private Q_SLOTS:
    void handleSimulationFinished(const QString& rawPath);
    void processBufferedData();

private:
    explicit SimulationManager(QObject* parent = nullptr);
    ~SimulationManager();

    bool loadNetlistInternal(const QString& netlist, bool keepStorage, QString* errorOut);

    bool m_isInitialized;
    bool m_lastLoadFailed = false;
    QString m_lastErrorMessage;
    bool m_bgRunIssued = false;
    bool m_stopRequested = false;
    bool m_pauseRequested = false;
    bool m_switchToggleInProgress = false;  // Prevents simulationFinished() during switch toggles
    QString m_currentNetlist;
    SimControl* m_streamingControl = nullptr;
    
    // Thread-safe buffering for real-time updates
    struct SimDataPoint {
        double time;
        std::vector<double> values;
    };
    std::vector<SimDataPoint> m_simBuffer;
    std::mutex m_bufferMutex;
    QTimer* m_bufferTimer = nullptr;
    int m_streamingCounter = 0;
    int m_skipFactor = 1;

    std::vector<QString> m_logBuffer;
    std::mutex m_logMutex;
    std::mutex m_controlMutex;

    
    // Vector mapping for real-time streaming
    struct VectorMap {
        int index;
        QString name;
        bool isVoltage;
        bool isScale = false;
    };
    std::vector<VectorMap> m_vectorMap;

    std::vector<QByteArray> m_circStorage;
    std::vector<char*> m_circPtrs;

    // Phase 2: Thread-safe switch state storage for real-time callback
    std::map<std::string, double> m_switchResistances;
    std::mutex m_switchMutex;

    // Callbacks from ngspice (static because they are C function pointers)
    static int cbSendChar(char* output, int id, void* userData);
    static int cbSendStat(char* stat, int id, void* userData);
    static int cbControlledExit(int status, bool immediate, bool quit, int id, void* userData);

    // Phase 2: Interactive switch callback (called by ngspice during simulation)
    static int cbGetSwitchData(double* resistance, const char* name, int ident, void* userData);

#ifdef HAVE_NGSPICE
    static int cbSendData(pvecvaluesall vecArray, int numStructs, int id, void* userData);
    static int cbSendInitData(pvecinfoall initData, int id, void* userData);
    static int cbBGThreadRunning(bool finished, int id, void* userData);
#else
    static int cbSendData(void* vecArray, int numStructs, int id, void* userData);
    static int cbSendInitData(void* initData, int id, void* userData);
    static int cbBGThreadRunning(bool finished, int id, void* userData);
#endif
};

#endif // SIMULATION_MANAGER_H
