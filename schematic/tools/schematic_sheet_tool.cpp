#include "schematic_sheet_tool.h"
#include "schematic_view.h"
#include "schematic_item_factory.h"
#include "schematic_sheet_item.h"
#include "schematic_commands.h"
#include <QGraphicsScene>
#include <QMouseEvent>

SchematicSheetTool::SchematicSheetTool(QObject* parent)
    : SchematicTool("Sheet", parent), m_previewItem(nullptr) {
}

void SchematicSheetTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
    m_previewItem = new SchematicSheetItem();
    m_previewItem->setOpacity(0.5);
    m_previewItem->setZValue(1000);
    m_previewItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
    m_previewItem->setAcceptedMouseButtons(Qt::NoButton);
    view->scene()->addItem(m_previewItem);
}

void SchematicSheetTool::deactivate() {
    if (m_previewItem) {
        if (view() && view()->scene()) view()->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    SchematicTool::deactivate();
}

void SchematicSheetTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_previewItem && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        m_previewItem->setPos(view()->snapToGrid(scenePos));
    }
}

void SchematicSheetTool::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !view()) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    SchematicSheetItem* item = new SchematicSheetItem(snappedPos);
    if (view()->undoStack()) {
        view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
    } else {
        view()->scene()->addItem(item);
    }
    
    event->accept();
}
