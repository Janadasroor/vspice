#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryFile>
#include <QTimer>
#include <QSignalSpy>

#include "core/simulation_manager.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    auto& sim = SimulationManager::instance();
    
    if (!sim.isAvailable()) {
        qCritical() << "Ngspice not available (HAVE_NGSPICE not defined)";
        return 1;
    }

    qDebug() << "Initializing Ngspice...";
    sim.initialize();

    // Create a simple netlist
    QTemporaryFile netlistFile;
    if (!netlistFile.open()) {
        qCritical() << "Failed to create temporary netlist file";
        return 1;
    }

    QString netlistContent = 
        "Test RC circuit\n"
        "V1 1 0 10\n"
        "R1 1 2 1k\n"
        "C1 2 0 1u\n"
        ".tran 1u 10m\n"
        ".end\n";

    netlistFile.write(netlistContent.toUtf8());
    QString netlistPath = netlistFile.fileName();
    netlistFile.close();

    qDebug() << "Running simulation with netlist:" << netlistPath;
    
    QSignalSpy startedSpy(&sim, &SimulationManager::simulationStarted);
    QSignalSpy finishedSpy(&sim, qOverload<>(&SimulationManager::simulationFinished));
    QSignalSpy outputSpy(&sim, &SimulationManager::outputReceived);

    sim.runSimulation(netlistPath);

    // Wait for simulation to finish (with timeout)
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(5000); // 5 seconds timeout
    loop.exec();

    if (finishedSpy.count() == 0) {
        qCritical() << "Simulation timed out!";
        return 1;
    }

    qDebug() << "Simulation finished successfully!";
    qDebug() << "Received" << outputSpy.count() << "output messages.";

    // Check if raw file was created
    QString rawPath = netlistPath;
    rawPath.replace(".tmp", ".raw"); // QTemporaryFile usually has .tmp
    // Wait a bit for the raw file to be written as it happens in cbBGThreadRunning
    QThread::msleep(100);

    if (QFile::exists(rawPath)) {
        qDebug() << "Raw output file created:" << rawPath;
        QFile::remove(rawPath);
    } else {
        qWarning() << "Raw output file NOT created at expected path:" << rawPath;
        // This might be because the replacement in cbBGThreadRunning didn't match
    }

    return 0;
}
