#ifndef WIREITEM_H
#define WIREITEM_H

#include "schematic_item.h"
#include <QPointF>
#include <QList>
#include <QPen>

class WireItem : public SchematicItem {
public:
    enum WireType {
        SignalWire,     // Thin, crisp signal lines
        PowerWire       // Thicker, color-coded power lines (VCC/GND)
    };

    WireItem(QPointF start = QPointF(), QPointF end = QPointF(), QGraphicsItem *parent = nullptr);
    WireItem(WireType type, QPointF start = QPointF(), QPointF end = QPointF(), QGraphicsItem *parent = nullptr);

    // SchematicItem interface
    QString itemTypeName() const override { return "Wire"; }
    ItemType itemType() const override { return SchematicItem::WireType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    // Wire-specific methods
    void setStartPoint(const QPointF& point);
    void setEndPoint(const QPointF& point);
    QPointF startPoint() const { return m_points.first(); }
    QPointF endPoint() const { return m_points.last(); }
    void addSegment(const QPointF& point);
    void removeLastSegment();
    void setPoints(const QList<QPointF>& points);
    QList<QPointF> points() const { return m_points; }
    QList<QPointF> connectionPoints() const override;

    void setPen(const QPen& pen) { m_pen = pen; update(); }
    QPen pen() const { return m_pen; }

    // Wire type management
    void setWireType(WireType type) { m_wireType = type; update(); }
    WireType wireType() const { return m_wireType; }

    // Junction management
    void addJunction(const QPointF& point);
    void removeJunction(const QPointF& point);
    void clearJunctions() { m_junctions.clear(); update(); }
    QList<QPointF> junctions() const { return m_junctions; }

    // Jump-over management
    struct JumpOver {
        QPointF position;
        qreal radius;
        bool isVertical; // true for vertical jump, false for horizontal
    };
    void addJumpOver(const JumpOver& jump);
    void clearJumpOvers() { m_jumpOvers.clear(); update(); }
    QList<JumpOver> jumpOvers() const { return m_jumpOvers; }

    void setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents) override;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void updatePen();

    QList<QPointF> m_points;
    QPen m_pen;
    WireType m_wireType;
    QList<QPointF> m_junctions;
    QList<JumpOver> m_jumpOvers;
    double m_voltage = 0.0;
    bool m_hasVoltage = false;
};

#endif // WIREITEM_H
