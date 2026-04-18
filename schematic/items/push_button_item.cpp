#include "push_button_item.h"
#include "../editor/schematic_editor.h"
#include "../ui/simulation_panel.h"
#include "simulation_manager.h"
#include <QApplication>
#include <QPainter>
#include <QJsonObject>

namespace {
void triggerInteractiveSimulationUpdateIfNeeded() {
    auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
    if (!editor || !editor->getSimulationPanel()) return;

    const auto cfg = editor->getSimulationPanel()->getAnalysisConfig();
    if (cfg.type == SimAnalysisType::Transient || cfg.type == SimAnalysisType::RealTime) {
        editor->getSimulationPanel()->onRunSimulation();
    }
}

void updatePushButtonRealTime(const QString& ref, bool pressed) {
    // Phase 1: Use alter command (proven working)
    auto& sim = SimulationManager::instance();
    sim.alterSwitchResistance(ref.startsWith("R") ? ref : "R" + ref, pressed ? 0.001 : 1e12);
}
}

PushButtonItem::PushButtonItem(QPointF pos, QGraphicsItem *parent) : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    m_isPressed = false;
    setReference("SW1");
    setValue("1e12"); // Open resistance
}

void PushButtonItem::onInteractivePress(const QPointF&) {
    m_isPressed = true;
    setValue("0.001"); // Closed resistance
    setParamExpression("resistance", "0.001");
    Q_EMIT interactiveStateChanged();
    updatePushButtonRealTime(reference(), m_isPressed);
    update();
}

void PushButtonItem::onInteractiveRelease(const QPointF&) {
    m_isPressed = false;
    setValue("1e12"); // Open resistance
    setParamExpression("resistance", "1e12");
    Q_EMIT interactiveStateChanged();
    updatePushButtonRealTime(reference(), m_isPressed);
    update();
}

QRectF PushButtonItem::boundingRect() const { return QRectF(-20, -20, 40, 40); }

void PushButtonItem::paint(QPainter *painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 2));
    
    painter->drawEllipse(-17, -2, 4, 4);
    painter->drawEllipse(13, -2, 4, 4);

    if (m_isPressed) {
        painter->drawLine(-15, 0, 15, 0);
        painter->fillRect(-10, -5, 20, 5, QColor(0, 255, 100));
    } else {
        painter->drawLine(-10, -10, 10, -10);
        painter->drawLine(0, -10, 0, -15);
        painter->drawRect(-5, -18, 10, 3);
    }
    
    drawConnectionPointHighlights(painter);
}

QList<QPointF> PushButtonItem::connectionPoints() const {
    return { QPointF(-15, 0), QPointF(15, 0) };
}

QJsonObject PushButtonItem::toJson() const { 
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "PushButton";
    return j; 
}

bool PushButtonItem::fromJson(const QJsonObject& j) { 
    return SchematicItem::fromJson(j);
}

SchematicItem* PushButtonItem::clone() const {
    return new PushButtonItem(pos());
}
