#ifndef SERPENTINE_GENERATOR_H
#define SERPENTINE_GENERATOR_H

#include <QObject>
#include <QList>
#include <QPointF>
#include <QString>

class QGraphicsScene;
class TraceItem;

/**
 * @brief Generates serpentine (accordion/meander) patterns for length tuning.
 * 
 * Algorithm:
 * 1. Find the longest trace segment for the target net
 * 2. Compute how much extra length is needed
 * 3. Generate serpentine bumps perpendicular to the trace direction
 * 4. Each bump adds: 2 * amplitude + spacing * 2 (up-down-up pattern)
 * 5. Place bumps near the receiver end of the trace
 * 6. DRC check: ensure clearance between adjacent serpentine segments
 */
class SerpentineGenerator : public QObject {
    Q_OBJECT

public:
    struct SerpentineConfig {
        QString netName;
        double extraLength = 0.0;    // mm of length to add
        double amplitude = 1.5;      // mm (perpendicular offset)
        double spacing = 0.3;        // mm (clearance between adjacent segments)
        double traceWidth = 0.25;    // mm
        double clearance = 0.2;      // mm minimum clearance
        int layer = -1;              // Layer ID (-1 = auto: use existing trace layer)
        bool placeNearReceiver = true; // Place serpentine near the end of the trace
    };

    struct SerpentineResult {
        bool success = false;
        int segmentsCreated = 0;
        double actualAddedLength = 0.0;
        QString error;
        QList<TraceItem*> newTraces;
    };

    explicit SerpentineGenerator(QGraphicsScene* scene, QObject* parent = nullptr);
    ~SerpentineGenerator();

    /**
     * @brief Generate serpentine pattern for the specified net.
     * @param config Serpentine configuration
     * @return Result with success status and created trace segments
     */
    SerpentineResult generateSerpentine(const SerpentineConfig& config);

    /**
     * @brief Compute how many serpentine bumps are needed for the required length.
     * @param extraLength Required additional length in mm
     * @param amplitude Serpentine amplitude in mm
     * @param spacing Serpentine spacing in mm
     * @return Number of bumps needed
     */
    static int computeBumpCount(double extraLength, double amplitude, double spacing);

    /**
     * @brief Compute the length added by a single serpentine bump.
     * A bump consists of: perpendicular segment + parallel segment + perpendicular return
     * Length = 2 * amplitude + (segment parallel to main trace)
     */
    static double lengthPerBump(double amplitude, double spacing);

private:
    struct TraceSegmentInfo {
        QPointF start;
        QPointF end;
        int layer;
        double length;
        double angle; // Angle of the segment in degrees
    };

    TraceSegmentInfo findLongestSegment(const QString& netName);
    QPointF findReceiverEnd(const QString& netName);
    bool hasClearanceViolation(QPointF pos, double clearance, const QString& excludeNet);
    QList<TraceItem*> createSerpentineChain(QPointF start, QPointF direction, int bumpCount,
                                             double amplitude, double spacing, int layer,
                                             const QString& netName, double width);

    QGraphicsScene* m_scene;
};

#endif // SERPENTINE_GENERATOR_H
