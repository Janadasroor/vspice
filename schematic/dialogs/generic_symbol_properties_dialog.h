#ifndef GENERIC_SYMBOL_PROPERTIES_DIALOG_H
#define GENERIC_SYMBOL_PROPERTIES_DIALOG_H

#include "smart_properties_dialog.h"

class GenericComponentItem;
class QComboBox;
class QLineEdit;
class QTableWidget;

class GenericSymbolPropertiesDialog : public SmartPropertiesDialog {
    Q_OBJECT

public:
    GenericSymbolPropertiesDialog(SchematicItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    
protected:
    void onApply() override;

private:
    void addSimulationTab();
    QMap<QString, QString> pinMappingFromTable() const;

    SchematicItem* m_item;
    GenericComponentItem* m_genericItem;
    QComboBox* m_subcktPicker;
    QLineEdit* m_spiceModelEdit;
    QTableWidget* m_pinMappingTable;
};

#endif // GENERIC_SYMBOL_PROPERTIES_DIALOG_H
