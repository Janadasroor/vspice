#ifndef WIRE_PROPERTIES_DIALOG_H
#define WIRE_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/wire_item.h"
#include "net_manager.h"

class WirePropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    WirePropertiesDialog(WireItem* wire, NetManager* netManager = nullptr, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    WireItem* m_wire;
    NetManager* m_netManager;
};

#endif // WIRE_PROPERTIES_DIALOG_H
