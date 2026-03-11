#include "logic_analyzer_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../simulator/core/sim_value_parser.h"

LogicAnalyzerPropertiesDialog::LogicAnalyzerPropertiesDialog(LogicAnalyzerItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    
    setWindowTitle("Logic Analyzer Properties - " + item->reference());
    
    PropertyTab generalTab;
    generalTab.title = "Channels";
    
    PropertyField chanField;
    chanField.name = "channels";
    chanField.label = "Number of Channels";
    chanField.type = PropertyField::Integer;
    chanField.defaultValue = 8;
    generalTab.fields.append(chanField);
    
    addTab(generalTab);
    
    PropertyTab captureTab;
    captureTab.title = "Capture";
    
    PropertyField rateField;
    rateField.name = "sampleRate";
    rateField.label = "Sample Rate";
    rateField.type = PropertyField::EngineeringValue;
    rateField.defaultValue = 1000000.0;
    rateField.unit = "Hz";
    captureTab.fields.append(rateField);
    
    PropertyField triggerMode;
    triggerMode.name = "trigger";
    triggerMode.label = "Trigger Mode";
    triggerMode.type = PropertyField::Choice;
    triggerMode.choices = {"Auto", "Rising Edge", "Falling Edge", "Pattern"};
    captureTab.fields.append(triggerMode);
    
    addTab(captureTab);

    PropertyTab simTab;
    simTab.title = "Simulation Control";
    simTab.fields.append({"sim_type", "Sync Simulation Type", PropertyField::Choice, "None", {"None", "Transient"}});
    simTab.fields.append({"t_stop", "Stop Time", PropertyField::EngineeringValue, "10ms", {}, "s"});
    addTab(simTab);
    
    setPropertyValue("channels", item->channelCount());
    setPropertyValue("sampleRate", "1M");
    setPropertyValue("trigger", "Auto");
    setPropertyValue("sim_type", "None");
}

void LogicAnalyzerPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    m_undoStack->beginMacro("Update Logic Analyzer");
    
    int newCount = getPropertyValue("channels").toInt();
    if (newCount != m_item->channelCount()) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "channels", m_item->channelCount(), newCount));
    }
    
    m_undoStack->endMacro();
}

void LogicAnalyzerPropertiesDialog::applyPreview() {
    int newCount = getPropertyValue("channels").toInt();
    m_item->setChannelCount(newCount);
    m_item->update();
}
