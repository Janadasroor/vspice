#include "schematic_select_tool.h"
#include "schematic_view.h"
#include "../../core/config_manager.h"
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QPen>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include "flux/core/net_manager.h"
#include "flux/core/theme_manager.h"
#include "schematic_item.h"
#include "wire_item.h"
#include "schematic_wire_utils.h"
#include <limits>

namespace {

constexpr qreal kAttachTolerance = 5.0;
constexpr qreal kSegmentTolerance = 2.0;
constexpr qreal kAttachProbeRadius = 6.0;

SchematicItem* nearestSchematicItem(QGraphicsItem* item) {
    QGraphicsItem* current = item;
    while (current) {
        if (auto* sItem = dynamic_cast<SchematicItem*>(current)) return sItem;
        current = current->parentItem();
    }
    return nullptr;
}

SchematicItem* preferredPickItem(SchematicView* view, const QPointF& scenePos, const QPoint& viewPos) {
    if (!view) return nullptr;

    QGraphicsItem* rawItem = view->itemAt(viewPos);
    SchematicItem* fallback = nearestSchematicItem(rawItem);
    if (fallback && fallback->isSubItem()) {
        if (auto* parent = dynamic_cast<SchematicItem*>(fallback->parentItem())) {
            if (parent->itemType() == SchematicItem::PowerType) {
                fallback = parent;
            }
        }
    }
    if (!view->scene()) return fallback;

    const QList<QGraphicsItem*> hits = view->scene()->items(
        scenePos,
        Qt::IntersectsItemShape,
        Qt::DescendingOrder,
        view->transform());

    // 1) If we're on a component pin, prefer the component (so pin interactions still work).
    for (QGraphicsItem* hit : hits) {
        SchematicItem* sItem = nearestSchematicItem(hit);
        if (!sItem || sItem->itemType() == SchematicItem::WireType) continue;
        const QList<QPointF> pins = sItem->connectionPoints();
        for (const QPointF& pinLocal : pins) {
            const QPointF pinScene = sItem->mapToScene(pinLocal);
            if (QLineF(scenePos, pinScene).length() <= kAttachTolerance) {
                return sItem;
            }
        }
    }

    // 1b) Power symbols should be draggable even when close to wires.
    for (QGraphicsItem* hit : hits) {
        SchematicItem* sItem = nearestSchematicItem(hit);
        if (sItem && sItem->itemType() == SchematicItem::PowerType) {
            return sItem;
        }
    }

    // 1c) Net labels and hierarchical ports should be draggable even when connected to wires.
    for (QGraphicsItem* hit : hits) {
        SchematicItem* sItem = nearestSchematicItem(hit);
        if (sItem && (sItem->itemType() == SchematicItem::NetLabelType ||
                      sItem->itemType() == SchematicItem::HierarchicalPortType)) {
            return sItem;
        }
    }

    // 2) If we're near a wire segment, prefer the wire (helps selection near components).
    WireItem* bestWire = nullptr;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (QGraphicsItem* hit : hits) {
        auto* wire = dynamic_cast<WireItem*>(hit);
        if (!wire) continue;
        const QList<QPointF> pts = wire->points();
        if (pts.size() < 2) continue;
        for (int i = 0; i < pts.size() - 1; ++i) {
            const QPointF a = wire->mapToScene(pts[i]);
            const QPointF b = wire->mapToScene(pts[i + 1]);
            const QPointF vec = b - a;
            const qreal lenSq = vec.x() * vec.x() + vec.y() * vec.y();
            if (lenSq <= 0.0) continue;
            qreal u = ((scenePos.x() - a.x()) * vec.x() + (scenePos.y() - a.y()) * vec.y()) / lenSq;
            u = std::clamp(u, 0.0, 1.0);
            const QPointF proj = a + u * vec;
            const qreal d = QLineF(scenePos, proj).length();
            if (d < bestDist) {
                bestDist = d;
                bestWire = wire;
            }
        }
    }
    if (bestWire && bestDist <= kSegmentTolerance + 1.5) {
        return bestWire;
    }

    SchematicItem* bestSubItem = nullptr;
    for (QGraphicsItem* hit : hits) {
        SchematicItem* sItem = nearestSchematicItem(hit);
        if (!sItem) continue;
        if (sItem->itemType() == SchematicItem::WireType) continue;
        if (!sItem->isSubItem()) return sItem;
        if (!bestSubItem) bestSubItem = sItem;
    }

    return bestSubItem ? bestSubItem : fallback;
}

bool pointLiesOnSegment(const QPointF& p, const QPointF& a, const QPointF& b, qreal tol = kSegmentTolerance) {
    const QPointF vec = b - a;
    const qreal lenSq = vec.x() * vec.x() + vec.y() * vec.y();
    if (lenSq <= 0.0) return QLineF(p, a).length() <= tol;

    qreal u = ((p.x() - a.x()) * vec.x() + (p.y() - a.y()) * vec.y()) / lenSq;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;

    const QPointF proj = a + u * vec;
    return QLineF(p, proj).length() <= tol;
}

bool hasWireAtScenePos(SchematicView* view, const QPointF& scenePos, qreal radius = kAttachProbeRadius) {
    if (!view || !view->scene()) return false;

    const QRectF probe(scenePos - QPointF(radius, radius), QSizeF(2.0 * radius, 2.0 * radius));
    const QList<QGraphicsItem*> nearby = view->scene()->items(
        probe,
        Qt::IntersectsItemShape,
        Qt::DescendingOrder,
        view->transform());

    for (QGraphicsItem* item : nearby) {
        if (dynamic_cast<WireItem*>(item)) return true;
    }
    return false;
}

bool hasExternalConnectionAtPoint(SchematicView* view,
                                  WireItem* ownerWire,
                                  const QPointF& pointScene,
                                  SchematicItem* ignoreItem = nullptr) {
    if (!view || !view->scene() || !ownerWire) return false;

    const QList<QGraphicsItem*> nearby = view->scene()->items(
        QRectF(pointScene - QPointF(kAttachProbeRadius, kAttachProbeRadius),
               QSizeF(2.0 * kAttachProbeRadius, 2.0 * kAttachProbeRadius)),
        Qt::IntersectsItemShape,
        Qt::DescendingOrder,
        view->transform());

    for (QGraphicsItem* item : nearby) {
        if (!item || item == ownerWire || item == ignoreItem) continue;

        if (WireItem* otherWire = dynamic_cast<WireItem*>(item)) {
            const QList<QPointF> pts = otherWire->points();
            if (pts.size() < 2) continue;

            if (QLineF(pointScene, otherWire->mapToScene(pts.first())).length() <= kAttachTolerance ||
                QLineF(pointScene, otherWire->mapToScene(pts.last())).length() <= kAttachTolerance) {
                return true;
            }

            for (int i = 0; i < pts.size() - 1; ++i) {
                const QPointF a = otherWire->mapToScene(pts[i]);
                const QPointF b = otherWire->mapToScene(pts[i + 1]);
                if (pointLiesOnSegment(pointScene, a, b, kSegmentTolerance)) {
                    return true;
                }
            }
            continue;
        }

        SchematicItem* otherItem = dynamic_cast<SchematicItem*>(item);
        if (!otherItem || otherItem == ownerWire || otherItem == ignoreItem) continue;
        const QList<QPointF> pins = otherItem->connectionPoints();
        for (const QPointF& pinLocal : pins) {
            if (QLineF(pointScene, otherItem->mapToScene(pinLocal)).length() <= kAttachTolerance) {
                return true;
            }
        }
    }

    // Fallback: scan all schematic items for pin proximity in case the pin lies
    // outside the item's shape (common for symbol pin dots).
    const QList<QGraphicsItem*> allItems = view->scene()->items();
    for (QGraphicsItem* item : allItems) {
        if (!item || item == ownerWire || item == ignoreItem) continue;

        if (WireItem* otherWire = dynamic_cast<WireItem*>(item)) {
            const QList<QPointF> pts = otherWire->points();
            if (pts.size() < 2) continue;

            if (QLineF(pointScene, otherWire->mapToScene(pts.first())).length() <= kAttachTolerance ||
                QLineF(pointScene, otherWire->mapToScene(pts.last())).length() <= kAttachTolerance) {
                return true;
            }

            for (int i = 0; i < pts.size() - 1; ++i) {
                const QPointF a = otherWire->mapToScene(pts[i]);
                const QPointF b = otherWire->mapToScene(pts[i + 1]);
                if (pointLiesOnSegment(pointScene, a, b, kSegmentTolerance)) {
                    return true;
                }
            }
            continue;
        }

        SchematicItem* otherItem = dynamic_cast<SchematicItem*>(item);
        if (!otherItem || otherItem == ownerWire || otherItem == ignoreItem) continue;
        const QList<QPointF> pins = otherItem->connectionPoints();
        for (const QPointF& pinLocal : pins) {
            if (QLineF(pointScene, otherItem->mapToScene(pinLocal)).length() <= kAttachTolerance) {
                return true;
            }
        }
    }

    return false;
}

bool findExternalAnchorOnWire(SchematicView* view,
                              WireItem* wire,
                              int excludeIndex,
                              SchematicItem* ignoreItem,
                              QPointF& anchorSceneOut) {
    if (!view || !wire) return false;
    const QList<QPointF> pts = wire->points();
    if (pts.size() < 2) return false;

    auto isAnchor = [&](int idx) {
        if (idx < 0 || idx >= pts.size()) return false;
        if (idx == excludeIndex) return false;
        const QPointF scenePos = wire->mapToScene(pts[idx]);
        return hasExternalConnectionAtPoint(view, wire, scenePos, ignoreItem);
    };

    if (excludeIndex == 0) {
        for (int i = pts.size() - 1; i >= 1; --i) {
            if (isAnchor(i)) {
                anchorSceneOut = wire->mapToScene(pts[i]);
                return true;
            }
        }
    } else if (excludeIndex == pts.size() - 1) {
        for (int i = 0; i <= pts.size() - 2; ++i) {
            if (isAnchor(i)) {
                anchorSceneOut = wire->mapToScene(pts[i]);
                return true;
            }
        }
    } else {
        for (int i = 0; i < pts.size(); ++i) {
            if (isAnchor(i)) {
                anchorSceneOut = wire->mapToScene(pts[i]);
                return true;
            }
        }
    }

    return false;
}

bool findExternalAnchorSegmentInfo(SchematicView* view,
                                   WireItem* ownerWire,
                                   const QPointF& anchorScene,
                                   SchematicItem* ignoreItem,
                                   QPointF& segAOut,
                                   QPointF& segBOut,
                                   bool& segHOut,
                                   bool& segVOut) {
    if (!view || !view->scene() || !ownerWire) return false;
    const QList<QGraphicsItem*> allItems = view->scene()->items();
    for (QGraphicsItem* item : allItems) {
        if (!item || item == ownerWire || item == ignoreItem) continue;
        WireItem* otherWire = dynamic_cast<WireItem*>(item);
        if (!otherWire) continue;
        const QList<QPointF> pts = otherWire->points();
        if (pts.size() < 2) continue;
        for (int i = 0; i < pts.size() - 1; ++i) {
            const QPointF a = otherWire->mapToScene(pts[i]);
            const QPointF b = otherWire->mapToScene(pts[i + 1]);
            if (!pointLiesOnSegment(anchorScene, a, b, kSegmentTolerance)) continue;
            if (QLineF(anchorScene, a).length() <= kAttachTolerance ||
                QLineF(anchorScene, b).length() <= kAttachTolerance) {
                continue; // treat endpoint connections as fixed anchors
            }
            const bool h = qAbs(a.y() - b.y()) < 1.0;
            const bool v = qAbs(a.x() - b.x()) < 1.0;
            if (!h && !v) continue;
            segAOut = a;
            segBOut = b;
            segHOut = h;
            segVOut = v;
            return true;
        }
    }
    return false;
}

bool isOrthogonalWire(const QList<QPointF>& pts) {
    if (pts.size() < 2) return true;
    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF& a = pts[i];
        const QPointF& b = pts[i + 1];
        const bool h = qAbs(a.y() - b.y()) < 1.0;
        const bool v = qAbs(a.x() - b.x()) < 1.0;
        if (!h && !v) return false;
    }
    return true;
}

bool isObstacleItem(const SchematicItem* item) {
    if (!item) return false;
    switch (item->itemType()) {
    case SchematicItem::WireType:
    case SchematicItem::LabelType:
    case SchematicItem::NetLabelType:
    case SchematicItem::JunctionType:
    case SchematicItem::NoConnectType:
    case SchematicItem::BusType:
    case SchematicItem::SheetType:
    case SchematicItem::HierarchicalPortType:
    case SchematicItem::SpiceDirectiveType:
        return false;
    default:
        return true;
    }
}

bool segmentIntersectsRect(const QPointF& a, const QPointF& b, const QRectF& rect) {
    if (rect.contains(a) || rect.contains(b)) return true;
    QLineF seg(a, b);
    QPointF inter;
    const QPointF tl = rect.topLeft();
    const QPointF tr = rect.topRight();
    const QPointF br = rect.bottomRight();
    const QPointF bl = rect.bottomLeft();
    if (seg.intersects(QLineF(tl, tr), &inter) == QLineF::BoundedIntersection) return true;
    if (seg.intersects(QLineF(tr, br), &inter) == QLineF::BoundedIntersection) return true;
    if (seg.intersects(QLineF(br, bl), &inter) == QLineF::BoundedIntersection) return true;
    if (seg.intersects(QLineF(bl, tl), &inter) == QLineF::BoundedIntersection) return true;
    return false;
}

bool segmentCollidesWithObstacles(const QPointF& a,
                                  const QPointF& b,
                                  SchematicView* view,
                                  const QSet<SchematicItem*>& ignore) {
    if (!view || !view->scene()) return false;
    const QList<QGraphicsItem*> candidates = view->scene()->items();
    for (QGraphicsItem* item : candidates) {
        auto* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem || ignore.contains(sItem)) continue;
        if (!isObstacleItem(sItem)) continue;
        QRectF rect = sItem->sceneBoundingRect().adjusted(-2, -2, 2, 2);
        if (segmentIntersectsRect(a, b, rect)) return true;
    }
    return false;
}

QList<QPointF> nudgeLWireAwayFromObstacles(WireItem* wire,
                                           const QList<QPointF>& currPts,
                                           SchematicView* view,
                                           const QSet<SchematicItem*>& ignore) {
    if (!wire || currPts.size() != 3 || !view) return currPts;
    QPointF startScene = wire->mapToScene(currPts[0]);
    QPointF elbowScene = wire->mapToScene(currPts[1]);
    QPointF endScene = wire->mapToScene(currPts[2]);

    if (qAbs(startScene.x() - endScene.x()) < 1.0 || qAbs(startScene.y() - endScene.y()) < 1.0) {
        return currPts; // straight wire
    }

    const bool collides =
        segmentCollidesWithObstacles(startScene, elbowScene, view, ignore) ||
        segmentCollidesWithObstacles(elbowScene, endScene, view, ignore);

    if (!collides) return currPts;

    const QPointF altA = QPointF(startScene.x(), endScene.y());
    const QPointF altB = QPointF(endScene.x(), startScene.y());
    QPointF altElbow = altA;
    if (QLineF(altB, elbowScene).length() > QLineF(altA, elbowScene).length()) {
        altElbow = altB;
    }
    if (QLineF(altElbow, elbowScene).length() < 0.5) {
        altElbow = (QLineF(altA, elbowScene).length() < 0.5) ? altB : altA;
    }
    altElbow = view->snapToGrid(altElbow);

    const bool altCollides =
        segmentCollidesWithObstacles(startScene, altElbow, view, ignore) ||
        segmentCollidesWithObstacles(altElbow, endScene, view, ignore);

    if (!altCollides) {
        QList<QPointF> updated = currPts;
        updated[1] = wire->mapFromScene(altElbow);
        return updated;
    }

    // If both L paths collide, try small orthogonal nudges off the current elbow.
    const qreal grid = view->gridSize();
    const QVector<QPointF> offsets = {
        QPointF(0.0, -grid),
        QPointF(0.0, grid),
        QPointF(-grid, 0.0),
        QPointF(grid, 0.0),
        QPointF(0.0, -2.0 * grid),
        QPointF(0.0, 2.0 * grid),
        QPointF(-2.0 * grid, 0.0),
        QPointF(2.0 * grid, 0.0)
    };

    QPointF bestElbow = elbowScene;
    bool found = false;
    qreal bestDistance = std::numeric_limits<qreal>::max();
    for (const QPointF& off : offsets) {
        const QPointF candidate = view->snapToGrid(elbowScene + off);
        const bool candidateCollides =
            segmentCollidesWithObstacles(startScene, candidate, view, ignore) ||
            segmentCollidesWithObstacles(candidate, endScene, view, ignore);
        if (!candidateCollides) {
            const qreal dist = QLineF(candidate, elbowScene).length();
            if (!found || dist < bestDistance) {
                bestDistance = dist;
                bestElbow = candidate;
                found = true;
            }
        }
    }

    if (!found) return currPts;

    QList<QPointF> updated = currPts;
    updated[1] = wire->mapFromScene(bestElbow);
    return updated;
}

bool isMostlyOrthogonalWire(const QList<QPointF>& pts, qreal tol = 1.5) {
    if (pts.size() < 2) return true;
    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF& a = pts[i];
        const QPointF& b = pts[i + 1];
        const qreal dx = qAbs(a.x() - b.x());
        const qreal dy = qAbs(a.y() - b.y());
        if (dx < tol || dy < tol) continue;
        return false;
    }
    return true;
}

QList<QPointF> rebuildSimpleOrthogonal(const QList<QPointF>& currentPts,
                                       const QList<QPointF>& originalPts,
                                       SchematicView* view) {
    if (currentPts.size() < 2) return currentPts;
    if (originalPts.size() < 2) return currentPts;

    const QPointF start = currentPts.first();
    const QPointF end = currentPts.last();

    auto segmentOrientation = [](const QPointF& a, const QPointF& b) {
        const qreal dx = qAbs(a.x() - b.x());
        const qreal dy = qAbs(a.y() - b.y());
        if (dy < 1.0) return 1; // Horizontal
        if (dx < 1.0) return 2; // Vertical
        return 0; // Unknown
    };

    const int startOri = segmentOrientation(originalPts[0], originalPts[1]);
    const int endOri = segmentOrientation(originalPts[originalPts.size() - 1],
                                          originalPts[originalPts.size() - 2]);

    const qreal startDelta = QLineF(start, originalPts[0]).length();
    const qreal endDelta = QLineF(end, originalPts[originalPts.size() - 1]).length();
    const bool movedStart = startDelta >= endDelta;

    // Decide orientation based on original endpoint orientation and which end moved.
    bool hFirst = true;
    bool hFirstSet = false;

    if (startOri != 0 && endOri != 0 && startOri != endOri) {
        // Use start orientation to satisfy both ends.
        hFirst = (startOri == 1);
        hFirstSet = true;
    }

    if (!hFirstSet) {
        if (movedStart && startOri != 0) {
            hFirst = (startOri == 1);
            hFirstSet = true;
        } else if (!movedStart && endOri != 0) {
            // End orientation is horizontal when hFirst is false.
            hFirst = (endOri == 2);
            hFirstSet = true;
        }
    }

    if (!hFirstSet) {
        // Fallback to original first-segment orientation.
        const QPointF o0 = originalPts[0];
        const QPointF o1 = originalPts[1];
        const qreal odx = qAbs(o0.x() - o1.x());
        const qreal ody = qAbs(o0.y() - o1.y());
        hFirst = !(ody > odx);
    }

    // Straight line if aligned.
    if (qAbs(start.x() - end.x()) < 1.0 || qAbs(start.y() - end.y()) < 1.0) {
        return {start, end};
    }

    QPointF corner = hFirst ? QPointF(end.x(), start.y()) : QPointF(start.x(), end.y());
    if (view) {
        QPointF snapped = view->snapToGrid(corner);
        corner = hFirst ? QPointF(snapped.x(), start.y()) : QPointF(start.x(), snapped.y());
    }
    return {start, corner, end};
}

void forceOrthogonal(QList<QPointF>& pts, qreal tol = 1.5) {
    if (pts.size() < 2) return;
    for (int i = 0; i < pts.size() - 1; ++i) {
        QPointF& a = pts[i];
        QPointF& b = pts[i + 1];
        const qreal dx = qAbs(a.x() - b.x());
        const qreal dy = qAbs(a.y() - b.y());
        if (dx < tol) {
            b.setX(a.x());
        } else if (dy < tol) {
            b.setY(a.y());
        } else {
            if (dx > dy) b.setY(a.y());
            else b.setX(a.x());
        }
    }
}

void orthogonalizeFromOriginal(QList<QPointF>& pts, const QList<QPointF>& original) {
    const int n = std::min(pts.size(), original.size());
    if (n < 2) return;
    for (int i = 0; i < n - 1; ++i) {
        Q_ASSERT(i >= 0 && i < pts.size());
        Q_ASSERT(i + 1 < pts.size());
        const QPointF& oa = original[i];
        const QPointF& ob = original[i + 1];
        const bool h = qAbs(oa.y() - ob.y()) < 1.0;
        const bool v = qAbs(oa.x() - ob.x()) < 1.0;
        if (h) {
            pts[i + 1].setY(pts[i].y());
        } else if (v) {
            pts[i + 1].setX(pts[i].x());
        }
    }
}

QList<QPointF> moveAttachedWireEndpoint(
    WireItem* wire,
    bool isStartPoint,
    const QPointF& pinScenePos,
    bool isHorizontal,
    bool isVertical,
    bool keepNeighborFixed,
    const QPointF& fixedNeighborScenePos
) {
    if (!wire) return {};

    QList<QPointF> pts = wire->points();
    const QList<QPointF> orig = pts;
    if (pts.size() < 2) return pts;

    const int idx = isStartPoint ? 0 : static_cast<int>(pts.size()) - 1;
    const int nIdx = isStartPoint ? 1 : static_cast<int>(pts.size()) - 2;
    if (nIdx < 0 || nIdx >= pts.size()) return pts;

    // Move endpoint to the pin.
    pts[idx] = wire->mapFromScene(pinScenePos);
    const QPointF p = pts[idx];

    // If the immediate neighbor is a junction to external geometry, keep it fixed and
    // maintain orthogonality by adding/updating a single elbow near the moved endpoint.
    if (keepNeighborFixed) {
        int fixedIdx = -1;
        qreal bestDist = std::numeric_limits<qreal>::max();
        const QPointF fixedHintLocal = wire->mapFromScene(fixedNeighborScenePos);

        const int begin = isStartPoint ? 1 : 0;
        const int end = isStartPoint ? static_cast<int>(pts.size()) : static_cast<int>(pts.size()) - 1;
        for (int i = begin; i < end; ++i) {
            const qreal d = QLineF(pts[i], fixedHintLocal).length();
            if (d < bestDist) {
                bestDist = d;
                fixedIdx = i;
            }
        }

        if (fixedIdx == -1) {
            fixedIdx = nIdx;
        }

        // Always honor the external anchor position, even if the current points drifted.
        const QPointF fixedPoint = fixedHintLocal;
        QPointF elbow = fixedPoint;
        if (isHorizontal) {
            elbow.setY(p.y());
        } else if (isVertical) {
            elbow.setX(p.x());
        } else {
            if (qAbs(p.x() - fixedPoint.x()) > qAbs(p.y() - fixedPoint.y())) elbow.setY(p.y());
            else elbow.setX(p.x());
        }

        QList<QPointF> rebuilt;
        auto appendUnique = [&rebuilt](const QPointF& point) {
            if (rebuilt.isEmpty() || QLineF(rebuilt.last(), point).length() > 0.001) {
                rebuilt.append(point);
            }
        };

        if (isStartPoint) {
            appendUnique(p);
            appendUnique(elbow);
            appendUnique(fixedPoint);
            for (int i = fixedIdx + 1; i < pts.size(); ++i) appendUnique(pts[i]);
        } else {
            for (int i = 0; i < fixedIdx; ++i) appendUnique(pts[i]);
            appendUnique(fixedPoint);
            appendUnique(elbow);
            appendUnique(p);
        }

        if (rebuilt.size() >= 2) return rebuilt;
        return pts;
    }

    // Otherwise preserve orthogonality at the endpoint.
    QPointF n = pts[nIdx];
    if (isHorizontal) {
        // Move the whole horizontal run that starts at the neighbor.
        const qreal targetY = p.y();
        int dir = isStartPoint ? 1 : -1;
        int i = nIdx;
        while (i >= 0 && i < pts.size()) {
            pts[i].setY(targetY);
            const int next = i + dir;
            if (next < 0 || next >= pts.size()) break;
            if (qAbs(orig[i].y() - orig[next].y()) >= 1.0) break;
            i = next;
        }
    } else if (isVertical) {
        const qreal targetX = p.x();
        int dir = isStartPoint ? 1 : -1;
        int i = nIdx;
        while (i >= 0 && i < pts.size()) {
            pts[i].setX(targetX);
            const int next = i + dir;
            if (next < 0 || next >= pts.size()) break;
            if (qAbs(orig[i].x() - orig[next].x()) >= 1.0) break;
            i = next;
        }
    } else {
        if (qAbs(p.x() - n.x()) > qAbs(p.y() - n.y())) pts[nIdx].setY(p.y());
        else pts[nIdx].setX(p.x());
    }

    return pts;
}

QList<QPointF> moveAttachedWireEndpointPreserveTopology(
    WireItem* wire,
    bool isStartPoint,
    const QPointF& pinScenePos,
    bool isHorizontal,
    bool isVertical
) {
    if (!wire) return {};

    QList<QPointF> pts = wire->points();
    const QList<QPointF> orig = pts;
    if (pts.size() < 2) return pts;

    const int idx = isStartPoint ? 0 : static_cast<int>(pts.size()) - 1;
    const int nIdx = isStartPoint ? 1 : static_cast<int>(pts.size()) - 2;
    if (nIdx < 0 || nIdx >= pts.size()) return pts;

    pts[idx] = wire->mapFromScene(pinScenePos);
    const QPointF p = pts[idx];

    if (isHorizontal) {
        const qreal targetY = p.y();
        int dir = isStartPoint ? 1 : -1;
        int i = nIdx;
        while (i >= 0 && i < pts.size()) {
            pts[i].setY(targetY);
            const int next = i + dir;
            if (next < 0 || next >= pts.size()) break;
            if (qAbs(orig[i].y() - orig[next].y()) >= 1.0) break;
            i = next;
        }
    } else if (isVertical) {
        const qreal targetX = p.x();
        int dir = isStartPoint ? 1 : -1;
        int i = nIdx;
        while (i >= 0 && i < pts.size()) {
            pts[i].setX(targetX);
            const int next = i + dir;
            if (next < 0 || next >= pts.size()) break;
            if (qAbs(orig[i].x() - orig[next].x()) >= 1.0) break;
            i = next;
        }
    } else {
        const QPointF n = pts[nIdx];
        if (qAbs(p.x() - n.x()) > qAbs(p.y() - n.y())) pts[nIdx].setY(p.y());
        else pts[nIdx].setX(p.x());
    }

    return pts;
}

QList<WireItem*> collectConnectedWiresAtPins(
    SchematicView* view,
    SchematicItem* component
) {
    QList<WireItem*> connected;
    if (!view || !view->scene() || !component || component->itemType() == SchematicItem::WireType) {
        return connected;
    }

    const QList<QPointF> pins = component->connectionPoints();
    for (const QPointF& pinLocal : pins) {
        const QPointF pinScene = component->mapToScene(pinLocal);
        const QRectF probe(
            pinScene - QPointF(kAttachProbeRadius, kAttachProbeRadius),
            QSizeF(2.0 * kAttachProbeRadius, 2.0 * kAttachProbeRadius));

        const QList<QGraphicsItem*> nearby = view->scene()->items(
            probe,
            Qt::IntersectsItemShape,
            Qt::DescendingOrder,
            view->transform());

        for (QGraphicsItem* candidate : nearby) {
            WireItem* wire = dynamic_cast<WireItem*>(candidate);
            if (!wire) continue;

            const QList<QPointF> pts = wire->points();
            if (pts.size() < 2) continue;

            const QPointF startScene = wire->mapToScene(pts.first());
            const QPointF endScene = wire->mapToScene(pts.last());
            bool matches = QLineF(pinScene, startScene).length() <= kAttachTolerance ||
                           QLineF(pinScene, endScene).length() <= kAttachTolerance;

            if (!matches) {
                for (int i = 0; i < pts.size() - 1; ++i) {
                    const QPointF a = wire->mapToScene(pts[i]);
                    const QPointF b = wire->mapToScene(pts[i + 1]);
                    if (pointLiesOnSegment(pinScene, a, b, kSegmentTolerance)) {
                        matches = true;
                        break;
                    }
                }
            }

            if (matches && !connected.contains(wire)) connected.append(wire);
        }
    }

    return connected;
}

} // namespace

bool SchematicSelectTool::captureEndpointAnchor(SchematicView* view,
                                                WireItem* ownerWire,
                                                const QPointF& endpointScene,
                                                WireEndpointAnchor& anchor,
                                                bool isStart) {
    if (!view || !view->scene() || !ownerWire) return false;

    SchematicItem* bestItem = nullptr;
    int bestPin = -1;
    QPointF bestPinScene;
    qreal bestDist = kAttachTolerance;

    const QList<QGraphicsItem*> allItems = view->scene()->items();
    for (QGraphicsItem* item : allItems) {
        if (!item || item == ownerWire) continue;

        SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem || sItem->itemType() == SchematicItem::WireType) continue;

        const QList<QPointF> pins = sItem->connectionPoints();
        for (int i = 0; i < pins.size(); ++i) {
            const QPointF pinScene = sItem->mapToScene(pins[i]);
            const qreal dist = QLineF(pinScene, endpointScene).length();
            if (dist <= bestDist) {
                bestDist = dist;
                bestItem = sItem;
                bestPin = i;
                bestPinScene = pinScene;
            }
        }
    }

    if (!bestItem) return false;

    if (isStart) {
        anchor.startAnchored = true;
        anchor.startItem = bestItem;
        anchor.startPinIndex = bestPin;
        anchor.startScene = bestPinScene;
    } else {
        anchor.endAnchored = true;
        anchor.endItem = bestItem;
        anchor.endPinIndex = bestPin;
        anchor.endScene = bestPinScene;
    }
    return true;
}

QPointF SchematicSelectTool::resolveAnchorScene(const WireEndpointAnchor& anchor,
                                                bool isStart) const {
    if (isStart) {
        if (anchor.startItem && anchor.startPinIndex >= 0) {
            const QList<QPointF> pins = anchor.startItem->connectionPoints();
            if (anchor.startPinIndex < pins.size()) {
                return anchor.startItem->mapToScene(pins[anchor.startPinIndex]);
            }
        }
        return anchor.startScene;
    }
    if (anchor.endItem && anchor.endPinIndex >= 0) {
        const QList<QPointF> pins = anchor.endItem->connectionPoints();
        if (anchor.endPinIndex < pins.size()) {
            return anchor.endItem->mapToScene(pins[anchor.endPinIndex]);
        }
    }
    return anchor.endScene;
}

void SchematicSelectTool::applyEndpointAnchorsToWire(WireItem* wire,
                                                     const WireEndpointAnchor& anchor) {
    if (!wire) return;
    QList<QPointF> pts = wire->points();
    if (pts.size() < 2) return;

    if (anchor.startAnchored) {
        const QPointF targetScene = resolveAnchorScene(anchor, true);
        const QPointF newLocal = wire->mapFromScene(targetScene);
        pts = SchematicWireUtils::maintainOrthogonality(pts, 0, newLocal);
    }
    if (anchor.endAnchored) {
        const QPointF targetScene = resolveAnchorScene(anchor, false);
        const QPointF newLocal = wire->mapFromScene(targetScene);
        pts = SchematicWireUtils::maintainOrthogonality(pts, pts.size() - 1, newLocal);
    }

    wire->setPoints(pts);
}

void SchematicSelectTool::adjustEndpointOrientationForDrag(const AttachedWire& aw,
                                                           const QPointF& currentPinScene,
                                                           bool keepNeighborFixed,
                                                           bool& isHorizontal,
                                                           bool& isVertical) const {
    if (!keepNeighborFixed) return;
    if (!m_initialWireAnchors.contains(aw.wire)) return;

    const WireEndpointAnchor& anchor = m_initialWireAnchors[aw.wire];
    QPointF oldPinScene;
    bool hasOld = false;
    if (aw.isStartPoint && anchor.startAnchored) {
        oldPinScene = anchor.startScene;
        hasOld = true;
    } else if (!aw.isStartPoint && anchor.endAnchored) {
        oldPinScene = anchor.endScene;
        hasOld = true;
    }
    if (!hasOld) return;

    const qreal dx = currentPinScene.x() - oldPinScene.x();
    const qreal dy = currentPinScene.y() - oldPinScene.y();
    const qreal absDx = qAbs(dx);
    const qreal absDy = qAbs(dy);
    const qreal eps = 0.5;

    if (absDx > absDy + eps) {
        // Mostly horizontal drag -> prefer horizontal-first elbow (match new X at anchor Y)
        isVertical = true;
        isHorizontal = false;
    } else if (absDy > absDx + eps) {
        // Mostly vertical drag -> prefer vertical-first elbow (match new Y at anchor X)
        isHorizontal = true;
        isVertical = false;
    }
}

QPointF SchematicSelectTool::adjustFixedNeighborSceneForSliding(const AttachedWire& aw,
                                                                const QPointF& currentPinScene,
                                                                const QPointF& fixedNeighborScene) const {
    if (!aw.neighborAnchorIsSegment || !aw.wire) return fixedNeighborScene;
    const QList<QPointF> pts = aw.wire->points();
    if (pts.size() != 2) return fixedNeighborScene;

    QPointF adjusted = fixedNeighborScene;
    if (aw.isVertical && aw.neighborAnchorSegmentHorizontal) {
        const qreal minX = qMin(aw.neighborAnchorSegmentA.x(), aw.neighborAnchorSegmentB.x());
        const qreal maxX = qMax(aw.neighborAnchorSegmentA.x(), aw.neighborAnchorSegmentB.x());
        const qreal targetX = qBound(minX, currentPinScene.x(), maxX);
        adjusted.setX(targetX);
        adjusted.setY(aw.neighborAnchorSegmentA.y());
    } else if (aw.isHorizontal && aw.neighborAnchorSegmentVertical) {
        const qreal minY = qMin(aw.neighborAnchorSegmentA.y(), aw.neighborAnchorSegmentB.y());
        const qreal maxY = qMax(aw.neighborAnchorSegmentA.y(), aw.neighborAnchorSegmentB.y());
        const qreal targetY = qBound(minY, currentPinScene.y(), maxY);
        adjusted.setY(targetY);
        adjusted.setX(aw.neighborAnchorSegmentA.x());
    }
    return adjusted;
}

SchematicSelectTool::SchematicSelectTool(QObject* parent)
    : SchematicTool("Select", parent) {
}

void SchematicSelectTool::deactivate() {
    if (m_rubberBandItem) {
        if (m_rubberBandItem->scene() && view() && view()->scene() == m_rubberBandItem->scene()) {
            m_rubberBandItem->scene()->removeItem(m_rubberBandItem);
        }
        delete m_rubberBandItem;
    }
    m_rubberBandItem = nullptr;
    m_rubberBandActive = false;
    clearSegmentHoverCue();
    clearPinHoverCue();
    m_segmentDragActive = false;
    m_segmentWire = nullptr;
    m_segmentIndex = -1;
    m_segmentIsHorizontal = false;
    m_segmentIsVertical = false;
    m_vertexDragActive = false;
    m_vertexWire = nullptr;
    m_vertexIndex = -1;
    m_rigidGroupMove = false;
    m_dragSelection.clear();
    if (m_dragViewOptimized && view()) {
        view()->setViewportUpdateMode(m_prevViewportUpdateMode);
        view()->setRenderHint(QPainter::Antialiasing, m_prevAntialiasing);
        view()->setRenderHint(QPainter::TextAntialiasing, m_prevTextAntialiasing);
        m_dragViewOptimized = false;
    }
    m_hasLastAppliedMove = false;
    m_tJunctions.clear();
    SchematicTool::deactivate();
}

QPointF SchematicSelectTool::snapPoint(QPointF scenePos) {
    if (m_isDragging || m_rubberBandActive) {
        return scenePos; // Don't snap crosshair during dragging or selection
    }
    return view() ? view()->snapToGrid(scenePos) : scenePos;
}

#include "schematic_commands.h"
#include "../analysis/schematic_connectivity.h"
#include <QUndoStack>

// ... headers ...

void SchematicSelectTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;
    
    QPointF scenePos = view()->mapToScene(event->pos());
    SchematicItem* nearestItem = preferredPickItem(view(), scenePos, event->pos());
    QGraphicsItem* item = nearestItem ? static_cast<QGraphicsItem*>(nearestItem)
                                      : view()->itemAt(event->pos());
    
    // Check if we clicked on the background or a non-schematic item (like the page frame)
    bool isBackground = (nearestItem == nullptr);

    
    // Normal selection logic...
    if (event->button() == Qt::LeftButton) {
        m_tJunctions.clear();
        if (isBackground) {
            // Empty space: Start rubber band selection
            m_rubberBandActive = true;
            m_rubberBandOrigin = scenePos;
            
            if (m_rubberBandItem) {
                view()->scene()->removeItem(m_rubberBandItem);
                delete m_rubberBandItem;
            }
            
            m_rubberBandItem = new QGraphicsRectItem();
            QColor selColor = Qt::blue;
            if (ThemeManager::theme()) selColor = ThemeManager::theme()->selectionBox();
            
            QColor fillColor = selColor;
            fillColor.setAlpha(40);
            
            m_rubberBandItem->setPen(QPen(selColor, 1, Qt::DashLine));
            m_rubberBandItem->setBrush(QBrush(fillColor));
            m_rubberBandItem->setZValue(1000); // Always on top
            view()->scene()->addItem(m_rubberBandItem);
            m_rubberBandItem->setRect(QRectF(m_rubberBandOrigin, QSize(0, 0)));
            
            // Clear selection unless Ctrl is held (additive selection)
            if (!(event->modifiers() & Qt::ControlModifier)) {
                view()->scene()->clearSelection();
            }
            event->accept();
            return;
        } else {
            // Segment drag mode: if a selected wire segment is clicked, drag only that segment.
            WireItem* clickedWire = dynamic_cast<WireItem*>(item);
            if (clickedWire && clickedWire->isSelected() &&
                !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {

                // Capture initial positions for group move consistency
                QList<QGraphicsItem*> selectedItems = view()->scene()->selectedItems();
                for (QGraphicsItem* selItem : selectedItems) {
                    SchematicItem* sItem = dynamic_cast<SchematicItem*>(selItem);
                    if (sItem) {
                        m_initialPositions[sItem] = sItem->pos();
                        if (WireItem* w = dynamic_cast<WireItem*>(sItem)) {
                            m_initialWirePoints[w] = w->points();
                        }
                    }
                }

                const int bestVertex = findWireVertexAt(clickedWire, scenePos);
                if (bestVertex != -1) {
                    m_vertexDragActive = true;
                    m_vertexWire = clickedWire;
                    m_vertexIndex = bestVertex;
                    m_vertexDragStartScenePos = scenePos;
                    m_isDragging = true;
                    m_lastMousePos = event->pos();
                    m_lastMousePosOrigin = event->pos();
                    m_initialMouseScenePos = scenePos;
                    m_attachedWires.clear();
                    clearSegmentHoverCue();
                    event->accept();
                    return;
                }

                const int bestSeg = findWireSegmentAt(clickedWire, scenePos);
                const QList<QPointF> pts = clickedWire->points();
                if (bestSeg >= 0 && bestSeg < pts.size() - 1) {
                    const QPointF a = pts[bestSeg];
                    const QPointF b = pts[bestSeg + 1];
                    m_segmentDragActive = true;
                    m_segmentWire = clickedWire;
                    m_segmentIndex = bestSeg;
                    m_segmentIsHorizontal = qAbs(a.y() - b.y()) < 1.0;
                    m_segmentIsVertical = qAbs(a.x() - b.x()) < 1.0;
                    m_segmentDragStartScenePos = scenePos;
                    m_isDragging = true;
                    m_lastMousePos = event->pos();
                    m_lastMousePosOrigin = event->pos();
                    m_initialMouseScenePos = scenePos;
                    clearSegmentHoverCue();
                    
                    // Let it fall through to populate m_attachedWires if needed, 
                    // or return if we only want segment drag. 
                    // Actually, we want to populate m_attachedWires for rubber-banding effect.
                }
            }

            // Smart context detection: Check if clicking on a connection point
            SchematicItem* sItem = nearestItem;
            
            if (sItem && sItem->itemType() != SchematicItem::WireType) {
                // Check if click is near a connection point
                QList<QPointF> connectionPoints = sItem->connectionPoints();
                QPointF clickPosLocal = sItem->mapFromScene(scenePos);
                
                bool clickedOnPin = false;
                QPointF nearestPin;
                qreal minDistance = 5.0; // Reduced tolerance to 5 pixels for better dragging
                
                for (const QPointF& pin : connectionPoints) {
                    qreal distance = QLineF(clickPosLocal, pin).length();
                    if (distance < minDistance) {
                        clickedOnPin = true;
                        nearestPin = sItem->mapToScene(pin);
                        minDistance = distance;
                    }
                }
                
                if (clickedOnPin && hasWireAtScenePos(view(), nearestPin)) {
                    clickedOnPin = false;
                }

                if (clickedOnPin) {
                    // Auto-switch to Wire tool - use a deferred approach to avoid crashes
                    event->accept();
                    
                    // Switch tool immediately
                    view()->setCurrentTool("Wire");
                    
                    // Use QTimer to simulate the click after tool is fully switched
                    QTimer::singleShot(0, [this, nearestPin]() {
                        if (!view() || !view()->currentTool()) return;
                        
                        // Create synthetic click event at the pin location
                        QPoint viewPos = view()->mapFromScene(nearestPin);
                        QMouseEvent syntheticEvent(
                            QEvent::MouseButtonPress,
                            viewPos,
                            view()->mapToGlobal(viewPos),
                            Qt::LeftButton,
                            Qt::LeftButton,
                            Qt::NoModifier
                        );
                        view()->currentTool()->mousePressEvent(&syntheticEvent);
                    });
                    
                    return;
                }
            }
            
            // Not a pin click - proceed with selection/dragging
            bool isCtrlHeld = event->modifiers() & Qt::ControlModifier;
            bool isShiftHeld = event->modifiers() & Qt::ShiftModifier;
            
            if (isShiftHeld) {
                // Shift: Toggle selection of clicked item
                item->setSelected(!item->isSelected());
            } else if (isCtrlHeld) {
                // Ctrl: Toggle selection (additive/subtractive selection)
                item->setSelected(!item->isSelected());
            } else {
                // Normal click: Select only this item (unless already selected for multi-drag)
                if (!item->isSelected()) {
                    view()->scene()->clearSelection();
                    item->setSelected(true);
                }
                // If item was already selected, keep all selections for group drag
            }
            
            m_isDragging = true;
            m_lastMousePos = event->pos();
            m_lastMousePosOrigin = event->pos();
            m_initialMouseScenePos = scenePos;
            m_axisLockActive = false;
            m_axisLockHorizontal = true;
            m_attachedWires.clear();
            m_initialPositions.clear();
            m_initialWirePoints.clear();
            m_initialWireAnchors.clear();
            m_topologyLockedWires.clear();

            // Capture initial positions of ALL selected items
            QList<QGraphicsItem*> selectedItems = view()->scene()->selectedItems();
            bool selectedHasWire = false;
            for (QGraphicsItem* selItem : selectedItems) {
                SchematicItem* sItem = dynamic_cast<SchematicItem*>(selItem);
                if (sItem) {
                    m_initialPositions[sItem] = sItem->pos();
                    if (sItem->itemType() == SchematicItem::WireType) {
                        selectedHasWire = true;
                        WireItem* wire = static_cast<WireItem*>(sItem);
                        m_initialWirePoints[wire] = wire->points();
                    }
                }
            }
            m_rigidGroupMove = (selectedItems.size() > 1 && selectedHasWire);
            m_dragSelection.clear();
            m_dragSelection.reserve(m_initialPositions.size());
            for (auto it = m_initialPositions.constBegin(); it != m_initialPositions.constEnd(); ++it) {
                if (it.key()) m_dragSelection.append(it.key());
            }
            m_hasLastAppliedMove = false;
            if (!m_dragViewOptimized) {
                m_prevViewportUpdateMode = view()->viewportUpdateMode();
                m_prevAntialiasing = view()->renderHints().testFlag(QPainter::Antialiasing);
                m_prevTextAntialiasing = view()->renderHints().testFlag(QPainter::TextAntialiasing);
                view()->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
                view()->setRenderHint(QPainter::Antialiasing, false);
                view()->setRenderHint(QPainter::TextAntialiasing, false);
                m_dragViewOptimized = true;
            }

            auto rememberWire = [this](WireItem* wire) {
                if (!wire) return;
                if (!m_initialWirePoints.contains(wire)) {
                    m_initialWirePoints[wire] = wire->points();
                    if (wire->points().size() > 3) {
                        m_topologyLockedWires.insert(wire);
                    }
                }
            };

            // Find connected items for "Rubber banding"
            if (!m_rigidGroupMove) for (QGraphicsItem* selItem : selectedItems) {
                SchematicItem* sItem = dynamic_cast<SchematicItem*>(selItem);
                if (!sItem) continue;

                QList<QPointF> pinLocalPoints = sItem->connectionPoints();
                for (int i = 0; i < pinLocalPoints.size(); ++i) {
                    QPointF pinScene = sItem->mapToScene(pinLocalPoints[i]);
                    
                    // Search for things that might be attached to this pin
                    QList<QGraphicsItem*> crossing = view()->scene()->items(
                        QRectF(
                            pinScene - QPointF(kAttachProbeRadius, kAttachProbeRadius),
                            QSizeF(2.0 * kAttachProbeRadius, 2.0 * kAttachProbeRadius)));
                    for (QGraphicsItem* other : crossing) {
                        if (other == sItem || other->isSelected()) continue;
                        
                        SchematicItem* otherSItem = dynamic_cast<SchematicItem*>(other);
                        if (!otherSItem) continue;

                        // Case 1: Selected Item is Component, 'other' is unselected Wire
                        if (sItem->itemType() != SchematicItem::WireType && otherSItem->itemType() == SchematicItem::WireType) {
                            WireItem* wire = static_cast<WireItem*>(otherSItem);
                            QList<QPointF> wPts = wire->points();
                            int idx = -1;
                            if (QLineF(wire->mapToScene(wPts.first()), pinScene).length() <= kAttachTolerance) {
                                idx = 0;
                            } else if (QLineF(wire->mapToScene(wPts.last()), pinScene).length() <= kAttachTolerance) {
                                idx = static_cast<int>(wPts.size()) - 1;
                            }

                            if (idx != -1) {
                                int nextIdx = (idx == 0) ? 1 : idx - 1;
                                bool isH = false, isV = false;
                                QPointF neighborScene;
                                QPointF externalAnchorScene;
                                bool neighborAnchored = false;
                                bool anchorIsSegment = false;
                                bool segH = false;
                                bool segV = false;
                                QPointF segA;
                                QPointF segB;
                                const bool lockElbow = false;
                                if (wPts.size() > 1) {
                                    isH = qAbs(wPts[idx].y() - wPts[nextIdx].y()) < 1.0;
                                    isV = qAbs(wPts[idx].x() - wPts[nextIdx].x()) < 1.0;
                                    neighborScene = wire->mapToScene(wPts[nextIdx]);
                                    QPointF anchorScene;
                                    neighborAnchored = findExternalAnchorOnWire(view(), wire, idx, sItem, anchorScene);
                                    if (neighborAnchored) {
                                        externalAnchorScene = anchorScene;
                                        anchorIsSegment = findExternalAnchorSegmentInfo(view(), wire, anchorScene, sItem,
                                                                                       segA, segB, segH, segV);
                                    } else {
                                        externalAnchorScene = neighborScene;
                                    }
                                }
                                m_attachedWires.append(
                                    {wire, idx == 0, sItem, i, isH, isV, neighborScene, externalAnchorScene, neighborAnchored, lockElbow,
                                     anchorIsSegment, segH, segV, segA, segB});
                                rememberWire(wire);
                            }
                        }
                        // Case 2: Selected Item is Wire, 'other' is unselected Component
                        else if (sItem->itemType() == SchematicItem::WireType && otherSItem->itemType() != SchematicItem::WireType) {
                            WireItem* wire = static_cast<WireItem*>(sItem);
                            QList<QPointF> wPts = wire->points();
                            QList<QPointF> otherPins = otherSItem->connectionPoints();
                            for (int j = 0; j < otherPins.size(); ++j) {
                                QPointF otherPinScene = otherSItem->mapToScene(otherPins[j]);
                                int idx = -1;
                                if (QLineF(wire->mapToScene(wPts.first()), otherPinScene).length() <= kAttachTolerance) {
                                    idx = 0;
                                } else if (QLineF(wire->mapToScene(wPts.last()), otherPinScene).length() <= kAttachTolerance) {
                                    idx = static_cast<int>(wPts.size()) - 1;
                                }

                                if (idx != -1) {
                                    int nextIdx = (idx == 0) ? 1 : idx - 1;
                                    bool isH = false, isV = false;
                                    QPointF neighborScene;
                                    QPointF externalAnchorScene;
                                    bool neighborAnchored = false;
                                    bool anchorIsSegment = false;
                                    bool segH = false;
                                    bool segV = false;
                                    QPointF segA;
                                    QPointF segB;
                                    const bool lockElbow = false;
                                    if (wPts.size() > 1) {
                                        isH = qAbs(wPts[idx].y() - wPts[nextIdx].y()) < 1.0;
                                        isV = qAbs(wPts[idx].x() - wPts[nextIdx].x()) < 1.0;
                                        neighborScene = wire->mapToScene(wPts[nextIdx]);
                                        QPointF anchorScene;
                                        neighborAnchored = findExternalAnchorOnWire(view(), wire, idx, otherSItem, anchorScene);
                                        if (neighborAnchored) {
                                            externalAnchorScene = anchorScene;
                                            anchorIsSegment = findExternalAnchorSegmentInfo(view(), wire, anchorScene, otherSItem,
                                                                                           segA, segB, segH, segV);
                                        } else {
                                            externalAnchorScene = neighborScene;
                                        }
                                    }
                                    m_attachedWires.append(
                                        {wire, idx == 0, otherSItem, j, isH, isV, neighborScene, externalAnchorScene, neighborAnchored, lockElbow,
                                         anchorIsSegment, segH, segV, segA, segB});
                                    rememberWire(wire);
                                }
                            }
                        }
                        // Case 3: Both are wires (Junctions)
                        else if (sItem->itemType() == SchematicItem::WireType && otherSItem->itemType() == SchematicItem::WireType) {
                            WireItem* selWire = static_cast<WireItem*>(sItem);
                            WireItem* otherWire = static_cast<WireItem*>(otherSItem);
                            QList<QPointF> selPts = selWire->points();
                            QList<QPointF> otherPts = otherWire->points();
                            
                            for (int si : {0, (int)selPts.size() - 1}) {
                                QPointF pSiScene = selWire->mapToScene(selPts[si]);
                                for (int oi = 0; oi < otherPts.size(); ++oi) {
                                    if (QLineF(pSiScene, otherWire->mapToScene(otherPts[oi])).length() <= kAttachTolerance) {
                                        int nextIdx = (si == 0) ? 1 : si - 1;
                                        bool isH = false, isV = false;
                                        QPointF neighborScene;
                                        QPointF externalAnchorScene;
                                        bool neighborAnchored = false;
                                        bool anchorIsSegment = false;
                                        bool segH = false;
                                        bool segV = false;
                                        QPointF segA;
                                        QPointF segB;
                                        const bool lockElbow = false;
                                        if (selPts.size() > 1) {
                                            isH = qAbs(selPts[si].y() - selPts[nextIdx].y()) < 1.0;
                                            isV = qAbs(selPts[si].x() - selPts[nextIdx].x()) < 1.0;
                                            neighborScene = selWire->mapToScene(selPts[nextIdx]);
                                            QPointF anchorScene;
                                            neighborAnchored = findExternalAnchorOnWire(view(), selWire, si, nullptr, anchorScene);
                                            if (neighborAnchored) {
                                                externalAnchorScene = anchorScene;
                                                anchorIsSegment = findExternalAnchorSegmentInfo(view(), selWire, anchorScene, nullptr,
                                                                                               segA, segB, segH, segV);
                                            } else {
                                                externalAnchorScene = neighborScene;
                                            }
                                        }
                                        m_attachedWires.append(
                                            {selWire, si == 0, otherWire, oi, isH, isV, neighborScene, externalAnchorScene, neighborAnchored, lockElbow,
                                             anchorIsSegment, segH, segV, segA, segB});
                                        rememberWire(selWire);
                                    }
                                }
                            }
                            // Also unselected wire end against selected wire
                            for (int oi : {0, (int)otherPts.size() - 1}) {
                                QPointF pOiScene = otherWire->mapToScene(otherPts[oi]);
                                for (int si = 0; si < selPts.size(); ++si) {
                                     if (QLineF(pOiScene, selWire->mapToScene(selPts[si])).length() <= kAttachTolerance) {
                                        int nextIdx = (oi == 0) ? 1 : oi - 1;
                                        bool isH = false, isV = false;
                                        QPointF neighborScene;
                                        QPointF externalAnchorScene;
                                        bool neighborAnchored = false;
                                        bool anchorIsSegment = false;
                                        bool segH = false;
                                        bool segV = false;
                                        QPointF segA;
                                        QPointF segB;
                                        const bool lockElbow = false;
                                        if (otherPts.size() > 1) {
                                            isH = qAbs(otherPts[oi].y() - otherPts[nextIdx].y()) < 1.0;
                                            isV = qAbs(otherPts[oi].x() - otherPts[nextIdx].x()) < 1.0;
                                            neighborScene = otherWire->mapToScene(otherPts[nextIdx]);
                                            QPointF anchorScene;
                                            neighborAnchored = findExternalAnchorOnWire(view(), otherWire, oi, nullptr, anchorScene);
                                            if (neighborAnchored) {
                                                externalAnchorScene = anchorScene;
                                                anchorIsSegment = findExternalAnchorSegmentInfo(view(), otherWire, anchorScene, nullptr,
                                                                                               segA, segB, segH, segV);
                                            } else {
                                                externalAnchorScene = neighborScene;
                                            }
                                        }
                                        m_attachedWires.append(
                                            {otherWire, oi == 0, selWire, si, isH, isV, neighborScene, externalAnchorScene, neighborAnchored, lockElbow,
                                             anchorIsSegment, segH, segV, segA, segB});
                                        rememberWire(otherWire);
                                    }
                                }
                            }
                         }
                    }
                }
            }

            // --- T-Junction Discovery ---
            // Track endpoint-on-segment attachments for moving wires, including
            // unselected wires that move indirectly via component drag.
            auto discoverTJunctionsForMovingWire = [this, rememberWire](WireItem* movingWire) {
                if (!view() || !view()->scene() || !movingWire) return;
                const QList<QPointF> movingPts = movingWire->points();
                if (movingPts.size() < 2) return;

                QList<QGraphicsItem*> potentialWires =
                    view()->scene()->items(movingWire->sceneBoundingRect().adjusted(-5, -5, 5, 5));

                for (QGraphicsItem* other : potentialWires) {
                    WireItem* otherWire = dynamic_cast<WireItem*>(other);
                    if (!otherWire || otherWire == movingWire || otherWire->isSelected()) continue;
                    const QList<QPointF> otherPts = otherWire->points();
                    if (otherPts.isEmpty()) continue;

                    QPointF ends[2] = {
                        otherWire->mapToScene(otherPts.first()),
                        otherWire->mapToScene(otherPts.last())
                    };

                    for (int j = 0; j < 2; ++j) {
                        for (int i = 0; i < movingPts.size() - 1; ++i) {
                            const QPointF p1 = movingWire->mapToScene(movingPts[i]);
                            const QPointF p2 = movingWire->mapToScene(movingPts[i + 1]);
                            const QPointF vec = p2 - p1;
                            const qreal lenSq = vec.x() * vec.x() + vec.y() * vec.y();
                            if (lenSq <= 0.0) continue;

                            const qreal u = ((ends[j].x() - p1.x()) * vec.x() + (ends[j].y() - p1.y()) * vec.y()) / lenSq;
                            if (u <= 0.01 || u >= 0.99) continue;

                            const QPointF proj = p1 + u * vec;
                            if (QLineF(ends[j], proj).length() > kSegmentTolerance) continue;

                            bool duplicate = false;
                            for (const TJunctionTracker& tj : m_tJunctions) {
                                if (tj.movingWire == movingWire &&
                                    tj.segmentIndex == i &&
                                    tj.attachedWire == otherWire &&
                                    tj.attachedWireIsStart == (j == 0) &&
                                    qAbs(tj.u - u) < 0.001) {
                                    duplicate = true;
                                    break;
                                }
                            }
                            if (duplicate) continue;

                            m_tJunctions.append({movingWire, i, u, otherWire, j == 0});
                            rememberWire(otherWire);
                        }
                    }
                }
            };

            if (!m_rigidGroupMove) for (QGraphicsItem* selItem : selectedItems) {
                if (WireItem* selectedWire = dynamic_cast<WireItem*>(selItem)) {
                    discoverTJunctionsForMovingWire(selectedWire);
                }
            }
            if (!m_rigidGroupMove) for (const AttachedWire& aw : m_attachedWires) {
                discoverTJunctionsForMovingWire(aw.wire);
            }

            // Capture anchors for wire endpoints so they can be re-snapped on release.
            if (!m_rigidGroupMove) for (auto it = m_initialWirePoints.begin(); it != m_initialWirePoints.end(); ++it) {
                WireItem* wire = it.key();
                if (!wire) continue;
                WireEndpointAnchor anchor;
                const QList<QPointF> pts = wire->points();
                if (pts.size() >= 2) {
                    const QPointF startScene = wire->mapToScene(pts.first());
                    const QPointF endScene = wire->mapToScene(pts.last());
                    captureEndpointAnchor(view(), wire, startScene, anchor, true);
                    captureEndpointAnchor(view(), wire, endScene, anchor, false);
                }
                if (anchor.startAnchored || anchor.endAnchored) {
                    m_initialWireAnchors[wire] = anchor;
                }
            }
            
            event->accept();
            return;
        }
    }

    SchematicTool::mousePressEvent(event);
}

void SchematicSelectTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBandActive && m_rubberBandItem && view()) {
        QPointF currentPos = view()->mapToScene(event->pos());
        QRectF rect = QRectF(m_rubberBandOrigin, currentPos).normalized();
        m_rubberBandItem->setRect(rect);
        event->accept();
        return;
    }

    if (m_isDragging && view()) {
        if (m_vertexDragActive && m_vertexWire) {
            if (m_initialWirePoints.contains(m_vertexWire)) {
                const QList<QPointF> original = m_initialWirePoints.value(m_vertexWire);
                if (m_vertexIndex >= 0 && m_vertexIndex < original.size()) {
                    QList<QPointF> points = original;
                    const QPointF currScene = view()->mapToScene(event->pos());
                    const QPointF snappedScene = view()->snapToGrid(currScene);
                    const QPointF localDelta = m_vertexWire->mapFromScene(snappedScene) - m_vertexWire->mapFromScene(m_vertexDragStartScenePos);
                    const int i = m_vertexIndex;

                    points[i] = original[i] + localDelta;

                    // Preserve orthogonal orientation for directly connected segments.
                    if (i > 0) {
                        const QPointF& leftOrig = original[i - 1];
                        const QPointF& thisOrig = original[i];
                        if (qAbs(leftOrig.y() - thisOrig.y()) < 1.0) {
                            points[i - 1].setY(points[i].y());
                        } else if (qAbs(leftOrig.x() - thisOrig.x()) < 1.0) {
                            points[i - 1].setX(points[i].x());
                        }
                    }
                    if (i + 1 < original.size()) {
                        const QPointF& rightOrig = original[i + 1];
                        const QPointF& thisOrig = original[i];
                        if (qAbs(rightOrig.y() - thisOrig.y()) < 1.0) {
                            points[i + 1].setY(points[i].y());
                        } else if (qAbs(rightOrig.x() - thisOrig.x()) < 1.0) {
                            points[i + 1].setX(points[i].x());
                        }
                    }

                    if (m_initialWirePoints.contains(m_vertexWire)) {
                        const QList<QPointF>& orig = m_initialWirePoints.value(m_vertexWire);
                        if (isOrthogonalWire(orig)) {
                            orthogonalizeFromOriginal(points, orig);
                        }
                    }
                    m_vertexWire->setPoints(points);
                }
            }

            m_lastMousePos = event->pos();
            event->accept();
            return;
        }

        if (m_segmentDragActive && m_segmentWire) {
            if (m_initialWirePoints.contains(m_segmentWire)) {
                const QList<QPointF> original = m_initialWirePoints.value(m_segmentWire);
                QList<QPointF> points = original;
                if (m_segmentIndex >= 0 && (m_segmentIndex + 1) < original.size()) {
                    const QPointF currScene = view()->mapToScene(event->pos());
                    const QPointF snappedScene = view()->snapToGrid(currScene);
                    QPointF localDelta = m_segmentWire->mapFromScene(snappedScene) - m_segmentWire->mapFromScene(m_segmentDragStartScenePos);
                    
                    if (m_segmentIsHorizontal) {
                        localDelta.setX(0.0);
                    } else if (m_segmentIsVertical) {
                        localDelta.setY(0.0);
                    }

                    const int i0 = m_segmentIndex;
                    const int i1 = m_segmentIndex + 1;
                    const QPointF p0New = original[i0] + localDelta;
                    const QPointF p1New = original[i1] + localDelta;
                    
                    bool anchor0 = false;
                    bool anchor1 = false;
                    for (const AttachedWire& aw : m_attachedWires) {
                        if (aw.wire == m_segmentWire && aw.anchor->itemType() != SchematicItem::WireType) {
                            if (aw.isStartPoint) anchor0 = true;
                            else anchor1 = true;
                        }
                        if (aw.anchor == m_segmentWire && aw.wire->itemType() == SchematicItem::WireType) {
                            int myIdx = aw.anchorPointIndex;
                            if (myIdx == i0) {
                                if (m_segmentIsHorizontal && aw.isHorizontal) anchor0 = true;
                                if (m_segmentIsVertical && aw.isVertical) anchor0 = true;
                            } else if (myIdx == i1) {
                                if (m_segmentIsHorizontal && aw.isHorizontal) anchor1 = true;
                                if (m_segmentIsVertical && aw.isVertical) anchor1 = true;
                            }
                        }
                    }

                    QList<QPointF> rebuilt;
                    auto appendUnique = [&rebuilt](const QPointF& p) {
                        if (rebuilt.isEmpty() || QLineF(rebuilt.last(), p).length() > 0.001) {
                            rebuilt.append(p);
                        }
                    };

                    for (int i = 0; i < i0; ++i) {
                        appendUnique(original[i]);
                    }

                    if (i0 > 0) {
                        const QPointF pPrev = original[i0 - 1];
                        QPointF c0 = p0New;
                        if (qAbs(original[i0].y() - pPrev.y()) < 1.0) {
                            c0 = QPointF(p0New.x(), pPrev.y());
                        } else if (qAbs(original[i0].x() - pPrev.x()) < 1.0) {
                            c0 = QPointF(pPrev.x(), p0New.y());
                        }
                        appendUnique(c0);
                    } else {
                        if (anchor0) appendUnique(original[0]);
                    }

                    appendUnique(p0New);
                    appendUnique(p1New);

                    if (i1 + 1 < original.size()) {
                        const QPointF pNext = original[i1 + 1];
                        QPointF c1 = p1New;
                        if (qAbs(original[i1].y() - pNext.y()) < 1.0) {
                            c1 = QPointF(p1New.x(), pNext.y());
                        } else if (qAbs(original[i1].x() - pNext.x()) < 1.0) {
                            c1 = QPointF(pNext.x(), p1New.y());
                        }
                        appendUnique(c1);
                    } else {
                        if (anchor1) appendUnique(original[i1]);
                    }

                    for (int i = i1 + 1; i < original.size(); ++i) {
                        appendUnique(original[i]);
                    }

                    if (rebuilt.size() >= 2) {
                        if (m_initialWirePoints.contains(m_segmentWire)) {
                            const QList<QPointF>& orig = m_initialWirePoints.value(m_segmentWire);
                            if (isOrthogonalWire(orig)) {
                                orthogonalizeFromOriginal(rebuilt, orig);
                            }
                        }
                        m_segmentWire->setPoints(rebuilt);
                    } else {
                        m_segmentWire->setPoints(points);
                    }
                }
            }
        }

        // --- MULTI-ITEM DRAGGING WITH GRID SNAPPING ---
        if (!m_initialPositions.isEmpty()) {
            // Total accumulated movement in scene coordinates
            QPointF totalMove = view()->mapToScene(event->pos()) - m_initialMouseScenePos;
            const bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
            if (shiftHeld && (qAbs(totalMove.x()) > 0.001 || qAbs(totalMove.y()) > 0.001)) {
                if (!m_axisLockActive) {
                    m_axisLockHorizontal = qAbs(totalMove.x()) >= qAbs(totalMove.y());
                    m_axisLockActive = true;
                }
                if (m_axisLockHorizontal) {
                    totalMove.setY(0.0);
                } else {
                    totalMove.setX(0.0);
                }
            } else {
                m_axisLockActive = false;
            }

            // Tiny pointer jitter produces excessive repaints on large selections.
            // Skip updates until the accumulated move changes meaningfully.
            if (m_hasLastAppliedMove &&
                qAbs(totalMove.x() - m_lastAppliedTotalMove.x()) < 0.35 &&
                qAbs(totalMove.y() - m_lastAppliedTotalMove.y()) < 0.35) {
                event->accept();
                return;
            }
            
            const QList<SchematicItem*> dragItems =
                m_dragSelection.isEmpty() ? QList<SchematicItem*>() : m_dragSelection;
            if (!dragItems.isEmpty()) {
                for (SchematicItem* sItem : dragItems) {
                    if (!sItem || !m_initialPositions.contains(sItem)) continue;
                    if (m_segmentDragActive && sItem == m_segmentWire) continue;
                    if (m_vertexDragActive && sItem == m_vertexWire) continue;
                    // Avoid double-transform jitter: when a label/net label is a child of a selected
                    // schematic item, let the parent carry it during group drag.
                    if ((sItem->itemType() == SchematicItem::LabelType ||
                         sItem->itemType() == SchematicItem::NetLabelType) &&
                        sItem->parentItem() && sItem->parentItem()->isSelected()) {
                        continue;
                    }

                    if (sItem->itemType() == SchematicItem::WireType) {
                        WireItem* wire = static_cast<WireItem*>(sItem);
                        if (m_initialWirePoints.contains(wire)) {
                            const QList<QPointF> orig = m_initialWirePoints.value(wire);
                            QList<QPointF> moved;
                            for (const QPointF& p : orig) moved.append(p + totalMove);
                            wire->setPoints(moved);
                        }
                    } else {
                        QPointF initialPos = m_initialPositions[sItem];
                        QPointF targetPos = initialPos + totalMove;
                        // Keep group-drag movement continuous for smooth visuals.
                        // Per-item snapping during drag causes visible jitter vs wire movement.
                        sItem->setPos(targetPos);
                    }
                }
            } else {
                QList<QGraphicsItem*> selectedItems = view()->scene()->selectedItems();
                for (QGraphicsItem* item : selectedItems) {
                    if (m_segmentDragActive && item == m_segmentWire) continue;
                    if (m_vertexDragActive && item == m_vertexWire) continue;
                    SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
                    if (!sItem || !m_initialPositions.contains(sItem)) continue;
                    if (sItem->itemType() == SchematicItem::WireType) {
                        WireItem* wire = static_cast<WireItem*>(sItem);
                        if (m_initialWirePoints.contains(wire)) {
                            const QList<QPointF> orig = m_initialWirePoints.value(wire);
                            QList<QPointF> moved;
                            for (const QPointF& p : orig) moved.append(p + totalMove);
                            wire->setPoints(moved);
                        }
                    } else {
                        QPointF initialPos = m_initialPositions[sItem];
                        sItem->setPos(initialPos + totalMove);
                    }
                }
            }
            m_lastAppliedTotalMove = totalMove;
            m_hasLastAppliedMove = true;
        } else if (m_isDragging) {
             QPointF sceneDelta = view()->mapToScene(event->pos()) - view()->mapToScene(m_lastMousePos);
             const bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
             if (shiftHeld && (qAbs(sceneDelta.x()) > 0.001 || qAbs(sceneDelta.y()) > 0.001)) {
                 if (!m_axisLockActive) {
                     m_axisLockHorizontal = qAbs(sceneDelta.x()) >= qAbs(sceneDelta.y());
                     m_axisLockActive = true;
                 }
                 if (m_axisLockHorizontal) {
                     sceneDelta.setY(0.0);
                 } else {
                     sceneDelta.setX(0.0);
                 }
             } else {
                 m_axisLockActive = false;
             }
             if (sceneDelta.manhattanLength() > 0.001) {
                const QList<SchematicItem*> dragItems =
                    m_dragSelection.isEmpty() ? QList<SchematicItem*>() : m_dragSelection;
                if (!dragItems.isEmpty()) {
                    for (SchematicItem* sItem : dragItems) {
                        if (!sItem) continue;
                        if (m_segmentDragActive && sItem == m_segmentWire) continue;
                        if (m_vertexDragActive && sItem == m_vertexWire) continue;
                        if (sItem->itemType() == SchematicItem::LabelType ||
                            sItem->itemType() == SchematicItem::NetLabelType) {
                            if (sItem->parentItem() && sItem->parentItem()->isSelected()) {
                                continue;
                            }
                        }
                        sItem->setPos(sItem->pos() + sceneDelta);
                    }
                } else {
                    QList<QGraphicsItem*> selectedItems = view()->scene()->selectedItems();
                    for (QGraphicsItem* item : selectedItems) {
                        if (m_segmentDragActive && item == m_segmentWire) continue;
                        if (m_vertexDragActive && item == m_vertexWire) continue;
                        if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
                            if (sItem->itemType() == SchematicItem::LabelType ||
                                sItem->itemType() == SchematicItem::NetLabelType) {
                                if (QGraphicsItem* parent = sItem->parentItem()) {
                                    const QPointF initialScene = parent->mapToScene(sItem->pos());
                                    const QPointF targetScene = initialScene + sceneDelta;
                                    sItem->setPos(parent->mapFromScene(targetScene));
                                    continue;
                                }
                            }
                        }
                        item->setPos(item->pos() + sceneDelta);
                    }
                }
            }
        }
        
        // --- WIRE UPDATE LOGIC (Absolute Scene Mode) ---
        if (!m_attachedWires.isEmpty()) {
            for (const AttachedWire& aw : m_attachedWires) {
                if (m_segmentDragActive && aw.wire == m_segmentWire) continue;
                if (m_vertexDragActive && aw.wire == m_vertexWire) continue;

                const QPointF currentPinScene = aw.anchor->mapToScene(aw.anchor->connectionPoints()[aw.anchorPointIndex]);
                QList<QPointF> pts;
                if (m_topologyLockedWires.contains(aw.wire)) {
                    pts = moveAttachedWireEndpointPreserveTopology(
                        aw.wire,
                        aw.isStartPoint,
                        currentPinScene,
                        aw.isHorizontal,
                        aw.isVertical
                    );
                } else {
                    const bool keepNeighborFixed = aw.neighborExternallyAnchored;
                    QPointF fixedNeighborScene = aw.neighborExternallyAnchored ? aw.externalAnchorScenePos : aw.neighborScenePos;
                    bool isH = aw.isHorizontal;
                    bool isV = aw.isVertical;
                    adjustEndpointOrientationForDrag(aw, currentPinScene, keepNeighborFixed, isH, isV);
                    fixedNeighborScene = adjustFixedNeighborSceneForSliding(aw, currentPinScene, fixedNeighborScene);
                    pts = moveAttachedWireEndpoint(
                        aw.wire,
                        aw.isStartPoint,
                        currentPinScene,
                        isH,
                        isV,
                        keepNeighborFixed,
                        fixedNeighborScene
                    );
                }
                if (m_initialWirePoints.contains(aw.wire)) {
                    const QList<QPointF>& orig = m_initialWirePoints.value(aw.wire);
                    if (isOrthogonalWire(orig) && orig.size() <= 3) {
                        pts = rebuildSimpleOrthogonal(pts, orig, view());
                    }
                    if (isOrthogonalWire(orig)) {
                        orthogonalizeFromOriginal(pts, orig);
                    } else if (isMostlyOrthogonalWire(pts)) {
                        forceOrthogonal(pts);
                    }
                }
                aw.wire->setPoints(pts);
            }
        }
        
        // Skip live ERC during drag for responsiveness.
        // ERC updates on release/selection changes.

        m_lastMousePos = event->pos();
        event->accept();
        return;
    }

    if (view() && !m_rubberBandActive && !m_segmentDragActive && !m_vertexDragActive) {
        updateSegmentHoverCue(view()->mapToScene(event->pos()));
        if (!m_hoverSegmentItem || !m_hoverSegmentItem->isVisible()) {
            updatePinHoverCue(view()->mapToScene(event->pos()));
        } else {
            clearPinHoverCue();
        }
    }
    
    SchematicTool::mouseMoveEvent(event);
}

void SchematicSelectTool::mouseReleaseEvent(QMouseEvent* event) {
    if (m_rubberBandActive && m_rubberBandItem && view()) {
        QRectF rect = m_rubberBandItem->rect();
        
        // Select items in rect - Only capture items FULLY CONTAINED within the rectangle
        QList<QGraphicsItem*> itemsInRect = view()->scene()->items(rect, Qt::ContainsItemShape);
        for (QGraphicsItem* item : itemsInRect) {
            if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
                sItem->setSelected(true);
            }
        }
        
        view()->scene()->removeItem(m_rubberBandItem);
        delete m_rubberBandItem;
        m_rubberBandItem = nullptr;
        m_rubberBandActive = false;
        event->accept();
    }
    
    if (view()) {
        view()->setDragMode(QGraphicsView::NoDrag);
    }
    
    if (m_isDragging && (!m_initialPositions.isEmpty() || !m_initialWirePoints.isEmpty())) {
        // --- FINAL CONNECTION RECONCILIATION ---
        // Skip all auto-reconcile/orthogonal cleanup when rigid group move is active.
        if (!m_rigidGroupMove && !m_attachedWires.isEmpty()) {
            // Propagate moved vertices to neighboring unselected wire endpoints.
            for (const AttachedWire& aw : m_attachedWires) {
                if (m_segmentDragActive && aw.wire == m_segmentWire) continue;
                if (m_vertexDragActive && aw.wire == m_vertexWire) continue;
                if (aw.neighborExternallyAnchored) continue;
                if (!m_initialWirePoints.contains(aw.wire)) continue;

                const QList<QPointF> origPts = m_initialWirePoints.value(aw.wire);
                const QList<QPointF> currPts = aw.wire->points();
                const int n = qMin(origPts.size(), currPts.size());

                for (int vi = 0; vi < n; ++vi) {
                    const QPointF origScene = aw.wire->mapToScene(origPts[vi]);
                    const QPointF currScene = aw.wire->mapToScene(currPts[vi]);
                    if (QLineF(origScene, currScene).length() < 0.5) continue;

                    QList<QGraphicsItem*> nearby = view()->scene()->items(
                        QRectF(currScene - QPointF(3, 3), QSizeF(6, 6)));
                    nearby += view()->scene()->items(
                        QRectF(origScene - QPointF(3, 3), QSizeF(6, 6)));

                    for (QGraphicsItem* ni : nearby) {
                        WireItem* otherWire = dynamic_cast<WireItem*>(ni);
                        if (!otherWire || otherWire == aw.wire || otherWire->isSelected()) continue;

                        bool alreadyTracked = false;
                        for (const AttachedWire& aw2 : m_attachedWires) {
                            if (aw2.wire == otherWire) { alreadyTracked = true; break; }
                        }
                        if (alreadyTracked) continue;

                        QList<QPointF> otherPts = otherWire->points();
                        if (otherPts.isEmpty()) continue;

                        const QPointF otherStart = otherWire->mapToScene(otherPts.first());
                        const QPointF otherEnd = otherWire->mapToScene(otherPts.last());

                        if (QLineF(otherStart, origScene).length() < 2.0) {
                            const QPointF newLocal = otherWire->mapFromScene(currScene);
                            QList<QPointF> updated = SchematicWireUtils::maintainOrthogonality(otherPts, 0, newLocal);
                            otherWire->setPoints(updated);
                            if (!m_initialWirePoints.contains(otherWire)) {
                                m_initialWirePoints[otherWire] = otherPts;
                            }
                        } else if (QLineF(otherEnd, origScene).length() < 2.0) {
                            const QPointF newLocal = otherWire->mapFromScene(currScene);
                            QList<QPointF> updated = SchematicWireUtils::maintainOrthogonality(otherPts, otherPts.size() - 1, newLocal);
                            otherWire->setPoints(updated);
                            if (!m_initialWirePoints.contains(otherWire)) {
                                m_initialWirePoints[otherWire] = otherPts;
                            }
                        }
                    }
                }
            }
        }

        // Keep endpoint-on-segment T-junctions attached.
        if (!m_rigidGroupMove) for (const TJunctionTracker& tj : m_tJunctions) {
            const QList<QPointF> movingPts = tj.movingWire->points();
            if (tj.segmentIndex < 0 || tj.segmentIndex >= movingPts.size() - 1) continue;

            const QPointF p1 = tj.movingWire->mapToScene(movingPts[tj.segmentIndex]);
            const QPointF p2 = tj.movingWire->mapToScene(movingPts[tj.segmentIndex + 1]);
            const QPointF newScenePos = p1 + tj.u * (p2 - p1);

            QList<QPointF> attachedPts = tj.attachedWire->points();
            if (attachedPts.isEmpty()) continue;
            const int targetIndex = tj.attachedWireIsStart ? 0 : attachedPts.size() - 1;
            const QPointF newLocalPos = tj.attachedWire->mapFromScene(newScenePos);
            QList<QPointF> updatedPoints =
                SchematicWireUtils::maintainOrthogonality(attachedPts, targetIndex, newLocalPos);
            tj.attachedWire->setPoints(updatedPoints);
        }

        if (!m_rigidGroupMove) {
            // Final orthogonal cleanup for wires that were originally orthogonal.
            auto buildIgnoreSet = [this](WireItem* wire) {
                QSet<SchematicItem*> ignore;
                if (m_initialWireAnchors.contains(wire)) {
                    const WireEndpointAnchor& anchor = m_initialWireAnchors[wire];
                    if (anchor.startItem) ignore.insert(anchor.startItem);
                    if (anchor.endItem) ignore.insert(anchor.endItem);
                }
                return ignore;
            };

            for (auto it = m_initialWirePoints.begin(); it != m_initialWirePoints.end(); ++it) {
                WireItem* wire = it.key();
                if (!wire) continue;
                const QList<QPointF>& orig = it.value();
                if (isOrthogonalWire(orig) || isMostlyOrthogonalWire(orig)) {
                    QList<QPointF> curr = wire->points();
                    if (orig.size() <= 3) {
                        curr = rebuildSimpleOrthogonal(curr, orig, view());
                    }
                    forceOrthogonal(curr);
                    wire->setPoints(curr);
                }
            }

            // Re-snap attached wire endpoints to their pins after cleanup.
            if (!m_attachedWires.isEmpty()) {
                for (const AttachedWire& aw : m_attachedWires) {
                    if (!aw.wire || !aw.anchor) continue;
                    if (aw.anchor->itemType() == SchematicItem::WireType) continue;
                    const QList<QPointF> pins = aw.anchor->connectionPoints();
                    if (aw.anchorPointIndex < 0 || aw.anchorPointIndex >= pins.size()) continue;
                    const QPointF pinScene = aw.anchor->mapToScene(pins[aw.anchorPointIndex]);
                    QList<QPointF> pts;
                    if (m_topologyLockedWires.contains(aw.wire)) {
                        pts = moveAttachedWireEndpointPreserveTopology(
                            aw.wire,
                            aw.isStartPoint,
                            pinScene,
                            aw.isHorizontal,
                            aw.isVertical
                        );
                    } else {
                        const bool keepNeighborFixed = aw.neighborExternallyAnchored;
                        QPointF fixedNeighborScene = aw.neighborExternallyAnchored ? aw.externalAnchorScenePos : aw.neighborScenePos;
                        bool isH = aw.isHorizontal;
                        bool isV = aw.isVertical;
                        adjustEndpointOrientationForDrag(aw, pinScene, keepNeighborFixed, isH, isV);
                        fixedNeighborScene = adjustFixedNeighborSceneForSliding(aw, pinScene, fixedNeighborScene);
                        pts = moveAttachedWireEndpoint(
                            aw.wire,
                            aw.isStartPoint,
                            pinScene,
                            isH,
                            isV,
                            keepNeighborFixed,
                            fixedNeighborScene
                        );
                    }
                    if (m_initialWirePoints.contains(aw.wire)) {
                        const QList<QPointF>& orig = m_initialWirePoints.value(aw.wire);
                        if (isOrthogonalWire(orig) && orig.size() <= 3) {
                            pts = rebuildSimpleOrthogonal(pts, orig, view());
                        }
                    }
                    aw.wire->setPoints(pts);
                }
            }

            // Clamp endpoints to their original anchors (pins) to prevent drift.
            if (!m_initialWireAnchors.isEmpty()) {
                for (auto it = m_initialWireAnchors.begin(); it != m_initialWireAnchors.end(); ++it) {
                    applyEndpointAnchorsToWire(it.key(), it.value());
                }
            }

            // Final obstacle avoidance for simple L-wires after anchors are clamped.
            for (auto it = m_initialWirePoints.begin(); it != m_initialWirePoints.end(); ++it) {
                WireItem* wire = it.key();
                if (!wire) continue;
                const QList<QPointF>& orig = it.value();
                if (orig.size() != 3) continue;
                QList<QPointF> curr = wire->points();
                curr = nudgeLWireAwayFromObstacles(wire, curr, view(), buildIgnoreSet(wire));
                wire->setPoints(curr);
            }

            for (auto it = m_initialWirePoints.begin(); it != m_initialWirePoints.end(); ++it) {
                if (m_topologyLockedWires.contains(it.key())) continue;
                it.key()->setPoints(simplifyWirePoints(it.key()->points()));
            }
        }

        if (view()->undoStack()) {
            bool hasMoved = false;
            
            // Check for moved items
            for (auto it = m_initialPositions.begin(); it != m_initialPositions.end(); ++it) {
                if (it.key()->pos() != it.value()) { hasMoved = true; break; }
            }
            
            // Check for stretched wires
            QList<WireItem*> stretchedWires;
            for (auto it = m_initialWirePoints.begin(); it != m_initialWirePoints.end(); ++it) {
                if (it.key()->points() != it.value()) {
                    stretchedWires.append(it.key());
                    hasMoved = true; 
                }
            }

            if (hasMoved) {
                view()->undoStack()->beginMacro("Move & Stretch");
                
                QList<SchematicItem*> movedItems;
                QList<QPointF> oldPos, newPos;
                for (auto it = m_initialPositions.begin(); it != m_initialPositions.end(); ++it) {
                    if (it.key()->pos() != it.value()) {
                        movedItems.append(it.key());
                        oldPos.append(it.value());
                        newPos.append(it.key()->pos());
                    }
                }
                
                if (!movedItems.isEmpty()) {
                    view()->undoStack()->push(new MoveItemCommand(view()->scene(), movedItems, oldPos, newPos));
                    
                    // Auto-cut wires after move
                    if (!m_rigidGroupMove) {
                        QList<WireItem*> attached;
                        for (const AttachedWire& aw : m_attachedWires) {
                            if (aw.wire && !attached.contains(aw.wire)) attached.append(aw.wire);
                        }
                        for (SchematicItem* item : movedItems) {
                            const QList<WireItem*> pinConnected = collectConnectedWiresAtPins(view(), item);
                            for (WireItem* wire : pinConnected) {
                                if (wire && !attached.contains(wire)) attached.append(wire);
                            }
                        }
                        for (SchematicItem* item : movedItems) {
                            SchematicWireUtils::splitWiresByComponent(item, view()->scene(), view()->undoStack(), attached);
                        }
                    }
                }
                
                for (WireItem* wire : stretchedWires) {
                    view()->undoStack()->push(new UpdateWirePointsCommand(view()->scene(), wire, m_initialWirePoints[wire], wire->points()));
                }
                
                view()->undoStack()->endMacro();
            }
        }
    }
    
    m_isDragging = false;
    m_segmentDragActive = false;
    m_segmentWire = nullptr;
    m_segmentIndex = -1;
    m_segmentIsHorizontal = false;
    m_segmentIsVertical = false;
    m_vertexDragActive = false;
    m_vertexWire = nullptr;
    m_vertexIndex = -1;
    clearSegmentHoverCue();
    clearPinHoverCue();
    m_attachedWires.clear();
    m_tJunctions.clear();
    m_initialPositions.clear();
    m_initialWirePoints.clear();
    m_rigidGroupMove = false;
    m_dragSelection.clear();
    m_hasLastAppliedMove = false;
    if (m_dragViewOptimized && view()) {
        view()->setViewportUpdateMode(m_prevViewportUpdateMode);
        view()->setRenderHint(QPainter::Antialiasing, m_prevAntialiasing);
        view()->setRenderHint(QPainter::TextAntialiasing, m_prevTextAntialiasing);
        m_dragViewOptimized = false;
    }

    // Refresh net highlights after move
    if (view()) {
        SchematicConnectivity::updateVisualConnections(view()->scene());
        Q_EMIT view()->selectionChanged();
    }

    SchematicTool::mouseReleaseEvent(event);
}

void SchematicSelectTool::keyPressEvent(QKeyEvent* event) {
    if (!view()) {
        SchematicTool::keyPressEvent(event);
        return;
    }
    
    // Ctrl+A: Select All
    if (event->matches(QKeySequence::SelectAll)) {
        QList<QGraphicsItem*> allItems = view()->scene()->items();
        for (QGraphicsItem* item : allItems) {
            SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
            if (sItem) {
                sItem->setSelected(true);
            }
        }
        event->accept();
        return;
    }
    
    // Ctrl+D or Escape: Deselect All
    if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier)) {
        view()->scene()->clearSelection();
        event->accept();
        return;
    }
    
    if (event->key() == Qt::Key_Escape) {
        view()->scene()->clearSelection();
        event->accept();
        return;
    }

    // Ctrl+R: Rotate selected items
    if (event->key() == Qt::Key_R && (event->modifiers() & Qt::ControlModifier)) {
        QList<SchematicItem*> selectedItems;
        for (QGraphicsItem* item : view()->scene()->selectedItems()) {
            if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
                selectedItems.append(sItem);
            }
        }
        
        if (!selectedItems.isEmpty() && view()->undoStack()) {
            view()->undoStack()->push(new RotateItemCommand(view()->scene(), selectedItems, 90));
        }
        event->accept();
        return;
    }
    
    SchematicTool::keyPressEvent(event);
}

int SchematicSelectTool::findWireVertexAt(WireItem* wire, const QPointF& scenePos, qreal tolerance) const {
    if (!wire) return -1;
    const QList<QPointF> pts = wire->points();
    if (pts.isEmpty()) return -1;

    const QPointF localPos = wire->mapFromScene(scenePos);
    const qreal tolSq = tolerance * tolerance;
    qreal bestDistSq = std::numeric_limits<qreal>::max();
    int bestIdx = -1;

    for (int i = 0; i < pts.size(); ++i) {
        const qreal dx = localPos.x() - pts[i].x();
        const qreal dy = localPos.y() - pts[i].y();
        const qreal distSq = dx * dx + dy * dy;
        if (distSq <= tolSq && distSq < bestDistSq) {
            bestDistSq = distSq;
            bestIdx = i;
        }
    }
    return bestIdx;
}

int SchematicSelectTool::findWireSegmentAt(WireItem* wire, const QPointF& scenePos, qreal tolerance) const {
    if (!wire) return -1;
    const QList<QPointF> pts = wire->points();
    if (pts.size() < 2) return -1;

    const QPointF localPos = wire->mapFromScene(scenePos);
    const qreal tolSq = tolerance * tolerance;
    qreal bestDistSq = std::numeric_limits<qreal>::max();
    int bestSeg = -1;

    for (int i = 0; i < pts.size() - 1; ++i) {
        const QPointF a = pts[i];
        const QPointF b = pts[i + 1];
        const qreal vx = b.x() - a.x();
        const qreal vy = b.y() - a.y();
        const qreal lenSq = vx * vx + vy * vy;
        if (lenSq <= 0.0) continue;

        const qreal wx = localPos.x() - a.x();
        const qreal wy = localPos.y() - a.y();
        qreal u = (wx * vx + wy * vy) / lenSq;
        if (u < 0.0) u = 0.0;
        if (u > 1.0) u = 1.0;

        const QPointF proj(a.x() + u * vx, a.y() + u * vy);
        const qreal dx = localPos.x() - proj.x();
        const qreal dy = localPos.y() - proj.y();
        const qreal distSq = dx * dx + dy * dy;
        if (distSq <= tolSq && distSq < bestDistSq) {
            bestDistSq = distSq;
            bestSeg = i;
        }
    }

    return bestSeg;
}

void SchematicSelectTool::updateSegmentHoverCue(const QPointF& scenePos) {
    if (!view() || !view()->scene()) return;

    QGraphicsItem* item = view()->itemAt(view()->mapFromScene(scenePos));
    WireItem* wire = dynamic_cast<WireItem*>(item);
    if (!wire || !wire->isSelected()) {
        clearSegmentHoverCue();
        return;
    }

    const int vertexIdx = findWireVertexAt(wire, scenePos);
    if (vertexIdx >= 0) {
        if (!m_hoverVertexHandle) {
            m_hoverVertexHandle = new QGraphicsEllipseItem();
            QColor c = ThemeManager::theme() ? ThemeManager::theme()->selectionBox() : QColor(0, 170, 255);
            m_hoverVertexHandle->setPen(QPen(c, 1.75));
            QColor fill = c;
            fill.setAlpha(235);
            m_hoverVertexHandle->setBrush(fill);
            m_hoverVertexHandle->setZValue(2002);
            view()->scene()->addItem(m_hoverVertexHandle);
        } else if (!m_hoverVertexHandle->scene()) {
            view()->scene()->addItem(m_hoverVertexHandle);
        }

        if (m_hoverSegmentItem) m_hoverSegmentItem->setVisible(false);
        if (m_hoverHandleStart) m_hoverHandleStart->setVisible(false);
        if (m_hoverHandleEnd) m_hoverHandleEnd->setVisible(false);

        const QList<QPointF> pts = wire->points();
        const QPointF v = wire->mapToScene(pts[vertexIdx]);
        const qreal r = 4.5;
        m_hoverVertexHandle->setRect(QRectF(v.x() - r, v.y() - r, 2.0 * r, 2.0 * r));
        m_hoverVertexHandle->setVisible(true);

        if (view() && view()->viewport()) {
            view()->viewport()->setCursor(Qt::SizeAllCursor);
        }

        m_hoverWire = wire;
        m_hoverVertexIndex = vertexIdx;
        m_hoverSegmentIndex = -1;
        return;
    }

    const int segIdx = findWireSegmentAt(wire, scenePos);
    if (segIdx < 0) {
        clearSegmentHoverCue();
        return;
    }

    if (!m_hoverSegmentItem) {
        m_hoverSegmentItem = new QGraphicsLineItem();
        QColor c = ThemeManager::theme() ? ThemeManager::theme()->selectionBox() : QColor(0, 170, 255);
        m_hoverSegmentItem->setPen(QPen(c, 2.5, Qt::DashLine, Qt::RoundCap));
        m_hoverSegmentItem->setZValue(2000);
        view()->scene()->addItem(m_hoverSegmentItem);
    } else if (!m_hoverSegmentItem->scene()) {
        view()->scene()->addItem(m_hoverSegmentItem);
    }

    auto ensureHandle = [this](QGraphicsEllipseItem*& handle) {
        if (!handle) {
            handle = new QGraphicsEllipseItem();
            QColor c = ThemeManager::theme() ? ThemeManager::theme()->selectionBox() : QColor(0, 170, 255);
            handle->setPen(QPen(c, 1.5));
            QColor fill = c;
            fill.setAlpha(220);
            handle->setBrush(fill);
            handle->setZValue(2001);
            view()->scene()->addItem(handle);
        } else if (!handle->scene()) {
            view()->scene()->addItem(handle);
        }
    };
    ensureHandle(m_hoverHandleStart);
    ensureHandle(m_hoverHandleEnd);
    if (m_hoverVertexHandle) m_hoverVertexHandle->setVisible(false);

    const QList<QPointF> pts = wire->points();
    const QPointF a = wire->mapToScene(pts[segIdx]);
    const QPointF b = wire->mapToScene(pts[segIdx + 1]);
    m_hoverSegmentItem->setLine(QLineF(a, b));
    m_hoverSegmentItem->setVisible(true);

    // --- SMART CURSOR: Show drag direction ---
    bool isHorizontal = qAbs(a.y() - b.y()) < 1.0;
    if (isHorizontal) {
        view()->viewport()->setCursor(Qt::SizeVerCursor); // Drag UP/DOWN
    } else {
        view()->viewport()->setCursor(Qt::SizeHorCursor); // Drag LEFT/RIGHT
    }

    const qreal r = 3.5;
    m_hoverHandleStart->setRect(QRectF(a.x() - r, a.y() - r, 2.0 * r, 2.0 * r));
    m_hoverHandleEnd->setRect(QRectF(b.x() - r, b.y() - r, 2.0 * r, 2.0 * r));
    m_hoverHandleStart->setVisible(true);
    m_hoverHandleEnd->setVisible(true);
    m_hoverWire = wire;
    m_hoverSegmentIndex = segIdx;
    m_hoverVertexIndex = -1;
}

void SchematicSelectTool::clearSegmentHoverCue() {
    if (m_hoverSegmentItem && view() && view()->scene() && m_hoverSegmentItem->scene()) {
        view()->scene()->removeItem(m_hoverSegmentItem);
    }
    delete m_hoverSegmentItem;
    m_hoverSegmentItem = nullptr;
    if (m_hoverHandleStart && view() && view()->scene() && m_hoverHandleStart->scene()) {
        view()->scene()->removeItem(m_hoverHandleStart);
    }
    if (m_hoverHandleEnd && view() && view()->scene() && m_hoverHandleEnd->scene()) {
        view()->scene()->removeItem(m_hoverHandleEnd);
    }
    delete m_hoverHandleStart;
    delete m_hoverHandleEnd;
    m_hoverHandleStart = nullptr;
    m_hoverHandleEnd = nullptr;
    if (m_hoverVertexHandle && view() && view()->scene() && m_hoverVertexHandle->scene()) {
        view()->scene()->removeItem(m_hoverVertexHandle);
    }
    delete m_hoverVertexHandle;
    m_hoverVertexHandle = nullptr;

    if (view() && view()->viewport()) {
        view()->viewport()->setCursor(Qt::ArrowCursor);
    }

    m_hoverWire = nullptr;
    m_hoverSegmentIndex = -1;
    m_hoverVertexIndex = -1;
}

QList<QPointF> SchematicSelectTool::simplifyWirePoints(const QList<QPointF>& points) const {
    if (points.size() < 3) return points;

    QList<QPointF> cleaned;
    cleaned.reserve(points.size());

    for (const QPointF& p : points) {
        if (cleaned.isEmpty() || QLineF(cleaned.last(), p).length() > 0.001) {
            cleaned.append(p);
        }
    }

    if (cleaned.size() < 3) return cleaned;

    QList<QPointF> simplified;
    simplified.append(cleaned.first());
    for (int i = 1; i < cleaned.size() - 1; ++i) {
        const QPointF& a = simplified.last();
        const QPointF& b = cleaned[i];
        const QPointF& c = cleaned[i + 1];

        const bool abH = qAbs(a.y() - b.y()) < 1.0;
        const bool abV = qAbs(a.x() - b.x()) < 1.0;
        const bool bcH = qAbs(b.y() - c.y()) < 1.0;
        const bool bcV = qAbs(b.x() - c.x()) < 1.0;
        const bool collinear = (abH && bcH) || (abV && bcV);

        if (!collinear) {
            simplified.append(b);
        }
    }
    simplified.append(cleaned.last());

    return (simplified.size() >= 2) ? simplified : cleaned;
}

void SchematicSelectTool::updatePinHoverCue(const QPointF& scenePos) {
    if (!view() || !view()->scene()) return;

    QGraphicsItem* item = view()->itemAt(view()->mapFromScene(scenePos));
    SchematicItem* sItem = nearestSchematicItem(item);
    
    if (!sItem) {
        clearPinHoverCue();
        return;
    }

    QList<QPointF> connectionPoints = sItem->connectionPoints();
    QPointF clickPosLocal = sItem->mapFromScene(scenePos);
    
    bool foundPin = false;
    QPointF nearestPin;
    qreal minDistance = 5.0; // Same tolerance as selection routing
    
    for (const QPointF& pin : connectionPoints) {
        qreal distance = QLineF(clickPosLocal, pin).length();
        if (distance < minDistance) {
            foundPin = true;
            nearestPin = sItem->mapToScene(pin);
            minDistance = distance;
        }
    }

    if (foundPin) {
        if (!m_hoverPinIndicator) {
            m_hoverPinIndicator = new QGraphicsEllipseItem();
            QColor c = Qt::red;
            m_hoverPinIndicator->setPen(QPen(c, 1.5));
            c.setAlpha(150);
            m_hoverPinIndicator->setBrush(c);
            m_hoverPinIndicator->setZValue(2005);
            view()->scene()->addItem(m_hoverPinIndicator);
        } else if (!m_hoverPinIndicator->scene()) {
            view()->scene()->addItem(m_hoverPinIndicator);
        }

        const qreal r = 4.0;
        m_hoverPinIndicator->setRect(QRectF(nearestPin.x() - r, nearestPin.y() - r, 2.0 * r, 2.0 * r));
        m_hoverPinIndicator->setVisible(true);
    } else {
        clearPinHoverCue();
    }
}

void SchematicSelectTool::clearPinHoverCue() {
    if (m_hoverPinIndicator) {
        m_hoverPinIndicator->setVisible(false);
    }
}
