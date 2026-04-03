#include "pcb_erase_tool.h"
#include "pcb_view.h"
#include "../items/pcb_item.h"
#include "pcb_commands.h"
#include <QGraphicsScene>
#include <QUndoStack>

PCBEraseTool::PCBEraseTool(QObject* parent)
    : PCBTool("Erase", parent) {
}

QCursor PCBEraseTool::cursor() const {
    // Cross cursor with a forbidden sign or custom eraser if possible
    QPixmap pix(":/icons/tool_erase.svg");
    return QCursor(pix.scaled(24, 24, Qt::KeepAspectRatio), 0, 24);
}

void PCBEraseTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || !view()->scene()) return;

    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* hit = view()->itemAt(event->pos());
        if (hit) {
            // Find the top-most selectable PCBItem
            PCBItem* pcbItem = nullptr;
            QGraphicsItem* current = hit;
            while (current) {
                if (PCBItem* candidate = dynamic_cast<PCBItem*>(current)) {
                    if (candidate->flags() & QGraphicsItem::ItemIsSelectable) {
                        pcbItem = candidate;
                    }
                }
                current = current->parentItem();
            }

            if (pcbItem && !pcbItem->isLocked()) {
                if (view()->undoStack()) {
                    view()->undoStack()->push(new PCBRemoveItemCommand(view()->scene(), {pcbItem}));
                } else {
                    view()->scene()->removeItem(pcbItem);
                    delete pcbItem;
                }
            } else if (!pcbItem) {
                // If it's a raw shape
                if (hit->flags() & QGraphicsItem::ItemIsSelectable) {
                    view()->scene()->removeItem(hit);
                    delete hit;
                }
            }
        }
    }
}
