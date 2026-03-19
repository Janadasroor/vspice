#include "spice_netlist_generator.h"
#include "../items/schematic_item.h"
#include "net_manager.h"
#include "../io/netlist_generator.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/models/symbol_definition.h"
#include "../../simulator/bridge/model_library_manager.h"
#include "../items/schematic_spice_directive_item.h"
#include <QGraphicsScene>
#include <QSet>
#include <QMap>
#include <QDebug>
#include <QDateTime>
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <cmath>
#include "../../core/config_manager.h"

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

QString inlinePwlFileIfNeeded(const QString& value, const QString& projectDir) {
    const QString v = value.trimmed();
    if (!v.contains("PWL", Qt::CaseInsensitive)) return value;

    QRegularExpression reFile1("PWL\\s*\\([^\\)]*FILE\\s*=\\s*\\\"([^\\\"]+)\\\"[^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reFile2("PWL\\s*\\([^\\)]*FILE\\s*=\\s*([^\\)\\s]+)[^\\)]*\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression reFile3("PWL\\s+FILE\\s+\\\"([^\\\"]+)\\\"", QRegularExpression::CaseInsensitiveOption);

    QString path;
    auto m1 = reFile1.match(v);
    if (m1.hasMatch()) path = m1.captured(1);
    else {
        auto m2 = reFile2.match(v);
        if (m2.hasMatch()) path = m2.captured(1);
        else {
            auto m3 = reFile3.match(v);
            if (m3.hasMatch()) path = m3.captured(1);
        }
    }

    if (path.isEmpty()) return value;
    QFileInfo fi(path);
    if (fi.isRelative() && !projectDir.isEmpty()) {
        path = QDir(projectDir).filePath(path);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return value;
    }

    QStringList tokens;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith("*") || line.startsWith("#") || line.startsWith(";")) continue;
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            tokens << parts[0] << parts[1];
        }
    }
    file.close();

    if (tokens.isEmpty()) return value;

    QString inlined = QString("PWL(%1)").arg(tokens.join(' '));

    QRegularExpression reRepeat("\\bR\\s*=\\s*[-+]?\\d*\\.?\\d+|\\bREPEAT\\b", QRegularExpression::CaseInsensitiveOption);
    if (reRepeat.match(v).hasMatch()) {
        inlined += " r=0";
    }

    return inlined;
}

QString formatPwlValueForNetlist(const QString& value, int maxLen = 140) {
    const QString v = value.trimmed();
    if (!v.startsWith("PWL", Qt::CaseInsensitive)) return value;

    int closeIdx = v.lastIndexOf(')');
    if (closeIdx < 0) return value;

    QString tail = v.mid(closeIdx + 1).trimmed();
    QString inside = v.left(closeIdx + 1);
    int openIdx = inside.indexOf('(');
    if (openIdx < 0) return value;

    const QString head = inside.left(openIdx + 1); // "PWL("
    const QString body = inside.mid(openIdx + 1, inside.length() - openIdx - 2);
    QStringList tokens = body.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return value;

    QStringList lines;
    QString current = head;
    for (const QString& token : tokens) {
        const int extra = token.length() + 1;
        if (current.length() + extra > maxLen && current != head) {
            lines << current.trimmed();
            current = "+ " + token;
        } else {
            if (!current.endsWith("(") && !current.endsWith("+ ")) current += " ";
            current += token;
        }
    }

    current += ")";
    if (!tail.isEmpty()) current += " " + tail;
    lines << current.trimmed();

    return lines.join("\n");
}

struct VoltageParasitics {
    QString value;
    QString rser;
    QString cpar;
};

static VoltageParasitics stripVoltageParasitics(const QString& value) {
    VoltageParasitics out{value, "", ""};
    QRegularExpression rserRe("\\bRser\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression cparRe("\\bCpar\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);

    auto rserMatch = rserRe.match(out.value);
    if (rserMatch.hasMatch()) {
        out.rser = rserMatch.captured(1).trimmed();
        out.value.remove(rserRe);
    }
    auto cparMatch = cparRe.match(out.value);
    if (cparMatch.hasMatch()) {
        out.cpar = cparMatch.captured(1).trimmed();
        out.value.remove(cparRe);
    }
    out.value = out.value.simplified();
    return out;
}

QString resolveModelPath(const QString& modelPath, const QString& projectDir) {
    if (modelPath.trimmed().isEmpty()) return QString();
    QFileInfo fi(modelPath);
    if (fi.isAbsolute()) return fi.absoluteFilePath();

    const QString source = modelPath;
    if (!projectDir.isEmpty()) {
        const QString candidate = QDir(projectDir).filePath(source);
        if (QFileInfo::exists(candidate)) return candidate;
    }

    const QStringList roots = ConfigManager::instance().libraryRoots();
    for (const QString& root : roots) {
        if (root.trimmed().isEmpty()) continue;
        const QString candidate = QDir(root).filePath(source);
        if (QFileInfo::exists(candidate)) return candidate;
    }

    // Fallback: default Viospice subcircuit library
    {
        const QString candidate = QDir(QDir::homePath() + "/ViospiceLib/sub").filePath(source);
        if (QFileInfo::exists(candidate)) return candidate;
    }

    return modelPath;
}
}

QString SpiceNetlistGenerator::generate(QGraphicsScene* scene, const QString& projectDir, NetManager* /*netManager*/, const SimulationParams& params) {
    if (!scene) return "* Missing scene\n";

    QString netlist;
    netlist += "* Viora EDA Automated Hierarchical SPICE Netlist\n";
    netlist += "* Generated on " + QDateTime::currentDateTime().toString() + "\n\n";

    // 0. Append SPICE Directives from schematic at the TOP 
    // This ensures .params and .model are defined before use
    netlist += "* Custom SPICE Directives\n";
    for (QGraphicsItem* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->itemType() == SchematicItem::SpiceDirectiveType) {
                if (auto* dir = dynamic_cast<SchematicSpiceDirectiveItem*>(si)) {
                    QString cmd = dir->text().trimmed();
                    if (!cmd.isEmpty()) {
                        netlist += cmd + "\n";
                    }
                }
            }
        }
    }
    netlist += "\n";

    // 0.5 Collect model includes from symbols
    QSet<QString> includePaths;

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

    // Collect include paths from symbol metadata
    for (const auto& comp : pkg.components) {
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (!sym) continue;
        if (!sym->modelPath().isEmpty()) {
            const QString resolved = resolveModelPath(sym->modelPath(), projectDir);
            if (!resolved.isEmpty()) includePaths.insert(resolved);
        }
    }

    if (!includePaths.isEmpty()) {
        QStringList includeList = includePaths.values();
        includeList.sort();
        netlist += "* Model Includes\n";
        for (const QString& inc : includeList) {
            netlist += QString(".include \"%1\"\n").arg(inc);
        }
        netlist += "\n";
    }

    // 3. Export components
    QSet<QString> switchModelsAdded;
    QMap<QString, QString> powerNetVoltages;
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) {
            netlist += "* Skipping " + comp.reference + " (Excluded from simulation)\n";
            continue;
        }

        QString ref = comp.reference;
        int type = comp.type;
        QMap<QString, QString> pins = componentPins.value(ref);
        
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
        bool isInstrument = (comp.typeName == "OscilloscopeInstrument" ||
                             comp.typeName == "Oscilloscope Instrument" ||
                             comp.typeName == "VoltmeterInstrument" ||
                             comp.typeName == "Voltmeter (DC)" ||
                             comp.typeName == "Voltmeter (AC)" ||
                             comp.typeName == "AmmeterInstrument" ||
                             comp.typeName == "Ammeter (DC)" ||
                             comp.typeName == "Ammeter (AC)" ||
                             comp.typeName == "WattmeterInstrument" ||
                             comp.typeName == "Wattmeter" ||
                             comp.typeName == "FrequencyCounterInstrument" ||
                             comp.typeName == "Frequency Counter" ||
                             comp.typeName == "LogicProbeInstrument" ||
                             comp.typeName == "Logic Probe");

        if (isInstrument) {
            QStringList keys = pins.keys();
            std::sort(keys.begin(), keys.end());
            for (const QString& pk : keys) {
                QString node = pins[pk].replace(" ", "_");
                if (node.isEmpty() || node.toUpper().startsWith("NC")) continue;
                if (node == "0") continue; // No need to ground ground
                
                netlist += QString("R_%1_%2 %3 0 100Meg\n").arg(ref, pk, node);
            }
            continue;
        }

        // Determine SPICE prefix
        if (type == SchematicItem::ResistorType) line = "R" + ref;
        else if (type == SchematicItem::CapacitorType) line = "C" + ref;
        else if (type == SchematicItem::InductorType) line = "L" + ref;
        else if (type == SchematicItem::DiodeType) line = "D" + ref;
        else if (type == SchematicItem::TransistorType) line = "Q" + ref;
        else if (type == SchematicItem::VoltageSourceType) {
            if (comp.value.trimmed().startsWith("V=", Qt::CaseInsensitive)) line = "B" + ref;
            else line = "V" + ref;
        }
        else if (comp.typeName.toLower().contains("gate") || comp.typeName.toLower().contains("digital")) {
            line = "A" + ref; // XSPICE A-device
        }
        else line = "X" + ref; // Subcircuit or generic

        // Fallback: if we don't know the type but reference has a known prefix,
        // use the reference as-is to avoid invalid X-lines.
        if (line.startsWith("X") && !ref.isEmpty()) {
            const QChar p = ref.at(0).toUpper();
            const QString known = "RCLVIDQMB";
            if (known.contains(p)) {
                line = ref;
            }
        }

        // --- SPICE Mapper Logic ---
        QString value = comp.value;
        if (!comp.spiceModel.isEmpty()) value = comp.spiceModel;
        value = inlinePwlFileIfNeeded(value, projectDir);
        value = formatPwlValueForNetlist(value);
        QStringList nodes;
        
        // Find Symbol definition to check for custom mapping
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (sym) {
            if (!sym->spiceModelName().isEmpty() && comp.spiceModel.isEmpty()) value = sym->spiceModelName();

            if (!sym->modelName().isEmpty() && comp.spiceModel.isEmpty()) {
                if (line.startsWith("X") || line.startsWith("D") || line.startsWith("Q")) {
                    value = sym->modelName();
                }
            }

            if (!sym->modelName().isEmpty()) {
                const SimModel* mdl = ModelLibraryManager::instance().findModel(sym->modelName());
                const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(sym->modelName());
                if (!mdl && !sub) {
                    netlist += QString("* Warning: Model '%1' not found for %2\n").arg(sym->modelName(), ref);
                } else if (sub) {
                    const int symPins = sym->connectionPoints().size();
                    const int subPins = static_cast<int>(sub->pinNames.size());
                    if (symPins > 0 && subPins > 0 && symPins != subPins) {
                        netlist += QString("* Warning: Pin count mismatch for %1 (symbol %2 vs subckt %3)\n")
                                       .arg(ref)
                                       .arg(symPins)
                                       .arg(subPins);
                    }
                }
            }
            
            auto mapping = sym->spiceNodeMapping();
            if (!mapping.isEmpty()) {
                // Mapping is Node Index -> Pin Name
                QList<int> sortedIndices = mapping.keys();
                std::sort(sortedIndices.begin(), sortedIndices.end());
                for (int idx : sortedIndices) {
                    QString pinName = mapping[idx];
                    QString net = pins.value(pinName, "0").replace(" ", "_");
                    nodes.append(net);
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

            // XSPICE A-device vector grouping: [in1 in2 ...] out
            bool isADevice = line.startsWith("A");
            if (isADevice) {
                QStringList inputs;
                QStringList outputs;
                for (const QString& pk : sortedKeys) {
                    QString net = pins[pk].replace(" ", "_");
                    if (net.isEmpty()) net = "NC_" + ref;
                    
                    if (pk.toLower().contains("in") || pk.toLower().startsWith("a") || pk.toLower().startsWith("b")) 
                        inputs.append(net);
                    else 
                        outputs.append(net);
                }
                
                if (!inputs.isEmpty()) {
                    nodes.append("[" + inputs.join(" ") + "]");
                }
                nodes.append(outputs);
            } else {
                for (const QString& pk : sortedKeys) {
                    QString net = pins[pk];
                    if (net.isEmpty()) net = "NC_" + ref;
                    nodes.append(net.replace(" ", "_"));
                }
            }
        }

        // Strip unsupported voltage parasitics and emit separate elements for ngspice.
        const bool isVoltageSource = (type == SchematicItem::VoltageSourceType) ||
                                     comp.typeName.startsWith("Voltage_Source", Qt::CaseInsensitive);
        if (isVoltageSource) {
            VoltageParasitics paras = stripVoltageParasitics(value);
            value = paras.value;
            const bool hasRser = !paras.rser.isEmpty() && paras.rser != "0" && paras.rser != "0.0";
            const bool hasCpar = !paras.cpar.isEmpty() && paras.cpar != "0" && paras.cpar != "0.0";
            if ((hasRser || hasCpar) && nodes.size() >= 2) {
                QString n1 = nodes.value(0, "0");
                QString n2 = nodes.value(1, "0");
                QString srcPos = n1;
                if (hasRser) {
                    QString nInt = QString("VSR_%1").arg(ref);
                    nInt.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
                    netlist += QString("R_%1 %2 %3 %4\n").arg(ref, n1, nInt, paras.rser);
                    srcPos = nInt;
                }
                if (hasCpar) {
                    netlist += QString("C_%1 %2 %3 %4\n").arg(ref, srcPos, n2, paras.cpar);
                }
                nodes[0] = srcPos;
                nodes[1] = n2;
            }
        }

        const bool isBehavioralCurrentSource = (comp.typeName.compare("Current_Source_Behavioral", Qt::CaseInsensitive) == 0);
        if (isBehavioralCurrentSource) {
            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            QString expr = value.trimmed();
            if (expr.isEmpty()) expr = "I=0";
            if (!expr.startsWith("I=", Qt::CaseInsensitive)) expr = "I=" + expr;

            QString bref = ref;
            if (!bref.startsWith("B", Qt::CaseInsensitive)) bref = "B" + ref;
            netlist += QString("%1 %2 %3 %4\n").arg(bref, n1, n2, expr);
            continue;
        }

        const bool isVoltageControlledSwitch = (comp.typeName.compare("Voltage Controlled Switch", Qt::CaseInsensitive) == 0);
        if (isVoltageControlledSwitch) {
            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            const QString ctrlp = nodes.value(2, "0");
            const QString ctrln = nodes.value(3, "0");

            QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
            if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

            QString ron = comp.paramExpressions.value("switch.ron").trimmed();
            if (ron.isEmpty()) ron = "0.1";
            QString roff = comp.paramExpressions.value("switch.roff").trimmed();
            if (roff.isEmpty()) roff = "1Meg";
            QString vt = comp.paramExpressions.value("switch.vt").trimmed();
            if (vt.isEmpty()) vt = "0.5";
            QString vh = comp.paramExpressions.value("switch.vh").trimmed();
            if (vh.isEmpty()) vh = "0.1";

            if (!switchModelsAdded.contains(modelName)) {
                netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                               .arg(modelName, ron, roff, vt, vh);
                switchModelsAdded.insert(modelName);
            }

            QString switchRef = ref;
            if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
            netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(switchRef, n1, n2, ctrlp, ctrln, modelName);
            continue;
        }

        const bool isSwitch = (comp.typeName.compare("Switch", Qt::CaseInsensitive) == 0) ||
                              (comp.typeName.compare("sw", Qt::CaseInsensitive) == 0) ||
                              ref.startsWith("SW", Qt::CaseInsensitive) ||
                              ref.startsWith("S", Qt::CaseInsensitive);
        if (isSwitch) {
            // If the symbol provides control pins, treat it as a voltage-controlled switch.
            if (nodes.size() >= 4) {
                const QString n1 = nodes.value(0, "0");
                const QString n2 = nodes.value(1, "0");
                const QString ctrlp = nodes.value(2, "0");
                const QString ctrln = nodes.value(3, "0");

                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = "0.1";
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                QString vt = comp.paramExpressions.value("switch.vt").trimmed();
                if (vt.isEmpty()) vt = "0.5";
                QString vh = comp.paramExpressions.value("switch.vh").trimmed();
                if (vh.isEmpty()) vh = "0.1";

                if (!switchModelsAdded.contains(modelName)) {
                    netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                                   .arg(modelName, ron, roff, vt, vh);
                    switchModelsAdded.insert(modelName);
                }

                QString switchRef = ref;
                if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
                netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(switchRef, n1, n2, ctrlp, ctrln, modelName);
                continue;
            }

            const QString n1 = nodes.value(0, "0");
            const QString n2 = nodes.value(1, "0");
            const QString useModelExpr = comp.paramExpressions.value("switch.use_model").trimmed();
            const bool useModel = (useModelExpr == "1" || useModelExpr.compare("true", Qt::CaseInsensitive) == 0);

            if (useModel) {
                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                if (modelName.isEmpty()) modelName = QString("SW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = "0.1";
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                QString vt = comp.paramExpressions.value("switch.vt").trimmed();
                if (vt.isEmpty()) vt = "0.5";
                QString vh = comp.paramExpressions.value("switch.vh").trimmed();
                if (vh.isEmpty()) vh = "0.1";

                if (!switchModelsAdded.contains(modelName)) {
                    netlist += QString(".model %1 SW(Ron=%2 Roff=%3 Vt=%4 Vh=%5)\n")
                                   .arg(modelName, ron, roff, vt, vh);
                    switchModelsAdded.insert(modelName);
                }

                QString switchRef = ref;
                if (!switchRef.startsWith("S", Qt::CaseInsensitive)) switchRef = "S" + ref;
                QString ctlNode = QString("SWCTL_%1").arg(ref);

                const QString stateExpr = comp.paramExpressions.value("switch.state").trimmed().toLower();
                const bool isOpen = (stateExpr.isEmpty() ? true : (stateExpr == "open"));

                bool okVt = false;
                bool okVh = false;
                const double vtNum = vt.toDouble(&okVt);
                const double vhNum = vh.toDouble(&okVh);
                const double vhAbs = okVh ? std::abs(vhNum) : 0.1;
                const double vtBase = okVt ? vtNum : 0.5;
                const double high = vtBase + vhAbs + 0.1;
                const double low = vtBase - vhAbs - 0.1;
                const double controlV = isOpen ? low : high;

                QString vref = QString("VSW_%1").arg(ref);
                if (!vref.startsWith("V", Qt::CaseInsensitive)) vref = "V" + vref;

                netlist += QString("%1 %2 %3 %4 0 %5\n").arg(switchRef, n1, n2, ctlNode, modelName);
                netlist += QString("%1 %2 0 DC %3\n").arg(vref, ctlNode, QString::number(controlV, 'g', 6));
                continue;
            }

            QString switchRef = ref;
            if (!switchRef.startsWith("R", Qt::CaseInsensitive)) switchRef = "R" + ref;
            QString switchValue = value.isEmpty() ? "1e12" : value;
            netlist += QString("%1 %2 %3 %4\n").arg(switchRef, n1, n2, switchValue);
            continue;
        }

        for (const QString& node : nodes) {
            line += " " + node;
        }

        // Add value
        if (value.isEmpty()) value = "1k"; // Default
        line += " " + value;
        if (!value.endsWith("\n")) line += "\n";
        
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

    // 5. Simulation command
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

    netlist += ".control\nrun\n.save all\n.endc\n.end\n";
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
