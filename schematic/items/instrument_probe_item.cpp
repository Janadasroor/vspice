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
        return QRectF(-40, -60, 80, 140);
    }
    return QRectF(-40, -30, 80, 70);
}

QList<QPointF> InstrumentProbeItem::connectionPoints() const {
    if (m_kind == Kind::Oscilloscope) {
        return {
            QPointF(-40, 17), // CH1
            QPointF(-40, 37), // CH2
            QPointF(-40, 57), // CH3
            QPointF(-40, 77)  // CH4
        };
    }

    // Two-terminal instrument symbol.
    return {
        QPointF(-40, 5),  // pin 1
        QPointF(40, 5)    // pin 2
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
        painter->drawRect(-30, -40, 60, 50);

        painter->setPen(QPen(QColor(30, 50, 30), 1));
        for (int i = -20; i <= 20; i += 10) painter->drawLine(i, -50, i, 0);
        for (int i = -40; i <= -10; i += 10) painter->drawLine(-30, i, 30, i);

        painter->setPen(Qt::white);
        QFont f = painter->font();
        f.setPointSize(7);
        painter->setFont(f);
        painter->drawText(QRectF(-35, 10, 30, 15), Qt::AlignLeft, "CH1");
        painter->drawText(QRectF(-35, 30, 30, 15), Qt::AlignLeft, "CH2");
        painter->drawText(QRectF(-35, 50, 30, 15), Qt::AlignLeft, "CH3");
        painter->drawText(QRectF(-35, 70, 30, 15), Qt::AlignLeft, "CH4");
        painter->drawLine(-40, 17, -30, 17);
        painter->drawLine(-40, 37, -30, 37);
        painter->drawLine(-40, 57, -30, 57);
        painter->drawLine(-40, 77, -30, 77);
    } else {
        painter->setPen(QPen(Qt::white, 1));
        painter->drawLine(-40, 5, -26, 5);
        painter->drawLine(26, 5, 40, 5);
        painter->drawEllipse(QRectF(-26, -10, 52, 30));

        QFont mono = painter->font();
        mono.setPointSize(9);
        mono.setBold(true);
        painter->setFont(mono);
        painter->setPen(QColor(255, 220, 120));
        QString glyph = "M";
        if (m_kind == Kind::FrequencyCounter) glyph = "Hz";
        if (m_kind == Kind::LogicProbe) glyph = "L";
        painter->drawText(QRectF(-20, -2, 40, 16), Qt::AlignCenter, glyph);
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
