#ifndef BUS_PROPERTIES_DIALOG_H
#define BUS_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/bus_item.h"

class BusPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    BusPropertiesDialog(BusItem* bus, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    BusItem* m_bus;
};

#endif // BUS_PROPERTIES_DIALOG_H
