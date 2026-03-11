#include "schematic_erase_tool.h"
#include "schematic_view.h"
#include "schematic_item.h"
#include "schematic_commands.h"
#include <QGraphicsScene>
#include <QUndoStack>

SchematicEraseTool::SchematicEraseTool(QObject* parent)
    : SchematicTool("Erase", parent) {
}

QCursor SchematicEraseTool::cursor() const {
    QPixmap pix(":/icons/tool_erase.svg");
    return QCursor(pix.scaled(24, 24, Qt::KeepAspectRatio), 0, 24); 
}

void SchematicEraseTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || !view()->scene()) return;

    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* hit = view()->itemAt(event->pos());
        if (hit) {
            SchematicItem* item = dynamic_cast<SchematicItem*>(hit);
            if (!item && hit->parentItem()) {
                item = dynamic_cast<SchematicItem*>(hit->parentItem());
            }

            if (item) {
                if (view()->undoStack()) {
                    view()->undoStack()->push(new RemoveItemCommand(view()->scene(), {item}));
                } else {
                    view()->scene()->removeItem(item);
                    delete item;
                }
            }
        }
    }
}
