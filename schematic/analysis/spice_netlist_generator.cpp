#include "spice_netlist_generator.h"
#include "../items/schematic_item.h"
#include "net_manager.h"
#include "../io/netlist_generator.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/models/symbol_definition.h"
#include <QGraphicsScene>
#include <QSet>
#include <QMap>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>

using Flux::Model::SymbolDefinition;

namespace {
QString pickPowerNetName(const QMap<QString, QString>& pins, const QString& fallbackValue) {
    QString netName = pins.value("1").trimmed();
    if (!netName.isEmpty()) return netName;

    for (auto it = pins.constBegin(); it != pins.constEnd(); ++it) {
        const QString candidate = it.value().trimmed();
        if (!candidate.isEmpty()) return candidate;
    }
    return fallbackValue.trimmed();
}

QString inferPowerVoltage(const QString& netName, const QString& valueText) {
    // Allow explicit numeric overrides such as "12", "3.3", "5V", "12v".
    const QString raw = valueText.trimmed();
    if (!raw.isEmpty()) {
        static const QRegularExpression numWithOptionalV(
            QStringLiteral("^([+-]?\\d*\\.?\\d+)\\s*[vV]?$"));
        const QRegularExpressionMatch m = numWithOptionalV.match(raw);
        if (m.hasMatch()) return m.captured(1);
    }

    const QString upperNet = netName.toUpper();
    if (upperNet.contains("12V")) return "12";
    if (upperNet.contains("9V")) return "9";
    if (upperNet.contains("5V")) return "5";
    if (upperNet.contains("3.3V") || upperNet.contains("3V3")) return "3.3";
    if (upperNet.contains("1.8V") || upperNet.contains("1V8")) return "1.8";
    if (upperNet.contains("VBAT") || upperNet.contains("BAT")) return "3.7";
    return "5";
}
}

QString SpiceNetlistGenerator::generate(QGraphicsScene* scene, const QString& projectDir, NetManager* /*netManager*/, const SimulationParams& params) {
    if (!scene) return "* Missing scene\n";

    QString netlist;
    netlist += "* Viora EDA Automated Hierarchical SPICE Netlist\n";
    netlist += "* Generated on " + QDateTime::currentDateTime().toString() + "\n\n";

    // 1. Get Flattened ECO Package (Components)
    ECOPackage pkg = NetlistGenerator::generateECOPackage(scene, projectDir, nullptr);
    
    // 2. Get Flattened Connectivity (Nets)
    QList<NetlistNet> nets = NetlistGenerator::buildConnectivity(scene, projectDir, nullptr);

    // Build mapping: ComponentRef -> map(PinName -> NetName)
    QMap<QString, QMap<QString, QString>> componentPins;
    for (const auto& net : nets) {
        QString netName = net.name;
        // SPICE ground is always 0
        if (netName.toUpper() == "GND" || netName == "0") netName = "0";
        
        for (const auto& pin : net.pins) {
            componentPins[pin.componentRef][pin.pinName] = netName;
        }
    }

    // 3. Export components
    QMap<QString, QString> powerNetVoltages;
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) {
            netlist += "* Skipping " + comp.reference + " (Excluded from simulation)\n";
            continue;
        }

        QString ref = comp.reference;
        int type = comp.type;
        
        // Handle Power Items separately
        if (type == SchematicItem::PowerType) {
            const QMap<QString, QString> pins = componentPins.value(ref);
            QString netName = pickPowerNetName(pins, comp.value);
            if (netName.isEmpty()) {
                qWarning() << "SPICE: skipping power symbol with empty net name:" << ref;
                continue;
            }
            if (netName.toUpper() != "GND" && netName != "0") {
                const QString v = inferPowerVoltage(netName, comp.value);
                powerNetVoltages[netName] = v;
            }
            continue;
        }

        QString line;
        
        // Determine SPICE prefix
        if (type == SchematicItem::ResistorType) line = "R" + ref;
        else if (type == SchematicItem::CapacitorType) line = "C" + ref;
        else if (type == SchematicItem::InductorType) line = "L" + ref;
        else if (type == SchematicItem::DiodeType) line = "D" + ref;
        else if (type == SchematicItem::TransistorType) line = "Q" + ref;
        else if (type == SchematicItem::VoltageSourceType) line = "V" + ref;
        else line = "X" + ref; // Subcircuit or generic

        // Map pins to nodes
        QMap<QString, QString> pins = componentPins.value(ref);
        
        // --- SPICE Mapper Logic ---
        QString value = comp.value;
        if (!comp.spiceModel.isEmpty()) value = comp.spiceModel;
        QStringList nodes;
        
        // Find Symbol definition to check for custom mapping
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (sym) {
            if (!sym->spiceModelName().isEmpty()) value = sym->spiceModelName();
            
            auto mapping = sym->spiceNodeMapping();
            if (!mapping.isEmpty()) {
                // Mapping is Node Index -> Pin Name
                QList<int> sortedIndices = mapping.keys();
                std::sort(sortedIndices.begin(), sortedIndices.end());
                for (int idx : sortedIndices) {
                    QString pinName = mapping[idx];
                    QString net = pins.value(pinName, "0"); // Default to 0 if not connected
                    nodes.append(net.replace(" ", "_"));
                }
            }
        }
        
        if (nodes.isEmpty() && type == SchematicItem::TransistorType) {
            // Hardcoded mapping for TransistorItem if no symbol definition provides it
            // TransistorItem pins: 0=B/G, 1=C/D, 2=E/S
            // ngspice expects: BJT=C B E, MOSFET=D G S
            QString b_g = pins.value("B", pins.value("G", "0")).replace(" ", "_");
            QString c_d = pins.value("C", pins.value("D", "0")).replace(" ", "_");
            QString e_s = pins.value("E", pins.value("S", "0")).replace(" ", "_");
            
            nodes.append(c_d);
            nodes.append(b_g);
            nodes.append(e_s);
        }

        if (nodes.isEmpty()) {
            // Default: Fallback to alphabetical sorting of pins
            QStringList sortedKeys = pins.keys();
            std::sort(sortedKeys.begin(), sortedKeys.end());
            
            if (sortedKeys.isEmpty()) {
                netlist += "* Skipping " + ref + " (no connections)\n";
                continue;
            }

            for (const QString& pk : sortedKeys) {
                QString net = pins[pk];
                if (net.isEmpty()) net = "NC_" + ref;
                nodes.append(net.replace(" ", "_"));
            }
        }

        for (const QString& node : nodes) {
            line += " " + node;
        }

        // Add value
        if (value.isEmpty()) value = "1k"; // Default
        line += " " + value + "\n";
        
        netlist += line;
    }

    // 4. Generate Voltage Sources for Power Rails
    netlist += "\n* Power Supply Rails\n";
    for (auto it = powerNetVoltages.constBegin(); it != powerNetVoltages.constEnd(); ++it) {
        QString net = it.key();
        QString voltage = it.value();
        if (net.trimmed().isEmpty()) continue;
        
        QString spiceNet = QString(net).replace(" ", "_");
        netlist += QString("V_%1 %1 0 DC %2\n").arg(spiceNet, voltage);
    }

    // Add simulation command
    netlist += "\n";
    switch (params.type) {
        case Transient:
            netlist += QString(".tran %1 %2\n").arg(params.step, params.stop);
            break;
        case DC:
            netlist += QString(".dc %1 %2 %3 %4\n").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
            break;
        case AC:
            netlist += QString(".ac dec 10 %1 %2\n").arg(params.start, params.stop);
            break;
        case OP:
            netlist += ".op\n";
            break;
    }

    netlist += ".control\nrun\n.endc\n.end\n";
    return netlist;
}

QString SpiceNetlistGenerator::formatValue(double value) {
    if (value <= 0) return "0";
    if (value < 1e-9) return QString::number(value * 1e12) + "p";
    if (value < 1e-6) return QString::number(value * 1e9) + "n";
    if (value < 1e-3) return QString::number(value * 1e06) + "u";
    if (value < 1) return QString::number(value * 1e3) + "m";
    return QString::number(value);
}
