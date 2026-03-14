#include "schematic_spice_directive_item.h"
#include <QColor>

SchematicSpiceDirectiveItem::SchematicSpiceDirectiveItem(QString text, QPointF pos, QGraphicsItem* parent)
    : SchematicTextItem(text, pos, parent)
{
    // SPICE directives are usually blue or cyan in LTspice
    setColor(QColor("#3b82f6")); // Bright blue
    QFont f = font();
    f.setFamily("Courier New"); // Monospace for SPICE
    f.setBold(true);
    setFont(f);
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
