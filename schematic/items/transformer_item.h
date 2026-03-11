#ifndef TRANSFORMERITEM_H
#define TRANSFORMERITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include <QBrush>
#include <QPen>
#include <memory>
#include <vector>

class TransformerItem : public SchematicItem {
public:
    TransformerItem(QPointF pos = QPointF(), QString value = "1:1", QGraphicsItem *parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override { return "Transformer"; }
    ItemType itemType() const override { return SchematicItem::TransformerType; }
    QString referencePrefix() const override { return "T"; }
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    
    // Connectivity
    QList<QPointF> connectionPoints() const override;

    // Properties
    QString value() const { return m_value; }
    void setValue(const QString& value);

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    QString m_value;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // TRANSFORMERITEM_H
