#ifndef CAPACITORITEM_H
#define CAPACITORITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QBrush>
#include <QPen>
#include <QColor>
#include <memory>
#include <vector>

class CapacitorItem : public SchematicItem {
public:
    enum CapacitorStyle { NonPolarized, Polarized };
    CapacitorItem(QPointF pos = QPointF(), QString value = "10uF", CapacitorStyle style = NonPolarized, QGraphicsItem *parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override { return "Capacitor"; }
    ItemType itemType() const override { return SchematicItem::CapacitorType; }
    QString referencePrefix() const override { return "C"; }
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    // Connectivity
    QList<QPointF> connectionPoints() const override;
    QList<PinElectricalType> pinElectricalTypes() const override { return { PassivePin, PassivePin }; }

    // Properties
    QString value() const { return m_value; }
    void setValue(const QString& value);

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    QString m_value;
    CapacitorStyle m_style;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // CAPACITORITEM_H
