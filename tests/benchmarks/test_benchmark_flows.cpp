#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGraphicsScene>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <iostream>
#include <vector>

#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"
#include "flux/schematic/factories/schematic_item_registry.h"
#include "flux/schematic/io/schematic_file_io.h"
#include "simulator/core/sim_engine.h"
#include "simulator/core/sim_netlist.h"
#include "schematic/analysis/wire_router.h"

namespace {

struct Metric {
    QString name;
    int iterations;
    double avgMs;
    double minMs;
    double maxMs;
};

template <typename Fn>
Metric runMetric(const QString& name, int iterations, Fn&& fn) {
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iterations));

    for (int i = 0; i < iterations; ++i) {
        QElapsedTimer timer;
        timer.start();
        const bool ok = fn();
        const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        if (!ok) {
            throw std::runtime_error(("benchmark failed: " + name).toStdString());
        }
        samples.push_back(ms);
    }

    const auto minIt = std::min_element(samples.begin(), samples.end());
    const auto maxIt = std::max_element(samples.begin(), samples.end());
    const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);

    return Metric{name, iterations, sum / static_cast<double>(iterations), *minIt, *maxIt};
}

QJsonObject metricToJson(const Metric& m) {
    QJsonObject obj;
    obj["name"] = m.name;
    obj["iterations"] = m.iterations;
    obj["avg_ms"] = m.avgMs;
    obj["min_ms"] = m.minMs;
    obj["max_ms"] = m.maxMs;
    return obj;
}

bool benchPcbLoad(const QString& fixturesDir) {
    const QString pcbFile = QDir(fixturesDir).filePath("test_fix.pcb");
    QGraphicsScene scene;
    return PCBFileIO::loadPCB(&scene, pcbFile) && !scene.items().isEmpty();
}

bool benchSchematicLoad(const QString& fixturesDir) {
    const QString schFile = QDir(fixturesDir).filePath("untitled.sch");
    QGraphicsScene scene;
    QString page;
    TitleBlockData tb;
    return SchematicFileIO::loadSchematic(&scene, schFile, page, tb) && !scene.items().isEmpty();
}

bool benchSchematicRender(const QString& fixturesDir) {
    const QString schFile = QDir(fixturesDir).filePath("untitled.sch");
    QGraphicsScene scene;
    QString page;
    TitleBlockData tb;
    if (!SchematicFileIO::loadSchematic(&scene, schFile, page, tb) || scene.items().isEmpty()) {
        return false;
    }

    QRectF rect = scene.itemsBoundingRect();
    if (rect.isEmpty()) {
        rect = QRectF(-200, -150, 400, 300);
    }

    QImage image(QSize(1280, 720), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(20, 20, 25));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    scene.render(&painter, QRectF(0, 0, image.width(), image.height()), rect);
    painter.end();

    return !image.isNull();
}

bool benchRoutingProxy(const QString& fixturesDir) {
    const QString schFile = QDir(fixturesDir).filePath("untitled.sch");
    QGraphicsScene scene;
    QString page;
    TitleBlockData tb;
    if (!SchematicFileIO::loadSchematic(&scene, schFile, page, tb) || scene.items().isEmpty()) {
        return false;
    }

    WireRouter router(&scene);
    router.updateObstaclesFromScene();

    const QList<QPointF> path = router.routeOrthogonal(QPointF(-450, -220), QPointF(300, 180), {}, 10.0);
    return path.size() >= 2;
}

bool benchSimulationOp() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const auto it = results.nodeVoltages.find("N2");
    return it != results.nodeVoltages.end() && it->second > 4.9 && it->second < 5.1;
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    PCBItemRegistry::registerBuiltInItems();
    SchematicItemRegistry::registerBuiltInItems();

    const QString fixturesDir = (argc > 1)
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("tests/regression/fixtures");

    const QString outFile = (argc > 2) ? QString::fromLocal8Bit(argv[2]) : QString();

    try {
        std::vector<Metric> metrics;
        metrics.push_back(runMetric("pcb_load_ms", 20, [&]() { return benchPcbLoad(fixturesDir); }));
        metrics.push_back(runMetric("schematic_load_ms", 20, [&]() { return benchSchematicLoad(fixturesDir); }));
        metrics.push_back(runMetric("schematic_render_ms", 15, [&]() { return benchSchematicRender(fixturesDir); }));
        metrics.push_back(runMetric("routing_proxy_ms", 20, [&]() { return benchRoutingProxy(fixturesDir); }));
        metrics.push_back(runMetric("simulate_op_ms", 30, [&]() { return benchSimulationOp(); }));

        QJsonArray arr;
        for (const Metric& m : metrics) {
            arr.append(metricToJson(m));
        }

        QJsonObject root;
        root["benchmark_version"] = 1;
        root["fixtures_dir"] = fixturesDir;
        root["metrics"] = arr;

        const QJsonDocument doc(root);
        const QByteArray json = doc.toJson(QJsonDocument::Indented);

        std::cout << json.constData() << std::endl;

        if (!outFile.isEmpty()) {
            QFile file(outFile);
            QDir().mkpath(QFileInfo(outFile).absolutePath());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                std::cerr << "failed to write benchmark file: " << outFile.toStdString() << std::endl;
                return 1;
            }
            file.write(json);
            file.close();
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
}
