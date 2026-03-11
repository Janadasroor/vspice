#include "sheet_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../editor/schematic_editor.h"
#include <QFileDialog>
#include <QPushButton>

SheetPropertiesDialog::SheetPropertiesDialog(SchematicSheetItem* sheet, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({sheet}, undoStack, scene, parent), m_sheet(sheet) {
    
    setWindowTitle("Sheet Properties - " + sheet->sheetName());
    
    if (auto* editor = qobject_cast<SchematicEditor*>(parent)) {
        // We'll need a way to get project dir, assuming it's available or passed.
    }

    PropertyTab generalTab;
    generalTab.title = "General";
    
    generalTab.fields.append({"sheetName", "Sheet Name", PropertyField::Text});
    generalTab.fields.append({"fileName", "File Path", PropertyField::Text});
    
    addTab(generalTab);
    
    // Add custom buttons to the bottom layout if possible, or just use the fields
    // For now, let's stick to the schema and add a "Browse" button next to fileName in a real implementation.
    
    setPropertyValue("sheetName", sheet->sheetName());
    setPropertyValue("fileName", sheet->fileName());
}

void SheetPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene) return;

    m_undoStack->beginMacro("Update Sheet Properties");
    
    QString newName = getPropertyValue("sheetName").toString();
    if (newName != m_sheet->sheetName()) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_sheet, "sheetName", m_sheet->sheetName(), newName));
    }
    
    QString newFile = getPropertyValue("fileName").toString();
    if (newFile != m_sheet->fileName()) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_sheet, "fileName", m_sheet->fileName(), newFile));
    }
    
    m_undoStack->endMacro();
}

void SheetPropertiesDialog::applyPreview() {
    QString newName = getPropertyValue("sheetName").toString();
    QString newFile = getPropertyValue("fileName").toString();
    
    m_sheet->setSheetName(newName);
    m_sheet->setFileName(newFile);
    m_sheet->update();
}

void SheetPropertiesDialog::onSyncPorts() {
    // Force immediate port update
    m_sheet->updatePorts();
}

void SheetPropertiesDialog::onBrowseFile() {
    QString path = QFileDialog::getOpenFileName(this, "Select Schematic File", "", "Schematic Files (*.sch *.json)");
    if (!path.isEmpty()) {
        setPropertyValue("fileName", path);
    }
}
