#include "pcb_component_tool.h"
#include "pcb_view.h"
#include "pcb_item_factory.h"
#include "pcb_commands.h"
#include <QGraphicsScene>
#include <QJsonObject>
#include <QDebug>
#include <QUndoStack>

PCBComponentTool::PCBComponentTool(QObject* parent)
    : PCBTool("Component", parent)
    , m_componentType("IC")
    , m_previewItem(nullptr) {
}

QCursor PCBComponentTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void PCBComponentTool::activate(PCBView* view) {
    PCBTool::activate(view);
    updatePreview();
}

void PCBComponentTool::deactivate() {
    if (m_previewItem && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    PCBTool::deactivate();
}

void PCBComponentTool::setComponentType(const QString& type) {
    if (m_componentType == type) return;
    m_componentType = type;
    updatePreview();
}

void PCBComponentTool::updatePreview() {
    if (!view() || !view()->scene()) return;

    if (m_previewItem) {
        view()->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }

    auto& factory = PCBItemFactory::instance();
    QJsonObject properties;
    properties["componentType"] = m_componentType;
    
    m_previewItem = factory.createItem("Component", QPointF(0, 0), properties);
    if (m_previewItem) {
        m_previewItem->setOpacity(0.5);
        m_previewItem->setZValue(1000); // Always on top
        // Disable selection and interaction for preview item
        m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
        m_previewItem->setFlag(QGraphicsItem::ItemIsMovable, false);
        view()->scene()->addItem(m_previewItem);
        
        // Move to current mouse position if possible
        QPoint localPos = view()->mapFromGlobal(QCursor::pos());
        QPointF scenePos = view()->mapToScene(localPos);
        m_previewItem->setPos(view()->snapToGrid(scenePos));
    }
}

void PCBComponentTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_previewItem && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        m_previewItem->setPos(view()->snapToGrid(scenePos));
        event->accept();
    }
}

void PCBComponentTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    auto& factory = PCBItemFactory::instance();
    QJsonObject properties;
    properties["componentType"] = m_componentType;

    PCBItem* component = factory.createItem("Component", snappedPos, properties);
    if (component) {
        if (view()->undoStack()) {
            view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), component));
        } else {
            view()->scene()->addItem(component);
        }
        qDebug() << "Placed" << m_componentType << "component at" << snappedPos;
    }
    event->accept();
}
