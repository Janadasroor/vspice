#include "signal_generator_item.h"
#include <QPainter>
#include <QJsonObject>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <cmath>

SignalGeneratorItem::SignalGeneratorItem(QPointF pos, QGraphicsItem *parent)
    : SchematicItem(parent), m_waveform(Sine), m_freq(1000.0), m_amplitude(5.0), m_offset(0.0), m_acMagnitude(1.0), m_acPhase(0.0) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("VGEN1");
    updateSpiceValue();
}

QRectF SignalGeneratorItem::boundingRect() const {
    return QRectF(-30, -30, 60, 60);
}

void SignalGeneratorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);
    
    // Body Circle
    QPen pen(Qt::white, 2);
    if (isSelected()) pen.setColor(QColor(0, 120, 255));
    painter->setPen(pen);
    painter->setBrush(QColor(40, 40, 45));
    painter->drawEllipse(QRectF(-20, -20, 40, 40));

    // Waveform symbol inside
    painter->setPen(QPen(Qt::cyan, 1.5));
    if (m_waveform == Sine) {
        QPainterPath sinePath;
        for (int i = -12; i <= 12; ++i) {
            double x = i;
            double y = -8.0 * std::sin(x * 0.25);
            if (i == -12) sinePath.moveTo(x, y);
            else sinePath.lineTo(x, y);
        }
        painter->drawPath(sinePath);
    } else if (m_waveform == Square) {
        painter->drawPolyline(QPolygonF() << QPointF(-12, 8) << QPointF(-12, -8) << QPointF(0, -8) << QPointF(0, 8) << QPointF(12, 8) << QPointF(12, -8));
    } else if (m_waveform == Triangle) {
        painter->drawPolyline(QPolygonF() << QPointF(-12, 8) << QPointF(0, -8) << QPointF(12, 8));
    } else if (m_waveform == Pulse) {
        painter->drawPolyline(QPolygonF() << QPointF(-12, 8) << QPointF(-12, -8) << QPointF(-4, -8) << QPointF(-4, 8) << QPointF(12, 8));
    } else if (m_waveform == DC) {
        painter->drawLine(-12, 0, 12, 0);
        painter->drawLine(-12, -4, 12, -4);
    }

    // Connections
    painter->setPen(QPen(Qt::white, 2));
    painter->drawLine(0, -20, 0, -30);
    painter->drawLine(0, 20, 0, 30);

    // Text label
    painter->setFont(QFont("Inter", 6));
    painter->setPen(Qt::lightGray);
    QString info = QString("%1Hz").arg(m_freq >= 1000 ? QString::number(m_freq/1000.0) + "k" : QString::number(m_freq));
    painter->drawText(QRectF(-30, 20, 60, 15), Qt::AlignCenter, info);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> SignalGeneratorItem::connectionPoints() const {
    return { QPointF(0, -30), QPointF(0, 30) };
}

void SignalGeneratorItem::onInteractiveClick(const QPointF& scenePos) {
    // Cycle waveform
    if (m_waveform == Sine) m_waveform = Square;
    else if (m_waveform == Square) m_waveform = Triangle;
    else if (m_waveform == Triangle) m_waveform = Pulse;
    else if (m_waveform == Pulse) m_waveform = DC;
    else m_waveform = Sine;
    
    updateSpiceValue();
    update();
}

QJsonObject SignalGeneratorItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Signal Generator";
    j["waveform"] = static_cast<int>(m_waveform);
    j["freq"] = m_freq;
    j["amplitude"] = m_amplitude;
    j["offset"] = m_offset;
    j["acMagnitude"] = m_acMagnitude;
    j["acPhase"] = m_acPhase;
    return j;
}

bool SignalGeneratorItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    if (json.contains("waveform")) m_waveform = static_cast<WaveformType>(json["waveform"].toInt());
    if (json.contains("freq")) m_freq = json["freq"].toDouble();
    if (json.contains("amplitude")) m_amplitude = json["amplitude"].toDouble();
    if (json.contains("offset")) m_offset = json["offset"].toDouble();
    if (json.contains("acMagnitude")) m_acMagnitude = json["acMagnitude"].toDouble();
    if (json.contains("acPhase")) m_acPhase = json["acPhase"].toDouble();
    updateSpiceValue();
    return true;
}

SchematicItem* SignalGeneratorItem::clone() const {
    auto* item = new SignalGeneratorItem(pos());
    item->m_waveform = m_waveform;
    item->m_freq = m_freq;
    item->m_amplitude = m_amplitude;
    item->m_offset = m_offset;
    item->m_acMagnitude = m_acMagnitude;
    item->m_acPhase = m_acPhase;
    item->updateSpiceValue();
    return item;
}

void SignalGeneratorItem::setWaveform(WaveformType type) { m_waveform = type; updateSpiceValue(); update(); }
void SignalGeneratorItem::setFrequency(double f) { m_freq = f; updateSpiceValue(); update(); }
void SignalGeneratorItem::setAmplitude(double a) { m_amplitude = a; updateSpiceValue(); update(); }
void SignalGeneratorItem::setOffset(double o) { m_offset = o; updateSpiceValue(); update(); }
void SignalGeneratorItem::setAcMagnitude(double m) { m_acMagnitude = m; updateSpiceValue(); update(); }
void SignalGeneratorItem::setAcPhase(double p) { m_acPhase = p; updateSpiceValue(); update(); }

void SignalGeneratorItem::updateSpiceValue() {
    QString spice;
    if (m_waveform == Sine) {
        spice = QString("SINE(%1 %2 %3)").arg(m_offset).arg(m_amplitude).arg(m_freq);
    } else if (m_waveform == Square) {
        double per = 1.0 / m_freq;
        spice = QString("PULSE(%1 %2 0 1n 1n %3 %4)").arg(m_offset - m_amplitude).arg(m_offset + m_amplitude).arg(per*0.5).arg(per);
    } else if (m_waveform == Triangle) {
        double per = 1.0 / m_freq;
        spice = QString("PULSE(%1 %2 0 %3 %4 1n %5)").arg(m_offset - m_amplitude).arg(m_offset + m_amplitude).arg(per*0.5).arg(per*0.5).arg(per);
    } else if (m_waveform == Pulse) {
        double per = 1.0 / m_freq;
        spice = QString("PULSE(%1 %2 0 1n 1n %3 %4)").arg(m_offset - m_amplitude).arg(m_offset + m_amplitude).arg(per*0.1).arg(per);
    } else if (m_waveform == DC) {
        spice = QString::number(m_offset);
    }

    if (m_acMagnitude > 0) {
        spice += QString(" AC %1 %2").arg(m_acMagnitude).arg(m_acPhase);
    }
    
    setValue(spice);
}
