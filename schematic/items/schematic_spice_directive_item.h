#ifndef SCHEMATIC_SPICE_DIRECTIVE_ITEM_H
#define SCHEMATIC_SPICE_DIRECTIVE_ITEM_H

#include "schematic_text_item.h"
#include <QMenu>
#include <QAction>

/**
 * @brief Schematic item for SPICE commands and directives (.tran, .model, etc.)
 */
class SchematicSpiceDirectiveItem : public SchematicTextItem {
    Q_OBJECT
public:
    SchematicSpiceDirectiveItem(QString text = "", QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);
    virtual ~SchematicSpiceDirectiveItem() = default;

    QString itemTypeName() const override;
    ItemType itemType() const override;
    
    SchematicItem* clone() const override;

    // Directives use text as their primary "value"
    QString value() const override { return text(); }
    void setValue(const QString& v) override { setText(v); }

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

signals:
    void editSimulationRequested(const QString& currentCommand);

private:
    void setupContextMenu();
};

#endif // SCHEMATIC_SPICE_DIRECTIVE_ITEM_H
