#include "schematic_wire_utils.h"
#include "../items/wire_item.h"
#include "../items/schematic_item.h"
#include "../editor/schematic_commands.h"
#include <QVector2D>
#include <QLineF>
#include <algorithm>

void SchematicWireUtils::splitWiresByComponent(
    SchematicItem* component,
    QGraphicsScene* scene,
    QUndoStack* undoStack,
    const QList<WireItem*>& excludeWires
) {
    if (!component || !scene || !undoStack) return;

    // Retrieve component pins in scene coordinates
    QList<QPointF> pins;
    for (const QPointF& p : component->connectionPoints()) {
        pins.append(component->mapToScene(p));
    }

    if (pins.size() < 2) return;

    QRectF compRect = component->sceneBoundingRect();
    // Expand rect slightly to catch wires exactly on the edge
    compRect.adjust(-2, -2, 2, 2);

    QList<QGraphicsItem*> items = scene->items(compRect);
    QList<WireItem*> wiresToCut;
    for (auto* item : items) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            if (excludeWires.contains(wire)) continue;
            // Don't cut if it's not a signal/power wire (e.g. preview)
            wiresToCut.append(wire);
        }
    }

    // Helper to check if a point is "inside" (covered by) the component body
    auto isInsideBody = [&](QPointF p) -> bool {
         for (int i = 0; i < pins.size(); ++i) {
             for (int j = i + 1; j < pins.size(); ++j) {
                 QLineF pinLine(pins[i], pins[j]);
                 qreal len = pinLine.length();
                 if (len < 1e-4) continue;
                 
                 qreal t = QVector2D::dotProduct(QVector2D(p - pins[i]), QVector2D(pins[j] - pins[i])) / (len * len);
                 if (t > 0.01 && t < 0.99) { // Strictly inside body
                     qreal dist = QLineF(p, pinLine.pointAt(t)).length();
                     if (dist < 2.0) return true;
                 }
             }
         }
         return false;
    };

    for (WireItem* wire : wiresToCut) {
        bool wireModified = false;
        QList<QList<QPointF>> newWireSegments;
        QList<QPointF> currentSegment;
        
        QList<QPointF> wPts = wire->points();
        if (wPts.size() < 2) continue;

        for (int i = 0; i < wPts.size() - 1; ++i) {
            QPointF pStart = wPts[i];
            QPointF pEnd = wPts[i+1];
            QLineF segment(pStart, pEnd);
            
            // Find all pins that lie on this segment
            QList<QPair<qreal, QPointF>> cuts;
            cuts.append({0.0, pStart});
            cuts.append({1.0, pEnd});

            for (const QPointF& pin : pins) {
                qreal len = segment.length();
                if (len > 1e-4) {
                    qreal t = QVector2D::dotProduct(QVector2D(pin - pStart), QVector2D(pEnd - pStart)) / (len * len);
                    if (t >= 0.0 && t <= 1.0) {
                        QPointF proj = segment.pointAt(t);
                        if (QLineF(pin, proj).length() < 2.5) { // Snapping tolerance
                            cuts.append({t, pin}); 
                        }
                    }
                }
            }

            // Sort by t to process sub-segments in order
            std::sort(cuts.begin(), cuts.end(), [](const auto& a, const auto& b){
                return a.first < b.first;
            });

            for (int k = 0; k < cuts.size() - 1; ++k) {
                QPointF subStart = cuts[k].second;
                QPointF subEnd = cuts[k+1].second;
                
                if (QLineF(subStart, subEnd).length() < 0.1) continue;

                QPointF mid = (subStart + subEnd) / 2.0;
                if (isInsideBody(mid)) {
                    // This segment is inside the component - it's a "cut"
                    if (!currentSegment.isEmpty()) {
                        currentSegment.append(subStart);
                        newWireSegments.append(currentSegment);
                        currentSegment.clear();
                    }
                    wireModified = true;
                } else {
                    // Keep this segment
                    if (currentSegment.isEmpty()) currentSegment.append(subStart);
                    else if (QLineF(currentSegment.last(), subStart).length() > 0.1) {
                        newWireSegments.append(currentSegment);
                        currentSegment.clear();
                        currentSegment.append(subStart);
                    }
                    currentSegment.append(subEnd);
                }
            }
        }
        if (!currentSegment.isEmpty()) newWireSegments.append(currentSegment);

        if (wireModified) {
            // Remove old wire and add new segments
            undoStack->push(new RemoveItemCommand(scene, {wire}));

            for (const auto& pts : newWireSegments) {
                if (pts.size() < 2) continue;
                WireItem* newWire = new WireItem();
                newWire->setPoints(pts);
                newWire->setWireType(wire->wireType());
                undoStack->push(new AddItemCommand(scene, newWire));
            }
        }
    }
}

QList<QPointF> SchematicWireUtils::maintainOrthogonality(const QList<QPointF>& points, int index, const QPointF& newPos) {
    if (points.size() < 2 || index < 0 || index >= points.size()) return points;

    QList<QPointF> result = points;
    result[index] = newPos;

    // Handle neighbors to maintain orthogonality
    auto fixNeighbor = [&](int target, int neighbor) {
        if (neighbor < 0 || neighbor >= points.size()) return;
        
        bool isHorizontal = qAbs(points[target].y() - points[neighbor].y()) < 0.1;
        bool isVertical = qAbs(points[target].x() - points[neighbor].x()) < 0.1;

        if (isHorizontal) {
            result[neighbor].setY(newPos.y());
        } else if (isVertical) {
            result[neighbor].setX(newPos.x());
        } else {
            // Force it
            if (qAbs(newPos.x() - points[neighbor].x()) > qAbs(newPos.y() - points[neighbor].y())) {
                result[neighbor].setY(newPos.y());
            } else {
                result[neighbor].setX(newPos.x());
            }
        }
    };

    if (index == 0) {
        fixNeighbor(0, 1);
    } else if (index == points.size() - 1) {
        fixNeighbor(points.size() - 1, points.size() - 2);
    } else {
        // Mid-point move (e.g. junction on a segment that was split)
        fixNeighbor(index, index - 1);
        fixNeighbor(index, index + 1);
    }

    return result;
}
