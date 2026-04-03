#ifndef RATSNESTITEM_H
#define RATSNESTITEM_H

#include "pcb_item.h"
#include <QPen>
#include <QPainter>

class RatsnestItem : public PCBItem {
public:
    RatsnestItem(QPointF p1, QPointF p2, QGraphicsItem* parent = nullptr)
        : PCBItem(parent), m_p1(p1), m_p2(p2) {
        setZValue(-10); // Draw below traces and components
        setFlag(QGraphicsItem::ItemIsSelectable, false);
        setFlag(QGraphicsItem::ItemIsMovable, false);
    }

    QString itemTypeName() const override { return "Ratsnest"; }
    ItemType itemType() const override { return RatsnestType; }
    QJsonObject toJson() const override { return QJsonObject(); } // Don't save ratsnest
    bool fromJson(const QJsonObject&) override { return true; }
    PCBItem* clone() const override { return nullptr; }

    QRectF boundingRect() const override {
        return QRectF(m_p1, m_p2).normalized().adjusted(-2, -2, 2, 2);
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        // Use a high-contrast, bright cyan for visibility on dark backgrounds
        QPen p(QColor(0, 255, 255, 160), 0.2, Qt::DashLine);
        p.setCosmetic(true);
        painter->setPen(p);
        painter->drawLine(m_p1, m_p2);
    }

    void setPoints(QPointF p1, QPointF p2) {
        prepareGeometryChange();
        m_p1 = p1;
        m_p2 = p2;
        update();
    }

private:
    QPointF m_p1, m_p2;
};

#endif // RATSNESTITEM_H
