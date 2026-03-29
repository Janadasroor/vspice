#include "tuning_slider_symbol_item.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation_panel.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QApplication>
#include <cmath>

TuningSliderSymbolItem::TuningSliderSymbolItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setReference("A");
    setValue("50");
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
}

TuningSliderSymbolItem::~TuningSliderSymbolItem() {
}

QRectF TuningSliderSymbolItem::boundingRect() const {
    return QRectF(-5, -15, m_width + 10, m_height + 25);
}

void TuningSliderSymbolItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // Glassy Background
    QLinearGradient grad(0, 0, 0, m_height);
    grad.setColorAt(0, QColor(45, 45, 55, 220));
    grad.setColorAt(1, QColor(30, 30, 40, 240));
    
    painter->setBrush(grad);
    painter->setPen(QPen(isSelected() ? QColor(59, 130, 246) : QColor(255, 255, 255, 60), 1.5));
    painter->drawRoundedRect(0, 0, m_width, m_height, 6, 6);

    // Title / Reference
    painter->setPen(Qt::white);
    QFont f = painter->font(); f.setPointSize(8); f.setBold(true);
    painter->setFont(f);
    painter->drawText(QRectF(5, 2, m_width - 10, 15), Qt::AlignLeft, reference());

    // Track
    painter->setPen(QPen(QColor(255, 255, 255, 40), 2, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(15, 22, m_width - 15, 22);

    // Handle
    double handleX = valueToPos(m_current);
    painter->setBrush(QColor(59, 130, 246));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(handleX, 22), 6, 6);

    // Value text
    painter->setPen(QColor(0, 255, 100));
    f.setPointSize(7); f.setBold(false);
    painter->setFont(f);
    QString valStr = QString::number(m_current, 'g', 4);
    painter->drawText(QRectF(0, 2, m_width - 5, 15), Qt::AlignRight, valStr);
}

void TuningSliderSymbolItem::setCurrentValue(double v) {
    m_current = qBound(m_min, v, m_max);
    setValue(QString::number(m_current));
    update();
    triggerRealTimeUpdate();
}

void TuningSliderSymbolItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (QRectF(0, 15, m_width, 20).contains(event->pos())) {
        m_dragging = true;
        setCurrentValue(posToValue(event->pos().x()));
        event->accept();
    } else {
        SchematicItem::mousePressEvent(event);
    }
}

void TuningSliderSymbolItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragging) {
        setCurrentValue(posToValue(event->pos().x()));
        event->accept();
    } else {
        SchematicItem::mouseMoveEvent(event);
    }
}

void TuningSliderSymbolItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    m_dragging = false;
    SchematicItem::mouseReleaseEvent(event);
}

double TuningSliderSymbolItem::posToValue(double x) const {
    double rel = qBound(0.0, (x - 15.0) / (m_width - 30.0), 1.0);
    return m_min + rel * (m_max - m_min);
}

double TuningSliderSymbolItem::valueToPos(double val) const {
    if (m_max <= m_min) return 15.0;
    double rel = (val - m_min) / (m_max - m_min);
    return 15.0 + rel * (m_width - 30.0);
}

void TuningSliderSymbolItem::triggerRealTimeUpdate() {
    auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
    if (editor && editor->getSimulationPanel() && editor->getSimulationPanel()->isRealTimeMode()) {
        editor->getSimulationPanel()->onRunSimulation();
    }
}

QJsonObject TuningSliderSymbolItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "TuningSlider";
    j["min"] = m_min;
    j["max"] = m_max;
    j["current"] = m_current;
    return j;
}

bool TuningSliderSymbolItem::fromJson(const QJsonObject& json) {
    if (!SchematicItem::fromJson(json)) return false;
    m_min = json["min"].toDouble(0.0);
    m_max = json["max"].toDouble(100.0);
    m_current = json["current"].toDouble(50.0);
    return true;
}

SchematicItem* TuningSliderSymbolItem::clone() const {
    auto* item = new TuningSliderSymbolItem(pos());
    item->fromJson(toJson());
    return item;
}
