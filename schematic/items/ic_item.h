#ifndef ICITEM_H
#define ICITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QPen>
#include <QBrush>
#include <QList>
#include <QPointF>
#include <memory>
#include <vector>

class ICItem : public SchematicItem {
public:
    ICItem(QPointF pos = QPointF(), QString value = "74HC00", 
           int pinCount = 8, QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "IC"; }
    ItemType itemType() const override { return SchematicItem::ICType; }
    QString referencePrefix() const override { return "U"; }
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    QList<QPointF> connectionPoints() const override;

    QString value() const { return m_value; }
    void setValue(const QString& value);
    
    int pinCount() const { return m_pinCount; }
    void setPinCount(int count);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    QString m_value;
    int m_pinCount;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // ICITEM_H
