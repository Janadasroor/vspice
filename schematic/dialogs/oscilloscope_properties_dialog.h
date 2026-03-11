#ifndef OSCILLOSCOPE_PROPERTIES_DIALOG_H
#define OSCILLOSCOPE_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/oscilloscope_item.h"

class OscilloscopePropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    OscilloscopePropertiesDialog(OscilloscopeItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    OscilloscopeItem* m_item;
};

#endif // OSCILLOSCOPE_PROPERTIES_DIALOG_H
