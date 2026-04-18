#ifndef NETLIST_IMPORTER_H
#define NETLIST_IMPORTER_H

#include <QString>
#include <QList>
#include <QMap>
#include <QVariant>
#include "eco_types.h"

/**
 * @brief Represents a component extracted from a netlist for PCB import.
 */
struct NetlistImportComponent {
    QString reference;       // e.g. "R1", "U3"
    QString value;           // e.g. "10k", "LM358"
    QString footprint;       // Assigned footprint (may be auto-suggested or empty)
    QString spiceModel;      // SPICE model name
    QString typeName;        // Component type name (e.g. "Resistor", "NPN")
    int pinCount = 0;        // Number of pins from netlist
    QMap<QString, QString> pinPadMapping; // pin name -> pad name
    bool excludeFromPcb = false;

    // Metadata for footprint suggestion
    QString packageHint;     // e.g. "0603", "SOT-23" from netlist annotations
    QString variant;         // e.g. "SMD", "THT"
};

/**
 * @brief Represents a pin connection within a net from the netlist.
 */
struct NetlistImportPin {
    QString componentRef;
    QString pinName;
};

/**
 * @brief Represents a net from the imported netlist.
 */
struct NetlistImportNet {
    QString name;
    QList<NetlistImportPin> pins;
};

/**
 * @brief The complete package of data imported from a netlist.
 */
struct NetlistImportPackage {
    QList<NetlistImportComponent> components;
    QList<NetlistImportNet> nets;
    QString sourcePath;      // File path or "Schematic Editor"
    QString format;          // "FluxJSON", "Protel", "SPICE", "ECO"
};

/**
 * @brief Headless netlist importer - parses netlist files and prepares import data for PCB.
 * 
 * Supports multiple netlist formats and integrates with the existing ECO infrastructure.
 */
class PCBNetlistImporter {
public:
    enum Format {
        AutoDetect,
        FluxJSON,      // VioSpice native JSON netlist
        Protel,        // Legacy Protel format
        SPICE,         // SPICE circuit file
        ECO            // Direct ECO package from schematic
    };

    /**
     * @brief Load and parse a netlist file from disk.
     * @param filePath Path to the netlist file
     * @param format Format hint (AutoDetect will try to infer from extension/content)
     * @return Parsed NetlistImportPackage, or empty package on failure
     */
    static NetlistImportPackage loadFromFile(const QString& filePath, Format format = AutoDetect);

    /**
     * @brief Generate an import package directly from a schematic scene (headless via model).
     * @param schematicModel The schematic page model
     * @return NetlistImportPackage ready for import
     */
    static NetlistImportPackage generateFromSchematic(const void* schematicModel);

    /**
     * @brief Convert a NetlistImportPackage to the internal ECOPackage format.
     * This allows reuse of the existing PCB applyECO() machinery.
     */
    static ECOPackage toECOPackage(const NetlistImportPackage& importPkg);

    /**
     * @brief Auto-suggest footprints for components that don't have one assigned.
     * Uses component type, value, pin count, and package hints.
     * @param pkg The import package (modified in place)
     * @param libraryFootprints List of available footprint names from the library
     */
    static void suggestFootprints(NetlistImportPackage& pkg, const QStringList& libraryFootprints);

    /**
     * @brief Validate the import package for completeness.
     * @return List of validation warnings/errors
     */
    static QStringList validate(const NetlistImportPackage& pkg);

    /**
     * @brief Generate a human-readable summary of the import package.
     */
    static QString summary(const NetlistImportPackage& pkg);

private:
    static NetlistImportPackage loadFluxJSON(const QString& content, const QString& sourcePath);
    static NetlistImportPackage loadProtel(const QString& content, const QString& sourcePath);
    static NetlistImportPackage loadSPICE(const QString& content, const QString& sourcePath);
    static Format detectFormat(const QString& content);
    static QString suggestFootprintForComponent(const NetlistImportComponent& comp, const QStringList& libraryFootprints);
    static QString normalizeFootprintName(const QString& name);
    static int extractPinCount(const NetlistImportComponent& comp);
};

#endif // NETLIST_IMPORTER_H
