#include "schematic_hierarchical_port_tool.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include "../items/hierarchical_port_item.h"
#include <QInputDialog>
#include <QGraphicsScene>

SchematicHierarchicalPortTool::SchematicHierarchicalPortTool(QObject* parent)
    : SchematicTool("Hierarchical Port", parent) {
}

void SchematicHierarchicalPortTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    bool ok;
    QString label = QInputDialog::getText(view(), "Hierarchical Port",
                                         "Enter port name:", QLineEdit::Normal,
                                         "PORT", &ok);
    if (ok && !label.isEmpty()) {
        HierarchicalPortItem* item = new HierarchicalPortItem(snappedPos, label);
        view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
    }
}
