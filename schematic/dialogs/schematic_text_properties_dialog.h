#ifndef SCHEMATIC_TEXT_PROPERTIES_DIALOG_H
#define SCHEMATIC_TEXT_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/schematic_text_item.h"
#include <QPointer>

class GenericComponentItem;

class SchematicTextPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    SchematicTextPropertiesDialog(SchematicTextItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);

protected:
    void onApply() override;
    void applyPreview() override;

private:
    SchematicTextItem* m_item;
    QPointer<GenericComponentItem> m_parentComponent;
};

#endif // SCHEMATIC_TEXT_PROPERTIES_DIALOG_H
