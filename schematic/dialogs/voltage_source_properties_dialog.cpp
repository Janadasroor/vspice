#include "voltage_source_properties_dialog.h"
#include "../items/voltage_source_item.h"
#include "../../simulator/core/sim_value_parser.h"
#include "../editor/schematic_commands.h"
#include <QJsonObject>

VoltageSourcePropertiesDialog::VoltageSourcePropertiesDialog(VoltageSourceItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    
    setWindowTitle("Voltage Source Configuration - " + item->reference());
    
    PropertyTab mainTab;
    mainTab.title = "Source Type";
    
    PropertyField typeField;
    typeField.name = "sourceType";
    typeField.label = "Waveform Type";
    typeField.type = PropertyField::Choice;
    typeField.choices = {"DC", "Sine", "Pulse"};
    typeField.defaultValue = (int)item->sourceType();
    mainTab.fields.append(typeField);

    mainTab.fields.append({"exclude_simulation", "Exclude from Simulation", PropertyField::Boolean, item->excludeFromSimulation()});
    mainTab.fields.append({"exclude_pcb", "Exclude from PCB Editor", PropertyField::Boolean, item->excludeFromPcb()});
    
    addTab(mainTab);

    // DC Settings
    PropertyTab dcTab;
    dcTab.title = "DC Value";
    dcTab.fields.append({"dcVoltage", "DC Offset / Voltage", PropertyField::Double, item->dcVoltage(), {}, "V"});
    addTab(dcTab);

    // Sine Settings
    PropertyTab sineTab;
    sineTab.title = "Sine";
    sineTab.fields.append({"sineAmplitude", "Amplitude (pk)", PropertyField::Double, item->sineAmplitude(), {}, "V"});
    sineTab.fields.append({"sineFrequency", "Frequency", PropertyField::EngineeringValue, item->sineFrequency(), {}, "Hz"});
    sineTab.fields.append({"sineOffset", "DC Offset", PropertyField::Double, item->sineOffset(), {}, "V"});
    addTab(sineTab);

    // Pulse Settings
    PropertyTab pulseTab;
    pulseTab.title = "Pulse";
    pulseTab.fields.append({"pulseV1", "Initial Value (V1)", PropertyField::Double, item->pulseV1(), {}, "V"});
    pulseTab.fields.append({"pulseV2", "Pulsed Value (V2)", PropertyField::Double, item->pulseV2(), {}, "V"});
    pulseTab.fields.append({"pulseDelay", "Delay Time", PropertyField::EngineeringValue, item->pulseDelay(), {}, "s"});
    pulseTab.fields.append({"pulseRise", "Rise Time", PropertyField::EngineeringValue, item->pulseRise(), {}, "s"});
    pulseTab.fields.append({"pulseFall", "Fall Time", PropertyField::EngineeringValue, item->pulseFall(), {}, "s"});
    pulseTab.fields.append({"pulseWidth", "On Time (Twidth)", PropertyField::EngineeringValue, item->pulseWidth(), {}, "s"});
    pulseTab.fields.append({"pulsePeriod", "Period (Tperiod)", PropertyField::EngineeringValue, item->pulsePeriod(), {}, "s"});
    addTab(pulseTab);

    // Sync initial visibility
    updateTabVisibility();
}

void VoltageSourcePropertiesDialog::onFieldChanged() {
    SmartPropertiesDialog::onFieldChanged();
    updateTabVisibility();
}

void VoltageSourcePropertiesDialog::updateTabVisibility() {
    QString type = getPropertyValue("sourceType").toString();
    
    // DC tab is index 1, Sine is 2, Pulse is 3
    setTabVisible(1, type == "DC");
    setTabVisible(2, type == "Sine");
    setTabVisible(3, type == "Pulse");
}

void VoltageSourcePropertiesDialog::onApply() {
    m_undoStack->beginMacro("Update Voltage Source");
    
    QJsonObject newState = m_item->toJson();
    newState["sourceType"] = getPropertyValue("sourceType").toInt();
    newState["dcVoltage"] = getPropertyValue("dcVoltage").toDouble();
    
    newState["excludeFromSim"] = getPropertyValue("exclude_simulation").toBool();
    newState["excludeFromPcb"] = getPropertyValue("exclude_pcb").toBool();

    newState["sineAmplitude"] = getPropertyValue("sineAmplitude").toDouble();
    newState["sineFrequency"] = getPropertyValue("sineFrequency").toDouble();
    newState["sineOffset"] = getPropertyValue("sineOffset").toDouble();
    
    newState["pulseV1"] = getPropertyValue("pulseV1").toDouble();
    newState["pulseV2"] = getPropertyValue("pulseV2").toDouble();
    newState["pulseDelay"] = getPropertyValue("pulseDelay").toDouble();
    newState["pulseRise"] = getPropertyValue("pulseRise").toDouble();
    newState["pulseFall"] = getPropertyValue("pulseFall").toDouble();
    newState["pulseWidth"] = getPropertyValue("pulseWidth").toDouble();
    newState["pulsePeriod"] = getPropertyValue("pulsePeriod").toDouble();

    m_undoStack->push(new BulkChangePropertyCommand(m_scene, m_item, newState));
    m_undoStack->endMacro();
}

void VoltageSourcePropertiesDialog::applyPreview() {
    auto type = static_cast<VoltageSourceItem::SourceType>(getPropertyValue("sourceType").toInt());
    m_item->setSourceType(type);
    
    m_item->setExcludeFromSimulation(getPropertyValue("exclude_simulation").toBool());
    m_item->setExcludeFromPcb(getPropertyValue("exclude_pcb").toBool());

    if (type == VoltageSourceItem::DC) {
        m_item->setDcVoltage(getPropertyValue("dcVoltage").toDouble());
    } else if (type == VoltageSourceItem::Sine) {
        m_item->setSineAmplitude(getPropertyValue("sineAmplitude").toDouble());
        m_item->setSineFrequency(getPropertyValue("sineFrequency").toDouble());
        m_item->setSineOffset(getPropertyValue("sineOffset").toDouble());
    } else if (type == VoltageSourceItem::Pulse) {
        m_item->setPulseV1(getPropertyValue("pulseV1").toDouble());
        m_item->setPulseV2(getPropertyValue("pulseV2").toDouble());
        m_item->setPulseDelay(getPropertyValue("pulseDelay").toDouble());
        m_item->setPulseRise(getPropertyValue("pulseRise").toDouble());
        m_item->setPulseFall(getPropertyValue("pulseFall").toDouble());
        m_item->setPulseWidth(getPropertyValue("pulseWidth").toDouble());
        m_item->setPulsePeriod(getPropertyValue("pulsePeriod").toDouble());
    }
    
    m_item->update();
}
