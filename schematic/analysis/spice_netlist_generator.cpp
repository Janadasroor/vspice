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
#include "../../simulator/core/sim_value_parser.h"

using Flux::Model::SymbolDefinition;

namespace {

QString spicetypeToString(SimComponentType type) {
    switch (type) {
        case SimComponentType::Diode:           return "D";
        case SimComponentType::BJT_NPN:         return "NPN";
        case SimComponentType::BJT_PNP:         return "PNP";
        case SimComponentType::MOSFET_NMOS:     return "NMOS";
        case SimComponentType::MOSFET_PMOS:     return "PMOS";
        case SimComponentType::JFET_NJF:        return "NJF";
        case SimComponentType::JFET_PJF:        return "PJF";
        case SimComponentType::Switch:          return "SW";
        case SimComponentType::CSW:             return "CSW";
        default: return "";
    }
}

QString modelToSpiceLine(const SimModel& model) {
    const QString typeStr = spicetypeToString(model.type);
    if (typeStr.isEmpty()) return QString();

    QSet<QString> allowed;
    switch (model.type) {
        case SimComponentType::Diode:
            allowed = {"IS", "N", "RS", "VJ", "CJO", "M", "TT", "BV", "IBV"};
            break;
        case SimComponentType::BJT_NPN:
        case SimComponentType::BJT_PNP:
            allowed = {"IS", "BF", "BR", "VAF", "VAR", "CJE", "CJC", "TF", "TR", "RB", "RE", "RC"};
            break;
        case SimComponentType::MOSFET_NMOS:
        case SimComponentType::MOSFET_PMOS:
            allowed = {"VTO", "KP", "LAMBDA", "RD", "RS", "RG", "CGSO", "CGDO", "CGBO", "CBD", "CBS", "PB", "GAMMA", "PHI", "LEVEL"};
            break;
        case SimComponentType::JFET_NJF:
        case SimComponentType::JFET_PJF:
            allowed = {"BETA", "VTO", "LAMBDA", "RD", "RS", "CGS", "CGD", "IS", "PB", "FC"};
            break;
        default:
            break;
    }

    QString line = QString(".model %1 %2(").arg(
        QString::fromStdString(model.name), typeStr);
    bool first = true;
    for (const auto& [key, val] : model.params) {
        const QString qkey = QString::fromStdString(key).toUpper();
        if (!allowed.isEmpty() && !allowed.contains(qkey)) continue;
        if (!first) line += " ";
        line += QString("%1=%2").arg(
            qkey,
            QString::number(val, 'g', 12));
        first = false;
    }
    line += ")";
    return line;
}

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

QString currentSaveVectorForRef(const QString& spiceRef) {
    const QString ref = spiceRef.trimmed();
    if (ref.isEmpty()) return QString();

    const QChar prefix = ref.at(0).toUpper();
    switch (prefix.unicode()) {
    case 'R':
    case 'C':
    case 'L':
    case 'D':
    case 'B':
        return QString("@%1[i]").arg(ref);
    case 'Q':
        return QString("@%1[ic]").arg(ref);
    case 'M':
    case 'J':
    case 'Z':
        return QString("@%1[id]").arg(ref);
    default:
        return QString();
    }
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

QString sanitizeDirectiveName(const QString& raw) {
    QString s = raw;
    s.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    s.replace(QRegularExpression("_+"), "_");
    s.remove(QRegularExpression("^_+"));
    s.remove(QRegularExpression("_+$"));
    return s.isEmpty() ? QString("m") : s.left(40);
}

QString normalizeMeanDirective(const QString& cmd) {
    static const QRegularExpression re(
        "^\\s*\\.mean\\s+(?:(avg|max|min|rms)\\s+)?([^\\s]+)(?:\\s+from\\s*=\\s*([^\\s]+))?(?:\\s+to\\s*=\\s*([^\\s]+))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(cmd.trimmed());
    if (!m.hasMatch()) return cmd;

    const QString mode = m.captured(1).isEmpty() ? QString("avg") : m.captured(1).toLower();
    const QString signal = m.captured(2).trimmed();
    const QString from = m.captured(3).trimmed();
    const QString to = m.captured(4).trimmed();
    if (signal.isEmpty()) return cmd;

    const QString name = QString("mean_%1_%2_%3")
        .arg(mode, sanitizeDirectiveName(signal))
        .arg(QString::number(qHash(cmd.toLower()), 16));

    QString out = QString(".meas tran %1 %2 %3").arg(name, mode, signal);
    if (!from.isEmpty()) out += QString(" from=%1").arg(from);
    if (!to.isEmpty()) out += QString(" to=%1").arg(to);
    return out;
}
}

QString SpiceNetlistGenerator::generate(QGraphicsScene* scene, const QString& projectDir, NetManager* /*netManager*/, const SimulationParams& params) {
    if (!scene) return "* Missing scene\n";

    QString netlist;
    netlist += "* viospice Automated Hierarchical SPICE Netlist\n";
    netlist += "* Generated on " + QDateTime::currentDateTime().toString() + "\n\n";

    // 0. Append SPICE Directives from schematic at the TOP 
    // This ensures .params and .model are defined before use
    netlist += "* Custom SPICE Directives\n";
    QSet<QString> switchModelsAdded;
    bool hasCustomTran = false;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->itemType() == SchematicItem::SpiceDirectiveType) {
                if (auto* dir = dynamic_cast<SchematicSpiceDirectiveItem*>(si)) {
                    QString cmd = dir->text().trimmed();
                    if (!cmd.isEmpty()) {
                        if (cmd.startsWith(".model", Qt::CaseInsensitive)) {
                            // Extract model name to prevent auto-generator duplicates
                            QStringList parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                            if (parts.size() >= 2) {
                                switchModelsAdded.insert(parts[1].toLower());
                            }
                        }
                        
                        if (cmd.startsWith(".mean", Qt::CaseInsensitive)) {
                            const QString converted = normalizeMeanDirective(cmd);
                            if (converted != cmd) {
                                netlist += "* " + cmd + "\n";
                                netlist += converted + "\n";
                            } else {
                                netlist += cmd + "\n";
                            }
                        } else {
                            netlist += cmd + "\n";
                        }
                        
                        if (cmd.toLower().startsWith(".tran")) {
                            hasCustomTran = true;
                        }
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

    // Collect include paths from symbol metadata (subcircuit .inc/.lib)
    for (const auto& comp : pkg.components) {
        SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(comp.typeName);
        if (!sym) continue;
        if (!sym->modelPath().isEmpty()) {
            const QString resolved = resolveModelPath(sym->modelPath(), projectDir);
            if (!resolved.isEmpty()) includePaths.insert(resolved);
        }
    }

    // Auto-embed .model lines for referenced component models
    QStringList embeddedModelLines;
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) continue;
        QString modelName = comp.value.trimmed();
        const QString typeLower = comp.typeName.trimmed().toLower();
        const bool isJfet = (typeLower == "njf" || typeLower == "pjf") ||
                            comp.reference.startsWith("J", Qt::CaseInsensitive);
        const bool isMos = (typeLower == "transistor_nmos" || typeLower == "transistor_pmos" ||
                            typeLower == "nmos" || typeLower == "nmos4" ||
                            typeLower == "pmos" || typeLower == "pmos4") ||
                           comp.reference.startsWith("M", Qt::CaseInsensitive);
        const bool isBjt = (typeLower == "transistor" || typeLower == "transistor_pnp" ||
                            typeLower == "npn" || typeLower == "npn2" || typeLower == "npn3" || typeLower == "npn4" ||
                            typeLower == "pnp" || typeLower == "pnp2" || typeLower == "pnp4" || typeLower == "lpnp") ||
                           comp.reference.startsWith("Q", Qt::CaseInsensitive);
        if (modelName.isEmpty() && isJfet) {
            modelName = (typeLower == "pjf" || comp.reference.startsWith("JP", Qt::CaseInsensitive)) ? "PJF" : "NJF";
        }
        if (modelName.isEmpty() && isBjt) {
            modelName = (typeLower == "transistor_pnp" || typeLower == "pnp" || typeLower == "pnp2" ||
                         typeLower == "pnp4" || typeLower == "lpnp" ||
                         comp.reference.startsWith("QP", Qt::CaseInsensitive)) ? "2N3906" : "2N2222";
        }
        if (modelName.isEmpty() && isMos) {
            modelName = (typeLower == "transistor_pmos" || typeLower == "pmos" || typeLower == "pmos4" ||
                         comp.reference.startsWith("MP", Qt::CaseInsensitive)) ? "BS250" : "2N7000";
        }
        if (modelName.isEmpty()) continue;
        if (switchModelsAdded.contains(modelName.toLower())) continue;

        const SimModel* mdl = ModelLibraryManager::instance().findModel(modelName);
        if (mdl) {
            const QString line = modelToSpiceLine(*mdl);
            if (!line.isEmpty()) {
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (comp.reference.startsWith("D", Qt::CaseInsensitive)) {
            // Generate .model from component paramExpressions for user-customized diodes
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                QString line = QString(".model %1 D(").arg(modelName);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("diode.Is");
                addParam("diode.N");
                addParam("diode.Rs");
                addParam("diode.Vj");
                addParam("diode.Cjo");
                addParam("diode.M");
                addParam("diode.tt");
                addParam("diode.BV");
                addParam("diode.IBV");

                // Strip "diode." prefix for SPICE format
                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("diode.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (isJfet) {
            // Generate .model from component paramExpressions for user-customized JFETs
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                const QString modelType = (typeLower == "pjf" || comp.reference.startsWith("JP", Qt::CaseInsensitive)) ? "PJF" : "NJF";
                QString line = QString(".model %1 %2(").arg(modelName, modelType);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("jfet.Beta");
                addParam("jfet.Vto");
                addParam("jfet.Lambda");
                addParam("jfet.Rd");
                addParam("jfet.Rs");
                addParam("jfet.Cgs");
                addParam("jfet.Cgd");
                addParam("jfet.Is");

                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("jfet.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (isBjt) {
            // Generate .model from component paramExpressions for user-customized BJTs
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                const QString bjtTypeExpr = pe.value("bjt.type").trimmed();
                const bool pnpFromExpr = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0;
                const QString modelType = (pnpFromExpr ||
                                           typeLower == "transistor_pnp" || typeLower == "pnp" || typeLower == "pnp2" ||
                                           typeLower == "pnp4" || typeLower == "lpnp" ||
                                           comp.reference.startsWith("QP", Qt::CaseInsensitive)) ? "PNP" : "NPN";
                QString line = QString(".model %1 %2(").arg(modelName, modelType);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    const QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("bjt.Is");
                addParam("bjt.Bf");
                addParam("bjt.Vaf");
                addParam("bjt.Cje");
                addParam("bjt.Cjc");
                addParam("bjt.Tf");
                addParam("bjt.Tr");

                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("bjt.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        } else if (isMos) {
            // Generate .model from component paramExpressions for user-customized MOSFETs
            const auto& pe = comp.paramExpressions;
            if (!pe.isEmpty()) {
                const QString mosTypeExpr = pe.value("mos.type").trimmed();
                const bool pmosFromExpr = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0;
                const QString modelType = (pmosFromExpr ||
                                           typeLower == "transistor_pmos" || typeLower == "pmos" || typeLower == "pmos4" ||
                                           comp.reference.startsWith("MP", Qt::CaseInsensitive)) ? "PMOS" : "NMOS";
                QString line = QString(".model %1 %2(").arg(modelName, modelType);
                QStringList params;
                auto addParam = [&](const QString& key) {
                    const QString val = pe.value(key).trimmed();
                    if (!val.isEmpty()) {
                        params.append(QString("%1=%2").arg(key, val));
                    }
                };
                addParam("mos.Vto");
                addParam("mos.Kp");
                addParam("mos.Lambda");
                addParam("mos.Rd");
                addParam("mos.Rs");
                addParam("mos.Cgso");
                addParam("mos.Cgdo");

                for (int i = 0; i < params.size(); ++i) {
                    params[i].replace("mos.", "");
                }

                line += params.join(" ") + ")";
                embeddedModelLines.append(line);
                switchModelsAdded.insert(modelName.toLower());
            }
        }
    }

    // Write .include directives (subcircuit/model files from symbol metadata)
    if (!includePaths.isEmpty()) {
        QStringList includeList = includePaths.values();
        includeList.sort();

        QStringList libRoots = ConfigManager::instance().libraryRoots();

        netlist += "* Model Includes\n";
        for (const QString& inc : includeList) {
            QString relPath = inc;
            for (const QString& root : libRoots) {
                if (!root.isEmpty() && inc.startsWith(root, Qt::CaseInsensitive)) {
                    relPath = QDir(root).relativeFilePath(inc);
                    break;
                }
            }
            if (QDir::isAbsolutePath(relPath)) {
                relPath = QFileInfo(inc).fileName();
            }
            netlist += QString(".include \"%1\"\n").arg(relPath);
        }
        netlist += "\n";
    }

    // Write embedded .model lines
    if (!embeddedModelLines.isEmpty()) {
        netlist += "* Embedded Models\n";
        for (const QString& ml : embeddedModelLines) {
            netlist += ml + "\n";
        }
        netlist += "\n";
    }

    // 3. Export components
    QMap<QString, QString> powerNetVoltages;
    QStringList savedCurrentVectors;
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

        // Helper to ensure proper SPICE prefix without doubling it
        auto ensurePrefix = [](const QString& r, const QString& p) -> QString {
            if (r.startsWith(p, Qt::CaseInsensitive)) return r;
            return p + r;
        };

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
        if (type == SchematicItem::ResistorType) line = ensurePrefix(ref, "R");
        else if (type == SchematicItem::CapacitorType) line = ensurePrefix(ref, "C");
        else if (type == SchematicItem::InductorType) line = ensurePrefix(ref, "L");
        else if (type == SchematicItem::DiodeType) line = ensurePrefix(ref, "D");
        else if (type == SchematicItem::TransistorType) line = ensurePrefix(ref, "Q");
        else if (type == SchematicItem::VoltageSourceType) {
            if (comp.value.trimmed().startsWith("V=", Qt::CaseInsensitive)) line = ensurePrefix(ref, "B");
            else line = ensurePrefix(ref, "V");
        }
        else if (comp.typeName.toLower().contains("gate") || comp.typeName.toLower().contains("digital")) {
            line = ensurePrefix(ref, "A"); // XSPICE A-device
        }
        else line = ensurePrefix(ref, "X"); // Subcircuit or generic
        // Fallback: if we don't know the type but reference has a known prefix,
        // use the reference as-is to avoid invalid X-lines.
        if (line.startsWith("X") && !ref.isEmpty()) {
            const QChar p = ref.at(0).toUpper();
            const QString known = "RCLVIDQMBEGFHJZ";
            if (known.contains(p)) {
                line = ref;
            }
        }
        const QString currentSaveVector = currentSaveVectorForRef(line);
        if (!currentSaveVector.isEmpty() && !savedCurrentVectors.contains(currentSaveVector, Qt::CaseInsensitive)) {
            savedCurrentVectors.append(currentSaveVector);
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
                    // Don't use modelName if it's just the device prefix (e.g., "D", "Q", "X")
                    const QString mn = sym->modelName();
                    if (mn.length() > 1 || mn.toLower() != line.left(1).toLower()) {
                        value = mn;
                    }
                }
            }

            if (!sym->modelName().isEmpty()) {
                // Skip warning if modelName is just the device prefix letter
                const QString mn = sym->modelName();
                bool isPrefixOnly = (mn.length() == 1 && mn.toLower() == line.left(1).toLower());
                if (!isPrefixOnly) {
                    const SimModel* mdl = ModelLibraryManager::instance().findModel(mn);
                    const SimSubcircuit* sub = ModelLibraryManager::instance().findSubcircuit(mn);
                    if (!mdl && !sub) {
                        netlist += QString("* Warning: Model '%1' not found for %2\n").arg(mn, ref);
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
            }
            
            auto mapping = sym->spiceNodeMapping();
            if (!mapping.isEmpty()) {
                // Mapping is Node Index -> Pin Name
                // But componentPins uses pin NUMBER as key, not pin name
                // So we try both: first the name, then the number as fallback
                QList<int> sortedIndices = mapping.keys();
                std::sort(sortedIndices.begin(), sortedIndices.end());
                for (int idx : sortedIndices) {
                    QString pinName = mapping[idx];
                    // Try pin name first
                    QString net = pins.value(pinName, "0").replace(" ", "_");
                    if (net == "0") {
                        // Fallback: try numeric pin number
                        net = pins.value(QString::number(idx), "0").replace(" ", "_");
                    }
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

        const bool isCurrentSource = (comp.type == SchematicItem::CurrentSourceType);
        if (isCurrentSource) {
            VoltageParasitics paras = stripVoltageParasitics(value);
            value = paras.value;
        }

        const bool isBehavioralCurrentSource = (comp.typeName.compare("Current_Source_Behavioral", Qt::CaseInsensitive) == 0) ||
                                              (comp.typeName.compare("bi", Qt::CaseInsensitive) == 0) ||
                                              (comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0);
        if (isBehavioralCurrentSource) {
            QString n1 = nodes.value(0, "0");
            QString n2 = nodes.value(1, "0");

            const QString arrowDir = comp.paramExpressions.value("bi.arrow_direction").trimmed().toLower();
            const bool swapForUpArrow = (arrowDir == "up") || (comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0);
            if (swapForUpArrow) {
                const QString tmp = n1;
                n1 = n2;
                n2 = tmp;
            }

            QString expr = value.trimmed();
            if (expr.isEmpty()) expr = "I=0";
            if (!expr.startsWith("I=", Qt::CaseInsensitive)) expr = "I=" + expr;

            QString bref = ref;
            if (!bref.startsWith("B", Qt::CaseInsensitive)) bref = "B" + ref;
            netlist += QString("%1 %2 %3 %4\n").arg(bref, n1, n2, expr);
            continue;
        }

        const bool isVCVS = (comp.typeName.compare("e", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("vcvs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("E", Qt::CaseInsensitive);
        const bool isVCCS = (comp.typeName.compare("g", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("vccs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("G", Qt::CaseInsensitive);

        if ((isVCVS || isVCCS)) {
            // Build nodes from pin numbers (pins map uses numeric keys "1","2","3","4")
            QStringList vcNodes;
            for (int i = 1; i <= 4; i++) {
                vcNodes.append(pins.value(QString::number(i), "0").replace(" ", "_"));
            }

            QString gain = value.trimmed();
            // Reject placeholder defaults like "E", "G", "g2", "vcvs", "vccs"
            const QString typeLower = comp.typeName.trimmed().toLower();
            const QString gainLower = gain.toLower();
            if (gain.isEmpty() || gainLower == typeLower ||
                gainLower == "e" || gainLower == "g" || gainLower == "g2" ||
                gainLower == "vcvs" || gainLower == "vccs") {
                gain = "1";
            }

            QString eref = ref;
            const QString pref = isVCVS ? "E" : "G";
            if (!eref.startsWith(pref, Qt::CaseInsensitive)) eref = pref + ref;
            netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(eref, vcNodes[0], vcNodes[1], vcNodes[2], vcNodes[3], gain);
            continue;
        }

        const bool isCCCS = (comp.typeName.compare("f", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("cccs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("F", Qt::CaseInsensitive);
        const bool isCCVS = (comp.typeName.compare("h", Qt::CaseInsensitive) == 0) ||
                            (comp.typeName.compare("ccvs", Qt::CaseInsensitive) == 0) ||
                            ref.startsWith("H", Qt::CaseInsensitive);

        if ((isCCCS || isCCVS) && nodes.size() >= 2) {
            const QString n1 = nodes.at(0);
            const QString n2 = nodes.at(1);

            // Expecting value to be "VSOURCE GAIN" or similar
            QString controlSource;
            QString gainVal = "1";
            
            QStringList parts = value.split(" ", Qt::SkipEmptyParts);
            if (parts.size() >= 1) {
                controlSource = parts[0];
                if (parts.size() >= 2) gainVal = parts[1];
            } else {
                controlSource = "V_UNKNOWN_CTRL"; 
            }

            // Apply V-prefix rule for control source
            if (!controlSource.startsWith("V", Qt::CaseInsensitive)) {
                controlSource = "V" + controlSource;
            }

            QString eref = ref;
            const QString pref = isCCCS ? "F" : "H";
            if (!eref.startsWith(pref, Qt::CaseInsensitive)) eref = pref + ref;
            netlist += QString("%1 %2 %3 %4 %5\n").arg(eref, n1, n2, controlSource, gainVal);
            continue;
        }

        const bool isLosslessTLine = (comp.typeName.compare("tline", Qt::CaseInsensitive) == 0) ||
                                     ref.startsWith("T", Qt::CaseInsensitive);
        const bool isLossyTLine = (comp.typeName.compare("ltline", Qt::CaseInsensitive) == 0) ||
                                  ref.startsWith("O", Qt::CaseInsensitive);
        if ((isLosslessTLine || isLossyTLine) && nodes.size() >= 4) {
            const QString n1 = nodes.at(0);
            const QString n2 = nodes.at(1);
            const QString n3 = nodes.at(2);
            const QString n4 = nodes.at(3);

            if (isLossyTLine) {
                QString modelName = value.trimmed();
                if (modelName.isEmpty() || modelName.compare("LTRA", Qt::CaseInsensitive) == 0) {
                    modelName = "LTRAmod";
                }
                const QString r = comp.paramExpressions.value("ltra.R").trimmed();
                const QString l = comp.paramExpressions.value("ltra.L").trimmed();
                const QString g = comp.paramExpressions.value("ltra.G").trimmed();
                const QString c = comp.paramExpressions.value("ltra.C").trimmed();
                const QString len = comp.paramExpressions.value("ltra.LEN").trimmed();

                QStringList modelTokens;
                if (!r.isEmpty()) modelTokens << QString("R=%1").arg(r);
                if (!l.isEmpty()) modelTokens << QString("L=%1").arg(l);
                if (!g.isEmpty()) modelTokens << QString("G=%1").arg(g);
                if (!c.isEmpty()) modelTokens << QString("C=%1").arg(c);
                if (!len.isEmpty()) modelTokens << QString("LEN=%1").arg(len);

                if (!modelTokens.isEmpty() && !switchModelsAdded.contains(modelName.toLower())) {
                    netlist += QString(".model %1 LTRA(%2)\n").arg(modelName, modelTokens.join(" "));
                    switchModelsAdded.insert(modelName.toLower());
                }

                QString oref = ref;
                if (!oref.startsWith("O", Qt::CaseInsensitive)) oref = "O" + ref;
                netlist += QString("%1 %2 %3 %4 %5 %6\n").arg(oref, n1, n2, n3, n4, modelName);
            } else {
                QString z0 = "50";
                QString td = "50n";
                const QString v = value.trimmed();
                const QRegularExpression reZ0("\\bZ0\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                const QRegularExpression reTd("\\bTd\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                auto mz = reZ0.match(v);
                auto mt = reTd.match(v);
                if (mz.hasMatch()) z0 = mz.captured(1);
                if (mt.hasMatch()) td = mt.captured(1);

                QString tref = ref;
                if (!tref.startsWith("T", Qt::CaseInsensitive)) tref = "T" + ref;
                netlist += QString("%1 %2 %3 %4 %5 Z0=%6 Td=%7\n").arg(tref, n1, n2, n3, n4, z0, td);
            }
            continue;
        }

        const bool isNJF = (comp.typeName.compare("njf", Qt::CaseInsensitive) == 0);
        const bool isPJF = (comp.typeName.compare("pjf", Qt::CaseInsensitive) == 0);
        const bool isJFET = isNJF || isPJF || ref.startsWith("J", Qt::CaseInsensitive);
        if (isJFET && nodes.size() >= 3) {
            QString jref = ref;
            if (!jref.startsWith("J", Qt::CaseInsensitive)) jref = "J" + ref;

            QString model = value.trimmed();
            if (model.isEmpty() || model.compare("njf", Qt::CaseInsensitive) == 0 || model.compare("pjf", Qt::CaseInsensitive) == 0) {
                model = isPJF ? "2N5460" : "2N3819";
            }

            if (!switchModelsAdded.contains(model.toLower()) && !ModelLibraryManager::instance().findModel(model)) {
                const QString typeToken = isPJF ? "PJF" : "NJF";
                const QString vto = isPJF ? "2" : "-2";
                netlist += QString(".model %1 %2(Beta=1m Vto=%3 Lambda=0.02 Rd=1 Rs=1 Cgs=2p Cgd=1p Is=1e-14)\n")
                    .arg(model, typeToken, vto);
                switchModelsAdded.insert(model.toLower());
            }

            const QString d = nodes.at(0);
            const QString g = nodes.at(1);
            const QString s = nodes.at(2);
            netlist += QString("%1 %2 %3 %4 %5\n").arg(jref, d, g, s, model);
            continue;
        }

        const bool isMesfet = (comp.typeName.compare("mesfet", Qt::CaseInsensitive) == 0) ||
                               ref.startsWith("Z", Qt::CaseInsensitive);
        if (isMesfet && nodes.size() >= 3) {
            QString zref = ref;
            if (!zref.startsWith("Z", Qt::CaseInsensitive)) zref = "Z" + ref;

            QString model = value.trimmed();
            if (model.isEmpty()) model = "NMF";

            if (!switchModelsAdded.contains(model.toLower()) && !ModelLibraryManager::instance().findModel(model)) {
                const bool pchannel = model.compare("PMF", Qt::CaseInsensitive) == 0;
                const QString mtype = pchannel ? "PMF" : "NMF";
                const QString vto = pchannel ? "2.1" : "-2.1";
                netlist += QString(".model %1 %2(Vto=%3 Beta=0.05 Lambda=0.02 Alpha=3 B=0.5 Rd=1 Rs=1 Cgs=1p Cgd=0.2p)\n")
                    .arg(model, mtype, vto);
                switchModelsAdded.insert(model.toLower());
            }

            const QString d = nodes.at(0);
            const QString g = nodes.at(1);
            const QString s = nodes.at(2);
            netlist += QString("%1 %2 %3 %4 %5\n").arg(zref, d, g, s, model);
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

        const bool isCSW = (comp.typeName.compare("csw", Qt::CaseInsensitive) == 0) || ref.startsWith("W", Qt::CaseInsensitive);
        const bool isSwitch = (comp.typeName.compare("Switch", Qt::CaseInsensitive) == 0) ||
                              (comp.typeName.compare("sw", Qt::CaseInsensitive) == 0) ||
                              ref.startsWith("SW", Qt::CaseInsensitive) ||
                              ref.startsWith("S", Qt::CaseInsensitive) ||
                              isCSW;
        if (isSwitch) {
            // If the symbol provides control pins, treat it as a voltage-controlled switch.
            if (nodes.size() >= 4 && !isCSW) {
                const QString n1 = nodes.at(0);
                const QString n2 = nodes.at(1);
                const QString ctrlp = nodes.at(2);
                const QString ctrln = nodes.at(3);

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
            if (isCSW) {
                QString modelName = comp.paramExpressions.value("switch.model_name").trimmed();
                QString controlSource = comp.paramExpressions.value("switch.control_source").trimmed();

                if (modelName.isEmpty() && !value.isEmpty()) {
                    QStringList parts = value.split(" ", Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts[0].startsWith("V", Qt::CaseInsensitive)) {
                        if (controlSource.isEmpty()) controlSource = parts[0];
                        modelName = parts[1];
                    } else if (parts.size() >= 1) {
                        modelName = parts[0];
                    }
                }

                if (modelName.isEmpty()) modelName = QString("CSW_%1").arg(ref);

                QString ron = comp.paramExpressions.value("switch.ron").trimmed();
                if (ron.isEmpty()) ron = comp.paramExpressions.value("csw.ron").trimmed();
                if (ron.isEmpty()) ron = "1";
                
                QString roff = comp.paramExpressions.value("switch.roff").trimmed();
                if (roff.isEmpty()) roff = comp.paramExpressions.value("csw.roff").trimmed();
                if (roff.isEmpty()) roff = "1Meg";
                
                QString it = comp.paramExpressions.value("switch.it").trimmed();
                if (it.isEmpty()) it = comp.paramExpressions.value("csw.it").trimmed();
                if (it.isEmpty()) it = "1m";
                
                QString ih = comp.paramExpressions.value("switch.ih").trimmed();
                if (ih.isEmpty()) ih = comp.paramExpressions.value("csw.ih").trimmed();
                if (ih.isEmpty()) ih = "0.2m";

                if (!switchModelsAdded.contains(modelName.toLower())) {
                    netlist += QString(".model %1 CSW(Ron=%2 Roff=%3 It=%4 Ih=%5)\n")
                                   .arg(modelName, ron, roff, it, ih);
                    switchModelsAdded.insert(modelName.toLower());
                }

                if (controlSource.isEmpty()) {
                    controlSource = "V_UNKNOWN_CTRL"; // Placeholder if user didn't specify
                } else if (!controlSource.startsWith("V", Qt::CaseInsensitive)) {
                    controlSource = "V" + controlSource;
                }
                QString switchRef = ref;
                if (!switchRef.startsWith("W", Qt::CaseInsensitive)) switchRef = "W" + ref;
                netlist += QString("%1 %2 %3 %4 %5\n").arg(switchRef, n1, n2, controlSource, modelName);
                continue;
            }

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

        QStringList emittedNodes = nodes;
        if (line.startsWith("Q", Qt::CaseInsensitive) && emittedNodes.size() == 4) {
            const QString sub = emittedNodes.at(3).trimmed();
            if (sub.isEmpty() || sub == "0") {
                emittedNodes[3] = emittedNodes.at(2);
            }
        }

        for (const QString& node : emittedNodes) {
            line += " " + node;
        }

        // ngspice MOSFET requires 4 nodes: D G S B. For 3-pin symbols, tie body to source.
        if (line.startsWith("M", Qt::CaseInsensitive) && emittedNodes.size() == 3) {
            line += " " + emittedNodes.at(2);
        }

        // Add value
        if (value.isEmpty()) {
            if (line.startsWith("D")) {
                // Generate default .model for diodes with no model specified
                QString defaultModel = QString("D_DEFAULT_%1").arg(ref);
                netlist += QString(".model %1 D(Is=2.52n N=1.752 Rs=0.568 Vj=0.7 Cjo=4p M=0.4 tt=20n)\n")
                    .arg(defaultModel);
                value = defaultModel;
            } else if (line.startsWith("M", Qt::CaseInsensitive)) {
                const QString mosTypeExpr = comp.paramExpressions.value("mos.type").trimmed();
                const bool pmosAlias = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                                       comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0 ||
                                       ref.startsWith("MP", Qt::CaseInsensitive);
                value = pmosAlias ? "BS250" : "2N7000";
            } else if (line.startsWith("Q")) {
                const QString bjtTypeExpr = comp.paramExpressions.value("bjt.type").trimmed();
                const bool pnpAlias = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("transistor_pnp", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp2", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("pnp4", Qt::CaseInsensitive) == 0 ||
                                      comp.typeName.compare("lpnp", Qt::CaseInsensitive) == 0 ||
                                      ref.startsWith("QP", Qt::CaseInsensitive);
                value = pnpAlias ? "2N3906" : "2N2222";
            } else {
                value = "1k"; // Default for R/C/L
            }
        } else if (line.startsWith("M", Qt::CaseInsensitive) && (value.compare("NMOS", Qt::CaseInsensitive) == 0 || value.compare("PMOS", Qt::CaseInsensitive) == 0)) {
            value = (value.compare("PMOS", Qt::CaseInsensitive) == 0) ? "BS250" : "2N7000";
        } else if (line.startsWith("Q") && (value.compare("NPN", Qt::CaseInsensitive) == 0 || value.compare("PNP", Qt::CaseInsensitive) == 0)) {
            value = (value.compare("PNP", Qt::CaseInsensitive) == 0) ? "2N3906" : "2N2222";
        }

        if (line.startsWith("M", Qt::CaseInsensitive) && !switchModelsAdded.contains(value.toLower()) && !ModelLibraryManager::instance().findModel(value)) {
            const QString mosTypeExpr = comp.paramExpressions.value("mos.type").trimmed();
            const bool pmosModel = mosTypeExpr.compare("PMOS", Qt::CaseInsensitive) == 0 ||
                                   value.compare("BS250", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                                   comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0 ||
                                   ref.startsWith("MP", Qt::CaseInsensitive);
            const QString mosType = pmosModel ? "PMOS" : "NMOS";
            const QString vto = pmosModel ? "-2" : "2";
            netlist += QString(".model %1 %2(Vto=%3 Kp=100u Lambda=0.02 Rd=1 Rs=1 Cgso=50p Cgdo=50p)\n")
                .arg(value, mosType, vto);
            switchModelsAdded.insert(value.toLower());
        }

        if (line.startsWith("Q") && !switchModelsAdded.contains(value.toLower()) && !ModelLibraryManager::instance().findModel(value)) {
            const QString bjtTypeExpr = comp.paramExpressions.value("bjt.type").trimmed();
            const bool pnpModel = bjtTypeExpr.compare("PNP", Qt::CaseInsensitive) == 0 ||
                                  value.compare("2N3906", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("transistor_pnp", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp2", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("pnp4", Qt::CaseInsensitive) == 0 ||
                                  comp.typeName.compare("lpnp", Qt::CaseInsensitive) == 0 ||
                                  ref.startsWith("QP", Qt::CaseInsensitive);
            const QString bjtType = pnpModel ? "PNP" : "NPN";
            netlist += QString(".model %1 %2(Is=1e-14 Bf=100 Vaf=100 Cje=8p Cjc=3p Tf=400p Tr=50n)\n")
                .arg(value, bjtType);
            switchModelsAdded.insert(value.toLower());
        }
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
            if (!hasCustomTran) {
                netlist += QString(".tran %1 %2\n").arg(params.step, params.stop);
            }
            break;
        case DC:
            netlist += QString(".dc %1 %2 %3 %4\n").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
            break;
        case AC:
            {
                auto safeNumber = [](const QString& text, double fallback) {
                    double parsed = 0.0;
                    if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                        return text.trimmed();
                    }
                    return QString::number(fallback, 'g', 12);
                };

                const QString pts = safeNumber(params.step, 10.0);
                const QString start = safeNumber(params.start, 10.0);
                const QString stop = safeNumber(params.stop, 1e6);
                netlist += QString(".ac dec %1 %2 %3\n").arg(pts, start, stop);
            }
            break;
        case OP:
            netlist += ".op\n";
            break;
        case Noise:
            {
                const QString output = params.noiseOutput.isEmpty() ? "V(out)" : params.noiseOutput;
                const QString source = params.noiseSource.isEmpty() ? "V1" : params.noiseSource;
                const QString pts = params.step.isEmpty() ? "10" : params.step;
                const QString fstart = params.start.isEmpty() ? "1" : params.start;
                const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
                netlist += QString(".noise %1 %2 %3 %4 %5\n").arg(output, source, pts, fstart, fstop);
            }
            break;
        case Fourier:
            {
                const QString freq = params.fourFreq.isEmpty() ? "1k" : params.fourFreq;
                QStringList outputs = params.fourOutputs;
                if (outputs.isEmpty()) outputs << "V(out)";
                netlist += QString(".four %1 %2\n").arg(freq, outputs.join(" "));
            }
            break;
        case TF:
            {
                const QString output = params.tfOutput.isEmpty() ? "V(out)" : params.tfOutput;
                const QString source = params.tfSource.isEmpty() ? "V1" : params.tfSource;
                netlist += QString(".tf %1 %2\n").arg(output, source);
            }
            break;
        case Disto:
            {
                const QString pts = params.step.isEmpty() ? "10" : params.step;
                const QString fstart = params.start.isEmpty() ? "1" : params.start;
                const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
                if (!params.distoF2OverF1.isEmpty()) {
                    netlist += QString(".disto %1 %2 %3 %4\n").arg(pts, fstart, fstop, params.distoF2OverF1);
                } else {
                    netlist += QString(".disto %1 %2 %3\n").arg(pts, fstart, fstop);
                }
            }
            break;
        case Meas:
            if (!params.measRaw.isEmpty()) {
                netlist += params.measRaw + "\n";
            }
            break;
        case Step:
            if (!params.stepRaw.isEmpty()) {
                netlist += params.stepRaw + "\n";
            }
            break;
        case Sens:
            {
                const QString output = params.sensOutput.isEmpty() ? "V(out)" : params.sensOutput;
                netlist += QString(".sens %1\n").arg(output);
            }
            break;
        case FFT:
            // FFT is handled post-simulation, not a SPICE directive itself
            break;
    }

    netlist += ".save all\n";
    for (const QString& saveVec : savedCurrentVectors) {
        netlist += QString(".save %1\n").arg(saveVec);
    }
    netlist += ".control\nrun\n.endc\n.end\n";
    return netlist;
}

QString SpiceNetlistGenerator::buildCommand(const SimulationParams& params) {
    switch (params.type) {
        case Transient:
            return QString(".tran %1 %2").arg(params.step, params.stop);
        case DC:
            return QString(".dc %1 %2 %3 %4").arg(params.dcSource, params.dcStart, params.dcStop, params.dcStep);
        case AC: {
            auto safeNumber = [](const QString& text, double fallback) {
                double parsed = 0.0;
                if (SimValueParser::parseSpiceNumber(text, parsed) && parsed > 0.0) {
                    return text.trimmed();
                }
                return QString::number(fallback, 'g', 12);
            };
            const QString pts = safeNumber(params.step, 10.0);
            const QString start = safeNumber(params.start, 10.0);
            const QString stop = safeNumber(params.stop, 1e6);
            return QString(".ac dec %1 %2 %3").arg(pts, start, stop);
        }
        case OP:
            return ".op";
        case Noise: {
            const QString output = params.noiseOutput.isEmpty() ? "V(out)" : params.noiseOutput;
            const QString source = params.noiseSource.isEmpty() ? "V1" : params.noiseSource;
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString fstart = params.start.isEmpty() ? "1" : params.start;
            const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
            return QString(".noise %1 %2 %3 %4 %5").arg(output, source, pts, fstart, fstop);
        }
        case Fourier: {
            const QString freq = params.fourFreq.isEmpty() ? "1k" : params.fourFreq;
            QStringList outputs = params.fourOutputs;
            if (outputs.isEmpty()) outputs << "V(out)";
            return QString(".four %1 %2").arg(freq, outputs.join(" "));
        }
        case TF: {
            const QString output = params.tfOutput.isEmpty() ? "V(out)" : params.tfOutput;
            const QString source = params.tfSource.isEmpty() ? "V1" : params.tfSource;
            return QString(".tf %1 %2").arg(output, source);
        }
        case Disto: {
            const QString pts = params.step.isEmpty() ? "10" : params.step;
            const QString fstart = params.start.isEmpty() ? "1" : params.start;
            const QString fstop = params.stop.isEmpty() ? "1Meg" : params.stop;
            if (!params.distoF2OverF1.isEmpty()) {
                return QString(".disto %1 %2 %3 %4").arg(pts, fstart, fstop, params.distoF2OverF1);
            }
            return QString(".disto %1 %2 %3").arg(pts, fstart, fstop);
        }
        case Meas:
            return params.measRaw.isEmpty() ? ".meas" : params.measRaw;
        case Step:
            return params.stepRaw.isEmpty() ? ".step" : params.stepRaw;
        case Sens: {
            const QString output = params.sensOutput.isEmpty() ? "V(out)" : params.sensOutput;
            return QString(".sens %1").arg(output);
        }
        case FFT:
            return ".fft";
    }
    return ".op";
}

QString SpiceNetlistGenerator::formatValue(double value) {
    if (value <= 0) return "0";
    if (value < 1e-9) return QString::number(value * 1e12) + "p";
    if (value < 1e-6) return QString::number(value * 1e9) + "n";
    if (value < 1e-3) return QString::number(value * 1e06) + "u";
    if (value < 1) return QString::number(value * 1e3) + "m";
    return QString::number(value);
}
