#include "schematic_text_tool.h"
#include "../items/schematic_text_item.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include "ui/text_properties_dialog.h"
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>

SchematicTextTool::SchematicTextTool(QObject* parent) 
    : SchematicTool("Text", parent)
{
}

void SchematicTextTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));
        
        TextPropertiesDialog dlg(view());
        if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
            SchematicTextItem* item = new SchematicTextItem(dlg.text(), pos);
            
            // Set additional properties from dialog
            QFont font = item->font();
            font.setPointSize(dlg.fontSize());
            item->setFont(font);
            item->setColor(dlg.color());
            
            if (dlg.alignment() == "Center") item->setAlignment(Qt::AlignCenter);
            else if (dlg.alignment() == "Right") item->setAlignment(Qt::AlignRight);
            else item->setAlignment(Qt::AlignLeft);
            
            // Add via undo stack
            if (view()->undoStack()) {
                view()->undoStack()->push(new AddItemCommand(view()->scene(), item));
            } else {
                view()->scene()->addItem(item);
            }
        }
    }
}
