#ifndef SIMULATION_MANAGER_H
#define SIMULATION_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

#ifdef HAVE_NGSPICE
#include <ngspice/sharedspice.h>
#endif

/**
 * @brief Manages interaction with the Ngspice simulation engine
 */
class SimulationManager : public QObject {
    Q_OBJECT

public:
    static SimulationManager& instance();

    bool isAvailable() const;
    void initialize();
    void runSimulation(const QString& netlist);
    void stopSimulation();

signals:
    void outputReceived(const QString& text);
    void simulationFinished();
    void simulationStarted();
    void errorOccurred(const QString& error);

private:
    explicit SimulationManager(QObject* parent = nullptr);
    QString m_currentNetlist;
    ~SimulationManager();

    bool m_isInitialized;

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
