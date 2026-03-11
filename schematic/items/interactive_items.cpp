#include "switch_item.h"
#include "led_item.h"
#include "signal_generator_item.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>

// --- SwitchItem Implementation ---
// ... (existing code kept) ...

// --- SignalGeneratorItem Implementation ---

SignalGeneratorItem::SignalGeneratorItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    m_freq = 1000.0;
    m_amplitude = 5.0;
    setReference("V?");
    updateSpiceValue();
}

void SignalGeneratorItem::updateSpiceValue() {
    // Standard SPICE Sine: SINE(offset amplitude freq)
    setValue(QString("SINE(0 %1 %2)").arg(m_amplitude).arg(m_freq));
}

void SignalGeneratorItem::onInteractiveClick(const QPointF&) {
    // Cycle frequency: 10Hz -> 100Hz -> 1kHz -> 10kHz
    if (m_freq < 100) m_freq = 100;
    else if (m_freq < 1000) m_freq = 1000;
    else if (m_freq < 10000) m_freq = 10000;
    else m_freq = 10;
    
    updateSpiceValue();
    update();
}

QRectF SignalGeneratorItem::boundingRect() const { return QRectF(-25, -25, 50, 50); }

void SignalGeneratorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 2));
    
    // Circle body
    painter->drawEllipse(-20, -20, 40, 40);
    
    // Sine wave icon inside
    QPainterPath path;
    path.moveTo(-12, 0);
    for(int i=-12; i<=12; ++i) {
        path.lineTo(i, -10 * std::sin(i * 0.25));
    }
    painter->drawPath(path);

    // Frequency label
    painter->setFont(QFont("Inter", 7));
    painter->drawText(boundingRect(), Qt::AlignBottom | Qt::AlignHCenter, QString::number(m_freq) + "Hz");
    
    drawConnectionPointHighlights(painter);
}

QList<QPointF> SignalGeneratorItem::connectionPoints() const {
    return { QPointF(0, -20), QPointF(0, 20) };
}

QJsonObject SignalGeneratorItem::toJson() const { 
    QJsonObject j; j["type"]="Signal Generator"; j["freq"]=m_freq; j["amp"]=m_amplitude; j["x"]=pos().x(); j["y"]=pos().y(); 
    return j; 
}
bool SignalGeneratorItem::fromJson(const QJsonObject& j) { 
    setPos(j["x"].toDouble(), j["y"].toDouble()); m_freq=j["freq"].toDouble(); m_amplitude=j["amp"].toDouble(); 
    updateSpiceValue(); return true; 
}
SchematicItem* SignalGeneratorItem::clone() const { return new SignalGeneratorItem(pos()); }

SwitchItem::SwitchItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    m_isOpen = true;
    setReference("SW?");
    setValue("1e12"); // Open resistance
}

void SwitchItem::onInteractiveClick(const QPointF&) {
    m_isOpen = !m_isOpen;
    setValue(m_isOpen ? "1e12" : "0.001");
    update();
}

QRectF SwitchItem::boundingRect() const { return QRectF(-20, -20, 40, 40); }

void SwitchItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 2));
    
    painter->drawEllipse(-17, -2, 4, 4);
    painter->drawEllipse(13, -2, 4, 4);

    if (m_isOpen) {
        painter->drawLine(-15, 0, 10, -15);
    } else {
        painter->drawLine(-15, 0, 15, 0);
    }
    
    drawConnectionPointHighlights(painter);
}

QList<QPointF> SwitchItem::connectionPoints() const {
    return { QPointF(-15, 0), QPointF(15, 0) };
}

QJsonObject SwitchItem::toJson() const { 
    QJsonObject j; j["type"]="Switch"; j["open"]=m_isOpen; j["x"]=pos().x(); j["y"]=pos().y(); 
    return j; 
}
bool SwitchItem::fromJson(const QJsonObject& j) { 
    setPos(j["x"].toDouble(), j["y"].toDouble()); m_isOpen=j["open"].toBool(); return true; 
}
SchematicItem* SwitchItem::clone() const { return new SwitchItem(pos()); }


// --- LEDItem Implementation ---

LEDItem::LEDItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("D?");
    setValue("RED");
    m_voltage = 0.0;
}

void LEDItem::setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>&) {
    double maxV = 0;
    for (auto v : nodeVoltages) maxV = std::max(maxV, v);
    m_voltage = maxV; 
    update();
}

QRectF LEDItem::boundingRect() const { return QRectF(-20, -20, 40, 40); }

void LEDItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    bool lit = m_voltage > 1.8;
    QColor ledColor = lit ? QColor(255, 50, 50) : QColor(60, 20, 20);

    painter->setPen(QPen(Qt::white, 2));
    painter->setBrush(Qt::NoBrush);
    
    QPolygonF poly;
    poly << QPointF(-10, -10) << QPointF(-10, 10) << QPointF(10, 0);
    painter->drawPolygon(poly);
    painter->drawLine(10, -10, 10, 10);

    if (lit) {
        QRadialGradient glow(0, 0, 20);
        glow.setColorAt(0, QColor(255, 0, 0, 150));
        glow.setColorAt(1, Qt::transparent);
        painter->setBrush(glow);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(-20, -20, 40, 40);
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LEDItem::connectionPoints() const {
    return { QPointF(-15, 0), QPointF(15, 0) };
}

QJsonObject LEDItem::toJson() const { return QJsonObject(); }
bool LEDItem::fromJson(const QJsonObject&) { return true; }
SchematicItem* LEDItem::clone() const { return new LEDItem(pos()); }
