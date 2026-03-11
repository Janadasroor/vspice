#ifndef NOCONNECTITEM_H
#define NOCONNECTITEM_H

#include "schematic_item.h"

class NoConnectItem : public SchematicItem {
public:
    NoConnectItem(QGraphicsItem *parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override { return "NoConnect"; }
    ItemType itemType() const override { return NoConnectType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    QList<QPointF> connectionPoints() const override;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    qreal m_size = 10.0;
};

#endif // NOCONNECTITEM_H
