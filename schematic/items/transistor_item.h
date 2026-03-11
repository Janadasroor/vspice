#ifndef TRANSISTORITEM_H
#define TRANSISTORITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QPen>
#include <QBrush>
#include <memory>
#include <vector>

class TransistorItem : public SchematicItem {
public:
    enum TransistorType { NPN, PNP, NMOS, PMOS };
    
    TransistorItem(QPointF pos = QPointF(), QString value = "2N2222", 
                   TransistorType type = NPN, QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "Transistor"; }
    ItemType itemType() const override { return SchematicItem::TransistorType; }
    QString referencePrefix() const override { return "Q"; }
    QString pinName(int index) const override;
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    // Connectivity
    QList<QPointF> connectionPoints() const override;
    QList<PinElectricalType> pinElectricalTypes() const override { return { PassivePin, PassivePin, PassivePin }; }

    QString value() const { return m_value; }
    void setValue(const QString& value);
    
    TransistorType transistorType() const { return m_transistorType; }
    void setTransistorType(TransistorType type);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    QString m_value;
    TransistorType m_transistorType;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // TRANSISTORITEM_H
