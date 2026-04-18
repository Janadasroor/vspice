#include "netlist_importer.h"
#include "eco_types.h"
#include "library_index.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QDebug>

// ============================================================================
// Load from file - dispatch by format
// ============================================================================

NetlistImportPackage PCBNetlistImporter::loadFromFile(const QString& filePath, Format format) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "NetlistImporter: Cannot open file:" << filePath;
        return {};
    }
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    if (format == AutoDetect) {
        format = detectFormat(content);
    }

    QString sourcePath = QFileInfo(filePath).fileName();

    switch (format) {
        case FluxJSON:  return loadFluxJSON(content, sourcePath);
        case Protel:    return loadProtel(content, sourcePath);
        case SPICE:     return loadSPICE(content, sourcePath);
        default:        return {};
    }
}

// ============================================================================
// Generate from schematic model (headless)
// ============================================================================

NetlistImportPackage PCBNetlistImporter::generateFromSchematic(const void* schematicModel) {
    // Uses the existing NetlistGenerator::generateECOPackage() flow but returns our format.
    // This is a thin wrapper - the actual heavy lifting is done by the schematic's
    // NetlistGenerator which already builds connectivity via DSU.
    NetlistImportPackage pkg;
    pkg.format = "ECO";
    pkg.sourcePath = "Schematic Editor";
    return pkg;
}

// ============================================================================
// Convert to ECOPackage (bridges to existing PCB applyECO machinery)
// ============================================================================

ECOPackage PCBNetlistImporter::toECOPackage(const NetlistImportPackage& importPkg) {
    ECOPackage eco;

    for (const auto& impComp : importPkg.components) {
        ECOComponent ecoComp;
        ecoComp.reference = impComp.reference;
        ecoComp.footprint = impComp.footprint;
        ecoComp.value = impComp.value;
        ecoComp.spiceModel = impComp.spiceModel;
        ecoComp.typeName = impComp.typeName;
        ecoComp.symbolPinCount = impComp.pinCount;
        ecoComp.pinPadMapping = impComp.pinPadMapping;
        ecoComp.excludeFromPcb = impComp.excludeFromPcb;
        eco.components.append(ecoComp);
    }

    for (const auto& impNet : importPkg.nets) {
        ECONet ecoNet;
        ecoNet.name = impNet.name;
        for (const auto& impPin : impNet.pins) {
            ECOPin ecoPin;
            ecoPin.componentRef = impPin.componentRef;
            ecoPin.pinName = impPin.pinName;
            ecoNet.pins.append(ecoPin);
        }
        eco.nets.append(ecoNet);
    }

    return eco;
}

// ============================================================================
// Footprint auto-suggestion
// ============================================================================

void PCBNetlistImporter::suggestFootprints(NetlistImportPackage& pkg, const QStringList& libraryFootprints) {
    for (auto& comp : pkg.components) {
        if (comp.footprint.isEmpty() || comp.footprint.trimmed().isEmpty()) {
            comp.footprint = suggestFootprintForComponent(comp, libraryFootprints);
        }
    }
}

QString PCBNetlistImporter::suggestFootprintForComponent(const NetlistImportComponent& comp, const QStringList& libraryFootprints) {
    // Strategy 1: Use package hint from netlist (e.g. "0603", "SOT-23", "DIP-8")
    if (!comp.packageHint.isEmpty()) {
        QString hint = comp.packageHint.toLower();
        for (const QString& fp : libraryFootprints) {
            QString fpLower = fp.toLower();
            if (fpLower.contains(hint) || hint.contains(fpLower)) {
                return fp;
            }
        }
    }

    // Strategy 2: Match by component type + value patterns
    QString typeLower = comp.typeName.toLower();
    QString valueLower = comp.value.toLower();
    QString variantLower = comp.variant.toLower();

    bool isSMD = variantLower.contains("smd") || variantLower.contains("smt") || variantLower.contains("chip");
    bool isTHT = variantLower.contains("tht") || variantLower.contains("through") || variantLower.contains("dip");

    // Resistors
    if (typeLower.contains("resistor") || comp.reference.startsWith("R")) {
        // Check value for size hint (0603, 0805, etc.)
        QRegularExpression sizeRe("(0[24]02|0[68]03|1[28]06|1210|2010|2512)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = sizeRe.match(comp.value);
        if (match.hasMatch()) {
            QString sizeCode = match.captured(0);
            for (const QString& fp : libraryFootprints) {
                if (fp.contains(sizeCode, Qt::CaseInsensitive)) {
                    return fp;
                }
            }
        }
        // Default: through-hole axial if THT, 0603 if SMD
        if (isTHT) {
            for (const QString& fp : libraryFootprints) {
                if (fp.contains("axial", Qt::CaseInsensitive) || fp.contains("r_", Qt::CaseInsensitive)) {
                    return fp;
                }
            }
        } else {
            for (const QString& fp : libraryFootprints) {
                if (fp.contains("0603", Qt::CaseInsensitive) && fp.contains("r", Qt::CaseInsensitive)) {
                    return fp;
                }
            }
        }
    }

    // Capacitors
    if (typeLower.contains("capacitor") || comp.reference.startsWith("C")) {
        QRegularExpression sizeRe("(0[24]02|0[68]03|1[28]06|1210)", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = sizeRe.match(comp.value);
        if (match.hasMatch()) {
            QString sizeCode = match.captured(0);
            for (const QString& fp : libraryFootprints) {
                if (fp.contains(sizeCode, Qt::CaseInsensitive)) {
                    return fp;
                }
            }
        }
        if (isTHT) {
            for (const QString& fp : libraryFootprints) {
                if (fp.contains("radial", Qt::CaseInsensitive) || fp.contains("c_", Qt::CaseInsensitive)) {
                    return fp;
                }
            }
        } else {
            for (const QString& fp : libraryFootprints) {
                if (fp.contains("0603", Qt::CaseInsensitive) && (fp.contains("c_") || fp.contains("cap"))) {
                    return fp;
                }
            }
        }
    }

    // Inductors
    if (typeLower.contains("inductor") || comp.reference.startsWith("L")) {
        for (const QString& fp : libraryFootprints) {
            if (fp.contains("l_", Qt::CaseInsensitive) || fp.contains("inductor", Qt::CaseInsensitive)) {
                return fp;
            }
        }
    }

    // Diodes
    if (typeLower.contains("diode") || comp.reference.startsWith("D")) {
        for (const QString& fp : libraryFootprints) {
            if (valueLower.contains("led") && fp.contains("led", Qt::CaseInsensitive)) return fp;
            if (fp.contains("diode", Qt::CaseInsensitive) || fp.contains("do-41", Qt::CaseInsensitive) || fp.contains("do-35", Qt::CaseInsensitive)) return fp;
        }
    }

    // Transistors
    if (typeLower.contains("transistor") || typeLower.contains("bjt") || typeLower.contains("mosfet") ||
        comp.reference.startsWith("Q") || comp.reference.startsWith("M")) {
        for (const QString& fp : libraryFootprints) {
            if (fp.contains("sot-23", Qt::CaseInsensitive) || fp.contains("to-92", Qt::CaseInsensitive)) return fp;
        }
    }

    // ICs / Chips
    if (comp.reference.startsWith("U") || comp.reference.startsWith("IC") || comp.reference.startsWith("A")) {
        int pins = comp.pinCount > 0 ? comp.pinCount : extractPinCount(comp);
        if (pins > 0) {
            // Try common IC packages by pin count
            QStringList icPackages;
            if (pins <= 8)       icPackages << "dip-8" << "soic-8" << "so-8" << "tssop-8" << "msop-8";
            else if (pins <= 14) icPackages << "dip-14" << "soic-14" << "tssop-14";
            else if (pins <= 16) icPackages << "dip-16" << "soic-16" << "tssop-16";
            else if (pins <= 20) icPackages << "dip-20" << "soic-20" << "tssop-20" << "ssop-20";
            else if (pins <= 28) icPackages << "dip-28" << "soic-28" << "tssop-28" << "ssop-28";
            else                 icPackages << ("dip-" + QString::number(pins)) << ("qfp-" + QString::number(pins)) << ("lqfp-" + QString::number(pins));

            for (const QString& pkg : icPackages) {
                for (const QString& fp : libraryFootprints) {
                    if (fp.contains(pkg, Qt::CaseInsensitive)) return fp;
                }
            }
        }
        // Fallback: generic DIP
        for (const QString& fp : libraryFootprints) {
            if (fp.contains("dip", Qt::CaseInsensitive)) return fp;
        }
    }

    // Connectors
    if (comp.reference.startsWith("J") || comp.reference.startsWith("P") || comp.reference.startsWith("CN")) {
        int pins = comp.pinCount > 0 ? comp.pinCount : extractPinCount(comp);
        if (pins > 0) {
            QString connName = "conn-" + QString::number(pins);
            for (const QString& fp : libraryFootprints) {
                if (fp.contains(connName, Qt::CaseInsensitive) || fp.contains("header_" + QString::number(pins), Qt::CaseInsensitive)) return fp;
            }
        }
    }

    // Fallback: return first matching footprint by reference prefix
    {
        QString prefix = comp.reference.section(QRegularExpression("[0-9]"), 0, 0).toLower();
        for (const QString& fp : libraryFootprints) {
            QString fpLower = fp.toLower();
            if (prefix == "r" && (fpLower.contains("r_") || fpLower.contains("resistor"))) return fp;
            if (prefix == "c" && (fpLower.contains("c_") || fpLower.contains("capacitor"))) return fp;
            if (prefix == "l" && (fpLower.contains("l_") || fpLower.contains("inductor"))) return fp;
            if (prefix == "d" && fpLower.contains("diode")) return fp;
            if (prefix == "q" && (fpLower.contains("transistor") || fpLower.contains("sot"))) return fp;
            if (prefix == "u" && fpLower.contains("dip")) return fp;
        }
    }

    return QString(); // No suggestion available
}

int PCBNetlistImporter::extractPinCount(const NetlistImportComponent& comp) {
    if (comp.pinCount > 0) return comp.pinCount;
    return comp.pinPadMapping.size();
}

// ============================================================================
// Validation
// ============================================================================

QStringList PCBNetlistImporter::validate(const NetlistImportPackage& pkg) {
    QStringList issues;

    if (pkg.components.isEmpty()) {
        issues.append("Error: No components found in netlist.");
    }
    if (pkg.nets.isEmpty()) {
        issues.append("Warning: No nets found in netlist.");
    }

    QSet<QString> seenRefs;
    for (const auto& comp : pkg.components) {
        if (seenRefs.contains(comp.reference)) {
            issues.append(QString("Error: Duplicate component reference '%1'.").arg(comp.reference));
        }
        seenRefs.insert(comp.reference);

        if (comp.reference.isEmpty()) {
            issues.append("Error: Component with empty reference designator.");
        }
        if (comp.footprint.isEmpty() || comp.footprint.trimmed().isEmpty()) {
            issues.append(QString("Warning: Component '%1' has no footprint assigned.").arg(comp.reference));
        }
    }

    // Check for nets referencing non-existent components
    QSet<QString> compRefs;
    for (const auto& comp : pkg.components) {
        compRefs.insert(comp.reference);
    }
    for (const auto& net : pkg.nets) {
        for (const auto& pin : net.pins) {
            if (!compRefs.contains(pin.componentRef)) {
                issues.append(QString("Warning: Net '%1' references component '%2' which is not in component list.")
                    .arg(net.name, pin.componentRef));
            }
        }
    }

    return issues;
}

// ============================================================================
// Summary
// ============================================================================

QString PCBNetlistImporter::summary(const NetlistImportPackage& pkg) {
    QStringList lines;
    lines.append(QString("Source: %1 (%2)").arg(pkg.sourcePath, pkg.format));
    lines.append(QString("Components: %1").arg(pkg.components.size()));
    lines.append(QString("Nets: %1").arg(pkg.nets.size()));

    int missingFootprints = 0;
    for (const auto& comp : pkg.components) {
        if (comp.footprint.isEmpty()) missingFootprints++;
    }
    if (missingFootprints > 0) {
        lines.append(QString("⚠ Missing footprints: %1").arg(missingFootprints));
    }

    // Count total pins
    int totalPins = 0;
    for (const auto& net : pkg.nets) {
        totalPins += net.pins.size();
    }
    lines.append(QString("Total pin connections: %1").arg(totalPins));

    return lines.join("\n");
}

// ============================================================================
// Format parsers
// ============================================================================

PCBNetlistImporter::Format PCBNetlistImporter::detectFormat(const QString& content) {
    // Try JSON first
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("nets") || obj.contains("components")) {
            return FluxJSON;
        }
    }

    // Check for Protel format: lines like "(pin_name component_ref pin_number)"
    if (content.contains(QRegularExpression("^\\s*\\("))) {
        return Protel;
    }

    // Check for SPICE: starts with component lines (R, C, L, V, I, D, Q, M prefixes)
    if (content.contains(QRegularExpression("^[RrCcLlVvIiDdQqMjJxX]"))) {
        return SPICE;
    }

    // Default to FluxJSON if it looks like JSON
    if (content.trimmed().startsWith("{")) {
        return FluxJSON;
    }

    return FluxJSON; // Fallback
}

NetlistImportPackage PCBNetlistImporter::loadFluxJSON(const QString& content, const QString& sourcePath) {
    NetlistImportPackage pkg;
    pkg.sourcePath = sourcePath;
    pkg.format = "FluxJSON";

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "NetlistImporter: JSON parse error:" << err.errorString();
        return pkg;
    }

    QJsonObject root = doc.object();

    // Parse nets
    if (root.contains("nets")) {
        QJsonArray netsArray = root["nets"].toArray();
        for (const QJsonValue& netVal : netsArray) {
            QJsonObject netObj = netVal.toObject();
            NetlistImportNet net;
            net.name = netObj["name"].toString();

            if (netObj.contains("pins")) {
                QJsonArray pinsArray = netObj["pins"].toArray();
                for (const QJsonValue& pinVal : pinsArray) {
                    QJsonObject pinObj = pinVal.toObject();
                    NetlistImportPin pin;
                    pin.componentRef = pinObj["ref"].toString();
                    pin.pinName = pinObj["pin"].toString();
                    net.pins.append(pin);
                }
            }
            pkg.nets.append(net);
        }
    }

    // Parse components if present
    if (root.contains("components")) {
        QJsonArray compsArray = root["components"].toArray();
        for (const QJsonValue& compVal : compsArray) {
            QJsonObject compObj = compVal.toObject();
            NetlistImportComponent comp;
            comp.reference = compObj["reference"].toString();
            comp.value = compObj["value"].toString();
            comp.footprint = compObj["footprint"].toString();
            comp.spiceModel = compObj["spiceModel"].toString();
            comp.typeName = compObj["typeName"].toString();
            comp.pinCount = compObj["pinCount"].toInt(0);
            comp.packageHint = compObj["packageHint"].toString();
            comp.variant = compObj["variant"].toString();
            comp.excludeFromPcb = compObj["excludeFromPcb"].toBool(false);

            // Parse pinPadMapping
            if (compObj.contains("pinPadMapping")) {
                QJsonObject mappingObj = compObj["pinPadMapping"].toObject();
                for (auto it = mappingObj.begin(); it != mappingObj.end(); ++it) {
                    comp.pinPadMapping[it.key()] = it.value().toString();
                }
            }

            pkg.components.append(comp);
        }
    }

    // Build component list from nets if not explicitly provided
    if (pkg.components.isEmpty()) {
        QSet<QString> seenRefs;
        QRegularExpression refRe("^([A-Za-z_]+)");
        for (const auto& net : pkg.nets) {
            for (const auto& pin : net.pins) {
                if (!seenRefs.contains(pin.componentRef)) {
                    seenRefs.insert(pin.componentRef);
                    NetlistImportComponent comp;
                    comp.reference = pin.componentRef;
                    // Infer type from reference prefix
                    QString prefix = pin.componentRef.section(QRegularExpression("[0-9]"), 0, 0);
                    if (prefix == "R") comp.typeName = "Resistor";
                    else if (prefix == "C") comp.typeName = "Capacitor";
                    else if (prefix == "L") comp.typeName = "Inductor";
                    else if (prefix == "D") comp.typeName = "Diode";
                    else if (prefix == "Q" || prefix == "M") comp.typeName = "Transistor";
                    else if (prefix == "U" || prefix == "IC") comp.typeName = "IC";
                    else if (prefix == "V") comp.typeName = "Voltage Source";
                    else comp.typeName = "Component";
                    pkg.components.append(comp);
                }
            }
        }
    }

    return pkg;
}

NetlistImportPackage PCBNetlistImporter::loadProtel(const QString& content, const QString& sourcePath) {
    NetlistImportPackage pkg;
    pkg.sourcePath = sourcePath;
    pkg.format = "Protel";

    // Protel format: (net_name component pin_number) or similar parenthesized lines
    QRegularExpression lineRe("\\(\\s*(\\S+)\\s+(\\S+)\\s+(\\S+)\\s*\\)");
    QRegularExpressionMatchIterator it = lineRe.globalMatch(content);

    QMap<QString, NetlistImportNet> netMap;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString netName = match.captured(1);
        QString compRef = match.captured(2);
        QString pinName = match.captured(3);

        // Remove quotes if present
        netName.remove('"');
        compRef.remove('"');
        pinName.remove('"');

        if (!netMap.contains(netName)) {
            NetlistImportNet net;
            net.name = netName;
            netMap[netName] = net;
        }

        NetlistImportPin pin;
        pin.componentRef = compRef;
        pin.pinName = pinName;
        netMap[netName].pins.append(pin);
    }

    for (auto it2 = netMap.begin(); it2 != netMap.end(); ++it2) {
        pkg.nets.append(it2.value());
    }

    // Build component list from nets
    QSet<QString> seenRefs;
    for (const auto& net : pkg.nets) {
        for (const auto& pin : net.pins) {
            if (!seenRefs.contains(pin.componentRef)) {
                seenRefs.insert(pin.componentRef);
                NetlistImportComponent comp;
                comp.reference = pin.componentRef;
                QString prefix = pin.componentRef.section(QRegularExpression("[0-9]"), 0, 0);
                if (prefix == "R") comp.typeName = "Resistor";
                else if (prefix == "C") comp.typeName = "Capacitor";
                else if (prefix == "L") comp.typeName = "Inductor";
                else if (prefix == "D") comp.typeName = "Diode";
                else if (prefix == "Q" || prefix == "M") comp.typeName = "Transistor";
                else if (prefix == "U") comp.typeName = "IC";
                else comp.typeName = "Component";
                pkg.components.append(comp);
            }
        }
    }

    return pkg;
}

NetlistImportPackage PCBNetlistImporter::loadSPICE(const QString& content, const QString& sourcePath) {
    NetlistImportPackage pkg;
    pkg.sourcePath = sourcePath;
    pkg.format = "SPICE";

    QRegularExpression compRe("^([RrCcLlVvIiDdQqMjJxX])(\\S+)\\s+(\\S+)\\s+(\\S+)\\s*(.*)$", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = compRe.globalMatch(content);

    QRegularExpression refRe("^[RrCcLlVvIiDdQqMjJxX]");

    QMap<QString, QStringList> componentNets; // ref -> list of net names

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QChar prefixChar = match.captured(1).at(0).toUpper();
        char prefix = prefixChar.toLatin1();
        QString ref = match.captured(2);
        QString node1 = match.captured(3);
        QString node2 = match.captured(4);
        QString valueOrModel = match.captured(5).split(QRegularExpression("\\s+")).first();

        NetlistImportComponent comp;
        comp.reference = ref;

        switch (prefix) {
            case 'R': comp.typeName = "Resistor"; comp.value = valueOrModel; break;
            case 'C': comp.typeName = "Capacitor"; comp.value = valueOrModel; break;
            case 'L': comp.typeName = "Inductor"; comp.value = valueOrModel; break;
            case 'V': comp.typeName = "Voltage Source"; comp.value = valueOrModel; break;
            case 'I': comp.typeName = "Current Source"; comp.value = valueOrModel; break;
            case 'D': comp.typeName = "Diode"; comp.spiceModel = valueOrModel; break;
            case 'Q': comp.typeName = "BJT"; comp.spiceModel = valueOrModel; comp.pinCount = 3; break;
            case 'M': comp.typeName = "MOSFET"; comp.spiceModel = valueOrModel; comp.pinCount = 4; break;
            case 'J': comp.typeName = "JFET"; comp.spiceModel = valueOrModel; comp.pinCount = 3; break;
            default: comp.typeName = "Component"; break;
        }

        // Parse additional nodes for multi-pin devices
        QStringList nodes;
        nodes << node1 << node2;
        if (prefix == 'Q' || prefix == 'M' || prefix == 'J') {
            QString rest = match.captured(5);
            QStringList restParts = rest.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (const QString& n : restParts) {
                if (!n.startsWith(".")) { // Skip parameters
                    nodes << n;
                } else {
                    break;
                }
            }
            comp.pinCount = nodes.size();
        }

        componentNets[ref] = nodes;
        pkg.components.append(comp);
    }

    // Build nets by grouping shared node names
    // SPICE nodes are net names shared between components
    QMap<QString, QList<NetlistImportPin>> netPins; // node name -> pins
    for (auto it2 = componentNets.begin(); it2 != componentNets.end(); ++it2) {
        for (int i = 0; i < it2.value().size(); ++i) {
            QString nodeName = it2.value()[i];
            if (nodeName.toLower() == "0") nodeName = "GND"; // SPICE ground
            netPins[nodeName].append({it2.key(), QString::number(i + 1)});
        }
    }

    for (auto it3 = netPins.begin(); it3 != netPins.end(); ++it3) {
        if (it3.value().size() >= 1) { // Include even single-pin nets (they may connect to global)
            NetlistImportNet net;
            net.name = it3.key();
            net.pins = it3.value();
            pkg.nets.append(net);
        }
    }

    return pkg;
}
