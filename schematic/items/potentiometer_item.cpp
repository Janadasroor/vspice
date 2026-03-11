#include "potentiometer_item.h"
#include "../../simulator/core/sim_value_parser.h"
#include <QPainter>
#include <QJsonObject>
#include <QGraphicsSceneMouseEvent>
#include <cmath>

PotentiometerItem::PotentiometerItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("RV1");
    setValue("10k");
    m_wiperPos = 0.5;
}

void PotentiometerItem::setWiperPosition(double pos) {
    m_wiperPos = std::clamp(pos, 0.0, 1.0);
    
    // Update simulation parameters if we have a total resistance value
    double totalR = 10000.0; // Default 10k
    bool ok = false;
    double parsed = 0.0;
    if (SimValueParser::parseSpiceNumber(m_value, parsed)) {
        totalR = parsed;
    }
    
    // Split resistance based on wiper position (with small minimum to avoid singular matrix)
    double r1 = std::max(0.001, totalR * (1.0 - m_wiperPos));
    double r2 = std::max(0.001, totalR * m_wiperPos);
    
    setParamExpression("r_upper", QString::number(r1));
    setParamExpression("r_lower", QString::number(r2));

    emit interactiveStateChanged();
    update();
}

void PotentiometerItem::onInteractivePress(const QPointF& pos) {
    // Determine if we clicked near the wiper knob
    QPointF local = mapFromScene(pos);
    if (QRectF(-10, -35, 20, 20).contains(local)) {
        m_isDraggingWiper = true;
    }
}

void PotentiometerItem::onInteractiveClick(const QPointF& pos) {
    // If just clicked, snap wiper to that X position
    QPointF local = mapFromScene(pos);
    if (local.x() >= -30 && local.x() <= 30) {
        setWiperPosition((local.x() + 30) / 60.0);
    }
}

QRectF PotentiometerItem::boundingRect() const {
    return QRectF(-45, -40, 90, 60);
}

void PotentiometerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(Qt::NoBrush);

    // Resistor Body (Zigzag)
    QPolygonF zigzag;
    zigzag << QPointF(-30, 0)
           << QPointF(-25, -10)
           << QPointF(-15, 10)
           << QPointF(-5, -10)
           << QPointF(5, 10)
           << QPointF(15, -10)
           << QPointF(25, 10)
           << QPointF(30, 0);
    painter->drawPolyline(zigzag);

    // Terminal pins
    painter->drawLine(-40, 0, -30, 0);
    painter->drawLine(30, 0, 40, 0);

    // Wiper Arrow
    double wx = -30 + (m_wiperPos * 60.0);
    painter->setPen(QPen(QColor(0, 255, 100), 2)); // Green wiper
    painter->drawLine(wx, -12, wx, -25);
    painter->drawLine(wx, -12, wx - 4, -18);
    painter->drawLine(wx, -12, wx + 4, -18);
    
    // Wiper pin line
    painter->drawLine(wx, -25, 0, -25);
    painter->drawLine(0, -25, 0, -35);

    // Knob handle
    painter->setBrush(m_isDraggingWiper ? QColor(0, 200, 80) : QColor(0, 100, 40));
    painter->drawEllipse(wx - 6, -30, 12, 12);

    painter->setPen(QPen(Qt::white, 1));
    painter->setFont(QFont("Inter", 6));
    painter->drawText(QRectF(-40, 10, 80, 10), Qt::AlignCenter, QString::number((int)(m_wiperPos * 100)) + "%");

    drawConnectionPointHighlights(painter);
}

QList<QPointF> PotentiometerItem::connectionPoints() const {
    return {
        QPointF(-40, 0),  // Pin 1 (A)
        QPointF(0, -35),  // Pin 2 (Wiper)
        QPointF(40, 0)    // Pin 3 (B)
    };
}

QString PotentiometerItem::pinName(int index) const {
    switch(index) {
        case 0: return "1";
        case 1: return "2";
        case 2: return "3";
        default: return QString::number(index+1);
    }
}

QJsonObject PotentiometerItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Potentiometer";
    j["wiper"] = m_wiperPos;
    return j;
}

bool PotentiometerItem::fromJson(const QJsonObject& j) {
    if (j["type"].toString() != "Potentiometer") return false;
    SchematicItem::fromJson(j);
    m_wiperPos = j["wiper"].toDouble(0.5);
    return true;
}

SchematicItem* PotentiometerItem::clone() const {
    PotentiometerItem* p = new PotentiometerItem(pos());
    p->setWiperPosition(m_wiperPos);
    return p;
}
