#include "schematic_bus_tool.h"
#include "bus_item.h"
#include "schematic_view.h"
#include "schematic_editor.h"
#include "../editor/schematic_commands.h"
#include "flux/core/config_manager.h"
#include <QMouseEvent>
#include <QGraphicsScene>

SchematicBusTool::SchematicBusTool(QObject* parent)
    : SchematicTool("Bus", parent), m_currentBus(nullptr), m_isDrawing(false), m_hFirst(true) {
    
    auto& config = ConfigManager::instance();
    m_width = config.toolProperty("Bus", "Width", 3.0).toDouble();
    m_color = config.toolProperty("Bus", "Color", "#3b82f6").toString();
    m_style = config.toolProperty("Bus", "Line Style", "Solid").toString();
}

QMap<QString, QVariant> SchematicBusTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Width"] = m_width;
    props["Color"] = m_color;
    props["Line Style"] = m_style;
    return props;
}

void SchematicBusTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Width") m_width = value.toDouble();
    else if (name == "Color") m_color = value.toString();
    else if (name == "Line Style") m_style = value.toString();

    ConfigManager::instance().setToolProperty("Bus", name, value);
}

QCursor SchematicBusTool::cursor() const {
    return Qt::CrossCursor;
}

void SchematicBusTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
    reset();
}

void SchematicBusTool::deactivate() {
    finishBus();
    SchematicTool::deactivate();
}

void SchematicBusTool::reset() {
    if (m_currentBus) {
        if (view() && view()->scene()) view()->scene()->removeItem(m_currentBus);
        delete m_currentBus;
        m_currentBus = nullptr;
    }
    m_isDrawing = false;
    m_committedPoints.clear();
}

void SchematicBusTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;
    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = view()->mapToScene(event->pos());
        QPointF snapped = view()->snapToGridOrPin(scenePos).point;

        if (!m_isDrawing) {
            m_isDrawing = true;
            m_committedPoints << snapped;
            m_currentBus = new BusItem(snapped, snapped);
            view()->scene()->addItem(m_currentBus);
        } else {
            // Add intermediate point based on current routing
            QPointF last = m_committedPoints.last();
            if (m_hFirst) {
                m_committedPoints << QPointF(snapped.x(), last.y());
            } else {
                m_committedPoints << QPointF(last.x(), snapped.y());
            }
            m_committedPoints << snapped;
            updatePreview();
        }
    } else if (event->button() == Qt::RightButton) {
        finishBus();
    }
}

void SchematicBusTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDrawing) {
        updatePreview();
    }
}

void SchematicBusTool::mouseReleaseEvent(QMouseEvent* event) {
    // Left-click adds points, Right-click finishes.
}

void SchematicBusTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        reset();
    } else if (event->key() == Qt::Key_Space) {
        m_hFirst = !m_hFirst;
        if (m_isDrawing) updatePreview();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishBus();
    }
}

void SchematicBusTool::updatePreview() {
    if (!view() || !m_isDrawing || !m_currentBus || m_committedPoints.isEmpty()) return;

    QPointF scenePos = view()->mapFromGlobal(QCursor::pos());
    QPointF snapped = view()->snapToGridOrPin(view()->mapToScene(scenePos.toPoint())).point;
    
    QList<QPointF> previewPoints = m_committedPoints;
    QPointF last = m_committedPoints.last();
    
    if (m_hFirst) {
        previewPoints << QPointF(snapped.x(), last.y());
    } else {
        previewPoints << QPointF(last.x(), snapped.y());
    }
    previewPoints << snapped;
    
    m_currentBus->setPoints(previewPoints);
}

void SchematicBusTool::finishBus() {
    if (!view()) return;
    if (m_isDrawing && m_currentBus) {
        // Use current points including the one under mouse
        QPointF scenePos = view()->mapFromGlobal(QCursor::pos());
        QPointF snapped = view()->snapToGridOrPin(view()->mapToScene(scenePos.toPoint())).point;
        
        QList<QPointF> finalPoints = m_committedPoints;
        QPointF last = m_committedPoints.last();
        if (last != snapped) {
             if (m_hFirst) {
                finalPoints << QPointF(snapped.x(), last.y());
            } else {
                finalPoints << QPointF(last.x(), snapped.y());
            }
            finalPoints << snapped;
        }

        if (finalPoints.size() >= 2) {
            BusItem* bus = new BusItem();
            bus->setPoints(finalPoints);
            
            if (view()->undoStack()) {
                view()->undoStack()->push(new AddItemCommand(view()->scene(), bus));
            } else {
                view()->scene()->addItem(bus);
            }
        }
    }
    reset();
}
