#include "schematic_spice_directive_item.h"
#include <QColor>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QClipboard>

SchematicSpiceDirectiveItem::SchematicSpiceDirectiveItem(QString text, QPointF pos, QGraphicsItem* parent)
    : SchematicTextItem(text, pos, parent)
{
    setColor(QColor("#3b82f6"));
    QFont f = font();
    f.setFamily("Courier New");
    f.setPointSize(10);
    f.setBold(true);
    setFont(f);
}

void SchematicSpiceDirectiveItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    QMenu menu;
    
    QAction* editAction = menu.addAction("Edit Simulation...");
    QAction* copyAction = menu.addAction("Copy");
    QAction* deleteAction = menu.addAction("Delete");
    
    QAction* selectedAction = menu.exec(event->screenPos());
    
    if (selectedAction == editAction) {
        emit editSimulationRequested(text());
    } else if (selectedAction == deleteAction) {
        if (QGraphicsScene* s = scene()) {
            s->removeItem(this);
        }
        delete this;
    } else if (selectedAction == copyAction) {
        QApplication::clipboard()->setText(text());
    }
}

SchematicItem::ItemType SchematicSpiceDirectiveItem::itemType() const {
    return SchematicItem::SpiceDirectiveType;
}

QString SchematicSpiceDirectiveItem::itemTypeName() const {
    return "Spice Directive";
}

SchematicItem* SchematicSpiceDirectiveItem::clone() const {
    SchematicSpiceDirectiveItem* newItem = new SchematicSpiceDirectiveItem(text(), pos(), parentItem());
    newItem->setFont(font());
    newItem->setColor(color());
    newItem->setAlignment(alignment());
    return newItem;
}
