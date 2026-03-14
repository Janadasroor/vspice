#include "schematic_scissors_tool.h"
#include "schematic_view.h"
#include "schematic_item.h"
#include "schematic_commands.h"
#include <QGraphicsScene>
#include <QUndoStack>

SchematicScissorsTool::SchematicScissorsTool(QObject* parent)
    : SchematicTool("Scissors", parent) {
}

QCursor SchematicScissorsTool::cursor() const {
    QPixmap pix(":/icons/tool_scissors.svg");
    // Standard large cursor size (32x32). Tip is at (25, 9).
    return QCursor(pix.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation), 25, 9); 
}

void SchematicScissorsTool::mousePressEvent(QMouseEvent* event) {
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
