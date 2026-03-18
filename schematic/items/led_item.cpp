#include "led_item.h"
#include <QPainter>
#include <QJsonObject>

LEDItem::LEDItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("D1");
    setValue("RED");
    m_voltage = 0.0;
    m_threshold = 1.5;
}

void LEDItem::setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>&) {
    QString nAnode = pinNet(0);
    QString nCathode = pinNet(1);
    
    double vAnode = nodeVoltages.value(nAnode, 0.0);
    double vCathode = nodeVoltages.value(nCathode, 0.0);
    
    // Auto-detect node name if not explicitly stored in pinNet
    // This handles the case where buildNetlist flattens the hierarchy
    if (nAnode.isEmpty()) vAnode = nodeVoltages.value(m_reference + ".1", 0.0);
    if (nCathode.isEmpty()) vCathode = nodeVoltages.value(m_reference + ".2", 0.0);

    m_voltage = vAnode - vCathode;
    update();
}

QRectF LEDItem::boundingRect() const { return QRectF(-50, -50, 100, 100); }

void LEDItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    // 1. Determine base color and state
    QColor baseColor = Qt::red;
    QString colorVal = m_value.toUpper();
    if (colorVal.contains("GREEN")) baseColor = Qt::green;
    else if (colorVal.contains("BLUE")) baseColor = Qt::blue;
    else if (colorVal.contains("YELLOW")) baseColor = QColor(255, 255, 0);

    bool lit = m_voltage > m_threshold;
    double brightness = std::clamp((m_voltage - m_threshold) / 0.7, 0.0, 1.0);

    // 1.5 Draw leads
    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawLine(-45, 0, -20, 0);
    painter->drawLine(20, 0, 45, 0);

    // 2. Draw Bloom/Halo (Proteus-style emission)
    if (lit) {
        // Core Halo
        QRadialGradient coreHalo(0, 0, 22);
        QColor c1 = baseColor;
        c1.setAlpha(static_cast<int>(120 * brightness));
        coreHalo.setColorAt(0, c1);
        coreHalo.setColorAt(0.4, c1);
        coreHalo.setColorAt(1, Qt::transparent);
        painter->setBrush(coreHalo);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(-22, -22, 44, 44);

        // Outer Glow (faint)
        QRadialGradient outerGlow(0, 0, 38);
        QColor c2 = baseColor;
        c2.setAlpha(static_cast<int>(40 * brightness));
        outerGlow.setColorAt(0, c2);
        outerGlow.setColorAt(1, Qt::transparent);
        painter->setBrush(outerGlow);
        painter->drawEllipse(-38, -38, 76, 76);
    }

    // 3. Draw 3D Lens Body
    QRectF lensRect(-10, -10, 20, 20);
    QRadialGradient lensGrad(-3, -3, 10);
    if (lit) {
        lensGrad.setColorAt(0, baseColor.lighter(150));
        lensGrad.setColorAt(1, baseColor);
    } else {
        lensGrad.setColorAt(0, baseColor.darker(200));
        lensGrad.setColorAt(1, baseColor.darker(400));
    }
    painter->setBrush(lensGrad);
    painter->setPen(QPen(Qt::black, 0.5));
    painter->drawEllipse(lensRect);

    // Specular Highlight (Lens Reflection)
    if (lit) {
        painter->setBrush(QColor(255, 255, 255, 180));
        painter->drawEllipse(-6, -6, 5, 4);
    } else {
        painter->setBrush(QColor(255, 255, 255, 40));
        painter->drawEllipse(-6, -6, 4, 3);
    }

    // 4. Draw Diode Symbol (Overlay)
    painter->setPen(QPen(lit ? Qt::white : QColor(100, 100, 100), 1.5));
    painter->setBrush(Qt::NoBrush);
    
    QPolygonF poly;
    poly << QPointF(-8, -8) << QPointF(-8, 8) << QPointF(8, 0);
    painter->drawPolygon(poly);
    painter->drawLine(8, -8, 8, 8);
    
    // Emission Rays (Small visual cues)
    if (lit) {
        painter->setPen(QPen(baseColor.lighter(120), 1));
        painter->drawLine(10, -10, 16, -16);
        painter->drawLine(13, -7, 19, -13);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LEDItem::connectionPoints() const {
    return { QPointF(-45, 0), QPointF(45, 0) };
}

QJsonObject LEDItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "LED";
    j["threshold"] = m_threshold;
    return j;
}

bool LEDItem::fromJson(const QJsonObject& j) {
    if (!SchematicItem::fromJson(j)) return false;
    if (j.contains("threshold")) m_threshold = j["threshold"].toDouble(1.5);
    return true;
}

SchematicItem* LEDItem::clone() const {
    auto* item = new LEDItem(pos());
    item->fromJson(this->toJson());
    return item;
}
