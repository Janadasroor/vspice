#ifndef PUSHBUTTONITEM_H
#define PUSHBUTTONITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Interactive momentary push button component.
 */
class PushButtonItem : public SchematicItem {
public:
    PushButtonItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "PushButton"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "SW"; }

    bool isInteractive() const override { return true; }
    void onInteractivePress(const QPointF& pos) override;
    void onInteractiveRelease(const QPointF& pos) override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

private:
    bool m_isPressed;
};

#endif // PUSHBUTTONITEM_H
