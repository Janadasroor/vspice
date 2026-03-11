#include "schematic_bus_entry_tool.h"
#include "bus_entry_item.h"
#include "bus_item.h"
#include "wire_item.h"
#include "schematic_view.h"
#include "../editor/schematic_commands.h"
#include <QMouseEvent>
#include <QUndoStack>
#include <QGraphicsScene>
#include <limits>

namespace {
qreal sqrLen(const QPointF& v) { return v.x() * v.x() + v.y() * v.y(); }

QPointF closestPointOnSegment(const QPointF& p, const QPointF& a, const QPointF& b, qreal* outDistSq = nullptr) {
    const QPointF ab = b - a;
    const qreal abLenSq = sqrLen(ab);
    qreal t = 0.0;
    if (abLenSq > 1e-9) {
        t = ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / abLenSq;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    const QPointF proj(a.x() + ab.x() * t, a.y() + ab.y() * t);
    if (outDistSq) *outDistSq = sqrLen(p - proj);
    return proj;
}

bool nearestPointOnItemSegments(QGraphicsScene* scene, const QPointF& p, bool busMode, QPointF& outPoint) {
    if (!scene) return false;
    constexpr qreal kSnapRadiusSq = 18.0 * 18.0;
    qreal best = std::numeric_limits<qreal>::max();
    bool found = false;

    for (QGraphicsItem* gi : scene->items(QRectF(p.x() - 24, p.y() - 24, 48, 48))) {
        QList<QPointF> pts;
        if (busMode) {
            if (auto* bus = dynamic_cast<BusItem*>(gi)) pts = bus->points();
        } else {
            if (auto* wire = dynamic_cast<WireItem*>(gi)) pts = wire->points();
        }
        if (pts.size() < 2) continue;
        for (int i = 0; i + 1 < pts.size(); ++i) {
            qreal d2 = 0.0;
            const QPointF cp = closestPointOnSegment(p, pts[i], pts[i + 1], &d2);
            if (d2 < best) {
                best = d2;
                outPoint = cp;
                found = true;
            }
        }
    }
    return found && best <= kSnapRadiusSq;
}

QList<QPointF> entryEndpoints(bool flipped) {
    if (flipped) return { QPointF(-10, 10), QPointF(10, -10) };
    return { QPointF(-10, -10), QPointF(10, 10) };
}
}

void SchematicBusEntryTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;
    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = view()->mapToScene(event->pos());
        QPointF snapped = view()->snapToGrid(scenePos);

        // Smart placement: if both a nearby bus and wire are found, orient/position entry
        // so one endpoint snaps to the bus and the other aims at the wire.
        QPointF busPt, wirePt;
        bool hasBus = nearestPointOnItemSegments(view()->scene(), snapped, true, busPt);
        bool hasWire = nearestPointOnItemSegments(view()->scene(), snapped, false, wirePt);

        bool flipped = m_flipped;
        QPointF center = snapped;
        if (hasBus && hasWire) {
            struct Candidate {
                bool flipped = false;
                int busEndpoint = 0; // 0 or 1
                QPointF center;
                qreal score = std::numeric_limits<qreal>::max();
            };

            Candidate best;
            const QPointF desired = wirePt - busPt;

            for (bool f : {false, true}) {
                const QList<QPointF> ep = entryEndpoints(f);
                for (int busIdx = 0; busIdx < 2; ++busIdx) {
                    const int wireIdx = 1 - busIdx;
                    const QPointF c = busPt - ep[busIdx];
                    const QPointF wireEnd = c + ep[wireIdx];
                    const QPointF vec = wireEnd - busPt;
                    const qreal align = -((vec.x() * desired.x()) + (vec.y() * desired.y())); // lower is better (more aligned)
                    const qreal wireErr = sqrLen(wireEnd - wirePt);
                    const qreal score = wireErr + align * 0.05;
                    if (score < best.score) {
                        best.flipped = f;
                        best.busEndpoint = busIdx;
                        best.center = c;
                        best.score = score;
                    }
                }
            }
            flipped = best.flipped;
            center = view()->snapToGrid(best.center);
        }
        
        BusEntryItem* item = new BusEntryItem(center, flipped);
        
        if (view()->undoStack()) {
            view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
        } else {
            view()->scene()->addItem(item);
        }
    }
}

void SchematicBusEntryTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        m_flipped = !m_flipped;
    }
}
