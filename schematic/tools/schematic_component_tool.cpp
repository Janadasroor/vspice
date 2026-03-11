#include "schematic_component_tool.h"
#include "schematic_view.h"
#include "schematic_item_factory.h"
#include "schematic_commands.h"
#include "schematic_item.h"
#include "wire_item.h"
#include "schematic_wire_utils.h"
#include <QUndoStack>
#include <QDebug>
#include <QVector2D>
#include <QtMath>

SchematicComponentTool::SchematicComponentTool(const QString& componentType, QObject* parent)
    : SchematicTool(componentType, parent)
    , m_componentType(componentType) {
}

QCursor SchematicComponentTool::cursor() const {
    // Return a cross cursor for placement
    return QCursor(Qt::CrossCursor);
}

void SchematicComponentTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
    
    // Create preview item
    if (view && view->scene()) {
        auto& factory = SchematicItemFactory::instance();
        m_previewItem = factory.createItem(m_componentType, QPointF(0, 0));
        if (m_previewItem) {
            m_previewItem->setOpacity(0.4);
            m_previewItem->setZValue(1000); // Always on top
            m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
            m_previewItem->setFlag(QGraphicsItem::ItemIsMovable, false);
            view->scene()->addItem(m_previewItem);
            
            // Initial position (if mouse is already in view)
            QPoint pos = view->mapFromGlobal(QCursor::pos());
            if (view->rect().contains(pos)) {
                QPointF snapped = view->snapToGrid(view->mapToScene(pos));
                m_previewItem->setPos(snapped);
            } else {
                m_previewItem->hide();
            }
        }
    }
}

void SchematicComponentTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_previewItem && view()) {
        m_previewItem->show();
        QPointF snappedPos = view()->snapToGrid(view()->mapToScene(event->pos()));
        m_previewItem->setPos(snappedPos);

        // Real-time routing preview (show indicators where wires will be cut/connected)
        clearPreviewWires();
        
        QList<QPointF> pins;
        for (const QPointF& p : m_previewItem->connectionPoints()) {
            pins.append(m_previewItem->mapToScene(p));
        }

        for (const QPointF& pin : pins) {
            // Find if this pin sits on a wire
            QRectF searchRect(pin - QPointF(1.5,1.5), QSize(3,3));
            QList<QGraphicsItem*> items = view()->scene()->items(searchRect);
            for (auto* item : items) {
                if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
                    // Show a small marker to indicate connection/cut point
                    WireItem* ghost = new WireItem(pin, pin);
                    ghost->setOpacity(0.8);
                    ghost->setZValue(1001);
                    QPen p(Qt::cyan, 1.5);
                    p.setCosmetic(true);
                    ghost->setPen(p);
                    view()->scene()->addItem(ghost);
                    m_previewWires.append(ghost);
                }
            }
        }
    }
    event->accept();
}

void SchematicComponentTool::clearPreviewWires() {
    if (!view() || !view()->scene()) return;
    for (WireItem* wire : m_previewWires) {
        if (wire->scene()) view()->scene()->removeItem(wire);
        delete wire;
    }
    m_previewWires.clear();
}

void SchematicComponentTool::deactivate() {
    clearPreviewWires();
    if (m_previewItem && view()) {
        if (view()->scene()) {
            view()->scene()->removeItem(m_previewItem);
        }
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    SchematicTool::deactivate();
}

void SchematicComponentTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    if (event->button() == Qt::RightButton) {
        view()->setCurrentTool("Select");
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    // Create a new component at the clicked position
    auto& factory = SchematicItemFactory::instance();
    SchematicItem* component = factory.createItem(m_componentType, snappedPos);
    
    if (component) {
        component->setRotation(m_currentRotation); // Apply rotation
        if (view()) {
            QString ref = view()->getNextReference(component->referencePrefix());
            component->setReference(ref);
        }

        if (view()->undoStack()) {
            view()->undoStack()->beginMacro("Place " + m_componentType);
            
            // 1. Add the component
            AddItemCommand* addCompCmd = new AddItemCommand(view()->scene(), component);
            view()->undoStack()->push(addCompCmd);

            // 2. Perform Auto-Cut logic (LTSpice style)
            SchematicWireUtils::splitWiresByComponent(component, view()->scene(), view()->undoStack());

            view()->undoStack()->endMacro();
        } else {
            // Direct add (fallback)
            view()->scene()->addItem(component);
        }
        qDebug() << "Placed schematic component:" << m_componentType << "at" << snappedPos;
        event->accept();
    }
}

void SchematicComponentTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_R) {
        bool isShift = (event->modifiers() & Qt::ShiftModifier);
        
        if (isShift) {
            m_currentRotation = fmod(m_currentRotation - 90.0, 360.0);
            if (m_currentRotation < 0) m_currentRotation += 360.0;
        } else {
            m_currentRotation = fmod(m_currentRotation + 90.0, 360.0);
        }

        if (m_previewItem) {
            m_previewItem->setRotation(m_currentRotation);
        }
        event->accept();
        return;
    }
    SchematicTool::keyPressEvent(event);
}
