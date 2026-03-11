#include "switch_item.h"
#include <QPainter>
#include <QJsonObject>

SwitchItem::SwitchItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    m_isOpen = true;
    setReference("SW1");
    setValue("1e12"); // Open resistance
}

void SwitchItem::onInteractiveClick(const QPointF&) {
    m_isOpen = !m_isOpen;
    // Set value for SPICE engine (used by buildNetlist)
    setValue(m_isOpen ? "1e12" : "0.001");
    
    // Also store in paramExpressions for robustness during RT
    setParamExpression("resistance", m_isOpen ? "1e12" : "0.001");

    emit interactiveStateChanged();
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
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Switch";
    j["open"] = m_isOpen;
    return j; 
}

bool SwitchItem::fromJson(const QJsonObject& j) { 
    SchematicItem::fromJson(j);
    m_isOpen = j["open"].toBool();
    return true; 
}

SchematicItem* SwitchItem::clone() const {
    auto* item = new SwitchItem(pos());
    item->m_isOpen = m_isOpen;
    return item;
}
