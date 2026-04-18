#include "oscilloscope_item.h"
#include "net_manager.h"
#include <QPainter>
#include <QJsonArray>
#include <QJsonObject>

OscilloscopeItem::OscilloscopeItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setExcludeFromPcb(true); // Instruments are excluded from PCB by default
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("OSC1");
    setValue("Oscilloscope");

    m_config.channels[0].color = Qt::yellow;
    m_config.channels[1].color = Qt::cyan;
    m_config.channels[2].color = Qt::magenta;
    m_config.channels[3].color = QColor(0, 255, 100);
}

QRectF OscilloscopeItem::boundingRect() const {
    return QRectF(-45, -75, 90, 150);
}

void OscilloscopeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Instrument Body (Professional Slate Gray)
    painter->setBrush(QColor(45, 45, 55));
    painter->setPen(QPen(Qt::white, 2));
    painter->drawRoundedRect(QRectF(-45, -75, 90, 150).adjusted(2, 2, -2, -2), 5, 5);

    // Title
    painter->setPen(QColor(0, 255, 100)); // Glowing green
    QFont titleFont = painter->font(); titleFont.setBold(true); titleFont.setPointSize(8);
    painter->setFont(titleFont);
    painter->drawText(QRectF(-45, -73, 90, 15), Qt::AlignCenter, "OSCILLOSCOPE");

    // Screen area
    painter->setBrush(QColor(10, 20, 10));
    painter->setPen(QPen(Qt::white, 1));
    painter->drawRect(-30, -50, 60, 50);
    
    // Fake grid on screen
    painter->setPen(QPen(QColor(30, 50, 30), 1));
    for(int i=-20; i<=20; i+=10) painter->drawLine(i, -50, i, 0);
    for(int i=-40; i<=-10; i+=10) painter->drawLine(-30, i, 30, i);

    // Channel Labels and Pins
    painter->setPen(Qt::white);
    QFont f = painter->font(); f.setPointSize(7); painter->setFont(f);
    
    // Labels centered on Y=15, 30, 45, 60
    painter->drawText(QRectF(-40, 7.5, 30, 15), Qt::AlignLeft, "CH1");
    painter->drawText(QRectF(-40, 22.5, 30, 15), Qt::AlignLeft, "CH2");
    painter->drawText(QRectF(-40, 37.5, 30, 15), Qt::AlignLeft, "CH3");
    painter->drawText(QRectF(-40, 52.5, 30, 15), Qt::AlignLeft, "CH4");

    // Pin connection lines (now aligned to grid)
    painter->drawLine(-45, 15, -35, 15);
    painter->drawLine(-45, 30, -35, 30);
    painter->drawLine(-45, 45, -35, 45);
    painter->drawLine(-45, 60, -35, 60);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> OscilloscopeItem::connectionPoints() const {
    return {
        QPointF(-45, 15), // CH1
        QPointF(-45, 30), // CH2
        QPointF(-45, 45), // CH3
        QPointF(-45, 60)  // CH4
    };
}

QJsonObject OscilloscopeItem::toJson() const {
    QJsonObject j;
    j["type"] = itemTypeName();
    j["id"] = id().toString();
    j["x"] = pos().x();
    j["y"] = pos().y();
    j["reference"] = reference();
    
    QJsonArray chs;
    for (int i = 0; i < 4; ++i) {
        QJsonObject c;
        c["enabled"] = m_config.channels[i].enabled;
        c["scale"] = m_config.channels[i].scale;
        c["offset"] = m_config.channels[i].offset;
        c["color"] = m_config.channels[i].color.name();
        chs.append(c);
    }
    j["channels"] = chs;
    j["timebase"] = m_config.timebase;
    j["triggerSource"] = m_config.triggerSource;
    j["triggerLevel"] = m_config.triggerLevel;
    
    return j;
}

bool OscilloscopeItem::fromJson(const QJsonObject& j) {
    if (j["type"].toString() != itemTypeName()) return false;
    
    setId(QUuid::fromString(j["id"].toString()));
    setPos(j["x"].toDouble(), j["y"].toDouble());
    setReference(j["reference"].toString());
    
    if (j.contains("channels")) {
        QJsonArray chs = j["channels"].toArray();
        for (int i = 0; i < 4 && i < chs.size(); ++i) {
            QJsonObject c = chs[i].toObject();
            m_config.channels[i].enabled = c["enabled"].toBool();
            m_config.channels[i].scale = c["scale"].toDouble();
            m_config.channels[i].offset = c["offset"].toDouble();
            m_config.channels[i].color = QColor(c["color"].toString());
        }
    }
    
    if (j.contains("timebase")) m_config.timebase = j["timebase"].toDouble();
    if (j.contains("triggerSource")) m_config.triggerSource = j["triggerSource"].toString();
    if (j.contains("triggerLevel")) m_config.triggerLevel = j["triggerLevel"].toDouble();
    
    update();
    return true;
}

SchematicItem* OscilloscopeItem::clone() const {
    return new OscilloscopeItem(pos());
}

QString OscilloscopeItem::channelNet(int chIdx) const {
    if (chIdx < 0 || chIdx >= 4) return QString();
    
    // Coordinates from connectionPoints()
    QPointF p;
    switch(chIdx) {
        case 0: p = QPointF(-45, 15); break;
        case 1: p = QPointF(-45, 30); break;
        case 2: p = QPointF(-45, 45); break;
        case 3: p = QPointF(-45, 60); break;
    }
    
    // Map to scene to find connections via NetManager
    QPointF scenePt = mapToScene(p);
    
    // Note: This requires access to the scene's net manager.
    // OscilloscopeItem usually doesn't have it directly, but we can find it through the scene.
    // If we can't find it, we return a placeholder.
    return QString("Node_%1_%2").arg(reference()).arg(chIdx + 1); // Fallback
}
void OscilloscopeItem::configChanged() {}
