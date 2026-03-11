#include "oscilloscope_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../simulator/core/sim_value_parser.h"

OscilloscopePropertiesDialog::OscilloscopePropertiesDialog(OscilloscopeItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle("Oscilloscope Configuration - " + item->reference());
    
    OscilloscopeItem::Config cfg = item->config();

    PropertyTab channelsTab;
    channelsTab.title = "Channels";
    
    for (int i = 1; i <= 4; ++i) {
        const auto& ch = cfg.channels[i-1];
        
        PropertyField chEnable;
        chEnable.name = QString("ch%1_enable").arg(i);
        chEnable.label = QString("Channel %1 Enable").arg(i);
        chEnable.type = PropertyField::Boolean;
        channelsTab.fields.append(chEnable);
        
        PropertyField chScale;
        chScale.name = QString("ch%1_scale").arg(i);
        chScale.label = QString("  Scale (V/div)");
        chScale.type = PropertyField::Double;
        channelsTab.fields.append(chScale);

        PropertyField chOffset;
        chOffset.name = QString("ch%1_offset").arg(i);
        chOffset.label = QString("  Offset (V)");
        chOffset.type = PropertyField::Double;
        channelsTab.fields.append(chOffset);
    }
    
    addTab(channelsTab);
    
    PropertyTab timebaseTab;
    timebaseTab.title = "Timebase & Trigger";
    
    timebaseTab.fields.append({"time_div", "Time/div", PropertyField::EngineeringValue, "1ms", {}, "s"});
    
    PropertyField trigSource;
    trigSource.name = "trig_source";
    trigSource.label = "Trigger Source";
    trigSource.type = PropertyField::Choice;
    trigSource.choices = {"CH1", "CH2", "CH3", "CH4", "External"};
    timebaseTab.fields.append(trigSource);

    PropertyField trigLevel;
    trigLevel.name = "trig_level";
    trigLevel.label = "Trigger Level";
    trigLevel.type = PropertyField::Double;
    trigLevel.unit = "V";
    timebaseTab.fields.append(trigLevel);
    
    addTab(timebaseTab);

    PropertyTab simTab;
    simTab.title = "Simulation Control";
    
    PropertyField simType;
    simType.name = "sim_type";
    simType.label = "Update Analysis Settings";
    simType.type = PropertyField::Choice;
    simType.choices = {"None", "Transient", "AC Sweep"};
    simTab.fields.append(simType);

    simTab.fields.append({"t_stop", "Transient Stop Time", PropertyField::EngineeringValue, "10ms", {}, "s"});
    simTab.fields.append({"f_start", "AC Start Freq", PropertyField::EngineeringValue, "10Hz", {}, "Hz"});
    simTab.fields.append({"f_stop", "AC Stop Freq", PropertyField::EngineeringValue, "1MHz", {}, "Hz"});

    addTab(simTab);
    
    // Initialize values from current config
    for (int i = 1; i <= 4; ++i) {
        const auto& ch = cfg.channels[i-1];
        setPropertyValue(QString("ch%1_enable").arg(i), ch.enabled);
        setPropertyValue(QString("ch%1_scale").arg(i), ch.scale);
        setPropertyValue(QString("ch%1_offset").arg(i), ch.offset);
    }
    setPropertyValue("time_div", QString::number(cfg.timebase)); 
    setPropertyValue("trig_source", cfg.triggerSource);
    setPropertyValue("trig_level", cfg.triggerLevel);
    setPropertyValue("sim_type", "None");
}

void OscilloscopePropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack) return;

    OscilloscopeItem::Config newCfg = m_item->config();
    for (int i = 1; i <= 4; ++i) {
        newCfg.channels[i-1].enabled = getPropertyValue(QString("ch%1_enable").arg(i)).toBool();
        newCfg.channels[i-1].scale = getPropertyValue(QString("ch%1_scale").arg(i)).toDouble();
        newCfg.channels[i-1].offset = getPropertyValue(QString("ch%1_offset").arg(i)).toDouble();
    }
    
    double tdiv = 0.001;
    SimValueParser::parseSpiceNumber(getPropertyValue("time_div").toString(), tdiv);
    newCfg.timebase = tdiv;
    
    newCfg.triggerSource = getPropertyValue("trig_source").toString();
    newCfg.triggerLevel = getPropertyValue("trig_level").toDouble();

    m_undoStack->push(new ChangeOscilloscopeConfigCommand(m_item, m_item->config(), newCfg));

    // Handle Auto-Setup Analysis
    QString simType = getPropertyValue("sim_type").toString();
    if (simType != "None") {
        // Logic to update global simulation settings would go here.
        // Usually via a signal to the editor or updating SimulationManager directly.
    }
}

void OscilloscopePropertiesDialog::applyPreview() {
    OscilloscopeItem::Config previewCfg = m_item->config();
    for (int i = 1; i <= 4; ++i) {
        previewCfg.channels[i-1].enabled = getPropertyValue(QString("ch%1_enable").arg(i)).toBool();
        previewCfg.channels[i-1].scale = getPropertyValue(QString("ch%1_scale").arg(i)).toDouble();
        previewCfg.channels[i-1].offset = getPropertyValue(QString("ch%1_offset").arg(i)).toDouble();
    }
    
    double tdiv = 0.001;
    SimValueParser::parseSpiceNumber(getPropertyValue("time_div").toString(), tdiv);
    previewCfg.timebase = tdiv;
    
    previewCfg.triggerSource = getPropertyValue("trig_source").toString();
    previewCfg.triggerLevel = getPropertyValue("trig_level").toDouble();

    m_item->setConfig(previewCfg);
}
