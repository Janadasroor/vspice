#ifndef BUSITEM_H
#define BUSITEM_H

#include "schematic_item.h"
#include <QPointF>
#include <QList>
#include <QPen>

class BusItem : public SchematicItem {
public:
    BusItem(QPointF start = QPointF(), QPointF end = QPointF(), QGraphicsItem *parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override { return "Bus"; }
    ItemType itemType() const override { return SchematicItem::BusType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    // Bus-specific methods
    void setStartPoint(const QPointF& point);
    void setEndPoint(const QPointF& point);
    QPointF startPoint() const { return m_points.isEmpty() ? QPointF() : m_points.first(); }
    QPointF endPoint() const { return m_points.isEmpty() ? QPointF() : m_points.last(); }
    void addSegment(const QPointF& point);
    void setPoints(const QList<QPointF>& points);
    QList<QPointF> points() const { return m_points; }
    QList<QPointF> connectionPoints() const override { return m_points; }
    
    void setPen(const QPen& pen) { m_pen = pen; update(); }
    QPen pen() const { return m_pen; }

    void setNetName(const QString& name) { setValue(name); }
    QString netName() const { return value(); }

    void addJunction(const QPointF& point);
    void clearJunctions() { m_junctions.clear(); update(); }
    QList<QPointF> junctions() const { return m_junctions; }

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void updatePen();
    QList<QPointF> m_points;
    QList<QPointF> m_junctions;
    QPen m_pen;
};

#endif // BUSITEM_H
