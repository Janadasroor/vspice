#include "schematic_layout_optimizer.h"
#include "wire_item.h"
#include "schematic_item.h"
#include <QGraphicsScene>
#include <QDebug>
#include <algorithm>
#include <cmath>

SchematicLayoutOptimizer::SchematicLayoutOptimizer(QObject* parent)
    : QObject(parent)
    , m_minimumClearance(5.0)    // 5 pixels minimum clearance
    , m_traceWidth(2.0)          // 2 pixels trace width
    , m_viaClearance(8.0)        // 8 pixels via clearance
    , m_enableOrthogonalRouting(true)
    , m_enableMiteredCorners(true)
    , m_enableBusGrouping(true)
    , m_gridSize(10.0) {
}

void SchematicLayoutOptimizer::optimizeLayout(QGraphicsScene* scene) {
    if (!scene) return;

    emit optimizationProgress(0);

    // Step 1: Apply orthogonal routing
    applyOrthogonalRouting(scene);
    emit optimizationProgress(25);

    // Step 2: Minimize wire crossings
    minimizeWireCrossings(scene);
    emit optimizationProgress(50);

    // Step 3: Create bus groups
    if (m_enableBusGrouping) {
        createBusGroups(scene);
    }
    emit optimizationProgress(75);

    // Step 4: Apply HDI clearance standards
    applyHDIClearance(scene);
    emit optimizationProgress(100);

    emit optimizationComplete(1); // Placeholder for actual improvement count
}

void SchematicLayoutOptimizer::applyOrthogonalRouting(QGraphicsScene* scene) {
    QList<QGraphicsItem*> items = scene->items();
    QList<WireItem*> wires;

    // Collect all wires
    for (QGraphicsItem* item : items) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            wires.append(wire);
        }
    }

    // Apply orthogonal routing to each wire
    for (WireItem* wire : wires) {
        optimizeWireRouting(wire, wires);
    }

    qDebug() << "Applied orthogonal routing to" << wires.size() << "wires";
}

void SchematicLayoutOptimizer::minimizeWireCrossings(QGraphicsScene* scene) {
    QList<WireItem*> crossingWires = findCrossingWires(scene);

    for (WireItem* wire : crossingWires) {
        // Find alternative path that avoids crossings
        QList<QPointF> points = wire->points();
        if (points.size() >= 2) {
            QList<QPointF> optimizedPath = findOptimalPath(points.first(), points.last(),
                                                          findCrossingWires(scene));

            // Clear existing points and add optimized path
            while (wire->points().size() > 1) {
                wire->removeLastSegment();
            }

            for (int i = 1; i < optimizedPath.size(); ++i) {
                wire->addSegment(optimizedPath[i]);
            }
        }
    }

    qDebug() << "Minimized crossings for" << crossingWires.size() << "wires";
}

void SchematicLayoutOptimizer::createBusGroups(QGraphicsScene* scene) {
    QList<QList<WireItem*>> busGroups = identifyBusGroups(scene);

    for (QList<WireItem*>& busGroup : busGroups) {
        if (busGroup.size() > 1) {
            alignBusWires(busGroup);
        }
    }

    qDebug() << "Created" << busGroups.size() << "bus groups";
}

void SchematicLayoutOptimizer::applyHDIClearance(QGraphicsScene* scene) {
    QList<QGraphicsItem*> items = scene->items();

    // Check clearance for all conductive elements
    for (QGraphicsItem* item : items) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            // Ensure wire maintains proper clearance
            QList<QPointF> points = wire->points();
            for (const QPointF& point : points) {
                if (!validateClearance(point, m_minimumClearance)) {
                    qWarning() << "HDI clearance violation at" << point;
                }
            }
        }
    }

    qDebug() << "Applied HDI clearance standards";
}

QList<WireItem*> SchematicLayoutOptimizer::findCrossingWires(QGraphicsScene* scene) {
    QList<QGraphicsItem*> items = scene->items();
    QList<WireItem*> wires;
    QList<WireItem*> crossingWires;

    // Collect all wires
    for (QGraphicsItem* item : items) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            wires.append(wire);
        }
    }

    // Find crossing wires
    for (int i = 0; i < wires.size(); ++i) {
        for (int j = i + 1; j < wires.size(); ++j) {
            if (this->wiresCross(wires[i], wires[j])) {
                if (!crossingWires.contains(wires[i])) {
                    crossingWires.append(wires[i]);
                }
                if (!crossingWires.contains(wires[j])) {
                    crossingWires.append(wires[j]);
                }
            }
        }
    }

    return crossingWires;
}

QList<QPointF> SchematicLayoutOptimizer::findOptimalPath(const QPointF& start, const QPointF& end,
                                                        const QList<WireItem*>& existingWires) {
    // Simple Manhattan distance routing for now
    // In a full implementation, this would use A* pathfinding
    QList<QPointF> path;

    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();

    // Determine if we should go horizontal first or vertical first
    bool horizontalFirst = qAbs(dx) > qAbs(dy);

    if (horizontalFirst) {
        path.append(QPointF(end.x(), start.y())); // Horizontal segment
        path.append(end);                         // Vertical segment
    } else {
        path.append(QPointF(start.x(), end.y())); // Vertical segment
        path.append(end);                         // Horizontal segment
    }

    return path;
}

bool SchematicLayoutOptimizer::validateClearance(const QPointF& point, qreal requiredClearance) {
    // In a full implementation, this would check against all other conductive elements
    // For now, just return true
    Q_UNUSED(point)
    Q_UNUSED(requiredClearance)
    return true;
}

QList<QList<WireItem*>> SchematicLayoutOptimizer::identifyBusGroups(QGraphicsScene* scene) {
    QList<QGraphicsItem*> items = scene->items();
    QList<WireItem*> allWires;

    // Collect all wires
    for (QGraphicsItem* item : items) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            allWires.append(wire);
        }
    }

    QList<QList<WireItem*>> busGroups;

    // Simple bus detection: wires that are parallel and close together
    for (WireItem* wire : allWires) {
        bool addedToGroup = false;

        for (QList<WireItem*>& group : busGroups) {
            if (this->isParallelAndClose(wire, group.first())) {
                group.append(wire);
                addedToGroup = true;
                break;
            }
        }

        if (!addedToGroup) {
            QList<WireItem*> newGroup;
            newGroup.append(wire);
            busGroups.append(newGroup);
        }
    }

    // Filter groups with only 1 wire
    QList<QList<WireItem*>> filteredGroups;
    for (const QList<WireItem*>& group : busGroups) {
        if (group.size() > 1) {
            filteredGroups.append(group);
        }
    }

    return filteredGroups;
}

void SchematicLayoutOptimizer::alignBusWires(QList<WireItem*>& busWires, qreal spacing) {
    if (busWires.size() < 2) return;

    // Sort wires by their position
    std::sort(busWires.begin(), busWires.end(),
              [](WireItem* a, WireItem* b) {
                  return a->pos().y() < b->pos().y();
              });

    // Align wires with proper spacing
    qreal startY = busWires.first()->pos().y();
    for (int i = 0; i < busWires.size(); ++i) {
        qreal targetY = startY + i * spacing;
        busWires[i]->setPos(busWires[i]->pos().x(), targetY);
    }
}

void SchematicLayoutOptimizer::optimizeWireRouting(WireItem* wire, const QList<WireItem*>& allWires) {
    if (!m_enableOrthogonalRouting) return;

    QList<QPointF> points = wire->points();
    if (points.size() < 3) return; // Nothing to optimize

    // Apply orthogonal constraints
    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF prev = points[i-1];
        QPointF curr = points[i];
        QPointF next = points[i+1];

        // Make segments orthogonal
        if (qAbs(curr.x() - prev.x()) < qAbs(curr.y() - prev.y())) {
            // Vertical segment - align X
            points[i].setX(prev.x());
        } else {
            // Horizontal segment - align Y
            points[i].setY(prev.y());
        }
    }

    // Apply mitered corners if enabled
    if (m_enableMiteredCorners) {
        applyMiteredCorners(wire);
    }
}

void SchematicLayoutOptimizer::applyMiteredCorners(WireItem* wire) {
    // Add 45-degree mitered corners at sharp angles
    // This is a simplified implementation
    QList<QPointF> points = wire->points();

    for (int i = 1; i < points.size() - 1; ++i) {
        QPointF prev = points[i-1];
        QPointF curr = points[i];
        QPointF next = points[i+1];

        // Check if this creates a sharp corner
        QPointF v1 = curr - prev;
        QPointF v2 = next - curr;

        // Normalize vectors manually
        qreal len1 = sqrt(v1.x() * v1.x() + v1.y() * v1.y());
        qreal len2 = sqrt(v2.x() * v2.x() + v2.y() * v2.y());
        if (len1 > 0) v1 /= len1;
        if (len2 > 0) v2 /= len2;

        qreal angle = atan2(v1.x() * v2.y() - v1.y() * v2.x(),
                           v1.x() * v2.x() + v1.y() * v2.y());

        if (qAbs(angle) > 0.1) { // Significant angle change
            // Add miter point
            qreal miterLength = 8.0;
            QPointF miterOffset = (v1 + v2) * miterLength;
            QPointF miterPoint = curr + miterOffset;

            wire->addSegment(miterPoint);
            break; // Only add one miter per optimization pass
        }
    }
}

bool SchematicLayoutOptimizer::wiresCross(const WireItem* wire1, const WireItem* wire2) {
    QList<QPointF> points1 = wire1->points();
    QList<QPointF> points2 = wire2->points();

    // Simple line segment intersection check
    for (int i = 0; i < points1.size() - 1; ++i) {
        for (int j = 0; j < points2.size() - 1; ++j) {
            if (this->lineSegmentsIntersect(points1[i], points1[i+1],
                                          points2[j], points2[j+1])) {
                return true;
            }
        }
    }

    return false;
}

QPointF SchematicLayoutOptimizer::findWireIntersection(const WireItem* wire1, const WireItem* wire2) {
    QList<QPointF> points1 = wire1->points();
    QList<QPointF> points2 = wire2->points();

    for (int i = 0; i < points1.size() - 1; ++i) {
        for (int j = 0; j < points2.size() - 1; ++j) {
            QPointF intersection;
            if (this->lineSegmentIntersection(points1[i], points1[i+1],
                                            points2[j], points2[j+1], intersection)) {
                return intersection;
            }
        }
    }

    return QPointF(); // No intersection
}

bool SchematicLayoutOptimizer::lineSegmentsIntersect(const QPointF& p1, const QPointF& p2,
                                                   const QPointF& p3, const QPointF& p4) {
    QPointF intersection;
    return lineSegmentIntersection(p1, p2, p3, p4, intersection);
}

bool SchematicLayoutOptimizer::lineSegmentIntersection(const QPointF& p1, const QPointF& p2,
                                                     const QPointF& p3, const QPointF& p4,
                                                     QPointF& intersection) {
    // Line segment intersection algorithm
    QPointF r = p2 - p1;
    QPointF s = p4 - p3;
    qreal rxs = r.x() * s.y() - r.y() * s.x();

    if (qAbs(rxs) < 1e-6) return false; // Parallel lines

    QPointF qp = p3 - p1;
    qreal t = (qp.x() * s.y() - qp.y() * s.x()) / rxs;
    qreal u = (qp.x() * r.y() - qp.y() * r.x()) / rxs;

    if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
        intersection = p1 + t * r;
        return true;
    }

    return false;
}

bool SchematicLayoutOptimizer::isParallelAndClose(WireItem* wire1, WireItem* wire2) {
    // Simple check: wires with similar orientation and close proximity
    QList<QPointF> points1 = wire1->points();
    QList<QPointF> points2 = wire2->points();

    if (points1.size() < 2 || points2.size() < 2) return false;

    // Check if both wires are mostly horizontal or vertical
    bool wire1Horizontal = qAbs(points1.last().x() - points1.first().x()) >
                          qAbs(points1.last().y() - points1.first().y());
    bool wire2Horizontal = qAbs(points2.last().x() - points2.first().x()) >
                          qAbs(points2.last().y() - points2.first().y());

    if (wire1Horizontal != wire2Horizontal) return false;

    // Check proximity
    qreal minDistance = 50.0; // Maximum distance for bus grouping
    qreal distance = QPointF(wire1->pos() - wire2->pos()).manhattanLength();

    return distance < minDistance;
}
