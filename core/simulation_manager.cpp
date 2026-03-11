#include "simulation_manager.h"
#include <QDebug>
#include <QThread>

SimulationManager& SimulationManager::instance() {
    static SimulationManager instance;
    return instance;
}

SimulationManager::SimulationManager(QObject* parent)
    : QObject(parent), m_isInitialized(false) {
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

void SimulationManager::runSimulation(const QString& netlist) {
    if (!isAvailable()) {
        emit errorOccurred("Simulation engine not installed.");
        return;
    }
    if (!m_isInitialized) initialize();

#ifdef HAVE_NGSPICE
    m_currentNetlist = netlist;
    emit simulationStarted();
    
    // Command sequence:
    // 1. source the netlist
    // 2. run the simulation
    // 3. (Optional) write output or query vectors
    
    // We need to convert QString to char*
    QByteArray ba = netlist.toLatin1(); // Or Utf8
    // ngspice expects commands. "source" might be needed if it's a file path
    // or "circ" if loading buffer (but API usually takes commands)
    // The shared lib usually loads a circuit via `source file`.
    
    // If 'netlist' is actual content, we might need to write to temp file first
    // For now assuming 'netlist' is a file path command like "source /tmp/circuit.cir"
    
    QString cmd = "source " + netlist;
    ngSpice_Command(cmd.toLatin1().data());
    
    // We send bg_run. The 'write' command must happen AFTER run.
    // In ngspice, we can use 'alias' or just wait for the finished signal.
    // Better yet, we can send multiple commands if they are semicolon separated? 
    // Actually, bg_run is its own thing. 
    // We will handle the 'write' in the cbBGThreadRunning callback to ensure it happens after simulation completes.
    ngSpice_Command((char*)"bg_run");
#endif
}

void SimulationManager::stopSimulation() {
#ifdef HAVE_NGSPICE
    ngSpice_Command((char*)"bg_halt");
#endif
}

// --- Callbacks ---

int SimulationManager::cbSendChar(char* output, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && output) {
        QString msg = QString::fromLatin1(output);
        // Clean up stderr/stdout distinction if needed
        if (msg.startsWith("stderr ")) msg.remove(0, 7);
        else if (msg.startsWith("stdout ")) msg.remove(0, 7);
        
        qDebug() << "[Ngspice]" << msg.trimmed();
        emit self->outputReceived(msg);
    }
    return 0;
}

int SimulationManager::cbSendStat(char* stat, int id, void* userData) {
    // Progress updates
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && stat) {
        QString msg = QString::fromLatin1(stat);
        // Usually contains "% complete" or time info
        emit self->outputReceived(msg); 
    }
    return 0;
}

int SimulationManager::cbControlledExit(int status, bool immediate, bool quit, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self) {
        qDebug() << "Ngspice exit request:" << status;
        // In shared lib mode, we usually don't want to exit the main app
        // We just unload the circuit
    }
    return 0;
}

#ifdef HAVE_NGSPICE
int SimulationManager::cbSendData(pvecvaluesall vecArray, int numStructs, int id, void* userData) {
    // Real-time data plotting hook
    // vecArray is vector_info*
    return 0;
}

int SimulationManager::cbSendInitData(pvecinfoall initData, int id, void* userData) {
    // Initialization info for vectors
    return 0;
}
#else
int SimulationManager::cbSendData(void* vecArray, int numStructs, int id, void* userData) {
    // Real-time data plotting hook
    // vecArray is vector_info*
    return 0;
}

int SimulationManager::cbSendInitData(void* initData, int id, void* userData) {
    // Initialization info for vectors
    return 0;
}
#endif

int SimulationManager::cbBGThreadRunning(bool finished, int id, void* userData) {
    SimulationManager* self = static_cast<SimulationManager*>(userData);
    if (self && finished) {
        // Trigger save to raw file once background simulation is done
        QString rawPath = self->m_currentNetlist;
        rawPath.replace(".cir", ".raw");
        QString writeCmd = "write " + rawPath;
#ifdef HAVE_NGSPICE
        ngSpice_Command(writeCmd.toLatin1().data());
#endif
        
        emit self->simulationFinished();
    }
    return 0;
}
