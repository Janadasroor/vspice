#ifndef VOLTAGE_SOURCE_PROPERTIES_DIALOG_H
#define VOLTAGE_SOURCE_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"
#include "../items/voltage_source_item.h"

/**
 * @brief Professional properties dialog for voltage sources (LTspice style).
 */
class VoltageSourcePropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    VoltageSourcePropertiesDialog(VoltageSourceItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);

protected:
    void onApply() override;
    void applyPreview() override;
    void onFieldChanged() override;

private:
    void updateTabVisibility();
    VoltageSourceItem* m_item;
};

#endif // VOLTAGE_SOURCE_PROPERTIES_DIALOG_H
