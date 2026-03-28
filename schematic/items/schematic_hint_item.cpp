#include "schematic_hint_item.h"
#include <QPainter>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QToolTip>
#include <QGraphicsDropShadowEffect>

SchematicHintItem::SchematicHintItem(const QString& text, QGraphicsItem* parent)
    : QGraphicsObject(parent), m_text(text) {
    setAcceptHoverEvents(true);
    setCursor(Qt::PointingHandCursor);
    m_hintColor = QColor(255, 191, 0); // Amber/Yellow
    
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 80));
    setGraphicsEffect(shadow);

    m_pulseAnim = new QPropertyAnimation(this, "scale", this);
    m_pulseAnim->setDuration(1200);
    m_pulseAnim->setStartValue(1.0);
    m_pulseAnim->setKeyValueAt(0.5, 1.15);
    m_pulseAnim->setEndValue(1.0);
    m_pulseAnim->setLoopCount(-1);
    m_pulseAnim->setEasingCurve(QEasingCurve::InOutQuad);
    m_pulseAnim->start();
}

SchematicHintItem::~SchematicHintItem() {
    if (m_pulseAnim) m_pulseAnim->stop();
}

QRectF SchematicHintItem::boundingRect() const {
    return QRectF(-14, -14, 28, 28);
}

void SchematicHintItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option) Q_UNUSED(widget)
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Smooth glow
    QRadialGradient glow(0, 0, 14);
    glow.setColorAt(0, m_hintColor.lighter(130));
    glow.setColorAt(1, QColor(0,0,0,0));
    painter->setBrush(glow);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(-14, -14, 28, 28);

    // Lightbulb body
    painter->setBrush(m_isHovered ? m_hintColor.lighter() : m_hintColor);
    painter->setPen(QPen(m_hintColor.darker(150), 1.5));
    
    // Procedural lightbulb
    QPainterPath path;
    path.moveTo(0, 8);
    path.arcTo(-6, -8, 12, 12, 210, 300);
    path.lineTo(3, 8);
    path.lineTo(-3, 8);
    path.closeSubpath();
    painter->drawPath(path);
    
    // Threads
    painter->setPen(QPen(m_hintColor.darker(150), 1));
    painter->drawLine(-3, 10, 3, 10);
    painter->drawLine(-2, 12, 2, 12);
}

void SchematicHintItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    m_isHovered = true;
    m_pulseAnim->pause();
    setScale(1.2);
    update();
    QToolTip::showText(event->screenPos(), m_text);
    QGraphicsObject::hoverEnterEvent(event);
}

void SchematicHintItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    m_isHovered = false;
    m_pulseAnim->resume();
    setScale(1.0);
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

void SchematicHintItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Expand the suggestion card
        m_isExpanded = !m_isExpanded;
        update();
    }
    QGraphicsObject::mousePressEvent(event);
}

void SchematicHintItem::setPulse(bool pulse) {
    if (pulse) m_pulseAnim->start(); else m_pulseAnim->stop();
}

void SchematicHintItem::expand() { m_isExpanded = true; update(); }
void SchematicHintItem::collapse() { m_isExpanded = false; update(); }
