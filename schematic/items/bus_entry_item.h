#ifndef BUSENTRYITEM_H
#define BUSENTRYITEM_H

#include "schematic_item.h"
#include <QPen>

class BusEntryItem : public SchematicItem {
public:
    BusEntryItem(QPointF pos = QPointF(), bool flipped = false, QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "BusEntry"; }
    ItemType itemType() const override { return static_cast<ItemType>(SchematicItem::CustomType + 1); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QList<QPointF> connectionPoints() const override;
    bool supportsTransformAction(TransformAction action) const override { Q_UNUSED(action) return true; }
    bool flipped() const { return m_flipped; }
    void setFlipped(bool flipped) { m_flipped = flipped; update(); }

private:
    bool m_flipped;
    QPen m_pen;
};

#endif // BUSENTRYITEM_H
