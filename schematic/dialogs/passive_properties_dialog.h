#ifndef PASSIVE_PROPERTIES_DIALOG_H
#define PASSIVE_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"

class PassivePropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    PassivePropertiesDialog(SchematicItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    SchematicItem* m_item;
};

#endif // PASSIVE_PROPERTIES_DIALOG_H
