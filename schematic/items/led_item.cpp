#include "led_item.h"
#include <QPainter>
#include <QJsonObject>

LEDItem::LEDItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("D1");
    setValue("RED");
    m_voltage = 0.0;
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

    bool lit = m_voltage > 1.5;
    double brightness = std::clamp((m_voltage - 1.5) / 0.7, 0.0, 1.0); // 1.5V to 2.2V scaling

    // 2. Draw Bloom/Halo (Proteus-style emission)
    if (lit) {
        // Core Halo
        QRadialGradient coreHalo(0, 0, 25);
        QColor c1 = baseColor;
        c1.setAlpha(static_cast<int>(120 * brightness));
        coreHalo.setColorAt(0, c1);
        coreHalo.setColorAt(0.4, c1);
        coreHalo.setColorAt(1, Qt::transparent);
        painter->setBrush(coreHalo);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(-25, -25, 50, 50);

        // Outer Glow (faint)
        QRadialGradient outerGlow(0, 0, 45);
        QColor c2 = baseColor;
        c2.setAlpha(static_cast<int>(40 * brightness));
        outerGlow.setColorAt(0, c2);
        outerGlow.setColorAt(1, Qt::transparent);
        painter->setBrush(outerGlow);
        painter->drawEllipse(-45, -45, 90, 90);
    }

    // 3. Draw 3D Lens Body
    QRectF lensRect(-12, -12, 24, 24);
    QRadialGradient lensGrad(-4, -4, 12);
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
        painter->drawEllipse(-7, -7, 6, 4);
    } else {
        painter->setBrush(QColor(255, 255, 255, 40));
        painter->drawEllipse(-7, -7, 5, 3);
    }

    // 4. Draw Diode Symbol (Overlay)
    painter->setPen(QPen(lit ? Qt::white : QColor(100, 100, 100), 1.5));
    painter->setBrush(Qt::NoBrush);
    
    QPolygonF poly;
    poly << QPointF(-6, -6) << QPointF(-6, 6) << QPointF(6, 0);
    painter->drawPolygon(poly);
    painter->drawLine(6, -6, 6, 6);
    
    // Emission Rays (Small visual cues)
    if (lit) {
        painter->setPen(QPen(baseColor.lighter(120), 1));
        painter->drawLine(8, -8, 14, -14);
        painter->drawLine(11, -5, 17, -11);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LEDItem::connectionPoints() const {
    return { QPointF(-15, 0), QPointF(15, 0) };
}

QJsonObject LEDItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "LED";
    return j;
}

bool LEDItem::fromJson(const QJsonObject& j) {
    return SchematicItem::fromJson(j);
}

SchematicItem* LEDItem::clone() const {
    return new LEDItem(pos());
}
