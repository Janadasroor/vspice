#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QGraphicsScene>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QCryptographicHash>
#include <iostream>

#include "core/plugins/plugin_manager.h"
// PCB Includes
#include "vioraeda/drc/pcb_drc.h"
#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"

// Schematic Includes
#include "flux/schematic/analysis/schematic_annotator.h"
#include "flux/schematic/analysis/schematic_erc.h"
#include "flux/schematic/factories/schematic_item_registry.h"
#include "flux/schematic/io/schematic_file_io.h"
#include "flux/schematic/items/wire_item.h"
#include "flux/schematic/editor/schematic_api.h"
#include "vioraeda/editor/pcb_api.h"
#include "vioraeda/io/pcb_file_io.h"
#include "flux/core/flux_python.h"
#include "flux/simulator/bridge/sim_schematic_bridge.h"
#include "flux/simulator/core/sim_engine.h"

namespace {
QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool runPluginPack(const QStringList& args) {
    if (args.size() < 4) {
        std::cerr << "Usage: flux-cmd plugin-pack <manifest.json> <artifact-file> <output.fluxplugin>" << std::endl;
        return false;
    }

    const QString manifestPath = args.at(1);
    const QString artifactPath = args.at(2);
    const QString outputPath = args.at(3);

    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read manifest: " << manifestPath.toStdString() << std::endl;
        return false;
    }
    const QByteArray manifestBytes = manifestFile.readAll();
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        std::cerr << "Error: Invalid manifest JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    const QJsonObject manifest = manifestDoc.object();
    const QString pluginId = manifest.value("id").toString().trimmed();
    const QString version = manifest.value("version").toString().trimmed();
    if (pluginId.isEmpty() || version.isEmpty()) {
        std::cerr << "Error: manifest must contain non-empty id and version fields." << std::endl;
        return false;
    }

    QFile artifactFile(artifactPath);
    if (!artifactFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read artifact: " << artifactPath.toStdString() << std::endl;
        return false;
    }
    const QByteArray artifactBytes = artifactFile.readAll();
    artifactFile.close();

    const QString artifactSha = sha256Hex(artifactBytes);
    const QString artifactName = QFileInfo(artifactPath).fileName();

    QJsonObject payload;
    payload["format"] = "fluxplugin-v1";
    payload["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    payload["manifest"] = manifest;

    QJsonObject artifact;
    artifact["name"] = artifactName;
    artifact["sizeBytes"] = static_cast<qint64>(artifactBytes.size());
    artifact["sha256"] = artifactSha;
    artifact["contentBase64"] = QString::fromLatin1(artifactBytes.toBase64());
    payload["artifact"] = artifact;

    const QJsonDocument outDoc(payload);
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Error: Cannot write output package: " << outputPath.toStdString() << std::endl;
        return false;
    }
    outFile.write(outDoc.toJson(QJsonDocument::Indented));
    outFile.close();

    std::cout << "Packed plugin:" << std::endl;
    std::cout << "  ID: " << pluginId.toStdString() << std::endl;
    std::cout << "  Version: " << version.toStdString() << std::endl;
    std::cout << "  Artifact: " << artifactName.toStdString() << " (" << artifactBytes.size() << " bytes)" << std::endl;
    std::cout << "  SHA-256: " << artifactSha.toStdString() << std::endl;
    std::cout << "  Output: " << outputPath.toStdString() << std::endl;
    return true;
}

bool runPluginInspect(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: flux-cmd plugin-inspect <package.fluxplugin>" << std::endl;
        return false;
    }
    const QString packagePath = args.at(1);

    QFile packageFile(packagePath);
    if (!packageFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read package: " << packagePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = packageFile.readAll();
    packageFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid package JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    const QJsonObject root = doc.object();
    const QString format = root.value("format").toString();
    const QJsonObject manifest = root.value("manifest").toObject();
    const QJsonObject artifact = root.value("artifact").toObject();

    const QString pluginId = manifest.value("id").toString();
    const QString version = manifest.value("version").toString();
    const QString artifactName = artifact.value("name").toString();
    const qint64 sizeBytes = static_cast<qint64>(artifact.value("sizeBytes").toDouble(0.0));
    const QString expectedSha = artifact.value("sha256").toString().toLower();
    const QByteArray content = QByteArray::fromBase64(artifact.value("contentBase64").toString().toLatin1());
    const QString actualSha = sha256Hex(content);

    std::cout << "Package: " << packagePath.toStdString() << std::endl;
    std::cout << "  Format: " << format.toStdString() << std::endl;
    std::cout << "  Plugin ID: " << pluginId.toStdString() << std::endl;
    std::cout << "  Version: " << version.toStdString() << std::endl;
    std::cout << "  Artifact: " << artifactName.toStdString() << std::endl;
    std::cout << "  Declared Size: " << sizeBytes << std::endl;
    std::cout << "  Extracted Size: " << content.size() << std::endl;
    std::cout << "  Declared SHA-256: " << expectedSha.toStdString() << std::endl;
    std::cout << "  Actual SHA-256:   " << actualSha.toStdString() << std::endl;

    const bool sizeOk = (sizeBytes == content.size());
    const bool shaOk = (!expectedSha.isEmpty() && expectedSha == actualSha);
    const bool manifestOk = (!pluginId.trimmed().isEmpty() && !version.trimmed().isEmpty());

    std::cout << "Validation:" << std::endl;
    std::cout << "  Manifest fields: " << (manifestOk ? "OK" : "FAIL") << std::endl;
    std::cout << "  Artifact size:   " << (sizeOk ? "OK" : "FAIL") << std::endl;
    std::cout << "  Artifact sha256: " << (shaOk ? "OK" : "FAIL") << std::endl;

    return manifestOk && sizeOk && shaOk;
}

QString analysisTypeToString(SimAnalysisType type) {
    switch (type) {
        case SimAnalysisType::OP: return "op";
        case SimAnalysisType::Transient: return "transient";
        case SimAnalysisType::AC: return "ac";
        case SimAnalysisType::MonteCarlo: return "monte_carlo";
        case SimAnalysisType::Sensitivity: return "sensitivity";
        case SimAnalysisType::ParametricSweep: return "parametric_sweep";
        case SimAnalysisType::Noise: return "noise";
        case SimAnalysisType::Distortion: return "distortion";
        case SimAnalysisType::Optimization: return "optimization";
        case SimAnalysisType::FFT: return "fft";
        case SimAnalysisType::RealTime: return "real_time";
    }
    return "unknown";
}

QJsonObject resultsToJson(const SimResults& results) {
    QJsonObject root;
    root["analysis"] = analysisTypeToString(results.analysisType);
    root["xAxis"] = QString::fromStdString(results.xAxisName);
    root["yAxis"] = QString::fromStdString(results.yAxisName);

    QJsonArray waves;
    for (const auto& wave : results.waveforms) {
        QJsonObject w;
        w["name"] = QString::fromStdString(wave.name);
        
        QJsonArray xData;
        for (double val : wave.xData) xData.append(val);
        w["x"] = xData;

        QJsonArray yData;
        for (double val : wave.yData) yData.append(val);
        w["y"] = yData;

        if (!wave.yPhase.empty()) {
            QJsonArray yPhase;
            for (double val : wave.yPhase) yPhase.append(val);
            w["phase"] = yPhase;
        }
        waves.append(w);
    }
    root["waveforms"] = waves;

    QJsonObject nodes;
    for (auto it = results.nodeVoltages.begin(); it != results.nodeVoltages.end(); ++it) {
        nodes[QString::fromStdString(it->first)] = it->second;
    }
    root["nodeVoltages"] = nodes;

    QJsonObject branches;
    for (auto it = results.branchCurrents.begin(); it != results.branchCurrents.end(); ++it) {
        branches[QString::fromStdString(it->first)] = it->second;
    }
    root["branchCurrents"] = branches;

    QJsonObject measurements;
    for (auto it = results.measurements.begin(); it != results.measurements.end(); ++it) {
        measurements[QString::fromStdString(it->first)] = it->second;
    }
    root["measurements"] = measurements;

    QJsonArray diags;
    for (const auto& d : results.diagnostics) diags.append(QString::fromStdString(d));
    root["diagnostics"] = diags;

    return root;
}
} // namespace

int main(int argc, char *argv[]) {
    // Some GUI classes like QGraphicsScene and QColor require QApplication
    // We run with offscreen platform to keep it CLI-friendly
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName("flux-cmd");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Viora EDA Command Line Interface");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption analysisOption(QStringList() << "a" << "analysis", "Analysis type (op, tran, ac)", "type", "op");
    parser.addOption(analysisOption);

    QCommandLineOption stopOption(QStringList() << "t" << "stop", "Stop time for transient", "time", "10m");
    parser.addOption(stopOption);

    QCommandLineOption stepOption(QStringList() << "s" << "step", "Step size for transient", "time", "100u");
    parser.addOption(stepOption);

    QCommandLineOption jsonOption("json", "Output results in JSON format");
    parser.addOption(jsonOption);

    // Positional arguments
    parser.addPositionalArgument("command", "Command to run: drc, erc, simulate, render, audit, autofix, process, python, plugins-smoke, plugin-pack, plugin-inspect");
    parser.addPositionalArgument("file", "File to process (.pcb or .sch), except for plugins-smoke");
    parser.addPositionalArgument("script", "JSON script file for 'process' command", "");

    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() < 1) {
        parser.showHelp();
        return 1;
    }

    QString command = args.at(0);

    if (command == "plugin-pack") {
        return runPluginPack(args) ? 0 : 1;
    }

    if (command == "plugin-inspect") {
        return runPluginInspect(args) ? 0 : 1;
    }

    if (command == "plugins-smoke") {
        PluginManager::instance().unloadPlugins();
        PluginManager::instance().loadPlugins();

        const auto plugins = PluginManager::instance().activePlugins();
        std::cout << "Loaded plugins: " << plugins.size() << std::endl;
        for (FluxPlugin* plugin : plugins) {
            std::cout << "  - " << plugin->name().toStdString()
                      << " v" << plugin->version().toStdString()
                      << " (sdk " << plugin->sdkVersionMajor()
                      << "." << plugin->sdkVersionMinor() << ")" << std::endl;
        }

        PluginManager::instance().unloadPlugins();
        std::cout << "Plugin lifecycle smoke test completed." << std::endl;
        return 0;
    }

    if (args.size() < 2) {
        parser.showHelp();
        return 1;
    }

    QString filePath = args.at(1);

    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: File not found: " << filePath.toStdString() << std::endl;
        return 1;
    }

    // Register items for correct deserialization
    PCBItemRegistry::registerBuiltInItems();
    SchematicItemRegistry::registerBuiltInItems();

    if (command == "drc") {
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        std::cout << "Running DRC on " << filePath.toStdString() << "..." << std::endl;
        PCBDRC drc;
        drc.runFullCheck(&scene);

        if (drc.violations().isEmpty()) {
            std::cout << "DRC Passed! No violations found." << std::endl;
        } else {
            std::cout << "DRC Failed! Found " << drc.violations().size() << " violations:" << std::endl;
            for (const auto& v : drc.violations()) {
                std::cout << "  [" << v.severityString().toStdString() << "] " 
                          << v.typeString().toStdString() << ": "
                          << v.message().toStdString() << " at (" 
                          << v.location().x() << ", " << v.location().y() << ")" << std::endl;
            }
            return drc.errorCount() > 0 ? 1 : 0;
        }
    } else if (command == "erc") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        std::cout << "Running ERC on " << filePath.toStdString() << "..." << std::endl;
        auto violations = SchematicERC::run(&scene, QFileInfo(filePath).absolutePath());

        if (violations.isEmpty()) {
            std::cout << "ERC Passed! No issues found." << std::endl;
        } else {
            std::cout << "ERC found " << violations.size() << " issues:" << std::endl;
            for (const auto& v : violations) {
                QString sev = (v.severity == ERCViolation::Error) ? "Error" : "Warning";
                std::cout << "  [" << sev.toStdString() << "] " 
                          << v.message.toStdString() << " at (" 
                          << v.position.x() << ", " << v.position.y() << ")" << std::endl;
            }
        }
    } else if (command == "simulate") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        std::cout << "Simulating circuit " << filePath.toStdString() << "..." << std::endl;
        
        // Use native built-in simulator
        SimNetlist netlist = SimSchematicBridge::buildNetlist(&scene, nullptr);
        
        QString analysisType = parser.value("analysis").toLower();
        SimAnalysisConfig config;
        
        auto parseVal = [](const QString& val) -> double {
            if (val.isEmpty()) return 0.0;
            QString s = val.trimmed().toLower();
            double multiplier = 1.0;
            if (s.endsWith("k")) { multiplier = 1e3; s.chop(1); }
            else if (s.endsWith("m") && !s.endsWith("meg")) { multiplier = 1e-3; s.chop(1); }
            else if (s.endsWith("u")) { multiplier = 1e-6; s.chop(1); }
            else if (s.endsWith("n")) { multiplier = 1e-9; s.chop(1); }
            else if (s.endsWith("p")) { multiplier = 1e-12; s.chop(1); }
            else if (s.endsWith("meg")) { multiplier = 1e6; s.chop(3); }
            else if (s.endsWith("g")) { multiplier = 1e9; s.chop(1); }
            bool ok;
            double d = s.toDouble(&ok);
            return ok ? d * multiplier : 0.0;
        };

        if (analysisType == "tran") {
            config.type = SimAnalysisType::Transient;
            config.tStop = parseVal(parser.value("stop"));
            config.tStep = parseVal(parser.value("step"));
            std::cout << "  - Type: Transient (Stop=" << config.tStop << "s, Step=" << config.tStep << "s)" << std::endl;
        } else if (analysisType == "ac") {
            config.type = SimAnalysisType::AC;
            config.fStart = 10;
            config.fStop = 1e6;
            config.fPoints = 100;
            std::cout << "  - Type: AC Sweep (10Hz to 1MHz)" << std::endl;
        } else {
            config.type = SimAnalysisType::OP;
            std::cout << "  - Type: DC Operating Point" << std::endl;
        }

        netlist.setAnalysis(config);

        SimEngine engine;
        SimResults results = engine.run(netlist);

        if (parser.isSet(jsonOption)) {
            QJsonDocument doc(resultsToJson(results));
            std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
            _Exit(0);
        }

        if (results.waveforms.empty() && results.nodeVoltages.empty()) {
            std::cerr << "Simulation failed to produce results." << std::endl;
            return 1;
        }

        if (config.type == SimAnalysisType::OP) {
            std::cout << "\n--- DC Operating Point Results ---" << std::endl;
            for (const auto& [node, v] : results.nodeVoltages) {
                std::cout << "V(" << node << ") = " << v << " V" << std::endl;
            }
            for (const auto& [branch, i] : results.branchCurrents) {
                std::cout << "I(" << branch << ") = " << (i * 1000.0) << " mA" << std::endl;
            }
        } else {
            std::cout << "\nGenerated " << results.waveforms.size() << " waveforms." << std::endl;
            for (const auto& wave : results.waveforms) {
                std::cout << "  - " << wave.name << " (" << wave.yData.size() << " points)" << std::endl;
                if (!wave.yData.empty()) {
                    std::cout << "    Range: [" << *std::min_element(wave.yData.begin(), wave.yData.end()) 
                              << " V, " << *std::max_element(wave.yData.begin(), wave.yData.end()) << " V]" << std::endl;
                }
            }
        }

        std::cout << "\nSimulation successful." << std::endl;
    } else if (command == "render") {
        if (args.size() < 3) {
            std::cerr << "Usage: flux-cmd render <file.pcb> <output.png>" << std::endl;
            return 1;
        }
        QString output = args.at(2);
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        std::cout << "Rendering " << filePath.toStdString() << " to " << output.toStdString() << "..." << std::endl;
        
        QRectF rect = scene.itemsBoundingRect();
        if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
        rect.adjust(-10, -10, 10, 10); // Adding margin

        QImage image(rect.size().toSize() * 4, QImage::Format_ARGB32); // 4x scale for high res
        image.fill(QColor(20, 20, 25)); // Dark board color
        
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene.render(&painter, QRectF(), rect);
        painter.end();

        if (image.save(output)) {
            std::cout << "Successfully rendered scene to " << output.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save image to " << output.toStdString() << std::endl;
            return 1;
        }
    } else if (command == "audit") {
        std::cout << "Project Doctor (Audit) starting on: " << filePath.toStdString() << std::endl;
        
        QJsonObject report;
        report["project_path"] = filePath;
        report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        QJsonArray pcbAudits;
        QJsonArray schAudits;

        QFileInfo info(filePath);
        QStringList pcbFiles;
        QStringList schFiles;

        if (info.isDir()) {
            QDir dir(filePath);
            for (const QString& f : dir.entryList({"*.pcb"}, QDir::Files)) pcbFiles << dir.filePath(f);
            for (const QString& f : dir.entryList({"*.sch"}, QDir::Files)) schFiles << dir.filePath(f);
        } else {
            if (filePath.endsWith(".pcb")) pcbFiles << filePath;
            else if (filePath.endsWith(".sch")) schFiles << filePath;
        }

        // Run DRC on PCBs
        for (const QString& pcbFile : pcbFiles) {
            QJsonObject pcbReport;
            pcbReport["file"] = QFileInfo(pcbFile).fileName();
            
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, pcbFile)) {
                PCBDRC drc;
                drc.runFullCheck(&scene);
                
                QJsonArray violations;
                for (const auto& v : drc.violations()) {
                    QJsonObject vio;
                    vio["type"] = v.typeString();
                    vio["severity"] = v.severityString();
                    vio["message"] = v.message();
                    vio["x"] = v.location().x();
                    vio["y"] = v.location().y();
                    violations.append(vio);
                }
                pcbReport["violations"] = violations;
                pcbReport["status"] = drc.errorCount() == 0 ? "Healthy" : "Needs Attention";
            } else {
                pcbReport["status"] = "Error Loading";
            }
            pcbAudits.append(pcbReport);
        }

        // Run ERC on Schematics
        for (const QString& schFile : schFiles) {
            QJsonObject schReport;
            schReport["file"] = QFileInfo(schFile).fileName();
            
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, schFile, pageSize, dummyTB)) {
                auto violations = SchematicERC::run(&scene, QFileInfo(schFile).absolutePath());
                
                QJsonArray issues;
                for (const auto& v : violations) {
                    QJsonObject issue;
                    issue["severity"] = (v.severity == ERCViolation::Error) ? "Error" : 
                                       (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
                    issue["message"] = v.message;
                    issue["x"] = v.position.x();
                    issue["y"] = v.position.y();
                    issues.append(issue);
                }
                schReport["issues"] = issues;
                schReport["status"] = issues.isEmpty() ? "Healthy" : "Needs Attention";
            } else {
                schReport["status"] = "Error Loading";
            }
            schAudits.append(schReport);
        }

        report["pcb_reports"] = pcbAudits;
        report["schematic_reports"] = schAudits;
        report["overall_status"] = (pcbFiles.size() + schFiles.size() > 0) ? "Audit Complete" : "No files found";

        QJsonDocument doc(report);
        QDir().mkpath("docs/reports");
        QString reportPath = "docs/reports/project_health_report.json";
        QFile file(reportPath);
        if (file.open(QFile::WriteOnly)) {
            file.write(doc.toJson());
            file.close();
            std::cout << "Project health report generated: " << reportPath.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save health report." << std::endl;
        }

    } else if (command == "autofix") {
        std::cout << "Project Autofix starting on: " << filePath.toStdString() << std::endl;
        
        if (filePath.endsWith(".sch")) {
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
                int fixedCount = 0;
                
                // 1. Run Annotation
                SchematicAnnotator::annotate(&scene, false); // Only fix '?'
                std::cout << "  - Reference annotation completed." << std::endl;

                // 2. Remove duplicate/redundant wires
                QList<WireItem*> wires;
                for (auto* item : scene.items()) {
                    if (auto* w = dynamic_cast<WireItem*>(item)) wires.append(w);
                }

                for (int i = 0; i < wires.size(); ++i) {
                    for (int j = i + 1; j < wires.size(); ++j) {
                        if (wires[i]->startPoint() == wires[j]->startPoint() && 
                            wires[i]->endPoint() == wires[j]->endPoint()) {
                            scene.removeItem(wires[j]);
                            delete wires[j];
                            wires.removeAt(j);
                            j--;
                            fixedCount++;
                        }
                    }
                }
                
                if (fixedCount > 0) std::cout << "  - Removed " << fixedCount << " duplicate wires." << std::endl;
                
                if (SchematicFileIO::saveSchematic(&scene, filePath, pageSize)) {
                    std::cout << "  - Schematic fixed and saved successfully." << std::endl;
                }
            }
        } else if (filePath.endsWith(".pcb")) {
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, filePath)) {
                int fixedTraces = 0;
                int snappedPoints = 0;
                int snappedComponents = 0;
                PCBDRC drc;
                double minWidth = drc.rules().minTraceWidth();
                double gridSize = 0.1; // 0.1mm grid for snapping

                // Helper to snap a point to grid
                auto snap = [&](QPointF p) {
                    double x = std::round(p.x() / gridSize) * gridSize;
                    double y = std::round(p.y() / gridSize) * gridSize;
                    return QPointF(x, y);
                };

                // 1. Grid Snapping (Shared points)
                QMap<QString, QPointF> pointMap; 
                auto ptKey = [](QPointF p) { return QString("%1,%2").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4); };

                QList<TraceItem*> traces;
                for (auto* item : scene.items()) {
                    if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                        traces.append(trace);
                        if (trace->width() < minWidth) {
                            trace->setWidth(minWidth);
                            fixedTraces++;
                        }
                        pointMap[ptKey(trace->startPoint())] = snap(trace->startPoint());
                        pointMap[ptKey(trace->endPoint())] = snap(trace->endPoint());
                    } else if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
                        QPointF oldPos = comp->pos();
                        QPointF newPos = snap(oldPos);
                        if (oldPos != newPos) {
                            comp->setPos(newPos);
                            snappedComponents++;
                        }
                    }
                }

                // Apply trace grid snaps
                for (auto* trace : traces) {
                    QPointF oldS = trace->startPoint();
                    QPointF oldE = trace->endPoint();
                    QPointF newS = pointMap[ptKey(oldS)];
                    QPointF newE = pointMap[ptKey(oldE)];

                    if (newS != oldS || newE != oldE) {
                        trace->setStartPoint(newS);
                        trace->setEndPoint(newE);
                        snappedPoints++;
                    }
                }

                if (fixedTraces > 0) std::cout << "  - Adjusted " << fixedTraces << " traces to minimum width (" << minWidth << "mm)." << std::endl;
                if (snappedPoints > 0) std::cout << "  - Snapped " << snappedPoints << " trace points to " << gridSize << "mm grid." << std::endl;
                if (snappedComponents > 0) std::cout << "  - Realigned " << snappedComponents << " components to " << gridSize << "mm grid." << std::endl;
                
                if (PCBFileIO::savePCB(&scene, filePath)) {
                    std::cout << "  - PCB fixed and saved successfully." << std::endl;
                }
            }
        } else {
            std::cerr << "Error: Autofix only supports .sch and .pcb files." << std::endl;
            return 1;
        }

    } else if (command == "process") {
        if (args.size() < 3) {
            std::cerr << "Usage: flux-cmd process <file.sch|.pcb> <script.json> [output.file]" << std::endl;
            return 1;
        }
        
        QString scriptPath = args.at(2);
        QString outputPath = (args.size() >= 4) ? args.at(3) : filePath;
        
        QGraphicsScene scene;
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error reading script: " << scriptPath.toStdString() << std::endl;
            return 1;
        }
        
        QJsonDocument scriptDoc = QJsonDocument::fromJson(scriptFile.readAll());
        if (!scriptDoc.isArray()) {
            std::cerr << "Error: Script must be a JSON array of commands." << std::endl;
            return 1;
        }

        if (filePath.endsWith(".sch")) {
            SchematicAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading schematic: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            std::cout << "Executed " << count << " schematic commands." << std::endl;
            if (api.save(outputPath)) {
                std::cout << "Saved processed schematic to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed schematic." << std::endl;
                return 1;
            }
        } else if (filePath.endsWith(".pcb")) {
            PCBAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading PCB: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            std::cout << "Executed " << count << " PCB commands." << std::endl;
            if (api.save(outputPath)) {
                std::cout << "Saved processed PCB to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed PCB." << std::endl;
                return 1;
            }
        } else {
            std::cerr << "Error: Unsupported file extension for process." << std::endl;
            return 1;
        }
    } else if (command == "python") {
        if (args.size() < 2) {
            std::cerr << "Usage: flux-cmd python <script.py> [args...]" << std::endl;
            return 1;
        }
        
        QString scriptPath = args.at(1);
        QFile file(scriptPath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Could not open python script: " << scriptPath.toStdString() << std::endl;
            return 1;
        }
        
        QString code = QString::fromUtf8(file.readAll());
        
        FluxPython& py = FluxPython::instance();
        py.initialize();
        
        extern void flux_python_init_bindings();
        flux_python_init_bindings();
        
        QString error;
        if (!py.executeString(code, &error)) {
            std::cerr << "Python Error: " << error.toStdString() << std::endl;
            return 1;
        }
        
        py.finalize();
    } else {
        std::cerr << "Unknown command: " << command.toStdString() << std::endl;
        return 1;
    }

    return 0;
}
