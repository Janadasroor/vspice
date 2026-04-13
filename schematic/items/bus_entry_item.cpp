#include "bus_entry_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>

BusEntryItem::BusEntryItem(QPointF pos, bool flipped, QGraphicsItem *parent)
    : SchematicItem(parent), m_flipped(flipped) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    
    QColor color = Qt::blue;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->schematicBus();
        if (color == Qt::transparent) color = Qt::blue;
    }
    m_pen = QPen(color, 2, Qt::SolidLine, Qt::RoundCap);
}

QRectF BusEntryItem::boundingRect() const {
    return QRectF(-12, -12, 24, 24);
}

void BusEntryItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPen p = m_pen;
    if (isSelected()) {
        p.setColor(Qt::yellow);
    }
    painter->setPen(p);
    
    if (m_flipped) {
        painter->drawLine(QPointF(-10, 10), QPointF(10, -10));
    } else {
        painter->drawLine(QPointF(-10, -10), QPointF(10, 10));
    }
    
    drawConnectionPointHighlights(painter);
}

QList<QPointF> BusEntryItem::connectionPoints() const {
    if (m_flipped) {
         return { QPointF(-10, 10), QPointF(10, -10) };
    } else {
         return { QPointF(-10, -10), QPointF(10, 10) };
    }
}

QJsonObject BusEntryItem::toJson() const {
    QJsonObject json;
    json["type"] = "BusEntry";
    json["id"] = m_id.toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["rotation"] = rotation();
    json["isMirroredX"] = isMirroredX();
    json["isMirroredY"] = isMirroredY();
    json["flipped"] = m_flipped;
    return json;
}

bool BusEntryItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setRotation(json["rotation"].toDouble(0.0));
    setMirroredX(json["isMirroredX"].toBool(false));
    setMirroredY(json["isMirroredY"].toBool(false));
    m_flipped = json["flipped"].toBool();
    update();
    return true;
}

SchematicItem* BusEntryItem::clone() const {
    auto* item = new BusEntryItem(pos(), m_flipped);
    item->setRotation(rotation());
    item->setMirroredX(isMirroredX());
    item->setMirroredY(isMirroredY());
    return item;
}
