#ifndef POTENTIOMETERITEM_H
#define POTENTIOMETERITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Interactive Potentiometer component (3-pin).
 * Pins: 1 (Terminal A), 2 (Wiper), 3 (Terminal B).
 */
class PotentiometerItem : public SchematicItem {
public:
    PotentiometerItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "Potentiometer"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "RV"; }

    bool isInteractive() const override { return true; }
    void onInteractivePress(const QPointF& pos) override;
    void onInteractiveClick(const QPointF& pos) override;

    // Custom properties
    double wiperPosition() const { return m_wiperPos; }
    void setWiperPosition(double pos);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;
    QString pinName(int index) const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

private:
    double m_wiperPos = 0.5; // 0.0 to 1.0
    bool m_isDraggingWiper = false;
};

#endif // POTENTIOMETERITEM_H
