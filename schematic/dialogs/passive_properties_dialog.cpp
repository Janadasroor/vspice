#include "passive_properties_dialog.h"
#include "../editor/schematic_commands.h"

PassivePropertiesDialog::PassivePropertiesDialog(SchematicItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle(item->itemTypeName() + " Properties");
    
    PropertyTab generalTab;
    generalTab.title = "General";
    
    PropertyField reference;
    reference.name = "reference";
    reference.label = "Reference";
    reference.type = PropertyField::Text;
    generalTab.fields.append(reference);
    
    PropertyField value;
    value.name = "value";
    value.label = "Value";
    value.type = PropertyField::EngineeringValue;
    generalTab.fields.append(value);

    generalTab.fields.append({"exclude_simulation", "Exclude from Simulation", PropertyField::Boolean});
    generalTab.fields.append({"exclude_pcb", "Exclude from PCB Editor", PropertyField::Boolean});
    
    addTab(generalTab);
    
    PropertyTab simulationTab;
    simulationTab.title = "Simulation";
    
    PropertyField spiceModel;
    spiceModel.name = "spiceModel";
    spiceModel.label = "SPICE Model";
    spiceModel.type = PropertyField::Text;
    simulationTab.fields.append(spiceModel);
    
    addTab(simulationTab);
    
    // Initialize values
    setPropertyValue("reference", item->reference());
    setPropertyValue("value", item->value());
    setPropertyValue("exclude_simulation", item->excludeFromSimulation());
    setPropertyValue("exclude_pcb", item->excludeFromPcb());
}

void PassivePropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    m_undoStack->beginMacro("Update Component Properties");
    
    QString newRef = getPropertyValue("reference").toString();
    QString newVal = getPropertyValue("value").toString();
    bool newExcludeSim = getPropertyValue("exclude_simulation").toBool();
    bool newExcludePcb = getPropertyValue("exclude_pcb").toBool();

    for (auto* item : m_items) {
        if (newRef != item->reference() && m_items.size() == 1) {
            // Only apply reference change if it's a single item (bulk ref change is usually bad)
            m_undoStack->push(new ChangePropertyCommand(m_scene, item, "reference", item->reference(), newRef));
        }
        
        if (newVal != item->value()) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, item, "value", item->value(), newVal));
        }

        if (newExcludeSim != item->excludeFromSimulation()) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, item, "exclude_simulation", item->excludeFromSimulation(), newExcludeSim));
        }

        if (newExcludePcb != item->excludeFromPcb()) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, item, "exclude_pcb", item->excludeFromPcb(), newExcludePcb));
        }
    }
    
    m_undoStack->endMacro();
}

void PassivePropertiesDialog::applyPreview() {
    QString newRef = getPropertyValue("reference").toString();
    QString newVal = getPropertyValue("value").toString();
    bool newExcludeSim = getPropertyValue("exclude_simulation").toBool();
    bool newExcludePcb = getPropertyValue("exclude_pcb").toBool();

    for (auto* item : m_items) {
        if (m_items.size() == 1) {
            item->setReference(newRef);
        }
        item->setValue(newVal);
        item->setExcludeFromSimulation(newExcludeSim);
        item->setExcludeFromPcb(newExcludePcb);
        item->update();
    }
}
