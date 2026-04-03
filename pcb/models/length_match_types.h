#ifndef LENGTH_MATCH_TYPES_H
#define LENGTH_MATCH_TYPES_H

#include <QString>
#include <QList>
#include <QSet>
#include <QPointF>

/**
 * @brief Data types for high-speed length matching and serpentine tuning.
 */

struct LengthMatchEntry {
    QString netName;
    double length = 0.0;          // Total routed length in mm
    double viaLength = 0.0;       // Vertical via contribution in mm
    int viaCount = 0;
    double delayPs = 0.0;         // Propagation delay in picoseconds
    double deltaFromTarget = 0.0; // mm (negative = too short)
    bool withinTolerance = true;
    bool isDiffPairP = false;     // Part of differential pair (P side)
    bool isDiffPairN = false;     // Part of differential pair (N side)
    QString diffPairName;         // If part of a diff pair, the pair name
};

struct DiffPairSkewInfo {
    QString pairName;
    QString pNet, nNet;
    double pLength = 0.0;
    double nLength = 0.0;
    double intraPairSkew = 0.0;   // |P - N| in mm
    bool withinTolerance = true;
};

struct SerpentineSegment {
    QPointF startPoint;
    QPointF endPoint;
    int layer;
    double width;
    QString netName;
};

struct SerpentineTuning {
    QString netName;
    double addedLength = 0.0;     // mm of serpentine added
    QList<SerpentineSegment> segments;
    double amplitude = 1.5;       // mm
    double spacing = 0.3;         // mm
    QString layer;                // Which layer tuning was applied
};

struct LengthMatchGroup {
    QString id;                   // Unique identifier
    QString name;                 // Display name (e.g. "DDR3_DQ_Bus")
    QSet<QString> netNames;       // Nets in this group
    double targetLength = 0.0;    // mm (0 = auto: longest net)
    double tolerance = 0.0;       // ±mm
    double intraPairTolerance = 0.1;  // mm for differential pairs
    bool autoComputeTarget = true;    // If true, target = longest net
    bool enableSerpentine = true;     // Allow auto-tuning
    QString tuningLayer;          // Preferred layer for serpentine (-1 = auto)
    double serpentineAmplitude = 1.5; // mm
    double serpentineSpacing = 0.3;   // mm (clearance)

    // Computed results
    QList<LengthMatchEntry> entries;
    QList<DiffPairSkewInfo> diffPairInfo;
    bool allWithinTolerance = false;
    double maxLength = 0.0;
    double minLength = 0.0;

    // Validation
    bool hasErrors = false;
    QStringList errors;
};

#endif // LENGTH_MATCH_TYPES_H
