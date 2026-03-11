#include "schematic_net_label_tool.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include "../items/net_label_item.h"
#include <QInputDialog>
#include <QGraphicsScene>

SchematicNetLabelTool::SchematicNetLabelTool(NetLabelItem::LabelScope scope, QObject* parent)
    : SchematicTool(scope == NetLabelItem::Global ? "Global Label" : "Net Label", parent)
    , m_scope(scope) {
}

void SchematicNetLabelTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGridOrPin(scenePos).point;

    QString title = (m_scope == NetLabelItem::Global) ? "Global Label" : "Net Label";
    bool ok;
    QString label = QInputDialog::getText(view(), title,
                                         "Enter net name:", QLineEdit::Normal,
                                         "NET", &ok);
    if (ok && !label.isEmpty()) {
        NetLabelItem* item = new NetLabelItem(snappedPos, label, nullptr, m_scope);
        view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
    }
}
