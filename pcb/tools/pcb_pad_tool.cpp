#include "pcb_pad_tool.h"
#include "pcb_view.h"
#include "pcb_item_factory.h"
#include <QDebug>
#include "pcb_commands.h"
#include <QUndoStack>

PCBPadTool::PCBPadTool(QObject* parent)
    : PCBTool("Pad", parent) {
}

QCursor PCBPadTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void PCBPadTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    auto& factory = PCBItemFactory::instance();
    PCBItem* pad = factory.createItem("Pad", snappedPos);
    if (pad) {
        if (view()->undoStack()) {
            view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), pad));
        } else {
            view()->scene()->addItem(pad);
        }
        qDebug() << "Placed pad at" << snappedPos;
    } else {
        qWarning() << "Failed to create pad item";
    }
}
