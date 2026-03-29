#include "tuning_slider_item.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <cmath>

static QString formatSpiceValue(double val) {
    const double absVal = std::abs(val);
    if (absVal < 1e-18) return "0";
    
    static const struct { double mult; const char* sym; } suffixes[] = {
        {1e12, "T"}, {1e9, "G"}, {1e6, "Meg"}, {1e3, "k"},
        {1.0, ""},
        {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}
    };
    
    for (const auto& s : suffixes) {
        if (absVal >= s.mult * 0.999) {
            return QString::number(val / s.mult, 'g', 4) + s.sym;
        }
    }
    return QString::number(val, 'g', 4);
}

TuningSliderItem::TuningSliderItem(SchematicItem* target, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_target(target) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(100); // Always on top
    
    if (m_target) {
        double current = SchematicItem::parseValue(m_target->value());
        if (current == 0) current = 1.0;
        setRange(current * 0.1, current * 10.0);
        setValue(current);
        
        // Initial position near target
        setPos(m_target->pos() + QPointF(0, 40));
    }
}

QRectF TuningSliderItem::boundingRect() const {
    return QRectF(-5, -5, m_width + 10, m_height + 10);
}

void TuningSliderItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Glassy background
    QLinearGradient grad(0, 0, 0, m_height);
    grad.setColorAt(0, QColor(255, 255, 255, 40));
    grad.setColorAt(1, QColor(255, 255, 255, 10));
    
    painter->setBrush(grad);
    painter->setPen(QPen(QColor(255, 255, 255, 60), 1));
    painter->drawRoundedRect(0, 0, m_width, m_height, 4, 4);

    // Track
    painter->setPen(QPen(QColor(255, 255, 255, 40), 2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(10, m_height/2, m_width - 10, m_height/2);

    // Handle
    double handleX = valueToPos(m_currentValue);
    painter->setBrush(QColor(59, 130, 246)); // Blue-500
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(handleX, m_height/2), 6, 6);
    
    // Value Label
    painter->setPen(Qt::white);
    QFont f = painter->font(); f.setPointSize(7); f.setBold(true);
    painter->setFont(f);
    painter->drawText(QRectF(0, -15, m_width, 15), Qt::AlignCenter, formatSpiceValue(m_currentValue));
}

void TuningSliderItem::setRange(double min, double max) {
    m_min = min;
    m_max = max;
    update();
}

void TuningSliderItem::setValue(double val) {
    m_currentValue = qBound(m_min, val, m_max);
    update();
}

void TuningSliderItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (QRectF(0, 0, m_width, m_height).contains(event->pos())) {
        m_dragging = true;
        m_currentValue = posToValue(event->pos().x());
        updateTargetValue();
        update();
        event->accept();
    } else {
        QGraphicsItem::mousePressEvent(event);
    }
}

void TuningSliderItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragging) {
        m_currentValue = posToValue(event->pos().x());
        updateTargetValue();
        update();
        event->accept();
    } else {
        QGraphicsItem::mouseMoveEvent(event);
    }
}

void TuningSliderItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    m_dragging = false;
    QGraphicsItem::mouseReleaseEvent(event);
}

void TuningSliderItem::updateTargetValue() {
    if (m_target) {
        m_target->setValue(formatSpiceValue(m_currentValue));
        emit valueChanged(m_currentValue);
    }
}

double TuningSliderItem::posToValue(double x) const {
    double rel = qBound(0.0, (x - 10.0) / (m_width - 20.0), 1.0);
    // Linear for now, could be logarithmic
    return m_min + rel * (m_max - m_min);
}

double TuningSliderItem::valueToPos(double val) const {
    double rel = (val - m_min) / (m_max - m_min);
    return 10.0 + rel * (m_width - 20.0);
}
