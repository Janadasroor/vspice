#include "erc_marker_item.h"
#include <QPainter>
#include <QToolTip>
#include <QGraphicsSceneMouseEvent>

ERCMarkerItem::ERCMarkerItem(const ERCViolation& violation, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_violation(violation) {
    setPos(violation.position);
    setZValue(2000); // Always on top
    setAcceptHoverEvents(true);
    setToolTip(QString("[%1] %2").arg(violation.severity == ERCViolation::Error ? "Error" : "Warning", violation.message));
}

QRectF ERCMarkerItem::boundingRect() const {
    return QRectF(-8, -8, 16, 16);
}

void ERCMarkerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);
    
    QColor color = (m_violation.severity == ERCViolation::Error || m_violation.severity == ERCViolation::Critical) 
                   ? QColor("#f87171") : QColor("#fbbf24");

    // Draw triangle marker
    QPainterPath path;
    path.moveTo(0, -7);
    path.lineTo(7, 7);
    path.lineTo(-7, 7);
    path.closeSubpath();

    painter->setPen(QPen(color, 1.5));
    painter->setBrush(QColor(color.red(), color.green(), color.blue(), 100));
    painter->drawPath(path);

    // Draw '!' in center
    painter->setPen(Qt::white);
    QFont f = painter->font();
    f.setBold(true);
    f.setPointSize(8);
    painter->setFont(f);
    painter->drawText(boundingRect(), Qt::AlignCenter, "!");
}
