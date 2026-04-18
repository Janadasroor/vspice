#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryFile>
#include <QTimer>
#include <QElapsedTimer>
#include <QCommandLineParser>

#include "simulation_manager.h"

struct BenchmarkResult {
    QString name;
    int runs;
    double avgTimeMs;
    double minTimeMs;
    double maxTimeMs;
    bool success;
};

bool runSingleSimulation(SimulationManager& sim, const QString& netlistPath, int timeoutMs = 60000) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(timeoutMs);
    
    QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    
    sim.runSimulation(netlistPath);
    loop.exec();
    
    return timer.isActive();
}

BenchmarkResult benchmarkFile(SimulationManager& sim, const QString& name, const QString& filepath, int runs) {
    QElapsedTimer timer;
    QVector<double> times;
    bool allSuccess = true;
    
    qDebug() << "\n=== Benchmarking:" << name << "===";
    qDebug() << "File:" << filepath;
    
    for (int i = 0; i < runs; ++i) {
        timer.restart();
        bool success = runSingleSimulation(sim, filepath, 60000);
        double elapsed = timer.elapsed();
        
        if (success) {
            times.append(elapsed);
            qDebug() << "  Run" << (i+1) << ":" << elapsed << "ms";
        } else {
            allSuccess = false;
            qWarning() << "  Run" << (i+1) << ": FAILED (timeout or error)";
        }
    }
    
    if (times.isEmpty()) {
        return {name, runs, -1, -1, -1, false};
    }
    
    double avg = 0, min = times[0], max = times[0];
    for (double t : times) {
        avg += t;
        min = qMin(min, t);
        max = qMax(max, t);
    }
    avg /= times.size();
    
    return {name, runs, avg, min, max, allSuccess};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(QCommandLineOption("runs", "Number of runs per circuit", "runs", "3"));
    parser.process(app);
    
    auto& sim = SimulationManager::instance();
    
    if (!sim.isAvailable()) {
        qCritical() << "Ngspice not available (HAVE_NGSPICE not defined)";
        return 1;
    }
    
    qDebug() << "Initializing Ngspice...";
    sim.initialize();
    
    int runs = parser.value("runs").toInt();
    
    qDebug() << "\n==========================================";
    qDebug() << "     FluxSimulator Benchmark";
    qDebug() << "==========================================";
    qDebug() << "Runs per circuit:" << runs;
    
    // RC Circuit
    auto rcResult = benchmarkFile(sim, "RC Circuit", "/home/jnd/qt_projects/viospice/test_circuits/rc_circuit.cir", runs);
    if (rcResult.avgTimeMs > 0) {
        qDebug().noquote() << QString("  Result: Avg=%1ms  Min=%2ms  Max=%3ms").arg(rcResult.avgTimeMs, 0, 'f', 2).arg(rcResult.minTimeMs, 0, 'f', 2).arg(rcResult.maxTimeMs, 0, 'f', 2);
    }
    
    // Boost Converter
    auto boostResult = benchmarkFile(sim, "Boost Converter (4 MOSFETs)", "/home/jnd/qt_projects/viospice/test_circuits/boost_converter.cir", runs);
    if (boostResult.avgTimeMs > 0) {
        qDebug().noquote() << QString("  Result: Avg=%1ms  Min=%2ms  Max=%3ms").arg(boostResult.avgTimeMs, 0, 'f', 2).arg(boostResult.minTimeMs, 0, 'f', 2).arg(boostResult.maxTimeMs, 0, 'f', 2);
    }
    
    qDebug() << "\n==========================================";
    qDebug() << "Benchmark complete!";
    qDebug() << "==========================================";
    
    return 0;
}
