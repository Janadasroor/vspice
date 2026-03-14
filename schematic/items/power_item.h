#ifndef POWERITEM_H
#define POWERITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QBrush>
#include <QPen>
#include <QColor>
#include <memory>
#include <vector>

class PowerItem : public SchematicItem {
public:
    enum PowerType { GND, VCC, VDD, VSS, VBAT, THREE_V_THREE, FIVE_V, TWELVE_V };
    
    PowerItem(QPointF pos = QPointF(), PowerType type = GND, QGraphicsItem *parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override;
    ItemType itemType() const override { return SchematicItem::PowerType; }
    QString referencePrefix() const override { return "#PWR"; } 
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    
    // Connectivity
    QList<QPointF> connectionPoints() const override;
    QList<PinElectricalType> pinElectricalTypes() const override { return { PowerInputPin }; }

    // Properties
    PowerType powerType() const { return m_type; }
    void setPowerType(PowerType type);
    void setValue(const QString& value) override;
    QString netName() const;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    PowerType m_type;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // POWERITEM_H
