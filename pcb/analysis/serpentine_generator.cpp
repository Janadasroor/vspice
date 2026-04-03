#include "serpentine_generator.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/pcb_item.h"
#include "../layers/pcb_layer.h"
#include <QGraphicsScene>
#include <QtMath>
#include <QLineF>

SerpentineGenerator::SerpentineGenerator(QGraphicsScene* scene, QObject* parent)
    : QObject(parent), m_scene(scene)
{
}

SerpentineGenerator::~SerpentineGenerator() = default;

// ============================================================================
// Static helpers
// ============================================================================

int SerpentineGenerator::computeBumpCount(double extraLength, double amplitude, double spacing) {
    double perBump = lengthPerBump(amplitude, spacing);
    if (perBump <= 0) return 0;
    return qCeil(extraLength / perBump);
}

double SerpentineGenerator::lengthPerBump(double amplitude, double spacing) {
    // A single bump adds: 2 * amplitude (perpendicular segments) + spacing (parallel segment)
    // The parallel segment replaces an equivalent straight trace segment, so net gain is:
    // Net gain = 2 * amplitude + spacing - spacing = 2 * amplitude
    // But we also need the return path, so actual gain per bump = 2 * amplitude
    return 2.0 * amplitude;
}

// ============================================================================
// Main generation
// ============================================================================

SerpentineGenerator::SerpentineResult SerpentineGenerator::generateSerpentine(const SerpentineConfig& config) {
    SerpentineResult result;

    if (!m_scene) {
        result.error = "No scene available";
        return result;
    }

    if (config.extraLength <= 0.0) {
        result.error = "No extra length needed";
        return result;
    }

    // Find the longest trace segment for this net
    TraceSegmentInfo longestSeg = findLongestSegment(config.netName);
    if (longestSeg.length < 0.01) {
        result.error = "No trace segments found for net: " + config.netName;
        return result;
    }

    // Determine layer
    int targetLayer = (config.layer >= 0) ? config.layer : longestSeg.layer;

    // Compute bump count
    int bumpCount = computeBumpCount(config.extraLength, config.amplitude, config.spacing);
    if (bumpCount <= 0) {
        result.error = "Invalid bump count computed";
        return result;
    }

    // Find where to place serpentine (near receiver end)
    QPointF receiverEnd = findReceiverEnd(config.netName);
    QPointF direction = QLineF(longestSeg.start, longestSeg.end).unitVector().p2() -
                        QLineF(longestSeg.start, longestSeg.end).unitVector().p1();

    // Check if we have enough room on the longest segment
    double segmentLen = longestSeg.length;
    double serpentineSpan = bumpCount * (config.amplitude + config.spacing);

    if (serpentineSpan > segmentLen * 0.8) {
        // Need more space than available - reduce bumps or increase amplitude
        bumpCount = qMax(1, static_cast<int>(segmentLen * 0.8 / (config.amplitude + config.spacing)));
    }

    // Find start position for serpentine (near receiver end of longest segment)
    QPointF serpStart;
    if (config.placeNearReceiver) {
        // Place near the end of the segment that's closer to the receiver
        double distToStart = QLineF(longestSeg.start, receiverEnd).length();
        double distToEnd = QLineF(longestSeg.end, receiverEnd).length();
        serpStart = (distToStart < distToEnd) ? longestSeg.start : longestSeg.end;
        // Back up a bit to leave room
        QPointF dir = direction.normalized();
        if (QLineF(longestSeg.start, receiverEnd).length() > QLineF(longestSeg.end, receiverEnd).length()) {
            serpStart = longestSeg.end - dir * 2.0;
        } else {
            serpStart = longestSeg.start + dir * 2.0;
        }
    } else {
        serpStart = longestSeg.start;
    }

    // Generate serpentine
    result.newTraces = createSerpentineChain(serpStart, direction, bumpCount,
                                              config.amplitude, config.spacing,
                                              targetLayer, config.netName, config.traceWidth);

    if (result.newTraces.isEmpty()) {
        result.error = "Failed to create serpentine segments";
        return result;
    }

    result.success = true;
    result.segmentsCreated = result.newTraces.size();
    result.actualAddedLength = bumpCount * lengthPerBump(config.amplitude, config.spacing);

    return result;
}

// ============================================================================
// Find longest segment
// ============================================================================

SerpentineGenerator::TraceSegmentInfo SerpentineGenerator::findLongestSegment(const QString& netName) {
    TraceSegmentInfo longest;
    longest.length = -1.0;

    if (!m_scene) return longest;

    for (auto* item : m_scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            if (trace->netName() != netName) continue;

            double len = QLineF(trace->startPoint(), trace->endPoint()).length();
            if (len > longest.length) {
                longest.start = trace->startPoint();
                longest.end = trace->endPoint();
                longest.length = len;
                longest.layer = trace->layer();
                longest.angle = QLineF(trace->startPoint(), trace->endPoint()).angle();
            }
        }
    }

    return longest;
}

QPointF SerpentineGenerator::findReceiverEnd(const QString& netName) {
    // Find pads for this net - the "receiver" is typically the pad with higher Y or X
    // (simplified heuristic - ideally this comes from schematic connectivity)
    QList<PadItem*> pads;

    if (!m_scene) return QPointF();

    for (auto* item : m_scene->items()) {
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->netName() == netName) {
                pads.append(pad);
            }
        }
    }

    if (pads.isEmpty()) return QPointF();

    // Simple heuristic: receiver is the pad with maximum (X + Y)
    // In a real design, this would come from schematic directionality
    QPointF receiver = pads[0]->scenePos();
    for (int i = 1; i < pads.size(); ++i) {
        QPointF pos = pads[i]->scenePos();
        if ((pos.x() + pos.y()) > (receiver.x() + receiver.y())) {
            receiver = pos;
        }
    }

    return receiver;
}

// ============================================================================
// Clearance check
// ============================================================================

bool SerpentineGenerator::hasClearanceViolation(QPointF pos, double clearance, const QString& excludeNet) {
    if (!m_scene) return false;

    // Check items near the position
    auto items = m_scene->items(QRectF(pos.x() - clearance, pos.y() - clearance,
                                        clearance * 2, clearance * 2));

    for (auto* item : items) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            if (trace->netName() == excludeNet) continue;
            // Check distance to trace
            QLineF toTrace(trace->startPoint(), trace->endPoint());
            QPointF closest = toTrace.pointAt(0.5); // Simplified - should compute actual closest point
            if (QLineF(pos, closest).length() < clearance) return true;
        }
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->netName() == excludeNet) continue;
            if (QLineF(pos, pad->scenePos()).length() < clearance) return true;
        }
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            if (via->netName() == excludeNet) continue;
            if (QLineF(pos, via->pos()).length() < clearance) return true;
        }
    }

    return false;
}

// ============================================================================
// Create serpentine chain
// ============================================================================

QList<TraceItem*> SerpentineGenerator::createSerpentineChain(QPointF start, QPointF direction,
                                                              int bumpCount, double amplitude,
                                                              double spacing, int layer,
                                                              const QString& netName, double width) {
    QList<TraceItem*> traces;

    if (!m_scene) return traces;

    // Normalize direction
    direction = direction.normalized();
    // Perpendicular direction (rotate 90 degrees)
    QPointF perp(-direction.y(), direction.x());

    QPointF currentPos = start;
    bool goingOut = true;

    for (int i = 0; i < bumpCount; ++i) {
        // Check for clearance violations before creating
        QPointF testPos = currentPos + perp * amplitude * (goingOut ? 1 : -1);
        if (hasClearanceViolation(testPos, spacing, netName)) {
            // Try opposite direction
            testPos = currentPos - perp * amplitude * (goingOut ? 1 : -1);
            if (hasClearanceViolation(testPos, spacing, netName)) {
                // Can't place bump here - skip
                continue;
            }
        }

        // Segment 1: Perpendicular out
        QPointF nextPos = currentPos + perp * amplitude * (goingOut ? 1 : -1);
        TraceItem* t1 = new TraceItem(currentPos, nextPos);
        t1->setLayer(layer);
        t1->setWidth(width);
        t1->setNetName(netName);
        m_scene->addItem(t1);
        traces.append(t1);
        currentPos = nextPos;

        // Segment 2: Parallel (along trace direction)
        nextPos = currentPos + direction * spacing;
        TraceItem* t2 = new TraceItem(currentPos, nextPos);
        t2->setLayer(layer);
        t2->setWidth(width);
        t2->setNetName(netName);
        m_scene->addItem(t2);
        traces.append(t2);
        currentPos = nextPos;

        // Segment 3: Perpendicular back
        nextPos = currentPos - perp * amplitude * (goingOut ? 1 : -1);
        TraceItem* t3 = new TraceItem(currentPos, nextPos);
        t3->setLayer(layer);
        t3->setWidth(width);
        t3->setNetName(netName);
        m_scene->addItem(t3);
        traces.append(t3);
        currentPos = nextPos;

        goingOut = !goingOut; // Alternate direction for accordion pattern
    }

    // Connect back to original trace endpoint if needed
    // (The serpentine replaces a segment of the original trace)

    return traces;
}
