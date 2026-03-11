#ifndef GENERIC_SYMBOL_PROPERTIES_DIALOG_H
#define GENERIC_SYMBOL_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"

class GenericSymbolPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    GenericSymbolPropertiesDialog(SchematicItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;

private:
    SchematicItem* m_item;
};

#endif // GENERIC_SYMBOL_PROPERTIES_DIALOG_H
