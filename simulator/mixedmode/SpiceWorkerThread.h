#ifndef VIOSPICE_MIXEDMODE_SPICEWORKERTHREAD_H
#define VIOSPICE_MIXEDMODE_SPICEWORKERTHREAD_H

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <atomic>

#ifdef HAVE_NGSPICE
#include <ngspice/sharedspice.h>
#endif

class SpiceWorkerThread : public QThread {
    Q_OBJECT
public:
    enum class DigitalState {
        Low,
        High,
        Unknown
    };
    Q_ENUM(DigitalState)

    struct DigitalNodeUpdate {
        QString nodeName;
        DigitalState state = DigitalState::Unknown;
        double simulationTime = 0.0;
    };

    explicit SpiceWorkerThread(QObject* parent = nullptr);
    ~SpiceWorkerThread() override;

    void submitNetlist(const QString& netlistText);
    void stopSimulation();

signals:
    void logMessage(const QString& message);
    void simulationStarted();
    void simulationStopped();
    void simulationError(const QString& message);
    void digitalStatesReady(const QVector<SpiceWorkerThread::DigitalNodeUpdate>& batch);

protected:
    void run() override;

private:
    struct BufferedState {
        DigitalState state = DigitalState::Unknown;
        double simulationTime = 0.0;
    };

    QString m_pendingNetlist;
    QMutex m_stateMutex;
    QMutex m_commandMutex;
    QWaitCondition m_commandReady;
    QHash<QString, BufferedState> m_digitalStates;
    QQueue<QString> m_logQueue;

    std::atomic<bool> m_stopRequested { false };
    std::atomic<bool> m_netlistPending { false };
    std::atomic<bool> m_ngspiceRunning { false };
    qint64 m_wallStartMs = 0;
    double m_latestSimTime = 0.0;

    void resetRunState();
    void wallClockThrottle(double simulationTimeSeconds);
    void flushDigitalStates();
    void flushLogs();
    bool loadCircuit(const QString& netlistText);
    void publishDigitalValue(const QString& nodeName, double value, double simulationTime);
    static DigitalState decodeDigitalValue(double value);

#ifdef HAVE_NGSPICE
    static int cbSendChar(char* output, int id, void* userData);
    static int cbSendStat(char* stat, int id, void* userData);
    static int cbControlledExit(int status, bool immediate, bool quit, int id, void* userData);
    static int cbSendData(pvecvaluesall vecArray, int numStructs, int id, void* userData);
    static int cbSendInitData(pvecinfoall initData, int id, void* userData);
    static int cbBGThreadRunning(bool finished, int id, void* userData);
#else
    static int cbSendChar(char* output, int id, void* userData);
    static int cbSendStat(char* stat, int id, void* userData);
    static int cbControlledExit(int status, bool immediate, bool quit, int id, void* userData);
    static int cbSendData(void* vecArray, int numStructs, int id, void* userData);
    static int cbSendInitData(void* initData, int id, void* userData);
    static int cbBGThreadRunning(bool finished, int id, void* userData);
#endif
};

#endif
