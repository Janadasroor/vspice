#ifndef SHEET_PROPERTIES_DIALOG_H
#define SHEET_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/schematic_sheet_item.h"

class SheetPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    SheetPropertiesDialog(SchematicSheetItem* sheet, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private slots:
    void onSyncPorts();
    void onBrowseFile();

private:
    SchematicSheetItem* m_sheet;
    QString m_projectDir;
};

#endif // SHEET_PROPERTIES_DIALOG_H
