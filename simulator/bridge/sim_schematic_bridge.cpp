#include "sim_schematic_bridge.h"
#include "../../schematic/io/netlist_generator.h"
#include "../../schematic/analysis/schematic_connectivity.h"
#include "../../schematic/items/schematic_item.h"
#include "../../schematic/items/smart_signal_item.h"
#include "../core/sim_model_parser.h"
#include "../core/sim_value_parser.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <QRegularExpression>
#include <QHash>
#include <algorithm>
#include <map>
#include <vector>

namespace {
bool isGroundNet(const QString& name) {
    static const QSet<QString> gndAliases = {
        "GND", "AGND", "DGND", "PGND", "0", "VSS", "COM", "GROUND",
        "VREFN", "VREF-", "REF-", "RETURN", "RTN"
    };
    QString n = name.trimmed().toUpper();
    return gndAliases.contains(n);
}

bool isPowerNet(const QString& name) {
    static const QSet<QString> pwrAliases = {
        "VCC", "VDD", "VPP", "VBAT", "VUSB", "V+", "VCC_AUX", "VIN",
        "VCC_IO", "VCORE", "AVCC", "DVCC", "VREF", "VREFP", "VREF+", "REF+"
    };
    QString n = name.trimmed().toUpper();
    if (pwrAliases.contains(n)) return true;
    
    // Check for voltage-style names like "3V3", "5V", "+12V"
    static QRegularExpression vReg("^(\\+?\\d+V\\d*|\\+?\\d+V|\\+?\\d+\\.?\\d*V)$", QRegularExpression::CaseInsensitiveOption);
    if (vReg.match(n).hasMatch()) return true;

    // Check for numeric-only node names like "12", "5", "3.3"
    // (provided it's not "0" which is ground)
    bool okNum = false;
    double val = n.toDouble(&okNum);
    if (okNum && val != 0.0) return true;

    return false;
}

double inferSupplyVoltageFromName(const QString& name) {
    QString n = name.trimmed().toUpper();
    if (n.isEmpty()) return 0.0;

    // First try: Plain numeric "12", "3.3" or "12V", "5v"
    double val = 0.0;
    if (SimValueParser::parseSpiceNumber(n, val)) return val;

    // Second try: Regex for "12V", "5V" etc (redundant with parseSpiceNumber but safer for weird cases)
    static QRegularExpression reVolts("^([+-]?\\d+\\.?\\d*)\\s*V", QRegularExpression::CaseInsensitiveOption);
    auto m = reVolts.match(n);
    if (m.hasMatch()) return m.captured(1).toDouble();

    // Third try: Common aliases
    if (n.contains("3V3")) return 3.3;
    if (n.contains("1V8")) return 1.8;
    if (n.contains("VBAT") || n.contains("BAT")) return 3.7;
    if (n.contains("VCC") || n.contains("VDD") || n.contains("VIN") || n.contains("VUSB")) return 5.0;
    
    return 0.0;
}

void parseVoltageSourceWaveform(const QString& raw, SimComponentInstance& inst) {
    const QString spec = raw.trimmed();
    if (spec.isEmpty()) return;

    auto cap = [&](const QString& pattern) -> QStringList {
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(spec);
        if (!m.hasMatch()) return {};
        return m.captured(1).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    };

    // DC value, supports "DC 5", "5", "5V"
    {
        QRegularExpression dcRe("^\\s*dc\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
        auto m = dcRe.match(spec);
        if (m.hasMatch()) {
            double dcVal = 0.0;
            if (SimValueParser::parseSpiceNumber(m.captured(1), dcVal)) {
                inst.params["voltage"] = dcVal;
            }
        }
    }

    if (!inst.params.count("voltage")) {
        double rawVal = 0.0;
        if (SimValueParser::parseSpiceNumber(spec, rawVal)) {
            inst.params["voltage"] = rawVal;
        }
    }

    // SIN(offset amplitude freq delay phase)
    {
        const QStringList t = cap("sine\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 1.0;
            double v = 0.0;
            inst.params["v_offset"] = (t.size() > 0 && SimValueParser::parseSpiceNumber(t[0], v)) ? v : 0.0;
            inst.params["v_ampl"] = (t.size() > 1 && SimValueParser::parseSpiceNumber(t[1], v)) ? v : 1.0;
            inst.params["v_freq"] = (t.size() > 2 && SimValueParser::parseSpiceNumber(t[2], v)) ? v : 1000.0;
            inst.params["v_delay"] = (t.size() > 3 && SimValueParser::parseSpiceNumber(t[3], v)) ? v : 0.0;
            inst.params["v_phase"] = (t.size() > 4 && SimValueParser::parseSpiceNumber(t[4], v)) ? v : 0.0;
            if (!inst.params.count("voltage")) inst.params["voltage"] = inst.params["v_offset"];
            return;
        }
    }

    // PULSE(v1 v2 td tr tf pw per)
    {
        const QStringList t = cap("pulse\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 2.0;
            double v = 0.0;
            inst.params["pulse_v1"] = (t.size() > 0 && SimValueParser::parseSpiceNumber(t[0], v)) ? v : 0.0;
            inst.params["pulse_v2"] = (t.size() > 1 && SimValueParser::parseSpiceNumber(t[1], v)) ? v : 5.0;
            inst.params["pulse_td"] = (t.size() > 2 && SimValueParser::parseSpiceNumber(t[2], v)) ? v : 0.0;
            inst.params["pulse_tr"] = (t.size() > 3 && SimValueParser::parseSpiceNumber(t[3], v)) ? v : 1e-9;
            inst.params["pulse_tf"] = (t.size() > 4 && SimValueParser::parseSpiceNumber(t[4], v)) ? v : 1e-9;
            inst.params["pulse_pw"] = (t.size() > 5 && SimValueParser::parseSpiceNumber(t[5], v)) ? v : 1e-3;
            inst.params["pulse_per"] = (t.size() > 6 && SimValueParser::parseSpiceNumber(t[6], v)) ? v : 2e-3;
            if (!inst.params.count("voltage")) inst.params["voltage"] = inst.params["pulse_v1"];
            return;
        }
    }

    // EXP(v1 v2 td1 tau1 td2 tau2)
    {
        const QStringList t = cap("exp\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 3.0;
            double v = 0.0;
            inst.params["exp_v1"] = (t.size() > 0 && SimValueParser::parseSpiceNumber(t[0], v)) ? v : 0.0;
            inst.params["exp_v2"] = (t.size() > 1 && SimValueParser::parseSpiceNumber(t[1], v)) ? v : 5.0;
            inst.params["exp_td1"] = (t.size() > 2 && SimValueParser::parseSpiceNumber(t[2], v)) ? v : 0.0;
            inst.params["exp_tau1"] = (t.size() > 3 && SimValueParser::parseSpiceNumber(t[3], v)) ? v : 1e-4;
            inst.params["exp_td2"] = (t.size() > 4 && SimValueParser::parseSpiceNumber(t[4], v)) ? v : 1e-3;
            inst.params["exp_tau2"] = (t.size() > 5 && SimValueParser::parseSpiceNumber(t[5], v)) ? v : 1e-4;
            if (!inst.params.count("voltage")) inst.params["voltage"] = inst.params["exp_v1"];
            return;
        }
    }

    // SFFM(vo va fc mdi fs)
    {
        const QStringList t = cap("sffm\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 4.0;
            double v = 0.0;
            inst.params["sffm_offset"] = (t.size() > 0 && SimValueParser::parseSpiceNumber(t[0], v)) ? v : 0.0;
            inst.params["sffm_ampl"] = (t.size() > 1 && SimValueParser::parseSpiceNumber(t[1], v)) ? v : 1.0;
            inst.params["sffm_carrier_freq"] = (t.size() > 2 && SimValueParser::parseSpiceNumber(t[2], v)) ? v : 1000.0;
            inst.params["sffm_mod_index"] = (t.size() > 3 && SimValueParser::parseSpiceNumber(t[3], v)) ? v : 1.0;
            inst.params["sffm_signal_freq"] = (t.size() > 4 && SimValueParser::parseSpiceNumber(t[4], v)) ? v : 100.0;
            if (!inst.params.count("voltage")) inst.params["voltage"] = inst.params["sffm_offset"];
            return;
        }
    }

    // PWL(t1 v1 t2 v2 ...)
    {
        const QStringList t = cap("pwl\\s*\\(([^\\)]*)\\)");
        if (t.size() >= 4 && (t.size() % 2 == 0)) {
            inst.params["wave_type"] = 5.0;
            const int pairs = t.size() / 2;
            inst.params["pwl_n"] = static_cast<double>(pairs);
            double parsed = 0.0;
            for (int i = 0; i < pairs; ++i) {
                const QString tKey = QString("pwl_t%1").arg(i);
                const QString vKey = QString("pwl_v%1").arg(i);
                inst.params[tKey.toStdString()] = SimValueParser::parseSpiceNumber(t[2 * i], parsed) ? parsed : 0.0;
                inst.params[vKey.toStdString()] = SimValueParser::parseSpiceNumber(t[2 * i + 1], parsed) ? parsed : 0.0;
            }
            if (!inst.params.count("voltage")) inst.params["voltage"] = inst.params["pwl_v0"];
            return;
        }
    }

    // AM(sa oc fm fc td)
    {
        const QStringList t = cap("am\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 6.0;
            double v = 0.0;
            inst.params["am_scale"] = (t.size() > 0 && SimValueParser::parseSpiceNumber(t[0], v)) ? v : 1.0;
            inst.params["am_offset_coeff"] = (t.size() > 1 && SimValueParser::parseSpiceNumber(t[1], v)) ? v : 1.0;
            inst.params["am_mod_freq"] = (t.size() > 2 && SimValueParser::parseSpiceNumber(t[2], v)) ? v : 1000.0;
            inst.params["am_carrier_freq"] = (t.size() > 3 && SimValueParser::parseSpiceNumber(t[3], v)) ? v : 10000.0;
            inst.params["am_delay"] = (t.size() > 4 && SimValueParser::parseSpiceNumber(t[4], v)) ? v : 0.0;
            if (!inst.params.count("voltage")) inst.params["voltage"] = 0.0;
            return;
        }
    }

    // FM(vo va fc fd fm)
    {
        const QStringList t = cap("fm\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 7.0;
            double v = 0.0;
            inst.params["fm_offset"] = (t.size() > 0 && SimValueParser::parseSpiceNumber(t[0], v)) ? v : 0.0;
            inst.params["fm_ampl"] = (t.size() > 1 && SimValueParser::parseSpiceNumber(t[1], v)) ? v : 1.0;
            inst.params["fm_carrier_freq"] = (t.size() > 2 && SimValueParser::parseSpiceNumber(t[2], v)) ? v : 10000.0;
            inst.params["fm_freq_dev"] = (t.size() > 3 && SimValueParser::parseSpiceNumber(t[3], v)) ? v : 2000.0;
            inst.params["fm_mod_freq"] = (t.size() > 4 && SimValueParser::parseSpiceNumber(t[4], v)) ? v : 1000.0;
            if (!inst.params.count("voltage")) inst.params["voltage"] = inst.params["fm_offset"];
            return;
        }
    }
}

struct MappingResult {
    bool supported = false;
    bool skipSilently = false;
    SimComponentType type = SimComponentType::Resistor;
    QString subcircuitName;
    QString reason;
};

bool extractSubcircuitName(const QString& rawValue, QString& subcktOut) {
    subcktOut.clear();
    const QString value = rawValue.trimmed();
    if (value.isEmpty()) return false;

    static const QRegularExpression explicitRe("^\\s*(?:SUBCKT|X)\\s*[: ]\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = explicitRe.match(value);
    if (!m.hasMatch()) return false;

    subcktOut = m.captured(1).trimmed();
    return !subcktOut.isEmpty();
}

QString diagnosticPrefix(SimParseDiagnosticSeverity severity) {
    switch (severity) {
        case SimParseDiagnosticSeverity::Info: return "info";
        case SimParseDiagnosticSeverity::Warning: return "warn";
        case SimParseDiagnosticSeverity::Error: return "error";
        default: return "diag";
    }
}

void loadProjectModelLibraries(SimNetlist& netlist, QStringList& mappingWarnings) {
    const QDir cwd(QDir::currentPath());
    const QStringList candidates = {
        "spice_models.lib",
        "spice_models.cir",
        "spice_models.spice",
        "models.lib",
        "models.cir"
    };

    for (const QString& relPath : candidates) {
        const QString absPath = cwd.absoluteFilePath(relPath);
        if (!QFileInfo::exists(absPath)) continue;

        QFile f(absPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            mappingWarnings.append(QString("model library '%1' could not be opened: %2").arg(absPath, f.errorString()));
            continue;
        }

        const std::string content = QString::fromUtf8(f.readAll()).toStdString();
        SimModelParseOptions opts;
        opts.sourceName = absPath.toStdString();

        std::vector<SimParseDiagnostic> diagnostics;
        const bool ok = SimModelParser::parseLibrary(netlist, content, opts, &diagnostics);
        if (!ok) {
            mappingWarnings.append(QString("model library '%1' parse failed").arg(absPath));
        } else {
            qDebug().noquote() << QString("Simulator: loaded model library '%1'").arg(absPath);
        }

        for (const SimParseDiagnostic& d : diagnostics) {
            const QString lineInfo = (d.line > 0) ? QString::number(d.line) : "?";
            mappingWarnings.append(
                QString("modellib %1:%2 [%3] %4")
                    .arg(QString::fromStdString(d.source.empty() ? opts.sourceName : d.source))
                    .arg(lineInfo, diagnosticPrefix(d.severity), QString::fromStdString(d.message))
            );
        }
    }
}

MappingResult mapComponentToSimType(const ECOComponent& comp) {
    MappingResult r;
    switch (comp.type) {
        case SchematicItem::ResistorType:
            r.supported = true;
            r.type = SimComponentType::Resistor;
            return r;
        case SchematicItem::CapacitorType:
            r.supported = true;
            r.type = SimComponentType::Capacitor;
            return r;
        case SchematicItem::InductorType:
            r.supported = true;
            r.type = SimComponentType::Inductor;
            return r;
        case SchematicItem::DiodeType:
            r.supported = true;
            r.type = SimComponentType::Diode;
            return r;
        case SchematicItem::TransistorType:
            r.supported = true;
            if (comp.value.contains("PNP", Qt::CaseInsensitive)) r.type = SimComponentType::BJT_PNP;
            else if (comp.value.contains("NMOS", Qt::CaseInsensitive)) r.type = SimComponentType::MOSFET_NMOS;
            else if (comp.value.contains("PMOS", Qt::CaseInsensitive)) r.type = SimComponentType::MOSFET_PMOS;
            else r.type = SimComponentType::BJT_NPN;
            return r;
        case SchematicItem::VoltageSourceType:
            r.supported = true;
            r.type = SimComponentType::VoltageSource;
            return r;
        case SchematicItem::TransformerType:
            r.supported = true;
            r.type = SimComponentType::TransmissionLine;
            return r;
        case SchematicItem::PowerType:
            if (isGroundNet(comp.value) || isGroundNet(comp.reference)) {
                r.skipSilently = true;
                r.reason = "ground reference symbol";
                return r;
            }
            r.supported = true;
            r.type = SimComponentType::VoltageSource;
            return r;
        case SchematicItem::SmartSignalType:
            r.supported = true;
            r.type = SimComponentType::FluxScript;
            return r;
        case SchematicItem::ComponentType:
        case SchematicItem::CustomType:
            if (comp.typeName == "Resistor" || comp.typeName == "Resistor_US" || comp.typeName == "Resistor_IEC") {
                r.supported = true;
                r.type = SimComponentType::Resistor;
                return r;
            }
            if (comp.typeName == "Capacitor" || comp.typeName == "Capacitor_NonPolar" || comp.typeName == "Capacitor_Polarized") {
                r.supported = true;
                r.type = SimComponentType::Capacitor;
                return r;
            }
            if (comp.typeName == "Inductor") {
                r.supported = true;
                r.type = SimComponentType::Inductor;
                return r;
            }
            if (comp.typeName == "Potentiometer") {
                r.supported = true;
                r.type = SimComponentType::SubcircuitInstance;
                return r;
            }
            if (comp.typeName == "Switch" || comp.typeName == "PushButton") {
                r.supported = true;
                r.type = SimComponentType::Resistor;
                return r;
            }
            if (comp.typeName == "Transformer") {
                r.supported = true;
                r.type = SimComponentType::TransmissionLine;
                return r;
            }
            if (comp.typeName == "Diode" || comp.typeName == "Zener Diode") {
                r.supported = true;
                r.type = SimComponentType::Diode;
                return r;
            }
            if (comp.typeName == "Transistor" || comp.typeName == "Transistor_PNP" || comp.typeName == "Transistor_NMOS" || comp.typeName == "Transistor_PMOS") {
                r.supported = true;
                if (comp.typeName == "Transistor_PNP") r.type = SimComponentType::BJT_PNP;
                else if (comp.typeName == "Transistor_NMOS") r.type = SimComponentType::MOSFET_NMOS;
                else if (comp.typeName == "Transistor_PMOS") r.type = SimComponentType::MOSFET_PMOS;
                else if (comp.value.contains("PNP", Qt::CaseInsensitive)) r.type = SimComponentType::BJT_PNP;
                else if (comp.value.contains("NMOS", Qt::CaseInsensitive)) r.type = SimComponentType::MOSFET_NMOS;
                else if (comp.value.contains("PMOS", Qt::CaseInsensitive)) r.type = SimComponentType::MOSFET_PMOS;
                else r.type = SimComponentType::BJT_NPN;
                return r;
            }
            if (comp.typeName == "Gate_AND") {
                r.supported = true;
                r.type = SimComponentType::LOGIC_AND;
                return r;
            }
            if (comp.typeName == "Gate_OR") {
                r.supported = true;
                r.type = SimComponentType::LOGIC_OR;
                return r;
            }
            if (comp.typeName == "Gate_XOR") {
                r.supported = true;
                r.type = SimComponentType::LOGIC_XOR;
                return r;
            }
            if (comp.typeName == "Gate_NAND") {
                r.supported = true;
                r.type = SimComponentType::LOGIC_NAND;
                return r;
            }
            if (comp.typeName == "Gate_NOR") {
                r.supported = true;
                r.type = SimComponentType::LOGIC_NOR;
                return r;
            }
            if (comp.typeName == "Gate_NOT") {
                r.supported = true;
                r.type = SimComponentType::LOGIC_NOT;
                return r;
            }
            if (comp.typeName == "OscilloscopeInstrument" ||
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
                comp.typeName == "Logic Probe") {
                r.supported = true;
                r.type = SimComponentType::Resistor; // High-Z probe mapping
                return r;
            }
            r.reason = QString("generic component type '%1' (enum %2) has no simulator mapping").arg(comp.typeName).arg(comp.type);
            return r;
        case SchematicItem::ICType:
            r.supported = true;
            if (QString subcktName; extractSubcircuitName(comp.value, subcktName)) {
                r.type = SimComponentType::SubcircuitInstance;
                r.subcircuitName = subcktName;
            } else
            if (comp.value.startsWith("B_V")) r.type = SimComponentType::B_VoltageSource;
            else if (comp.value.startsWith("B_I")) r.type = SimComponentType::B_CurrentSource;
            else r.type = SimComponentType::VoltageSource; // macro/subckt fallback
            return r;
        default:
            r.reason = QString("unsupported item type enum %1 (%2)").arg(comp.type).arg(comp.typeName);
            return r;
    }
}

QString compDebugLabel(const ECOComponent& comp) {
    return QString("%1 [type=%2, typeName=%3, value=%4]")
        .arg(comp.reference, QString::number(comp.type), comp.typeName, comp.value);
}

QString normalizePinToken(const QString& pin) {
    return pin.trimmed().toUpper();
}

using PinRoleAliases = QList<QStringList>;

PinRoleAliases pinOrderAliasesFor(const SimComponentInstance& inst, const ECOComponent& comp) {
    Q_UNUSED(comp)
    if (inst.type == SimComponentType::Diode) {
        // [Anode, Cathode]
        return {
            {"1", "A", "ANODE", "+"},
            {"2", "K", "CATHODE", "KATHODE", "-"}
        };
    }
    if (inst.type == SimComponentType::BJT_NPN || inst.type == SimComponentType::BJT_PNP) {
        // [Collector, Base, Emitter]
        return {
            {"1", "C", "COLLECTOR"},
            {"2", "B", "BASE"},
            {"3", "E", "EMITTER"}
        };
    }
    if (inst.type == SimComponentType::MOSFET_NMOS || inst.type == SimComponentType::MOSFET_PMOS) {
        // [Drain, Gate, Source]
        return {
            {"1", "D", "DRAIN"},
            {"2", "G", "GATE"},
            {"3", "S", "SOURCE"}
        };
    }
    if (inst.type == SimComponentType::LOGIC_AND || inst.type == SimComponentType::LOGIC_OR ||
        inst.type == SimComponentType::LOGIC_XOR || inst.type == SimComponentType::LOGIC_NAND ||
        inst.type == SimComponentType::LOGIC_NOR) {
        // [InA, InB, OutY]
        return {
            {"1", "A", "IN1", "INA"},
            {"2", "B", "IN2", "INB"},
            {"3", "Y", "OUT", "OUTY"}
        };
    }
    if (inst.type == SimComponentType::LOGIC_NOT) {
        // [InA, OutY]
        return {
            {"1", "A", "IN", "INA"},
            {"3", "Y", "OUT", "OUTY"}
        };
    }
    if (inst.type == SimComponentType::Resistor || inst.type == SimComponentType::Capacitor || inst.type == SimComponentType::Inductor ||
        inst.type == SimComponentType::VoltageSource || inst.type == SimComponentType::B_VoltageSource || inst.type == SimComponentType::B_CurrentSource) {
        // [Pos/Pin1, Neg/Pin2]
        return {
            {"1", "P", "POS", "+", "ANODE"},
            {"2", "N", "NEG", "-", "CATHODE"}
        };
    }
    return {};
}

bool appendPinsByAliasOrder(
    const QHash<QString, int>& normalizedPinToNode,
    const PinRoleAliases& aliasesByRole,
    QList<int>& outNodes,
    QStringList& missingRoles
) {
    outNodes.clear();
    missingRoles.clear();
    int roleIdx = 0;
    for (const QStringList& aliases : aliasesByRole) {
        bool found = false;
        for (const QString& a : aliases) {
            const QString key = normalizePinToken(a);
            auto it = normalizedPinToNode.find(key);
            if (it != normalizedPinToNode.end()) {
                outNodes.append(*it);
                found = true;
                break;
            }
        }
        if (!found) {
            missingRoles.append(QString("role#%1").arg(roleIdx + 1));
        }
        roleIdx++;
    }
    return missingRoles.isEmpty();
}
}

SimNetlist SimSchematicBridge::buildNetlist(QGraphicsScene* scene, NetManager* netManager) {
    SimNetlist netlist;
    if (!scene) return netlist;
    int vnum = 0;

    // 1. Get components first (to know which are excluded)
    ECOPackage pkg = NetlistGenerator::generateECOPackage(scene, "", netManager);
    
    QSet<QString> excludedSimRefs;
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) excludedSimRefs.insert(comp.reference);
    }

    // 2. Get canonical connectivity from SchematicConnectivity (NetManager-based),
    // falling back to NetlistGenerator compatibility path if needed.
    QList<SchematicConnectivityNet> nets = SchematicConnectivity::buildConnectivity(scene, netManager);
    if (nets.isEmpty()) {
        qWarning() << "Simulator: SchematicConnectivity returned no nets, falling back to NetlistGenerator path.";
        const QList<NetlistNet> legacyNets = NetlistGenerator::buildConnectivity(scene, "", netManager);
        for (const auto& n : legacyNets) {
            SchematicConnectivityNet converted;
            converted.name = n.name;
            for (const auto& p : n.pins) {
                converted.pins.append({p.componentRef, p.pinName});
            }
            nets.append(converted);
        }
    }

    // Filter pins belonging to components excluded from simulation
    if (!excludedSimRefs.isEmpty()) {
        for (int i = 0; i < nets.size(); ++i) {
            QList<SchematicConnectivityPin> filteredPins;
            for (const auto& pin : nets[i].pins) {
                if (!excludedSimRefs.contains(pin.componentRef)) {
                    filteredPins.append(pin);
                }
            }
            nets[i].pins = filteredPins;
        }
    }
    
    // 3. Identify and register nodes
    for (const auto& net : nets) {
        if (isGroundNet(net.name)) {
            // Mapping handled by findNode or explicit 0 mapping
            continue; 
        }
        netlist.addNode(net.name.toStdString());
    }

    // Load optional project-local model libraries before mapping components.
    QStringList mappingWarnings;
    loadProjectModelLibraries(netlist, mappingWarnings);

    // 4. Map components to simulator instances
    for (const auto& comp : pkg.components) {
        if (comp.excludeFromSim) {
            continue;
        }

        SimComponentInstance inst;
        inst.name = comp.reference.toStdString();

        // Advanced Simulation Metadata
        for (auto it = comp.paramExpressions.begin(); it != comp.paramExpressions.end(); ++it) {
            inst.paramExpressions[it.key().toStdString()] = it.value().toStdString();
        }
        for (auto it = comp.tolerances.begin(); it != comp.tolerances.end(); ++it) {
            inst.tolerances[it.key().toStdString()] = { it.value(), ToleranceDistribution::Uniform }; // Default to uniform for now
        }

        MappingResult map = mapComponentToSimType(comp);
        if (map.skipSilently) {
            continue;
        }
        if (!map.supported) {
            mappingWarnings.append(QString("%1 -> %2").arg(compDebugLabel(comp), map.reason));
            continue;
        }
        inst.type = map.type;
        
        // --- Special Expansion: Potentiometer ---
        if (comp.typeName == "Potentiometer") {
            // Find nodes for pins 1, 2, 3
            int nA = -1, nW = -1, nB = -1;
            for (const auto& net : nets) {
                for (const auto& pin : net.pins) {
                    if (pin.componentRef == comp.reference) {
                        int nodeIdx = isGroundNet(net.name) ? 0 : netlist.findNode(net.name.toStdString());
                        if (pin.pinName == "1") nA = nodeIdx;
                        else if (pin.pinName == "2") nW = nodeIdx;
                        else if (pin.pinName == "3") nB = nodeIdx;
                    }
                }
            }

            if (nA != -1 && nW != -1 && nB != -1) {
                double rUpper = 5000.0;
                double rLower = 5000.0;
                if (comp.paramExpressions.contains("r_upper")) rUpper = comp.paramExpressions["r_upper"].toDouble();
                if (comp.paramExpressions.contains("r_lower")) rLower = comp.paramExpressions["r_lower"].toDouble();

                SimComponentInstance r1;
                r1.name = inst.name + "_UPPER";
                r1.type = SimComponentType::Resistor;
                r1.nodes = { nA, nW };
                r1.params["resistance"] = std::max(0.001, rUpper);
                netlist.addComponent(r1);

                SimComponentInstance r2;
                r2.name = inst.name + "_LOWER";
                r2.type = SimComponentType::Resistor;
                r2.nodes = { nW, nB };
                r2.params["resistance"] = std::max(0.001, rLower);
                netlist.addComponent(r2);
                continue;
            }
        }

        if (inst.type == SimComponentType::SubcircuitInstance) {
            inst.subcircuitName = map.subcircuitName.toStdString();
        }

        // Special handling for programmable blocks
        if (inst.type == SimComponentType::FluxScript) {
            // Find the original item in the scene to get its logic and pin names
            for (auto* item : scene->items()) {
                if (auto* smart = dynamic_cast<SmartSignalItem*>(item)) {
                    if (smart->reference() == comp.reference) {
                        inst.pythonScript = smart->pythonCode().toStdString();
                        for (const QString& pin : smart->inputPins()) inst.inputPinNames.push_back(pin.toStdString());
                        for (const QString& pin : smart->outputPins()) inst.outputPinNames.push_back(pin.toStdString());
                        break;
                    }
                }
            }
        }

        // Find which nets this component is connected to
        std::map<std::string, int> pinToNode;
        for (const auto& net : nets) {
            int nodeIdx = isGroundNet(net.name) ? 0 : netlist.findNode(net.name.toStdString());
            for (const auto& pin : net.pins) {
                if (pin.componentRef == comp.reference) {
                    pinToNode[pin.pinName.toStdString()] = nodeIdx;
                }
            }
        }

        // Normalize input pin names once to support numeric and named aliases.
        QHash<QString, int> normalizedPinToNode;
        for (const auto& [pin, node] : pinToNode) {
            normalizedPinToNode.insert(normalizePinToken(QString::fromStdString(pin)), node);
        }

        if (inst.type == SimComponentType::VoltageSource && comp.type == SchematicItem::PowerType) {
            // Power port only has one pin (the net it names)
            // Connect Pos to the net, Neg to GND (0)
            if (inst.nodes.empty()) {
                // Determine the net name for this power symbol
                QString netName = comp.value;
                if (netName.isEmpty()) netName = comp.reference;

                // Find the node ID for this net
                int nodeIdx = isGroundNet(netName) ? 0 : netlist.findNode(netName.toStdString());
                if (nodeIdx != -1) {
                    inst.nodes.push_back(nodeIdx);
                } else {
                    // Symbol isn't connected to a wire but carries a name
                    // Create a node for it so we can attach a source
                    nodeIdx = netlist.addNode(netName.toStdString());
                    inst.nodes.push_back(nodeIdx);
                }
            }
            if (inst.nodes.size() == 1) {
                inst.nodes.push_back(0); // Second node is Ground
            }
        } else if (inst.type == SimComponentType::FluxScript) {
            // Signal blocks must have nodes in order of [input1, input2... output1, output2...]
            for (const auto& pin : inst.inputPinNames) {
                inst.nodes.push_back(normalizedPinToNode.value(normalizePinToken(QString::fromStdString(pin)), 0));
            }
            for (const auto& pin : inst.outputPinNames) {
                inst.nodes.push_back(normalizedPinToNode.value(normalizePinToken(QString::fromStdString(pin)), 0));
            }
        } else if (inst.type == SimComponentType::SubcircuitInstance) {
            std::map<int, int> pinNumToNode;
            int maxConnectedPin = 0;
            for (auto it = normalizedPinToNode.begin(); it != normalizedPinToNode.end(); ++it) {
                bool ok = false;
                const int pinNum = it.key().toInt(&ok);
                if (!ok || pinNum <= 0) continue;
                pinNumToNode[pinNum] = it.value();
                maxConnectedPin = std::max(maxConnectedPin, pinNum);
            }

            if (pinNumToNode.empty()) {
                mappingWarnings.append(
                    QString("%1 -> subckt '%2' requires numeric pin labels (1..N)")
                        .arg(compDebugLabel(comp), QString::fromStdString(inst.subcircuitName))
                );
                continue;
            }

            int expectedPinCount = 0;
            const SimSubcircuit* sub = netlist.findSubcircuit(inst.subcircuitName);
            if (sub) {
                expectedPinCount = static_cast<int>(sub->pinNames.size());
            } else {
                mappingWarnings.append(
                    QString("%1 -> subckt '%2' NOT FOUND in loaded libraries; using connected numeric pin order")
                        .arg(compDebugLabel(comp), QString::fromStdString(inst.subcircuitName))
                );
            }

            if (expectedPinCount > 0 && maxConnectedPin > expectedPinCount) {
                mappingWarnings.append(
                    QString("%1 -> subckt '%2' connected pin index %3 exceeds subckt pin count %4")
                        .arg(compDebugLabel(comp), QString::fromStdString(inst.subcircuitName))
                        .arg(maxConnectedPin).arg(expectedPinCount)
                );
                continue;
            }

            const int targetPinCount = std::max(expectedPinCount, maxConnectedPin);
            for (int pin = 1; pin <= targetPinCount; ++pin) {
                auto pinIt = pinNumToNode.find(pin);
                if (pinIt != pinNumToNode.end()) {
                    inst.nodes.push_back(pinIt->second);
                } else {
                    const std::string floatingName =
                        comp.reference.toStdString() + ":PIN" + std::to_string(pin);
                    inst.nodes.push_back(netlist.addNode(floatingName));
                }
            }
        } else {
            QStringList missingRoles;
            QList<int> ordered;
            const PinRoleAliases roleAliases = pinOrderAliasesFor(inst, comp);
            if (!roleAliases.isEmpty()) {
                appendPinsByAliasOrder(normalizedPinToNode, roleAliases, ordered, missingRoles);
                for (int n : ordered) {
                    inst.nodes.push_back(n);
                }
            } else {
                // Fallback for unsupported ordering specs: deterministic lexical order.
                QStringList keys = normalizedPinToNode.keys();
                std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
                    return a.toLower() < b.toLower();
                });
                for (const QString& k : keys) {
                    inst.nodes.push_back(normalizedPinToNode.value(k));
                }
            }

            if (!missingRoles.isEmpty()) {
                QStringList avail = normalizedPinToNode.keys();
                std::sort(avail.begin(), avail.end(), [](const QString& a, const QString& b) {
                    return a.toLower() < b.toLower();
                });
                mappingWarnings.append(
                    QString("%1 -> pin-order normalization missing required roles [%2], available pins [%3]")
                        .arg(compDebugLabel(comp))
                        .arg(missingRoles.join(", "))
                        .arg(avail.join(", "))
                );
                continue;
            }
        }

        if (inst.nodes.size() < 2) {
            mappingWarnings.append(QString("%1 -> insufficient connected pins (%2)")
                .arg(compDebugLabel(comp))
                .arg(inst.nodes.size()));
            continue;
        }

        // Parse value
        if (inst.type == SimComponentType::B_VoltageSource || inst.type == SimComponentType::B_CurrentSource) {
            inst.modelName = comp.value.toStdString(); // Store expression in modelName
        } else if (inst.type == SimComponentType::SubcircuitInstance) {
            // Subcircuit parameters can be added in future from IC value/options.
        } else {
            double val = parseValue(comp.value);
            if (inst.type == SimComponentType::VoltageSource) {
                parseVoltageSourceWaveform(comp.value, inst);
            }
            if (inst.type == SimComponentType::VoltageSource && val == 0.0) {
                // Power labels may carry names (VCC/12V) instead of plain numbers.
                val = inferSupplyVoltageFromName(comp.value);
                if (val == 0.0 && !inst.nodes.empty()) {
                    val = inferSupplyVoltageFromName(QString::fromStdString(netlist.nodeName(inst.nodes[0])));
                }
                if (val == 0.0 && comp.type == SchematicItem::PowerType) {
                    val = 5.0; // sensible default for unnamed power rails
                }
            }
            if (inst.type == SimComponentType::Resistor) inst.params["resistance"] = val;
            else if (inst.type == SimComponentType::VoltageSource && !inst.params.count("voltage")) inst.params["voltage"] = val;
            else if (inst.type == SimComponentType::Capacitor) inst.params["capacitance"] = val;
            else if (inst.type == SimComponentType::Inductor) inst.params["inductance"] = val;
            else if (inst.type == SimComponentType::Switch) {
                // Schematic switch item stores open/closed state as resistance in value.
                // For a 2-pin switch, stamp() uses roff as the branch resistance.
                if (val > 0.0) {
                    const double r = std::clamp(val, 1e-6, 1e12);
                    inst.params["roff"] = r;
                    inst.params["ron"] = r;
                }
            } else if (inst.type == SimComponentType::TransmissionLine) {
                // Transformer symbols currently map to a quasi-static 4-node element.
                // Allow numeric values (e.g. "75") to override default characteristic impedance.
                const double z0 = (val > 1e-6) ? val : 50.0;
                inst.params["z0"] = z0;
            }
        }

        if (inst.type == SimComponentType::Resistor &&
            (comp.typeName == "OscilloscopeInstrument" ||
             comp.typeName == "VoltmeterInstrument" ||
             comp.typeName == "AmmeterInstrument" ||
             comp.typeName == "WattmeterInstrument" ||
             comp.typeName == "FrequencyCounterInstrument" ||
             comp.typeName == "LogicProbeInstrument")) {
            // Instrument probes default to high-impedance to avoid disturbing circuits.
            inst.params["resistance"] = 1e8;

            // Register all connected nets for auto-probing.
            for (const auto& [pinName, nodeId] : pinToNode) {
                Q_UNUSED(pinName)
                if (nodeId <= 0) continue;
                std::string nodeName = netlist.nodeName(nodeId);
                if (nodeName != "?") {
                    netlist.addAutoProbe("V(" + nodeName + ")");
                }
            }
        }

        netlist.addComponent(inst);
    }

    if (!mappingWarnings.isEmpty()) {
        std::sort(mappingWarnings.begin(), mappingWarnings.end(), [](const QString& a, const QString& b) {
            return a.toLower() < b.toLower();
        });
        for (const QString& w : mappingWarnings) {
            netlist.addDiagnostic(w.toStdString());
        }
    }

    // 4. Automatic Power Supply Generation for named power nets
    // Collect all nets that already have an active source
    QSet<int> nodesWithSources;
    for (const auto& inst : netlist.components()) {
        if (inst.type == SimComponentType::VoltageSource) {
            if (!inst.nodes.empty()) nodesWithSources.insert(inst.nodes[0]);
        }
    }

    // For any net identified as a power net that doesn't have a source, add a default one
    for (const auto& net : nets) {
        if (isPowerNet(net.name) && !isGroundNet(net.name)) {
            int nodeIdx = netlist.findNode(net.name.toStdString());
            if (nodeIdx > 0 && !nodesWithSources.contains(nodeIdx)) {
                SimComponentInstance vps;
                vnum++;
                vps.name = "AutoPower_" + std::to_string(vnum);
                vps.type = SimComponentType::VoltageSource;
                vps.nodes = {nodeIdx, 0};
                
                // Try to detect voltage from name (e.g. "12V" -> 12.0)
                double val = 5.0; // Default
                QRegularExpression valMatch("(\\d+\\.?\\d*)V");
                auto m = valMatch.match(net.name);
                if (m.hasMatch()) val = m.captured(1).toDouble();
                
                vps.params["voltage"] = val;
                netlist.addComponent(vps);
                netlist.addAutoProbe("V(" + net.name.toStdString() + ")");
                nodesWithSources.insert(nodeIdx);
                qDebug() << "Simulator: Auto-detected power net" << net.name << "setting to" << val << "V";
            }
        }
    }

    return netlist;
}

double SimSchematicBridge::parseValue(const QString& val) {
    double parsed = 0.0;
    return SimValueParser::parseSpiceNumber(val, parsed) ? parsed : 0.0;
}

SimSchematicBridge::DiagnosticTarget SimSchematicBridge::extractDiagnosticTarget(const QString& message) {
    DiagnosticTarget target;
    if (message.trimmed().isEmpty()) {
        return target;
    }

    auto setComponent = [&](const QString& id) {
        if (id.trimmed().isEmpty()) return;
        target.type = DiagnosticTarget::Type::Component;
        target.id = id.trimmed();
    };
    auto setNet = [&](const QString& id) {
        if (id.trimmed().isEmpty()) return;
        target.type = DiagnosticTarget::Type::Net;
        target.id = id.trimmed();
    };

    // "Component 'R1'" / "component \"R1\""
    QRegularExpression compQuotedRe("component\\s*['\\\"]([^'\\\"]+)['\\\"]", QRegularExpression::CaseInsensitiveOption);
    auto compQuotedMatch = compQuotedRe.match(message);
    if (compQuotedMatch.hasMatch()) {
        setComponent(compQuotedMatch.captured(1));
        return target;
    }

    // Probe expressions: V(out), I(R1), P(netA)
    QRegularExpression probeRe("\\b[VIP]\\s*\\(\\s*([^\\)\\s]+)\\s*\\)", QRegularExpression::CaseInsensitiveOption);
    auto probeMatch = probeRe.match(message);
    if (probeMatch.hasMatch()) {
        setNet(probeMatch.captured(1));
        return target;
    }

    // "net OUT", "net 'OUT'"
    QRegularExpression netQuotedRe("\\bnet\\s*['\\\"]([^'\\\"]+)['\\\"]", QRegularExpression::CaseInsensitiveOption);
    auto netQuotedMatch = netQuotedRe.match(message);
    if (netQuotedMatch.hasMatch()) {
        setNet(netQuotedMatch.captured(1));
        return target;
    }
    QRegularExpression netBareRe("\\bnet\\s+([A-Za-z_][A-Za-z0-9_\\-:\\.]*)", QRegularExpression::CaseInsensitiveOption);
    auto netBareMatch = netBareRe.match(message);
    if (netBareMatch.hasMatch()) {
        setNet(netBareMatch.captured(1));
        return target;
    }

    // "near R1" / "at R1" style component hints.
    QRegularExpression refRe("\\b(?:near|at)\\s+([A-Za-z]{1,4}\\d+[A-Za-z0-9_]*)", QRegularExpression::CaseInsensitiveOption);
    auto refMatch = refRe.match(message);
    if (refMatch.hasMatch()) {
        setComponent(refMatch.captured(1));
        return target;
    }

    return target;
}
