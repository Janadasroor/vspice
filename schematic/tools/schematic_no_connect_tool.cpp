#include "schematic_no_connect_tool.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include "schematic_item.h"
#include "../items/no_connect_item.h"
#include <QUndoStack>
#include <QGraphicsScene>
#include <limits>

namespace {
QPointF findBestNoConnectPlacement(QGraphicsScene* scene, const QPointF& seedPoint) {
    if (!scene) return seedPoint;

    constexpr qreal kSearchRadius = 20.0;
    constexpr qreal kMaxSnapDistSq = 14.0 * 14.0;
    QRectF searchRect(seedPoint.x() - kSearchRadius, seedPoint.y() - kSearchRadius,
                      2.0 * kSearchRadius, 2.0 * kSearchRadius);

    qreal bestDistSq = std::numeric_limits<qreal>::max();
    QPointF bestPoint = seedPoint;

    const QList<QGraphicsItem*> nearby = scene->items(searchRect);
    for (QGraphicsItem* gi : nearby) {
        SchematicItem* item = dynamic_cast<SchematicItem*>(gi);
        if (!item) continue;
        if (item->itemType() == SchematicItem::WireType ||
            item->itemType() == SchematicItem::BusType ||
            item->itemType() == SchematicItem::JunctionType ||
            item->itemType() == SchematicItem::NoConnectType) {
            continue;
        }

        const QList<QPointF> pins = item->connectionPoints();
        for (const QPointF& pinLocal : pins) {
            const QPointF pinScene = item->mapToScene(pinLocal);
            const qreal dx = pinScene.x() - seedPoint.x();
            const qreal dy = pinScene.y() - seedPoint.y();
            const qreal d2 = dx * dx + dy * dy;
            if (d2 < bestDistSq && d2 <= kMaxSnapDistSq) {
                bestDistSq = d2;
                bestPoint = pinScene;
            }
        }
    }

    return bestPoint;
}
}

SchematicNoConnectTool::SchematicNoConnectTool(QObject* parent)
    : SchematicTool("No-Connect", parent) {
}

void SchematicNoConnectTool::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = view()->mapToScene(event->pos());
        QPointF gridPos = view()->snapToGridOrPin(scenePos).point;
        QPointF placePos = findBestNoConnectPlacement(view()->scene(), gridPos);

        NoConnectItem* item = new NoConnectItem();
        item->setPos(placePos);

        if (view()->undoStack()) {
            view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
        } else {
            view()->scene()->addItem(item);
        }
    }
}
