#ifndef SWITCHITEM_H
#define SWITCHITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Interactive SPICE-compatible switch component.
 */
class SwitchItem : public SchematicItem {
public:
    SwitchItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "Switch"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "SW"; }

    bool isInteractive() const override { return true; }
    void onInteractiveClick(const QPointF& pos) override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

private:
    bool m_isOpen;
};

#endif // SWITCHITEM_H
