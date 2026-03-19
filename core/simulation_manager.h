#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QTimer>
#include <functional>
#include <vector>
#include <mutex>

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
    void stopSimulation();
    void shutdown();

signals:
    void outputReceived(const QString& text);
    void simulationFinished();
    void rawResultsReady(const QString& rawPath);
    void simulationStarted();
    void errorOccurred(const QString& error);
    void realTimeDataBatchReceived(const std::vector<double>& times, const std::vector<std::vector<double>>& values, const QStringList& names);

private slots:
    void handleSimulationFinished(const QString& rawPath);
    void processBufferedData();

private:
    explicit SimulationManager(QObject* parent = nullptr);
    ~SimulationManager();

    bool loadNetlistInternal(const QString& netlist, bool keepStorage, QString* errorOut);

    bool m_isInitialized;
    bool m_lastLoadFailed = false;
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

    
    // Vector mapping for real-time streaming
    struct VectorMap {
        int index;
        QString name;
        bool isVoltage;
    };
    std::vector<VectorMap> m_vectorMap;

    std::vector<QByteArray> m_circStorage;
    std::vector<char*> m_circPtrs;

    // Callbacks from ngspice (static because they are C function pointers)
    static int cbSendChar(char* output, int id, void* userData);
    static int cbSendStat(char* stat, int id, void* userData);
    static int cbControlledExit(int status, bool immediate, bool quit, int id, void* userData);

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
