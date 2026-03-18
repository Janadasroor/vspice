#ifndef LEDITEM_H
#define LEDITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Real-time visual feedback LED component.
 */
class LEDItem : public SchematicItem {
public:
    LEDItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "LED"; }
    ItemType itemType() const override { return SchematicItem::DiodeType; }

    void setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& currents) override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    void setThreshold(double v) { m_threshold = v; }
    double threshold() const { return m_threshold; }

private:
    double m_voltage;
    double m_threshold = 1.5;
};

#endif // LEDITEM_H
