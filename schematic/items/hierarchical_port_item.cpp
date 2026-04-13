#include "hierarchical_port_item.h"
#include <QPainter>
#include <QGraphicsScene>
#include "flux/core/theme_manager.h"

HierarchicalPortItem::HierarchicalPortItem(QPointF pos, const QString& label, PortType type, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_portType(type) {
    setValue(label);
    setPos(pos);
    setFlags(ItemIsSelectable | ItemIsMovable);
    m_font = QFont("Inter", 8);
    setZValue(10);
}

void HierarchicalPortItem::setPortType(PortType type) {
    m_portType = type;
    update();
}

QJsonObject HierarchicalPortItem::toJson() const {
    QJsonObject json;
    json["type"] = "HierarchicalPort";
    json["id"] = m_id.toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["rotation"] = rotation();
    json["label"] = value();
    json["portType"] = (int)m_portType;
    return json;
}

bool HierarchicalPortItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setRotation(json["rotation"].toDouble(0.0));
    setValue(json["label"].toString());
    m_portType = (PortType)json["portType"].toInt();
    return true;
}

SchematicItem* HierarchicalPortItem::clone() const {
    auto* item = new HierarchicalPortItem(pos(), value(), m_portType);
    item->setRotation(rotation());
    return item;
}

QPainterPath HierarchicalPortItem::shape() const {
    QFontMetrics fm(m_font);
    int tw = fm.horizontalAdvance(value());
    qreal w = tw + 15;
    qreal h = 14;
    qreal y = -h/2;

    QPainterPath path;
    path.moveTo(0, 0);
    path.lineTo(10, y);
    path.lineTo(w, y);
    path.lineTo(w, -y);
    path.lineTo(10, -y);
    path.closeSubpath();
    return path;
}

QRectF HierarchicalPortItem::boundingRect() const {
    return shape().boundingRect().adjusted(-2, -2, 2, 2);
}

void HierarchicalPortItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    painter->setRenderHint(QPainter::Antialiasing);
    
    PCBTheme* theme = ThemeManager::theme();
    QColor color = theme ? theme->schematicLine() : Qt::cyan;
    if (isSelected()) color = theme ? theme->selectionBox() : Qt::yellow;

    // Draw highlight glow if enabled
    if (m_isHighlighted) {
        painter->save();
        painter->setPen(QPen(QColor(255, 215, 0, 100), 4.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(shape());
        painter->restore();
    }

    painter->setPen(QPen(color, 1.5));
    
    QPainterPath path = shape();
    painter->drawPath(path);

    // Draw connection dot at the tip
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(0, 0), 2.0, 2.0);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(color, 1.5));
    
    painter->setFont(m_font);
    // Reuse layout logic from shape()
    QRectF textRect = path.boundingRect();
    textRect.setLeft(10);
    painter->drawText(textRect, Qt::AlignCenter, value());
    
    if (isSelected()) {
        painter->setPen(QPen(color, 1, Qt::DashLine));
        painter->drawRect(boundingRect().adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

QList<QPointF> HierarchicalPortItem::connectionPoints() const {
    // Port connects at the tip of the arrow (0,0)
    return { QPointF(0, 0) };
}
