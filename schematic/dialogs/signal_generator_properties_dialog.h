#ifndef SIGNAL_GENERATOR_PROPERTIES_DIALOG_H
#define SIGNAL_GENERATOR_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/signal_generator_item.h"

class SignalGeneratorPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    SignalGeneratorPropertiesDialog(SignalGeneratorItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;
    void applyPreview() override;

private:
    SignalGeneratorItem* m_item;
};

#endif // SIGNAL_GENERATOR_PROPERTIES_DIALOG_H
