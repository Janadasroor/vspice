#ifndef PCB_DIFF_ENGINE_H
#define PCB_DIFF_ENGINE_H

#include <QString>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QSizeF>
#include <QJsonDocument>

namespace Flux { namespace Model {
    class BoardModel;
    class ComponentModel;
    class TraceModel;
    class ViaModel;
    class PadModel;
    class CopperPourModel;
}}

/**
 * @brief Types of differences detected by the PCB diff engine.
 */
enum class DiffType {
    Added,          // Exists in board B but not A
    Removed,        // Exists in board A but not B
    Moved,          // Position changed
    Rotated,        // Rotation changed
    Resized,        // Size changed
    NetChanged,     // Net name changed
    FootprintChanged, // Footprint changed (components only)
    ValueChanged,   // Value changed (components only)
    PropertyChanged, // Other property changed
    LayerChanged,   // Layer changed
    Unchanged       // No difference
};

/**
 * @brief A single difference entry with location and description.
 */
struct DiffEntry {
    DiffType type;
    QString category;       // "Component", "Trace", "Via", "CopperPour", "Net"
    QString identifier;     // Reference designator, net name, or UUID
    QString description;    // Human-readable description
    QPointF location;       // Scene coordinates (for zoom-to-diff)
    int layer = -1;         // Layer ID if applicable
    
    QString colorCode() const {
        switch (type) {
            case DiffType::Added:    return "#28a745"; // Green
            case DiffType::Removed:  return "#dc3545"; // Red
            case DiffType::Moved:
            case DiffType::Rotated:
            case DiffType::Resized:
            case DiffType::NetChanged:
            case DiffType::FootprintChanged:
            case DiffType::ValueChanged:
            case DiffType::PropertyChanged:
            case DiffType::LayerChanged: return "#ffc107"; // Yellow
            case DiffType::Unchanged:  return "#6c757d"; // Gray
        }
        return "#6c757d";
    }
    
    QString icon() const {
        switch (type) {
            case DiffType::Added:    return "➕";
            case DiffType::Removed:  return "➖";
            case DiffType::Moved:    return "↔️";
            case DiffType::Rotated:  return "🔄";
            case DiffType::Resized:  return "📐";
            case DiffType::NetChanged: return "🔗";
            case DiffType::FootprintChanged: return "📦";
            case DiffType::ValueChanged: return "🏷️";
            case DiffType::LayerChanged: return "📑";
            default: return "•";
        }
    }
};

/**
 * @brief Summary statistics of a board comparison.
 */
struct DiffStats {
    int componentsAdded = 0;
    int componentsRemoved = 0;
    int componentsModified = 0;
    int tracesAdded = 0;
    int tracesRemoved = 0;
    int tracesModified = 0;
    int viasAdded = 0;
    int viasRemoved = 0;
    int viasModified = 0;
    int copperPoursAdded = 0;
    int copperPoursRemoved = 0;
    int netsAdded = 0;
    int netsRemoved = 0;
    int netsModified = 0;
    
    int totalDifferences() const {
        return componentsAdded + componentsRemoved + componentsModified +
               tracesAdded + tracesRemoved + tracesModified +
               viasAdded + viasRemoved + viasModified +
               copperPoursAdded + copperPoursRemoved +
               netsAdded + netsRemoved + netsModified;
    }
    
    bool isEmpty() const { return totalDifferences() == 0; }
};

/**
 * @brief Complete diff report between two PCB boards.
 */
struct DiffReport {
    QString boardAName;       // Name/path of board A (baseline)
    QString boardBName;       // Name/path of board B (comparison)
    QList<DiffEntry> entries;
    DiffStats stats;
    bool identical = false;
    
    // Filtered access
    QList<DiffEntry> byCategory(const QString& category) const {
        QList<DiffEntry> result;
        for (const auto& e : entries) {
            if (e.category == category) result.append(e);
        }
        return result;
    }
    
    QList<DiffEntry> byType(DiffType type) const {
        QList<DiffEntry> result;
        for (const auto& e : entries) {
            if (e.type == type) result.append(e);
        }
        return result;
    }
    
    // JSON export
    QString toJson() const;
    static DiffReport fromJson(const QString& json);
};

/**
 * @brief Headless engine that compares two PCB boards.
 * 
 * Compares:
 * - Components (reference, footprint, value, position, rotation, layer)
 * - Traces (net, layer, start/end points, width)
 * - Vias (net, position, layers, diameter, drill)
 * - Copper pours (net, layer, clearance)
 * - Net connectivity (implicit from pad net assignments)
 */
class PCBDiffEngine {
public:
    struct CompareOptions {
        double positionTolerance = 0.01;   // mm — positions within this are "same"
        double rotationTolerance = 0.1;    // degrees — rotations within this are "same"
        double sizeTolerance = 0.01;       // mm — sizes within this are "same"
        bool compareNetNames = true;
        bool compareFootprints = true;
        bool compareValues = true;
        bool compareLayers = true;
    };

    /**
     * @brief Compare two board models and produce a diff report.
     * @param boardA Baseline board
     * @param boardB Comparison board
     * @param options Comparison tolerances
     * @return DiffReport with all differences
     */
    static DiffReport compare(const Flux::Model::BoardModel* boardA,
                              const Flux::Model::BoardModel* boardB,
                              const CompareOptions& options);

    /**
     * @brief Compare two boards loaded from JSON files.
     */
    static DiffReport compareFiles(const QString& filePathA, const QString& filePathB,
                                    const CompareOptions& options);

private:
    static void compareComponents(const Flux::Model::BoardModel* a,
                                   const Flux::Model::BoardModel* b,
                                   DiffReport& report, const CompareOptions& opts);
    static void compareTraces(const Flux::Model::BoardModel* a,
                               const Flux::Model::BoardModel* b,
                               DiffReport& report, const CompareOptions& opts);
    static void compareVias(const Flux::Model::BoardModel* a,
                             const Flux::Model::BoardModel* b,
                             DiffReport& report, const CompareOptions& opts);
    static void compareCopperPours(const Flux::Model::BoardModel* a,
                                    const Flux::Model::BoardModel* b,
                                    DiffReport& report, const CompareOptions& opts);
    static void compareNets(const Flux::Model::BoardModel* a,
                             const Flux::Model::BoardModel* b,
                             DiffReport& report);
};

#endif // PCB_DIFF_ENGINE_H
