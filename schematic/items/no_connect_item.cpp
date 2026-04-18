#include "no_connect_item.h"
#include <QPainter>
#include <QPen>
#include "theme_manager.h"

NoConnectItem::NoConnectItem(QGraphicsItem *parent)
    : SchematicItem(parent) {
    setZValue(5); // Above wires
}

QJsonObject NoConnectItem::toJson() const {
    QJsonObject json;
    json["type"] = "NoConnect";
    json["id"] = m_id.toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    return json;
}

bool NoConnectItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    setPos(json["x"].toDouble(), json["y"].toDouble());
    return true;
}

SchematicItem* NoConnectItem::clone() const {
    NoConnectItem* item = new NoConnectItem();
    item->setPos(pos());
    return item;
}

QList<QPointF> NoConnectItem::connectionPoints() const {
    // Only one connection point at the center
    return { QPointF(0, 0) };
}

QRectF NoConnectItem::boundingRect() const {
    qreal half = m_size / 2.0;
    return QRectF(-half, -half, m_size, m_size).adjusted(-2, -2, 2, 2);
}

void NoConnectItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    QColor color = Qt::red; // Default for No-Connect
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->errorColor();
    }

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));

    qreal half = m_size / 2.0;
    // Draw "X"
    painter->drawLine(QPointF(-half, -half), QPointF(half, half));
    painter->drawLine(QPointF(-half, half), QPointF(half, -half));

    if (isSelected()) {
        painter->setPen(QPen(Qt::blue, 1, Qt::DashLine));
        painter->drawRect(boundingRect().adjusted(1,1,-1,-1));
    }
}
