#ifndef SPICE_NETLIST_PARSER_H
#define SPICE_NETLIST_PARSER_H

#include <QString>
#include <QList>
#include <QMap>

/**
 * @brief Parses SPICE .cir netlist files into structured component and net data.
 *
 * Extracts component instances (type, reference, value, node connections) and
 * builds a net-to-pin map suitable for schematic reconstruction.
 *
 * Smart resolution: parses .model directives to determine component types
 * (e.g., NPN vs PNP, NMOS vs PMOS, NMF vs PMF) from model definitions.
 */
class SpiceNetlistParser {
public:
    struct ParsedComponent {
        QString reference;    // e.g. "R1", "C2", "Q3"
        QString typeName;     // Factory type name, e.g. "Resistor", "Capacitor"
        QString value;        // e.g. "10k", "100nF", "2N2222"
        QStringList nodes;    // Net names in pin order, e.g. ["net1", "0"]
    };

    struct NetPin {
        QString componentRef;
        int pinIndex;         // 0-based
    };

    struct ParsedNet {
        QString name;
        QList<NetPin> pins;
    };

    /// SPICE model type extracted from .model directives
    struct ModelInfo {
        QString name;         // e.g. "MyMesfet"
        QString spiceType;    // e.g. "NMF", "NPN", "NMOS", "D"
        QString factoryType;  // Resolved factory type name
    };

    struct ParsedNetlist {
        QList<ParsedComponent> components;
        QList<ParsedNet> nets;
        QStringList directives;   // .model, .tran, etc. (preserved but not used for placement)
        QMap<QString, ModelInfo> models;  // model name → model info
        QString title;
    };

    /**
     * @brief Parse a SPICE netlist file
     * @param filePath Path to the .cir file
     * @return Parsed netlist structure
     */
    static ParsedNetlist parse(const QString& filePath);

    /**
     * @brief Map a SPICE reference prefix letter to a SchematicItemFactory type name.
     * @param prefix The first character of the reference (R, C, Q, etc.)
     * @param modelType The SPICE model type from .model directive (NPN, PMOS, NMF, etc.)
     * @param value Fallback: the value/model string from the component line
     */
    static QString spicePrefixToTypeName(QChar prefix, const QString& modelType = QString(),
                                         const QString& value = QString());

private:
    static ParsedComponent parseLine(const QString& line, const QMap<QString, ModelInfo>& models);
    static void buildNets(ParsedNetlist& netlist);
    static void parseModels(const QStringList& directives, QMap<QString, ModelInfo>& models);
    static QString modelTypeToFactoryType(const QString& spiceModelType);
    static int nodeCountForPrefix(QChar prefix);
};

#endif // SPICE_NETLIST_PARSER_H
