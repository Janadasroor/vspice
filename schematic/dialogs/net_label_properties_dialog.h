#ifndef NET_LABEL_PROPERTIES_DIALOG_H
#define NET_LABEL_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/net_label_item.h"

class NetLabelPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    NetLabelPropertiesDialog(NetLabelItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    NetLabelItem* m_item;
};

#endif // NET_LABEL_PROPERTIES_DIALOG_H
