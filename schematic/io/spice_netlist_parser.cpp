#include "spice_netlist_parser.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>

// ─── .model type → factory type mapping ─────────────────────────────────────

QString SpiceNetlistParser::modelTypeToFactoryType(const QString& spiceModelType) {
    const QString t = spiceModelType.toUpper();

    // BJT
    if (t == "NPN")  return "Transistor";
    if (t == "PNP")  return "Transistor_PNP";

    // MOSFET
    if (t == "NMOS") return "Transistor_NMOS";
    if (t == "PMOS") return "Transistor_PMOS";

    // JFET
    if (t == "NJF")  return "njf";
    if (t == "PJF")  return "pjf";

    // MESFET
    if (t == "NMF")  return "mesfet";
    if (t == "PMF")  return "mesfet";

    // Diode
    if (t == "D")    return "Diode";

    // Switch models
    if (t == "SW")   return "Voltage Controlled Switch";
    if (t == "CSW")  return "Switch";

    // Resistor/Capacitor models (semiconductor)
    if (t == "R")    return "Resistor";
    if (t == "C")    return "Capacitor";

    return QString();
}

// ─── Parse .model directives ────────────────────────────────────────────────

void SpiceNetlistParser::parseModels(const QStringList& directives, QMap<QString, ModelInfo>& models) {
    // .model <name> <type> (params...)
    // e.g. .model MyMesfet NMF(Vto=-2.1 Beta=0.05 ...)
    //      .model 2N2222 NPN(Is=1e-14 ...)
    QRegularExpression modelRx(
        R"(^\.model\s+(\S+)\s+(\w+))",
        QRegularExpression::CaseInsensitiveOption
    );

    for (const QString& line : directives) {
        auto match = modelRx.match(line);
        if (match.hasMatch()) {
            ModelInfo info;
            info.name = match.captured(1);
            info.spiceType = match.captured(2).toUpper();
            info.factoryType = modelTypeToFactoryType(info.spiceType);

            models[info.name] = info;
            // Also store case-insensitive lookup
            models[info.name.toUpper()] = info;

            qDebug() << "SpiceNetlistParser: Found .model" << info.name
                     << "type:" << info.spiceType << "→" << info.factoryType;
        }
    }
}

// ─── Node count per SPICE prefix ────────────────────────────────────────────

int SpiceNetlistParser::nodeCountForPrefix(QChar prefix) {
    switch (prefix.toUpper().toLatin1()) {
        case 'R': case 'C': case 'L': case 'D':
        case 'V': case 'I': case 'B':
            return 2;
        case 'Q': case 'J': case 'M': case 'Z':
            return 3;
        case 'E': case 'G': case 'F': case 'H':
        case 'S': case 'W': case 'T':
            return 4;
        default:
            return 2;
    }
}

// ─── Prefix → factory type (with model awareness) ──────────────────────────

QString SpiceNetlistParser::spicePrefixToTypeName(QChar prefix, const QString& modelType,
                                                   const QString& value) {
    // If we have a resolved model type from .model directive, use it directly
    if (!modelType.isEmpty()) {
        QString resolved = modelTypeToFactoryType(modelType);
        if (!resolved.isEmpty()) return resolved;
    }

    // Fall back to prefix-based mapping with value hints
    switch (prefix.toUpper().toLatin1()) {
        case 'R': return "Resistor";
        case 'C': return "Capacitor";
        case 'L': return "Inductor";
        case 'D': return "Diode";
        case 'Q': {
            const QString v = value.toUpper();
            if (v.contains("PNP") || v.contains("2N3906") || v.contains("2N2907"))
                return "Transistor_PNP";
            return "Transistor";
        }
        case 'M': {
            const QString v = value.toUpper();
            if (v.contains("PMOS") || v.contains("BS250"))
                return "Transistor_PMOS";
            return "Transistor_NMOS";
        }
        case 'J': {
            const QString v = value.toUpper();
            if (v.contains("PJF"))
                return "pjf";
            return "njf";
        }
        case 'Z': return "mesfet";
        case 'V': return "Voltage_Source_DC";
        case 'I': return "Current_Source_DC";
        case 'E': return "E";
        case 'G': return "G";
        case 'F': return "F";
        case 'H': return "H";
        case 'K': return "Transformer";
        case 'S': return "Voltage Controlled Switch";
        case 'W': return "Switch";
        case 'X': return QString(); // Subcircuit — resolved from last token
        case 'B': return "Voltage_Source_Behavioral";
        case 'T': return "tline";
        default:  return QString();
    }
}

// ─── Parse a single component line ──────────────────────────────────────────

SpiceNetlistParser::ParsedComponent SpiceNetlistParser::parseLine(
        const QString& line, const QMap<QString, ModelInfo>& models) {
    ParsedComponent comp;

    QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return comp;

    comp.reference = tokens[0];
    if (comp.reference.isEmpty()) return comp;

    QChar prefix = comp.reference[0].toUpper();
    int nodeCount = nodeCountForPrefix(prefix);

    // Subcircuit special handling
    if (prefix == 'X') {
        if (tokens.size() >= 3) {
            comp.reference = tokens[0];
            comp.value = tokens.last();
            for (int i = 1; i < tokens.size() - 1; ++i)
                comp.nodes.append(tokens[i]);
            // Try to resolve subcircuit name through models or use as-is
            comp.typeName = comp.value;
            return comp;
        }
        return comp;
    }

    // Extract nodes
    for (int i = 1; i <= nodeCount && i < tokens.size(); ++i) {
        comp.nodes.append(tokens[i]);
    }

    // Everything after nodes is the value/model
    QStringList valueParts;
    for (int i = nodeCount + 1; i < tokens.size(); ++i) {
        valueParts.append(tokens[i]);
    }
    comp.value = valueParts.join(" ");

    // Smart type resolution:
    // 1. Check if the value references a known .model → use its type
    // 2. Fall back to prefix-based mapping with value hints
    QString modelType;
    QString modelName = comp.value.split(QRegularExpression("[\\s(]"), Qt::SkipEmptyParts).value(0);

    if (!modelName.isEmpty()) {
        // Look up in parsed .model directives (case-insensitive)
        auto it = models.find(modelName);
        if (it == models.end()) {
            it = models.find(modelName.toUpper());
        }
        if (it != models.end()) {
            modelType = it.value().spiceType;
            // If model has a direct factory type, use it
            if (!it.value().factoryType.isEmpty()) {
                comp.typeName = it.value().factoryType;
                return comp;
            }
        }
    }

    comp.typeName = spicePrefixToTypeName(prefix, modelType, comp.value);

    // If type is still empty (unknown prefix), use "IC" as fallback
    if (comp.typeName.isEmpty()) {
        comp.typeName = "IC";
    }

    return comp;
}

// ─── Main parse entry point ─────────────────────────────────────────────────

SpiceNetlistParser::ParsedNetlist SpiceNetlistParser::parse(const QString& filePath) {
    ParsedNetlist netlist;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "SpiceNetlistParser: Failed to open file:" << filePath;
        return netlist;
    }

    QTextStream in(&file);
    bool firstLine = true;
    QString pendingLine;
    QStringList allLines;

    // First pass: read all lines and handle continuation (+)
    while (!in.atEnd()) {
        QString raw = in.readLine();

        if (firstLine) {
            netlist.title = raw.trimmed();
            firstLine = false;
            continue;
        }

        QString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) continue;

        if (trimmed.startsWith('+')) {
            if (!pendingLine.isEmpty()) {
                pendingLine += " " + trimmed.mid(1).trimmed();
            }
            continue;
        }

        if (!pendingLine.isEmpty()) {
            allLines.append(pendingLine);
        }
        pendingLine = trimmed;
    }
    if (!pendingLine.isEmpty()) {
        allLines.append(pendingLine);
    }
    file.close();

    // Collect directives first (need .model info before parsing components)
    for (const QString& line : allLines) {
        if (line.startsWith('.') && line.compare(".end", Qt::CaseInsensitive) != 0) {
            netlist.directives.append(line);
        }
    }

    // Parse .model directives to build model registry
    parseModels(netlist.directives, netlist.models);

    // Second pass: parse component lines using model info
    for (const QString& line : allLines) {
        if (line.startsWith('*')) continue;  // comment
        if (line.startsWith('.')) continue;  // directive (already collected)

        ParsedComponent comp = parseLine(line, netlist.models);
        if (!comp.reference.isEmpty() && !comp.nodes.isEmpty()) {
            netlist.components.append(comp);
        }
    }

    buildNets(netlist);

    qDebug() << "SpiceNetlistParser: Parsed" << netlist.components.size()
             << "components," << netlist.nets.size() << "nets,"
             << netlist.models.size() << "models from" << filePath;

    return netlist;
}

// ─── Build net connectivity ─────────────────────────────────────────────────

void SpiceNetlistParser::buildNets(ParsedNetlist& netlist) {
    QMap<QString, QList<NetPin>> netMap;

    for (const auto& comp : netlist.components) {
        for (int i = 0; i < comp.nodes.size(); ++i) {
            QString netName = comp.nodes[i];
            if (netName == "0" || netName.toUpper() == "GND") {
                netName = "GND";
            }

            NetPin pin;
            pin.componentRef = comp.reference;
            pin.pinIndex = i;
            netMap[netName].append(pin);
        }
    }

    for (auto it = netMap.begin(); it != netMap.end(); ++it) {
        if (it.value().size() >= 2 || it.key() == "GND") {
            ParsedNet net;
            net.name = it.key();
            net.pins = it.value();
            netlist.nets.append(net);
        }
    }
}
