#include "design_report_generator.h"
#include "../items/component_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/copper_pour_item.h"
#include "../items/pcb_item.h"
#include "../items/ratsnest_item.h"
#include "../layers/pcb_layer.h"
#include "../drc/pcb_drc.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include "../analysis/length_measurement_engine.h"
#include "../../core/bom_manager.h"
#include "../../core/net_class.h"
#include "../../core/eco_types.h"

#include <QGraphicsScene>
#include <QDateTime>
#include <QTextStream>
#include <QFile>
#include <QPrinter>
#include <QTextDocument>
#include <QPainter>
#include <QFileInfo>
#include <QtMath>

// ============================================================================
// Data Collection
// ============================================================================

DesignReportData DesignReportGenerator::collectData(QGraphicsScene* scene) {
    DesignReportData data;
    data.generatedAt = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");

    if (!scene) return data;

    // --- Board dimensions ---
    QRectF bounds = scene->itemsBoundingRect();
    data.boardWidth = bounds.width();
    data.boardHeight = bounds.height();

    // --- Layer Stackup ---
    const auto& allLayers = PCBLayerManager::instance().layers();
    data.copperLayerCount = PCBLayerManager::instance().copperLayerCount();
    auto stackup = PCBLayerManager::instance().stackup();
    data.boardThickness = stackup.finishThickness;
    data.surfaceFinish = stackup.surfaceFinish;

    for (const auto& sl : stackup.stack) {
        DesignReportData::LayerInfo li;
        li.name = sl.name;
        li.type = sl.type;
        li.side = (sl.layerId == 0) ? "Top" : (sl.layerId == 1) ? "Bottom" : "Internal";
        li.thickness = sl.thickness;
        li.material = sl.material;
        li.copperWeightOz = sl.copperWeightOz;
        data.layers.append(li);
    }

    // Also add layers that might not be in stackup
    for (const auto& layer : allLayers) {
        bool found = false;
        for (const auto& li : data.layers) {
            if (li.name == layer.name()) { found = true; break; }
        }
        if (!found) {
            DesignReportData::LayerInfo li;
            li.name = layer.name();
            li.type = layer.typeString();
            li.side = layer.sideString();
            data.layers.append(li);
        }
    }

    // --- Components ---
    QMap<QString, int> typeCounts;
    QMap<QString, int> layerCounts;
    int smdCount = 0, thtCount = 0;

    for (auto* item : scene->items()) {
        if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            data.totalComponents++;
            typeCounts[comp->componentType()]++;
            int layerId = comp->layer();
            QString layerStr = (layerId == 1) ? "Bottom" : "Top";
            layerCounts[layerStr]++;
        }
    }
    data.componentsByType = typeCounts;
    data.componentsByLayer = layerCounts;

    // --- Traces, Vias, Pours ---
    for (auto* item : scene->items()) {
        if (dynamic_cast<TraceItem*>(item)) {
            data.totalTraceSegments++;
        } else if (auto* via = dynamic_cast<ViaItem*>(item)) {
            data.totalVias++;
            if (via->drillSize() > 0) data.drills++;
        } else if (dynamic_cast<CopperPourItem*>(item)) {
            data.copperPours++;
        }
    }

    // --- Nets ---
    QStringList netNames = LengthMeasurementEngine::getNetNames(scene);
    data.totalNets = netNames.size();

    // Measure net lengths
    auto netLengths = LengthMeasurementEngine::measureAllNets(scene);
    for (auto it = netLengths.begin(); it != netLengths.end(); ++it) {
        data.totalTraceLength += it.value().totalLength;
    }

    // --- Unrouted nets ---
    PCBRatsnestManager::instance().setScene(scene);
    PCBRatsnestManager::instance().update();

    int airwireCount = 0;
    QSet<QString> unroutedNetSet;
    for (auto* item : scene->items()) {
        if (auto* ratsnest = dynamic_cast<RatsnestItem*>(item)) {
            airwireCount++;
            unroutedNetSet.insert(ratsnest->netName());
        }
    }
    data.totalAirwires = airwireCount;
    data.unroutedNets = unroutedNetSet.size();

    // --- DRC ---
    PCBDRC drc;
    drc.runFullCheck(scene);
    data.drcErrors = drc.errorCount();
    data.drcWarnings = drc.warningCount();
    data.drcInfos = drc.infoCount();

    for (const auto& v : drc.violations()) {
        DesignReportData::DRCViolationInfo info;
        info.severity = v.severityString();
        info.type = v.typeString();
        info.message = v.message();
        info.location = QString("(%1, %2)").arg(v.location().x(), 0, 'f', 2)
                                            .arg(v.location().y(), 0, 'f', 2);
        data.drcViolations.append(info);
    }

    // --- Net Classes ---
    auto ncManager = NetClassManager::instance();
    for (const auto& nc : ncManager.classes()) {
        DesignReportData::NetClassInfo nci;
        nci.name = nc.name;
        nci.traceWidth = nc.traceWidth;
        nci.clearance = nc.clearance;
        nci.viaDiameter = nc.viaDiameter;
        nci.viaDrill = nc.viaDrill;
        // Count nets assigned to this class
        for (const QString& net : netNames) {
            if (ncManager.getClassForNet(net).name == nc.name) {
                nci.netCount++;
            }
        }
        data.netClasses.append(nci);
    }

    // --- BOM Summary ---
    // Generate a simple BOM from components on the board
    QMap<QString, DesignReportData::BOMSummary> bomMap;
    for (auto* item : scene->items()) {
        if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            QString key = comp->value() + "|" + comp->componentType();
            if (!bomMap.contains(key)) {
                DesignReportData::BOMSummary bs;
                bs.value = comp->value();
                bs.footprint = comp->componentType();
                bomMap[key] = bs;
            }
            bomMap[key].references.append(comp->name());
            bomMap[key].quantity++;
        }
    }
    for (auto it = bomMap.begin(); it != bomMap.end(); ++it) {
        data.bomSummary.append(it.value());
    }

    return data;
}

// ============================================================================
// HTML Generation
// ============================================================================

QString DesignReportGenerator::generateHTML(const DesignReportData& data, const ReportOptions& options) {
    QString html;
    QTextStream out(&html);

    out << "<!DOCTYPE html>\n<html><head>\n";
    out << "<meta charset=\"UTF-8\">\n";
    out << "<title>PCB Design Report - " << data.boardName << "</title>\n";
    out << "<style>\n";
    out << "body { font-family: 'Segoe UI', Arial, sans-serif; margin: 30px; color: #333; }\n";
    out << "h1 { color: #007acc; border-bottom: 3px solid #007acc; padding-bottom: 8px; }\n";
    out << "h2 { color: #28a745; border-bottom: 1px solid #ddd; padding-bottom: 4px; margin-top: 30px; }\n";
    out << "h3 { color: #555; margin-top: 20px; }\n";
    out << "table { border-collapse: collapse; width: 100%; margin: 10px 0; }\n";
    out << "th, td { border: 1px solid #ddd; padding: 8px 12px; text-align: left; }\n";
    out << "th { background-color: #007acc; color: white; font-weight: bold; }\n";
    out << "tr:nth-child(even) { background-color: #f8f9fa; }\n";
    out << ".error { color: #dc3545; font-weight: bold; }\n";
    out << ".warning { color: #ffc107; }\n";
    out << ".ok { color: #28a745; font-weight: bold; }\n";
    out << ".stat { font-size: 1.2em; font-weight: bold; }\n";
    out << ".footer { margin-top: 40px; padding-top: 10px; border-top: 1px solid #ddd; color: #888; font-size: 0.85em; }\n";
    out << "</style>\n</head><body>\n";

    // Header
    out << "<h1>PCB Design Report</h1>\n";
    out << "<table>\n";
    if (!options.projectName.isEmpty())
        out << "<tr><td><b>Project</b></td><td>" << options.projectName << "</td></tr>\n";
    if (!options.companyName.isEmpty())
        out << "<tr><td><b>Company</b></td><td>" << options.companyName << "</td></tr>\n";
    out << "<tr><td><b>Board</b></td><td>" << data.boardName << "</td></tr>\n";
    out << "<tr><td><b>Generated</b></td><td>" << data.generatedAt << "</td></tr>\n";
    out << "<tr><td><b>Board Size</b></td><td>" << QString::number(data.boardWidth, 'f', 2)
         << " x " << QString::number(data.boardHeight, 'f', 2) << " mm</td></tr>\n";
    out << "</table>\n";

    // --- Statistics ---
    if (options.includeStatistics) {
        out << "<h2>1. Design Statistics</h2>\n";
        out << "<table>\n";
        out << "<tr><td><b>Components</b></td><td class=\"stat\">" << data.totalComponents << "</td></tr>\n";
        out << "<tr><td><b>Nets</b></td><td class=\"stat\">" << data.totalNets << "</td></tr>\n";
        out << "<tr><td><b>Trace Segments</b></td><td class=\"stat\">" << data.totalTraceSegments << "</td></tr>\n";
        out << "<tr><td><b>Total Trace Length</b></td><td class=\"stat\">"
            << QString::number(data.totalTraceLength, 'f', 2) << " mm</td></tr>\n";
        out << "<tr><td><b>Vias</b></td><td class=\"stat\">" << data.totalVias << "</td></tr>\n";
        out << "<tr><td><b>Copper Pours</b></td><td class=\"stat\">" << data.copperPours << "</td></tr>\n";
        out << "<tr><td><b>Board Thickness</b></td><td>" << QString::number(data.boardThickness, 'f', 2) << " mm</td></tr>\n";
        out << "<tr><td><b>Surface Finish</b></td><td>" << (data.surfaceFinish.isEmpty() ? "N/A" : data.surfaceFinish) << "</td></tr>\n";
        out << "</table>\n";

        // Components by layer
        out << "<h3>Components by Layer</h3>\n<table>\n<tr><th>Layer</th><th>Count</th></tr>\n";
        for (auto it = data.componentsByLayer.begin(); it != data.componentsByLayer.end(); ++it) {
            out << "<tr><td>" << it.key() << "</td><td>" << it.value() << "</td></tr>\n";
        }
        out << "</table>\n";
    }

    // --- Layer Stackup ---
    if (options.includeLayers && !data.layers.isEmpty()) {
        out << "<h2>2. Layer Stackup</h2>\n";
        out << "<table>\n<tr><th>Layer</th><th>Type</th><th>Side</th><th>Thickness (mm)</th><th>Material</th><th>Cu Weight (oz)</th></tr>\n";
        for (const auto& li : data.layers) {
            out << "<tr><td>" << li.name << "</td><td>" << li.type << "</td><td>" << li.side
                << "</td><td>" << QString::number(li.thickness, 'f', 3)
                << "</td><td>" << (li.material.isEmpty() ? "—" : li.material)
                << "</td><td>" << (li.copperWeightOz > 0 ? QString::number(li.copperWeightOz, 'f', 1) : "—")
                << "</td></tr>\n";
        }
        out << "</table>\n";
    }

    // --- DRC Summary ---
    if (options.includeDRC) {
        out << "<h2>3. Design Rule Check (DRC)</h2>\n";
        out << "<table>\n";
        out << "<tr><td><b>Errors</b></td><td class=\"" << (data.drcErrors > 0 ? "error" : "ok")
            << "\">" << data.drcErrors << "</td></tr>\n";
        out << "<tr><td><b>Warnings</b></td><td class=\"" << (data.drcWarnings > 0 ? "warning" : "ok")
            << "\">" << data.drcWarnings << "</td></tr>\n";
        out << "<tr><td><b>Info</b></td><td>" << data.drcInfos << "</td></tr>\n";
        QString status = (data.drcErrors == 0) ? "<span class=\"ok\">✅ PASS</span>" : "<span class=\"error\">❌ FAIL</span>";
        out << "<tr><td><b>Status</b></td><td>" << status << "</td></tr>\n";
        out << "</table>\n";

        if (!data.drcViolations.isEmpty()) {
            out << "<h3>DRC Violations</h3>\n";
            out << "<table>\n<tr><th>Severity</th><th>Type</th><th>Message</th><th>Location</th></tr>\n";
            for (const auto& v : data.drcViolations) {
                QString cls = (v.severity == "Error") ? "error" : (v.severity == "Warning") ? "warning" : "";
                out << "<tr><td class=\"" << cls << "\">" << v.severity
                    << "</td><td>" << v.type
                    << "</td><td>" << v.message
                    << "</td><td>" << v.location << "</td></tr>\n";
            }
            out << "</table>\n";
        }
    }

    // --- Net Summary ---
    if (options.includeNets) {
        out << "<h2>4. Net Summary</h2>\n";
        out << "<table>\n";
        out << "<tr><td><b>Total Nets</b></td><td>" << data.totalNets << "</td></tr>\n";
        out << "<tr><td><b>Unrouted Nets</b></td><td class=\"" << (data.unroutedNets > 0 ? "warning" : "ok")
            << "\">" << data.unroutedNets << "</td></tr>\n";
        out << "<tr><td><b>Unrouted Airwires</b></td><td>" << data.totalAirwires << "</td></tr>\n";
        out << "<tr><td><b>Total Trace Length</b></td><td>" << QString::number(data.totalTraceLength, 'f', 2) << " mm</td></tr>\n";
        out << "</table>\n";
    }

    // --- Components ---
    if (options.includeComponents && !data.componentsByType.isEmpty()) {
        out << "<h2>5. Components by Type</h2>\n";
        out << "<table>\n<tr><th>Footprint Type</th><th>Count</th></tr>\n";
        // Sort by count descending
        auto sorted = data.componentsByType.values();
        auto keys = data.componentsByType.keys();
        std::sort(keys.begin(), keys.end(), [&data](const QString& a, const QString& b) {
            return data.componentsByType[a] > data.componentsByType[b];
        });
        for (const QString& key : keys) {
            out << "<tr><td>" << key << "</td><td>" << data.componentsByType[key] << "</td></tr>\n";
        }
        out << "</table>\n";
    }

    // --- Net Classes ---
    if (options.includeNetClasses && !data.netClasses.isEmpty()) {
        out << "<h2>6. Net Classes</h2>\n";
        out << "<table>\n<tr><th>Class</th><th>Trace Width (mm)</th><th>Clearance (mm)</th><th>Via ⌀ (mm)</th><th>Via Drill (mm)</th><th>Nets</th></tr>\n";
        for (const auto& nc : data.netClasses) {
            out << "<tr><td>" << nc.name << "</td><td>" << QString::number(nc.traceWidth, 'f', 2)
                << "</td><td>" << QString::number(nc.clearance, 'f', 2)
                << "</td><td>" << QString::number(nc.viaDiameter, 'f', 2)
                << "</td><td>" << QString::number(nc.viaDrill, 'f', 2)
                << "</td><td>" << nc.netCount << "</td></tr>\n";
        }
        out << "</table>\n";
    }

    // --- BOM Summary ---
    if (options.includeBOM && !data.bomSummary.isEmpty()) {
        out << "<h2>7. Bill of Materials (BOM) Summary</h2>\n";
        out << "<table>\n<tr><th>Value</th><th>Footprint</th><th>Refs</th><th>Qty</th></tr>\n";
        // Sort by reference count
        auto sorted = data.bomSummary;
        std::sort(sorted.begin(), sorted.end(), [](const DesignReportData::BOMSummary& a, const DesignReportData::BOMSummary& b) {
            return a.quantity > b.quantity;
        });
        for (const auto& bs : sorted) {
            out << "<tr><td>" << bs.value << "</td><td>" << bs.footprint
                << "</td><td>" << bs.references.join(", ")
                << "</td><td>" << bs.quantity << "</td></tr>\n";
        }
        out << "</table>\n";
    }

    // Footer
    out << "<div class=\"footer\">\n";
    out << "<p>Generated by <b>VioraEDA</b> on " << data.generatedAt << "</p>\n";
    if (!options.companyName.isEmpty())
        out << "<p>" << options.companyName << " — Confidential</p>\n";
    out << "</div>\n";

    out << "</body></html>";
    return html;
}

// ============================================================================
// File Generation
// ============================================================================

bool DesignReportGenerator::generateReport(QGraphicsScene* scene, const QString& filePath,
                                            const ReportOptions& options, QString* errorMessage) {
    DesignReportData data = collectData(scene);
    QString html = generateHTML(data, options);

    if (options.format == HTML) {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMessage) *errorMessage = "Cannot open output file: " + filePath;
            return false;
        }
        QTextStream out(&file);
        out << html;
        return true;
    }

    // PDF format
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Portrait);
    printer.setPageMargins(15, 15, 15, 15, QPageLayout::Millimeter);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&printer);

    return true;
}
