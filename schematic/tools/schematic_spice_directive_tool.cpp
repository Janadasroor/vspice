#include "schematic_spice_directive_tool.h"
#include "../items/schematic_spice_directive_item.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include "../../core/ui/text_properties_dialog.h"
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>

SchematicSpiceDirectiveTool::SchematicSpiceDirectiveTool(QObject* parent) 
    : SchematicTool("Spice Directive", parent)
{
}

void SchematicSpiceDirectiveTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));
        
        TextPropertiesDialog dlg(view());
        dlg.setWindowTitle("Place SPICE Directive");
        // Pre-configure for SPICE directive style
        dlg.setColor(QColor("#3b82f6")); 
        
        if (dlg.exec() == QDialog::Accepted && !dlg.text().isEmpty()) {
            SchematicSpiceDirectiveItem* item = new SchematicSpiceDirectiveItem(dlg.text(), pos);
            
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
