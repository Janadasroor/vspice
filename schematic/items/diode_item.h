#ifndef DIODEITEM_H
#define DIODEITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QPen>
#include <QBrush>
#include <memory>
#include <vector>

class DiodeItem : public SchematicItem {
public:
    DiodeItem(QPointF pos = QPointF(), QString value = "1N4148", QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "Diode"; }
    ItemType itemType() const override { return SchematicItem::DiodeType; }
    QString referencePrefix() const override { return "D"; }
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    // Connectivity
    QList<QPointF> connectionPoints() const override;
    QList<PinElectricalType> pinElectricalTypes() const override { return { PassivePin, PassivePin }; }

    QString value() const { return m_value; }
    void setValue(const QString& value);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    QString m_value;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // DIODEITEM_H
