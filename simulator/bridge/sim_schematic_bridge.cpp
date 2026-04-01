#include "sim_schematic_bridge.h"
#include "../../schematic/io/netlist_generator.h"
#include "../../schematic/analysis/schematic_connectivity.h"
#include "../../schematic/items/schematic_item.h"
#include "../../schematic/items/smart_signal_item.h"
#include "../core/sim_model_parser.h"
#include "../core/sim_value_parser.h"
#include "../../core/config_manager.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>

#include <QRegularExpression>
#include <QHash>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

namespace {
QString decodeSpiceTextInBridge(const QByteArray& raw) {
    if (raw.isEmpty()) return QString();

    auto decodeUtf16Le = [](const QByteArray& bytes, int start) {
        QVector<ushort> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const ushort ch = static_cast<ushort>(static_cast<unsigned char>(bytes[i])) |
                              (static_cast<ushort>(static_cast<unsigned char>(bytes[i + 1])) << 8);
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    auto decodeUtf16Be = [](const QByteArray& bytes, int start) {
        QVector<ushort> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const ushort ch = (static_cast<ushort>(static_cast<unsigned char>(bytes[i])) << 8) |
                               static_cast<ushort>(static_cast<unsigned char>(bytes[i + 1]));
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    if (raw.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(raw[0]);
        const unsigned char b1 = static_cast<unsigned char>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) return decodeUtf16Le(raw, 2);
        if (b0 == 0xFE && b1 == 0xFF) return decodeUtf16Be(raw, 2);
    }

    int oddZeros = 0;
    int evenZeros = 0;
    const int n = raw.size();
    for (int i = 0; i < n; ++i) {
        if (raw[i] == '\0') {
            if (i % 2 == 0) ++evenZeros;
            else ++oddZeros;
        }
    }
    if (oddZeros > n / 8) return decodeUtf16Le(raw, 0);
    if (evenZeros > n / 8) return decodeUtf16Be(raw, 0);

    return QString::fromUtf8(raw);
}

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

QString extractRefPrefix(const QString& ref) {
    QString out;
    const QString t = ref.trimmed().toUpper();
    for (const QChar& c : t) {
        if (!c.isLetter()) break;
        out.append(c);
    }
    return out;
}

QSet<QString> ltspiceRootBuiltinNames() {
    static const QSet<QString> names = {
        "FerriteBead",
        "FerriteBead2",
        "LED",
        "TVSdiode",
        "bi",
        "bi2",
        "bv",
        "cap",
        "csw",
        "current",
        "diode",
        "e",
        "e2",
        "f",
        "g",
        "g2",
        "h",
        "ind",
        "ind2",
        "load",
        "load2",
        "lpnp",
        "ltline",
        "mesfet",
        "njf",
        "nmos",
        "nmos4",
        "npn",
        "npn2",
        "npn3",
        "npn4",
        "pjf",
        "pmos",
        "pmos4",
        "pnp",
        "pnp2",
        "pnp4",
        "polcap",
        "res",
        "res2",
        "schottky",
        "sw",
        "tline",
        "varactor",
        "voltage",
        "zener"
    };
    return names;
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

void parseSourceWaveform(const QString& raw, SimComponentInstance& inst) {
    const bool isCurrent = (inst.type == SimComponentType::CurrentSource || inst.type == SimComponentType::B_CurrentSource);
    const std::string mainKey = isCurrent ? "current" : "voltage";
    const char pChar = isCurrent ? 'i' : 'v';
    const QString pStr = QString(pChar);
    const QString spec = raw.trimmed();
    if (spec.isEmpty()) return;

    auto cap = [&](const QString& pattern) -> QStringList {
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        auto m = re.match(spec);
        if (!m.hasMatch()) return {};
        // Split by both spaces and commas for robust parsing
        return m.captured(1).split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
    };

    // 1. AC parsing from the tail (e.g., "5V AC 1 0" or "SINE(...) AC 1")
    {
        // Support "AC 1", "AC 1.0 90", etc.
        QRegularExpression acRe("\\bAC\\s+([^\\s,]+)(?:[\\s,]+([^\\s,]+))?", QRegularExpression::CaseInsensitiveOption);
        auto m = acRe.match(spec);
        if (m.hasMatch()) {
            double mag = 1.0, phase = 0.0;
            if (SimValueParser::parseSpiceNumber(m.captured(1), mag)) inst.params["ac_mag"] = mag;
            if (!m.captured(2).isEmpty()) {
                if (SimValueParser::parseSpiceNumber(m.captured(2), phase)) inst.params["ac_phase"] = phase;
            }
        }
    }

    // 2. DC value parsing
    {
        QRegularExpression dcRe("^\\s*dc\\s+([^\\s,]+)", QRegularExpression::CaseInsensitiveOption);
        auto m = dcRe.match(spec);
        if (m.hasMatch()) {
            double dcVal = 0.0;
            if (SimValueParser::parseSpiceNumber(m.captured(1), dcVal)) {
                inst.params[mainKey] = dcVal;
            }
        }
    }

    if (!inst.params.count(mainKey)) {
        double rawVal = 0.0;
        // Check if the whole string is just a number (like "5" or "5V") or expression
        if (spec.startsWith("{") && spec.endsWith("}")) {
            inst.paramExpressions[mainKey] = spec.toStdString();
        } else {
            // Try to parse the first token as a DC value if it's not a function name
            QString first = spec.split(QRegularExpression("[\\s,]+")).first();
            if (!first.contains("(") && SimValueParser::parseSpiceNumber(first, rawVal)) {
                inst.params[mainKey] = rawVal;
            }
        }
    }

    // Helper: generic expression-aware token parser
    auto wParam = [&](const QStringList& t, int idx, const std::string& key, double fallback) {
        if (idx >= t.size()) { inst.params[key] = fallback; return; }
        const QString tok = t[idx].trimmed();
        if (tok.startsWith("{") && tok.endsWith("}")) {
            inst.paramExpressions[key] = tok.toStdString();
        } else {
            double v = 0.0;
            inst.params[key] = SimValueParser::parseSpiceNumber(tok, v) ? v : fallback;
        }
    };

    // 3. Waveform Functions

    // SINE(VO VA FREQ TD THETA PHI NCYCLES)
    {
        const QStringList t = cap("sine\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 1.0;
            wParam(t, 0, (pStr + "_offset").toStdString(), 0.0);
            wParam(t, 1, (pStr + "_ampl").toStdString(),   isCurrent ? 1e-3 : 1.0);
            wParam(t, 2, (pStr + "_freq").toStdString(),   1000.0);
            wParam(t, 3, (pStr + "_delay").toStdString(),  0.0);
            wParam(t, 4, (pStr + "_theta").toStdString(),  0.0);
            wParam(t, 5, (pStr + "_phase").toStdString(),  0.0); // Phi in degrees
            wParam(t, 6, (pStr + "_ncycles").toStdString(), 0.0);

            if (!inst.params.count(mainKey)) {
                std::string offKey = (pStr + "_offset").toStdString();
                if (inst.paramExpressions.count(offKey))
                    inst.paramExpressions[mainKey] = inst.paramExpressions[offKey];
                else
                    inst.params[mainKey] = inst.params.count(offKey) ? inst.params[offKey] : 0.0;
            }
            return;
        }
    }

    // PULSE(V1 V2 TD TR TF PW PER)
    {
        const QStringList t = cap("pulse\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 2.0;
            wParam(t, 0, (pStr + "pulse_v1").replace("vp", "p").toStdString(),  0.0); // pulse_v1 or pulse_i1
            wParam(t, 1, (pStr + "pulse_v2").replace("vp", "p").toStdString(),  isCurrent ? 1e-3 : 5.0);
            wParam(t, 2, "pulse_td",  0.0);
            wParam(t, 3, "pulse_tr",  1e-9);
            wParam(t, 4, "pulse_tf",  1e-9);
            wParam(t, 5, "pulse_pw",  1e-3);
            wParam(t, 6, "pulse_per", 2e-3);
            std::string v1Key = (pStr + "pulse_v1").replace("vp", "p").toStdString();
            if (!inst.params.count(mainKey))
                inst.params[mainKey] = inst.params.count(v1Key) ? inst.params[v1Key] : 0.0;
            return;
        }
    }

    // EXP(V1 V2 TD1 TAU1 TD2 TAU2)
    {
        const QStringList t = cap("exp\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 3.0;
            wParam(t, 0, (pStr + "exp_v1").replace("ve", "e").toStdString(),   0.0);
            wParam(t, 1, (pStr + "exp_v2").replace("ve", "e").toStdString(),   isCurrent ? 1e-3 : 5.0);
            wParam(t, 2, "exp_td1",  0.0);
            wParam(t, 3, "exp_tau1", 1e-4);
            wParam(t, 4, "exp_td2",  1e-3);
            wParam(t, 5, "exp_tau2", 1e-4);
            std::string v1Key = (pStr + "exp_v1").replace("ve", "e").toStdString();
            if (!inst.params.count(mainKey))
                inst.params[mainKey] = inst.params.count(v1Key) ? inst.params[v1Key] : 0.0;
            return;
        }
    }

    // SFFM(VO VA FC MDI FS)
    {
        const QStringList t = cap("sffm\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 4.0;
            wParam(t, 0, (pStr + "sffm_offset").replace("os", "s").toStdString(),       0.0);
            wParam(t, 1, (pStr + "sffm_ampl").replace("as", "s").toStdString(),         isCurrent ? 1e-3 : 1.0);
            wParam(t, 2, "sffm_carrier_freq",  1000.0);
            wParam(t, 3, "sffm_mod_index",    1.0);
            wParam(t, 4, "sffm_signal_freq",  100.0);
            std::string offKey = (pStr + "sffm_offset").replace("os", "s").toStdString();
            if (!inst.params.count(mainKey))
                inst.params[mainKey] = inst.params.count(offKey) ? inst.params[offKey] : 0.0;
            return;
        }
    }

    // PWL(T1 V1 T2 V2 ...)
    {
        const QStringList t = cap("pwl\\s*\\(([^\\)]*)\\)");
        if (t.size() >= 2) {
            inst.params["wave_type"] = 5.0;
            const int pairs = t.size() / 2;
            inst.params["pwl_n"] = static_cast<double>(pairs);
            for (int i = 0; i < pairs; ++i) {
                QString tKey = QString("pwl_t%1").arg(i);
                QString vKey = QString("pwl_%1%2").arg(pChar).arg(i);
                wParam(t, 2 * i, tKey.toStdString(), 0.0);
                wParam(t, 2 * i + 1, vKey.toStdString(), 0.0);
            }
            std::string v0Key = QString("pwl_%10").arg(pChar).toStdString();
            if (!inst.params.count(mainKey)) inst.params[mainKey] = inst.params[v0Key];
            return;
        }
    }

    // AM(SA OC FM FC TD)
    {
        const QStringList t = cap("am\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 6.0;
            wParam(t, 0, "am_scale",        1.0);
            wParam(t, 1, "am_offset_coeff", 1.0);
            wParam(t, 2, "am_mod_freq",     1000.0);
            wParam(t, 3, "am_carrier_freq", 10000.0);
            wParam(t, 4, "am_delay",        0.0);
            if (!inst.params.count("voltage")) inst.params["voltage"] = 0.0;
            return;
        }
    }

    // FM(VO VA FC FD FM)
    {
        const QStringList t = cap("fm\\s*\\(([^\\)]*)\\)");
        if (!t.isEmpty()) {
            inst.params["wave_type"] = 7.0;
            wParam(t, 0, (pStr + "fm_offset").replace("of", "f").toStdString(),       0.0);
            wParam(t, 1, (pStr + "fm_ampl").replace("af", "f").toStdString(),         isCurrent ? 1e-3 : 1.0);
            wParam(t, 2, "fm_carrier_freq", 10000.0);
            wParam(t, 3, "fm_freq_dev",     2000.0);
            wParam(t, 4, "fm_mod_freq",     1000.0);
            std::string offKey = (pStr + "fm_offset").replace("of", "f").toStdString();
            if (!inst.params.count(mainKey))
                inst.params[mainKey] = inst.params.count(offKey) ? inst.params[offKey] : 0.0;
            return;
        }
    }
}

static bool isExpression(const QString& s) {
    QString t = s.trimmed();
    return t.startsWith("{") && t.endsWith("}");
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

    auto loadLibraryFile = [&](const QString& absPath) {
        if (!QFileInfo::exists(absPath)) return;

        QFile f(absPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            mappingWarnings.append(QString("model library '%1' could not be opened: %2").arg(absPath, f.errorString()));
            return;
        }

        const std::string content = decodeSpiceTextInBridge(f.readAll()).toStdString();
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
    };

    auto scanModelPath = [&](const QString& path) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QStringList() << "*.lib" << "*.mod" << "*.sub" << "*.sp" << "*.inc" << "*.cmp" << "*.jft" << "*.bjt" << "*.mos",
                           QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                loadLibraryFile(it.next());
            }
        } else if (info.isFile()) {
            loadLibraryFile(info.absoluteFilePath());
        }
    };

    for (const QString& relPath : candidates) {
        const QString absPath = cwd.absoluteFilePath(relPath);
        loadLibraryFile(absPath);
    }

    // Also load configured model libraries (e.g., ~/ViospiceLib).
    const QStringList modelPaths = ConfigManager::instance().modelPaths();
    bool skipKicad = ConfigManager::instance().kicadDisabled();
    for (const QString& p : modelPaths) {
        if (p.trimmed().isEmpty()) continue;
        if (skipKicad && p.contains("kicad", Qt::CaseInsensitive)) continue;
        const QString resolved = QFileInfo(p).isAbsolute() ? p : cwd.absoluteFilePath(p);
        scanModelPath(resolved);
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
            if (comp.value.trimmed().startsWith("V=", Qt::CaseInsensitive)) {
                r.type = SimComponentType::B_VoltageSource;
            } else {
                r.type = SimComponentType::VoltageSource;
            }
            return r;
        case SchematicItem::CurrentSourceType:
            r.supported = true;
            if (comp.value.trimmed().startsWith("I=", Qt::CaseInsensitive)) {
                r.type = SimComponentType::B_CurrentSource;
            } else {
                r.type = SimComponentType::CurrentSource;
            }
            return r;
        case SchematicItem::TransformerType:
            r.supported = true;
            r.type = SimComponentType::SubcircuitInstance;
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
            if (comp.typeName == "Switch" ||
                comp.typeName == "PushButton" ||
                comp.typeName.compare("Voltage Controlled Switch", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("sw", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("vcsw", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::Switch;
                return r;
            }
            if (comp.typeName.compare("csw", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::CSW;
                return r;
            }
            if (comp.typeName.compare("e", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("e2", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("vcvs", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::VCVS;
                return r;
            }
            if (comp.typeName.compare("g", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("g2", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("vccs", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::VCCS;
                return r;
            }
            if (comp.typeName.compare("tline", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("ltline", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::TransmissionLine;
                return r;
            }
            if (comp.typeName == "Transformer") {
                r.supported = true;
                r.type = SimComponentType::SubcircuitInstance;
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
                else if (comp.reference.startsWith("MN", Qt::CaseInsensitive)) r.type = SimComponentType::MOSFET_NMOS;
                else if (comp.reference.startsWith("MP", Qt::CaseInsensitive)) r.type = SimComponentType::MOSFET_PMOS;
                else r.type = SimComponentType::BJT_NPN;
                return r;
            }
            if (comp.typeName.compare("nmos", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("nmos4", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::MOSFET_NMOS;
                return r;
            }
            if (comp.typeName.compare("pmos", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("pmos4", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::MOSFET_PMOS;
                return r;
            }
            if (comp.typeName.compare("mesfet", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::MOSFET_NMOS;
                return r;
            }
            if (comp.typeName.compare("njf", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("pjf", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = comp.typeName.compare("pjf", Qt::CaseInsensitive) == 0
                    ? SimComponentType::MOSFET_PMOS
                    : SimComponentType::MOSFET_NMOS;
                return r;
            }
            if (comp.typeName == "Voltage_Source_Behavioral" || comp.typeName == "Behavioral Voltage Source") {
                r.supported = true;
                r.type = SimComponentType::B_VoltageSource;
                return r;
            }
            if (comp.typeName == "Current_Source_Behavioral" || comp.typeName == "Behavioral Current Source" ||
                comp.typeName.compare("bi", Qt::CaseInsensitive) == 0 ||
                comp.typeName.compare("bi2", Qt::CaseInsensitive) == 0) {
                r.supported = true;
                r.type = SimComponentType::B_CurrentSource;
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
            if (comp.type == SchematicItem::CustomType) {
                const QSet<QString> ltBuiltins = ltspiceRootBuiltinNames();
                if (!ltBuiltins.isEmpty() && ltBuiltins.contains(comp.typeName)) {
                    const QString refPrefix = extractRefPrefix(comp.reference);
                    const QString nameLower = comp.typeName.toLower();
                    if (refPrefix == "R") { r.supported = true; r.type = SimComponentType::Resistor; return r; }
                    if (refPrefix == "C") { r.supported = true; r.type = SimComponentType::Capacitor; return r; }
                    if (refPrefix == "L") { r.supported = true; r.type = SimComponentType::Inductor; return r; }
                    if (refPrefix == "D") { r.supported = true; r.type = SimComponentType::Diode; return r; }
                    if (nameLower.contains("pnp")) { r.supported = true; r.type = SimComponentType::BJT_PNP; return r; }
                    if (nameLower.contains("npn")) { r.supported = true; r.type = SimComponentType::BJT_NPN; return r; }
                    if (refPrefix == "QN") { r.supported = true; r.type = SimComponentType::BJT_NPN; return r; }
                    if (refPrefix == "QP") { r.supported = true; r.type = SimComponentType::BJT_PNP; return r; }
                    if (refPrefix == "Q") { r.supported = true; r.type = SimComponentType::BJT_NPN; return r; }
                    if (nameLower.contains("pmos")) { r.supported = true; r.type = SimComponentType::MOSFET_PMOS; return r; }
                    if (nameLower.contains("nmos")) { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (nameLower == "pjf") { r.supported = true; r.type = SimComponentType::MOSFET_PMOS; return r; }
                    if (nameLower == "njf") { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (refPrefix == "MN") { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (refPrefix == "MP") { r.supported = true; r.type = SimComponentType::MOSFET_PMOS; return r; }
                    if (refPrefix == "M") { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (nameLower.contains("mesfet")) { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (refPrefix == "Z") { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (refPrefix == "JN") { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (refPrefix == "JP") { r.supported = true; r.type = SimComponentType::MOSFET_PMOS; return r; }
                    if (refPrefix == "J") { r.supported = true; r.type = SimComponentType::MOSFET_NMOS; return r; }
                    if (refPrefix == "V") { r.supported = true; r.type = SimComponentType::VoltageSource; return r; }
                    if (refPrefix == "I") { r.supported = true; r.type = SimComponentType::CurrentSource; return r; }
                    if (refPrefix == "S") { r.supported = true; r.type = SimComponentType::Switch; return r; }
                    if (refPrefix == "W") { r.supported = true; r.type = SimComponentType::Switch; return r; }
                    if (refPrefix == "E") { r.supported = true; r.type = SimComponentType::VCVS; return r; }
                    if (refPrefix == "G") { r.supported = true; r.type = SimComponentType::VCCS; return r; }
                    if (refPrefix == "F") { r.supported = true; r.type = SimComponentType::CCCS; return r; }
                    if (refPrefix == "H") { r.supported = true; r.type = SimComponentType::CCVS; return r; }
                    if (refPrefix == "T" || refPrefix == "O") { r.supported = true; r.type = SimComponentType::TransmissionLine; return r; }
                    if (refPrefix == "B") {
                        r.supported = true;
                        r.type = nameLower.startsWith("bi") ? SimComponentType::B_CurrentSource : SimComponentType::B_VoltageSource;
                        return r;
                    }
                    if (refPrefix == "A") {
                        r.supported = true;
                        if (nameLower.contains("nand")) r.type = SimComponentType::LOGIC_NAND;
                        else if (nameLower.contains("nor")) r.type = SimComponentType::LOGIC_NOR;
                        else if (nameLower.contains("xor")) r.type = SimComponentType::LOGIC_XOR;
                        else if (nameLower.contains("and")) r.type = SimComponentType::LOGIC_AND;
                        else if (nameLower.contains("or")) r.type = SimComponentType::LOGIC_OR;
                        else if (nameLower.contains("inv") || nameLower.contains("not")) r.type = SimComponentType::LOGIC_NOT;
                        else r.type = SimComponentType::LOGIC_OR;
                        return r;
                    }
                }

                QString subcktName;
                if (!comp.spiceModel.trimmed().isEmpty()) {
                    subcktName = comp.spiceModel.trimmed();
                } else if (extractSubcircuitName(comp.value, subcktName)) {
                    // explicit "SUBCKT: name" or "X name"
                } else {
                    const QString v = comp.value.trimmed();
                    if (!v.isEmpty() && v.compare(comp.typeName, Qt::CaseInsensitive) == 0) {
                        subcktName = v;
                    } else if (!v.isEmpty() && !v.contains(' ') && !v.contains('=')) {
                        subcktName = v;
                    }
                }

                if (!subcktName.isEmpty()) {
                    r.supported = true;
                    r.type = SimComponentType::SubcircuitInstance;
                    r.subcircuitName = subcktName;
                    return r;
                }

                // Generic D-prefix fallback for user-created diode symbols
                const QString refPrefix = extractRefPrefix(comp.reference);
                if (refPrefix == "D") { r.supported = true; r.type = SimComponentType::Diode; return r; }
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

int resolveMosBodyNode(const QHash<QString, int>& normalizedPinToNode, int sourceNode) {
    static const QStringList bodyAliases = {
        "4", "B", "BULK", "BODY", "SUB", "SB"
    };
    for (const QString& alias : bodyAliases) {
        auto it = normalizedPinToNode.find(alias);
        if (it != normalizedPinToNode.end()) {
            return *it;
        }
    }
    return sourceNode;
}

int resolveBjtSubstrateNode(const QHash<QString, int>& normalizedPinToNode, int emitterNode) {
    static const QStringList substrateAliases = {
        "4", "S", "SUB", "SUBSTRATE", "BULK"
    };
    for (const QString& alias : substrateAliases) {
        auto it = normalizedPinToNode.find(alias);
        if (it != normalizedPinToNode.end()) {
            return *it;
        }
    }
    return emitterNode;
}

using PinRoleAliases = QList<QStringList>;

PinRoleAliases pinOrderAliasesFor(const SimComponentInstance& inst, const ECOComponent& comp) {
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
    if (inst.type == SimComponentType::Switch) {
        const bool want4 = (comp.typeName.compare("Voltage Controlled Switch", Qt::CaseInsensitive) == 0) ||
                           (comp.typeName.compare("sw", Qt::CaseInsensitive) == 0) ||
                           (comp.typeName.compare("vcsw", Qt::CaseInsensitive) == 0) ||
                           comp.paramExpressions.contains("switch.vt") ||
                           comp.paramExpressions.contains("switch.vh");
        if (want4) {
            // [P, N, Ctrl+, Ctrl-]
            return {
                {"1", "P", "POS", "A", "ANODE"},
                {"2", "N", "NEG", "B", "CATHODE"},
                {"3", "CP", "CTRL+", "CONTROL+", "NC+", "IN+", "G"},
                {"4", "CN", "CTRL-", "CONTROL-", "NC-", "IN-"}
            };
        }
        // 2-pin interactive switch
        return {
            {"1", "P", "POS", "A", "ANODE"},
            {"2", "N", "NEG", "B", "CATHODE"}
        };
    }
    if (inst.type == SimComponentType::VCVS || inst.type == SimComponentType::VCCS) {
        // [Out+, Out-, Ctrl+, Ctrl-]
        return {
            {"1", "P", "POS", "+", "OUT+", "OP", "OUTP"},
            {"2", "N", "NEG", "-", "OUT-", "ON", "OUTN"},
            {"3", "CP", "CTRL+", "IN+", "IP", "CTRL_P"},
            {"4", "CN", "CTRL-", "IN-", "IN", "CTRL_N"}
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

    // Detect duplicate reference designators (simulation-blocking)
    QSet<QString> seenRefs;
    QSet<QString> dupRefs;
    for (const auto& comp : pkg.components) {
        const QString ref = comp.reference.trimmed();
        if (ref.isEmpty() || ref.contains("?")) continue;
        if (seenRefs.contains(ref)) {
            dupRefs.insert(ref);
        } else {
            seenRefs.insert(ref);
        }
    }
    for (const QString& ref : dupRefs) {
        netlist.addDiagnostic(QString("[error] Duplicate reference designator: '%1'").arg(ref).toStdString());
    }
    
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

    bool hasGroundNet = false;
    for (const auto& net : nets) {
        if (isGroundNet(net.name)) {
            hasGroundNet = true;
            break;
        }
    }

    // Detect zero-ohm shorts (R=0 between two different nets)
    QMap<QString, QSet<QString>> compNets;
    for (const auto& net : nets) {
        const QString netName = net.name.trimmed();
        if (netName.isEmpty()) continue;
        for (const auto& pin : net.pins) {
            if (pin.componentRef.isEmpty()) continue;
            compNets[pin.componentRef].insert(netName);
        }
    }
    for (const auto& comp : pkg.components) {
        if (comp.type != SchematicItem::ResistorType) continue;
        double val = 0.0;
        if (!SimValueParser::parseSpiceNumber(comp.value, val)) continue;
        if (std::abs(val) > 1e-12) continue;
        const QSet<QString> netsForComp = compNets.value(comp.reference);
        if (netsForComp.size() < 2) continue;
        const QStringList netsList = QStringList(netsForComp.begin(), netsForComp.end());
        netlist.addDiagnostic(QString("[error] Short circuit: %1 is 0ohm between %2")
                                  .arg(comp.reference, netsList.join(" / "))
                                  .toStdString());
    }

    // Detect 0V voltage sources shorting two different nets
    for (const auto& comp : pkg.components) {
        if (comp.type != SchematicItem::VoltageSourceType) continue;
        double val = 0.0;
        if (!SimValueParser::parseSpiceNumber(comp.value, val)) continue;
        if (std::abs(val) > 1e-12) continue;
        const QSet<QString> netsForComp = compNets.value(comp.reference);
        if (netsForComp.size() < 2) continue;
        const QStringList netsList = QStringList(netsForComp.begin(), netsForComp.end());
        netlist.addDiagnostic(QString("[error] Short circuit: %1 is 0V between %2")
                                  .arg(comp.reference, netsList.join(" / "))
                                  .toStdString());
    }
    
    // 3. Identify and register nodes
    for (const auto& net : nets) {
        netlist.addNode(net.name.toStdString());
    }

    // --- NEW: Process SPICE Directives (.param, .model, etc.) from the scene ---
    for (auto* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->itemType() == SchematicItem::SpiceDirectiveType) {
                QString text = si->value();
                if (text.isEmpty()) continue;
                
                SimModelParseOptions opts;
                opts.sourceName = "Schematic Directive";
                std::vector<SimParseDiagnostic> diags;
                SimModelParser::parseLibrary(netlist, text.toStdString(), opts, &diags);
                
                for (const auto& d : diags) {
                    if (d.severity == SimParseDiagnosticSeverity::Error) {
                        qWarning() << "Simulator: directive error:" << QString::fromStdString(d.message);
                    }
                }
            }
        }
    }

    // Load optional project-local model libraries before mapping components.
    QStringList mappingWarnings;
    loadProjectModelLibraries(netlist, mappingWarnings);

    if (!hasGroundNet) {
        netlist.addDiagnostic("[error] No ground reference found (GND/0). Simulation will fail.");
    }

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
            double tVal = 0.0;
            if (SimValueParser::parseSpiceNumber(it.value(), tVal)) {
                SimTolerance st;
                st.value = tVal;
                st.distribution = ToleranceDistribution::Uniform;
                inst.tolerances[it.key().toStdString()] = st;
            }
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
        
        // --- Support brace expressions in basic component values ---
        if (inst.type == SimComponentType::Resistor || inst.type == SimComponentType::Capacitor || inst.type == SimComponentType::Inductor) {
            if (isExpression(comp.value)) {
                std::string key = (inst.type == SimComponentType::Resistor) ? "resistance" :
                                 (inst.type == SimComponentType::Capacitor) ? "capacitance" : "inductance";
                inst.paramExpressions[key] = comp.value.trimmed().toStdString();
            } else {
                double val = 0.0;
                if (SimValueParser::parseSpiceNumber(comp.value, val)) {
                    std::string key = (inst.type == SimComponentType::Resistor) ? "resistance" :
                                     (inst.type == SimComponentType::Capacitor) ? "capacitance" : "inductance";
                    inst.params[key] = val;
                }
            }
        }

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
                if (comp.paramExpressions.contains("r_upper")) rUpper = parseValue(comp.paramExpressions["r_upper"]);
                if (comp.paramExpressions.contains("r_lower")) rLower = parseValue(comp.paramExpressions["r_lower"]);

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

        if ((inst.type == SimComponentType::MOSFET_NMOS || inst.type == SimComponentType::MOSFET_PMOS) &&
            inst.nodes.size() == 3) {
            inst.nodes.push_back(resolveMosBodyNode(normalizedPinToNode, inst.nodes[2]));
        }
        if ((inst.type == SimComponentType::BJT_NPN || inst.type == SimComponentType::BJT_PNP) &&
            inst.nodes.size() == 3) {
            inst.nodes.push_back(resolveBjtSubstrateNode(normalizedPinToNode, inst.nodes[2]));
        }

        // Parse value
        if (inst.type == SimComponentType::B_VoltageSource || inst.type == SimComponentType::B_CurrentSource) {
            QString expr = comp.value.trimmed();
            if (expr.startsWith("V=", Qt::CaseInsensitive) || expr.startsWith("I=", Qt::CaseInsensitive)) {
                expr = expr.mid(2).trimmed();
            }
            inst.modelName = expr.toStdString(); // Store expression in modelName
        } else if (inst.type == SimComponentType::SubcircuitInstance) {
            // Subcircuit parameters can be added in future from IC value/options.
        } else {
            const QString rawValue = comp.value.trimmed();
            if (rawValue.isEmpty() &&
                (inst.type == SimComponentType::Resistor ||
                 inst.type == SimComponentType::Capacitor ||
                 inst.type == SimComponentType::Inductor ||
                 inst.type == SimComponentType::VoltageSource ||
                 inst.type == SimComponentType::CurrentSource)) {
                netlist.addDiagnostic(QString("[error] Missing value for %1").arg(compDebugLabel(comp)).toStdString());
                continue;
            }

            double val = parseValue(comp.value);
            if (inst.type == SimComponentType::VoltageSource || inst.type == SimComponentType::CurrentSource) {
                parseSourceWaveform(comp.value, inst);
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
            if (inst.type == SimComponentType::Resistor ||
                inst.type == SimComponentType::Capacitor ||
                inst.type == SimComponentType::Inductor) {
                double parsed = 0.0;
                if (!SimValueParser::parseSpiceNumber(comp.value, parsed) && !isExpression(comp.value)) {
                    netlist.addDiagnostic(QString("[error] Invalid value for %1: '%2'")
                        .arg(compDebugLabel(comp), comp.value).toStdString());
                    continue;
                }
            }

            if (inst.type == SimComponentType::Resistor) inst.params["resistance"] = val;
            else if (inst.type == SimComponentType::VoltageSource && !inst.params.count("voltage")) inst.params["voltage"] = val;
            else if (inst.type == SimComponentType::Capacitor) inst.params["capacitance"] = val;
            else if (inst.type == SimComponentType::Inductor) inst.params["inductance"] = val;
            else if (inst.type == SimComponentType::Switch || inst.type == SimComponentType::CSW) {
                // Schematic switch item stores open/closed state as resistance in value.
                // For a 2-pin switch, stamp() uses roff as the branch resistance.
                if (val > 0.0) {
                    const double r = std::clamp(val, 1e-6, 1e12);
                    inst.params["roff"] = r;
                    inst.params["ron"] = r;
                }
            } else if (inst.type == SimComponentType::VCVS || inst.type == SimComponentType::VCCS ||
                       inst.type == SimComponentType::CCVS || inst.type == SimComponentType::CCCS) {
                // Gain for dependent sources
                if (isExpression(comp.value)) {
                    inst.paramExpressions["gain"] = comp.value.trimmed().toStdString();
                } else {
                    inst.params["gain"] = val;
                }
            } else if (inst.type == SimComponentType::TransmissionLine) {
                const QString v = comp.value.trimmed();
                double z0 = 50.0;
                double td = 50e-9;
                
                QRegularExpression reZ0("\\bZ0\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpression reTd("\\bTd\\s*=\\s*([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                auto mz = reZ0.match(v);
                auto mt = reTd.match(v);
                if (mz.hasMatch()) SimValueParser::parseSpiceNumber(mz.captured(1), z0);
                if (mt.hasMatch()) SimValueParser::parseSpiceNumber(mt.captured(1), td);

                // Fallback: if value is just a number, it's Z0
                if (!mz.hasMatch() && !mt.hasMatch()) {
                    SimValueParser::parseSpiceNumber(v, z0);
                }

                inst.params["z0"] = z0;
                inst.params["td"] = td;

                // Handle lossy parameters (LTRA) from paramExpressions
                auto extractLtra = [&](const QString& key, const std::string& target) {
                    if (comp.paramExpressions.contains(key)) {
                        double lval = 0.0;
                        if (SimValueParser::parseSpiceNumber(comp.paramExpressions[key], lval)) {
                            inst.params[target] = lval;
                        }
                    }
                };
                extractLtra("ltra.R", "r");
                extractLtra("ltra.L", "l");
                extractLtra("ltra.G", "g");
                extractLtra("ltra.C", "c");
                extractLtra("ltra.LEN", "len");

                // If it's explicitly lossy, use the value as model name
                if (comp.typeName.compare("ltline", Qt::CaseInsensitive) == 0 ||
                    comp.reference.startsWith("O", Qt::CaseInsensitive)) {
                    inst.modelName = v.toStdString();
                }
            } else if (inst.type == SimComponentType::Diode ||
                       inst.type == SimComponentType::BJT_NPN ||
                       inst.type == SimComponentType::BJT_PNP ||
                       inst.type == SimComponentType::MOSFET_NMOS ||
                       inst.type == SimComponentType::MOSFET_PMOS ||
                       inst.type == SimComponentType::CSW) {
                inst.modelName = comp.value.trimmed().toStdString();
                
                // Validate model existence
                if (!inst.modelName.empty()) {
                    if (!netlist.findModel(inst.modelName)) {
                        // Optional for M/D/Q as per user request, but required for CSW
                        if (inst.type == SimComponentType::CSW) {
                            netlist.addDiagnostic(QString("[error] Model '%1' for component '%2' not found. Please define it using a .model directive.")
                                .arg(QString::fromStdString(inst.modelName), comp.reference).toStdString());
                        } else {
                            // Informational warn for optional models
                            netlist.addDiagnostic(QString("[info] Model '%1' for component '%2' not defined; using SPICE defaults.")
                                .arg(QString::fromStdString(inst.modelName), comp.reference).toStdString());
                        }
                    }
                } else if (inst.type == SimComponentType::CSW) {
                    netlist.addDiagnostic(QString("[error] Component '%1' (CSW) requires a model name in its value field (e.g. 'MySwitch').")
                        .arg(comp.reference).toStdString());
                }
            }
        }

        bool isInstrument = (comp.typeName == "OscilloscopeInstrument" ||
                             comp.typeName == "VoltmeterInstrument" ||
                             comp.typeName == "AmmeterInstrument" ||
                             comp.typeName == "WattmeterInstrument" ||
                             comp.typeName == "FrequencyCounterInstrument" ||
                             comp.typeName == "LogicProbeInstrument");

        if (isInstrument) {
            // For instruments, we ensure every connected pin is drained to ground 
            // with a high-Z resistor so they aren't floating.
            for (const auto& [pinName, nodeId] : pinToNode) {
                if (nodeId <= 0) continue;

                SimComponentInstance probeRes;
                probeRes.name = inst.name + "_" + pinName;
                probeRes.type = SimComponentType::Resistor;
                probeRes.nodes = { nodeId, 0 };
                probeRes.params["resistance"] = 1e8;
                netlist.addComponent(probeRes);

                // Register for auto-probing
                std::string nodeName = netlist.nodeName(nodeId);
                if (nodeName != "?") {
                    netlist.addAutoProbe("V(" + nodeName + ")");
                }
            }
        } else {
            netlist.addComponent(inst);
        }
    }

    if (!mappingWarnings.isEmpty()) {
        std::sort(mappingWarnings.begin(), mappingWarnings.end(), [](const QString& a, const QString& b) {
            return a.toLower() < b.toLower();
        });
        for (const QString& w : mappingWarnings) {
            QString msg = w.trimmed();
            QString prefix = "[warn]";
            const QString lower = msg.toLower();
            if (lower.contains("not found") ||
                lower.contains("parse failed") ||
                lower.contains("could not be opened") ||
                lower.contains("requires numeric pin labels") ||
                lower.contains("exceeds subckt pin count") ||
                lower.contains("missing required roles") ||
                lower.contains("insufficient connected pins")) {
                prefix = "[error]";
            }
            if (!msg.startsWith("[warn]") && !msg.startsWith("[error]")) {
                msg = prefix + " " + msg;
            }
            netlist.addDiagnostic(msg.toStdString());
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

    // --- NEW: Finalize Netlist (Flatten subcircuits and Resolve parameter expressions) ---
    netlist.flatten();
    netlist.evaluateExpressions();

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
