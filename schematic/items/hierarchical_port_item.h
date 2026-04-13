#ifndef HIERARCHICAL_PORT_ITEM_H
#define HIERARCHICAL_PORT_ITEM_H

#include "schematic_item.h"
#include <QFont>
#include <QPen>

class HierarchicalPortItem : public SchematicItem {
public:
    enum PortType {
        Input,
        Output,
        Bidirectional,
        Passive
    };

    HierarchicalPortItem(QPointF pos = QPointF(), const QString& label = "PORT", PortType type = Input, QGraphicsItem* parent = nullptr);
    
    // SchematicItem interface
    QString itemTypeName() const override { return "Hierarchical Port"; }
    ItemType itemType() const override { return HierarchicalPortType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    
    void setLabel(const QString& label) { setValue(label); }
    QString label() const { return value(); }
    
    void setPortType(PortType type);
    PortType portType() const { return m_portType; }
    
    QPainterPath shape() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QList<QPointF> connectionPoints() const override;
    bool supportsTransformAction(TransformAction action) const override {
        return action == TransformAction::RotateCW || action == TransformAction::RotateCCW;
    }

private:
    PortType m_portType;
    QFont m_font;
    QPen m_pen;
};

#endif // HIERARCHICAL_PORT_ITEM_H
