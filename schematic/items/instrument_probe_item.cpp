#include "instrument_probe_item.h"

#include <QPainter>

InstrumentProbeItem::InstrumentProbeItem(Kind kind, QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_kind(kind) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference(referencePrefix() + "1");
    setValue(displayName());
}

QString InstrumentProbeItem::itemTypeName() const {
    return schemaTypeName();
}

QString InstrumentProbeItem::referencePrefix() const {
    switch (m_kind) {
    case Kind::Oscilloscope: return "OSC";
    case Kind::Voltmeter: return "VM";
    case Kind::Ammeter: return "AM";
    case Kind::Wattmeter: return "WM";
    case Kind::FrequencyCounter: return "FC";
    case Kind::LogicProbe: return "LP";
    }
    return "INS";
}

QString InstrumentProbeItem::displayName() const {
    switch (m_kind) {
    case Kind::Oscilloscope: return "Oscilloscope";
    case Kind::Voltmeter: return "Voltmeter";
    case Kind::Ammeter: return "Ammeter";
    case Kind::Wattmeter: return "Wattmeter";
    case Kind::FrequencyCounter: return "Frequency Counter";
    case Kind::LogicProbe: return "Logic Probe";
    }
    return "Instrument";
}

QString InstrumentProbeItem::schemaTypeName() const {
    switch (m_kind) {
    case Kind::Oscilloscope: return "OscilloscopeInstrument";
    case Kind::Voltmeter: return "VoltmeterInstrument";
    case Kind::Ammeter: return "AmmeterInstrument";
    case Kind::Wattmeter: return "WattmeterInstrument";
    case Kind::FrequencyCounter: return "FrequencyCounterInstrument";
    case Kind::LogicProbe: return "LogicProbeInstrument";
    }
    return "InstrumentProbe";
}

QRectF InstrumentProbeItem::boundingRect() const {
    if (m_kind == Kind::Oscilloscope) {
        return QRectF(-45, -75, 90, 150);
    }
    // Standard 2-terminal instrument
    return QRectF(-45, -30, 90, 75);
}

QList<QPointF> InstrumentProbeItem::connectionPoints() const {
    if (m_kind == Kind::Oscilloscope) {
        return {
            QPointF(-45, 15), // CH1
            QPointF(-45, 30), // CH2
            QPointF(-45, 45), // CH3
            QPointF(-45, 60)  // CH4
        };
    }

    // Two-terminal instrument symbol.
    return {
        QPointF(-45, 15),  // pin 1
        QPointF(45, 15)    // pin 2
    };
}

void InstrumentProbeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    const QRectF body = boundingRect().adjusted(2, 2, -2, -2);
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    painter->drawRoundedRect(body, 5, 5);

    painter->setPen(QColor(0, 255, 100));
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSize(8);
    painter->setFont(titleFont);

    const QString title = displayName().toUpper();
    painter->drawText(QRectF(body.left(), body.top() + 2, body.width(), 16), Qt::AlignCenter, title);

    if (m_kind == Kind::Oscilloscope) {
        painter->setBrush(QColor(10, 20, 10));
        painter->setPen(QPen(Qt::white, 1));
        painter->drawRect(-30, -50, 60, 50);

        painter->setPen(QPen(QColor(30, 50, 30), 1));
        for (int i = -20; i <= 20; i += 10) painter->drawLine(i, -50, i, 0);
        for (int i = -40; i <= -10; i += 10) painter->drawLine(-30, i, 30, i);

        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPointSize(7);
        painter->setFont(f);
        
        // Labels centered on Y=15, 30, 45, 60
        painter->drawText(QRectF(-40, 7.5, 30, 15), Qt::AlignLeft, "CH1");
        painter->drawText(QRectF(-40, 22.5, 30, 15), Qt::AlignLeft, "CH2");
        painter->drawText(QRectF(-40, 37.5, 30, 15), Qt::AlignLeft, "CH3");
        painter->drawText(QRectF(-40, 52.5, 30, 15), Qt::AlignLeft, "CH4");
        
        // Pin lines
        painter->drawLine(-45, 15, -35, 15);
        painter->drawLine(-45, 30, -35, 30);
        painter->drawLine(-45, 45, -35, 45);
        painter->drawLine(-45, 60, -35, 60);
    } else {
        painter->setPen(QPen(Qt::white, 1));
        painter->drawLine(-45, 15, -26, 15);
        painter->drawLine(26, 15, 45, 15);
        painter->drawEllipse(QRectF(-26, 0, 52, 30));

        QFont mono = painter->font();
        mono.setPointSize(9);
        mono.setBold(true);
        painter->setFont(mono);
        painter->setPen(QColor(255, 220, 120));
        QString glyph = "M";
        if (m_kind == Kind::FrequencyCounter) glyph = "Hz";
        if (m_kind == Kind::LogicProbe) glyph = "L";
        painter->drawText(QRectF(-20, 8, 40, 16), Qt::AlignCenter, glyph);
    }

    drawConnectionPointHighlights(painter);
}

QJsonObject InstrumentProbeItem::toJson() const {
    QJsonObject j;
    if (m_kind == Kind::Oscilloscope) j["type"] = "Oscilloscope Instrument";
    else if (m_kind == Kind::Voltmeter) j["type"] = "Voltmeter (DC)";
    else if (m_kind == Kind::Ammeter) j["type"] = "Ammeter (DC)";
    else if (m_kind == Kind::Wattmeter) j["type"] = "Wattmeter";
    else if (m_kind == Kind::FrequencyCounter) j["type"] = "Frequency Counter";
    else j["type"] = "Logic Probe";
    j["x"] = pos().x();
    j["y"] = pos().y();
    return j;
}

bool InstrumentProbeItem::fromJson(const QJsonObject& json) {
    setPos(json["x"].toDouble(), json["y"].toDouble());
    return true;
}

SchematicItem* InstrumentProbeItem::clone() const {
    return new InstrumentProbeItem(m_kind, pos());
}
