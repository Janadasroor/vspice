#include "signal_generator_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../simulator/core/sim_value_parser.h"

SignalGeneratorPropertiesDialog::SignalGeneratorPropertiesDialog(SignalGeneratorItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    
    setWindowTitle("Signal Generator Properties - " + item->reference());
    
    PropertyTab sourceTab;
    sourceTab.title = "Source Configuration";
    
    PropertyField waveField;
    waveField.name = "waveform";
    waveField.label = "Waveform";
    waveField.type = PropertyField::Choice;
    waveField.choices = {"Sine", "Square", "Triangle", "Pulse", "DC"};
    sourceTab.fields.append(waveField);

    sourceTab.fields.append({"freq", "Frequency", PropertyField::EngineeringValue, 1000.0, {}, "Hz"});
    sourceTab.fields.append({"amp", "Amplitude", PropertyField::Double, 5.0, {}, "V"});
    sourceTab.fields.append({"offset", "DC Offset", PropertyField::Double, 0.0, {}, "V"});
    
    addTab(sourceTab);

    PropertyTab acTab;
    acTab.title = "AC Analysis";
    acTab.fields.append({"ac_mag", "AC Magnitude", PropertyField::Double, 1.0, {}, "V"});
    acTab.fields.append({"ac_phase", "AC Phase", PropertyField::Double, 0.0, {}, "°"});
    addTab(acTab);

    PropertyTab simTab;
    simTab.title = "Simulation Control";
    
    PropertyField simType;
    simType.name = "sim_type";
    simType.label = "Auto-Setup Analysis";
    simType.type = PropertyField::Choice;
    simType.choices = {"None", "Transient (Time)", "AC Sweep (Freq)"};
    simTab.fields.append(simType);

    simTab.fields.append({"t_stop", "Stop Time", PropertyField::EngineeringValue, "10ms", {}, "s"});
    simTab.fields.append({"f_start", "Start Freq", PropertyField::EngineeringValue, "10Hz", {}, "Hz"});
    simTab.fields.append({"f_stop", "Stop Freq", PropertyField::EngineeringValue, "1MHz", {}, "Hz"});

    addTab(simTab);
    
    // Initialize
    setPropertyValue("waveform", 
        item->waveform() == SignalGeneratorItem::Sine ? "Sine" :
        item->waveform() == SignalGeneratorItem::Square ? "Square" :
        item->waveform() == SignalGeneratorItem::Triangle ? "Triangle" : 
        item->waveform() == SignalGeneratorItem::Pulse ? "Pulse" : "DC");
    
    setPropertyValue("freq", QString::number(item->frequency()));
    setPropertyValue("amp", item->amplitude());
    setPropertyValue("offset", item->offset());
    setPropertyValue("ac_mag", item->acMagnitude());
    setPropertyValue("ac_phase", item->acPhase());
    
    setPropertyValue("sim_type", "None");
}

void SignalGeneratorPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene) return;

    m_undoStack->beginMacro("Update Signal Generator");
    
    applyPreview(); // Commit changes to item
    
    m_undoStack->endMacro();
}

void SignalGeneratorPropertiesDialog::applyPreview() {
    QString waveStr = getPropertyValue("waveform").toString();
    SignalGeneratorItem::WaveformType wave = SignalGeneratorItem::Sine;
    if (waveStr == "Square") wave = SignalGeneratorItem::Square;
    else if (waveStr == "Triangle") wave = SignalGeneratorItem::Triangle;
    else if (waveStr == "Pulse") wave = SignalGeneratorItem::Pulse;
    else if (waveStr == "DC") wave = SignalGeneratorItem::DC;

    double freq = 1000.0;
    SimValueParser::parseSpiceNumber(getPropertyValue("freq").toString(), freq);
    
    m_item->setWaveform(wave);
    m_item->setFrequency(freq);
    m_item->setAmplitude(getPropertyValue("amp").toDouble());
    m_item->setOffset(getPropertyValue("offset").toDouble());
    m_item->setAcMagnitude(getPropertyValue("ac_mag").toDouble());
    m_item->setAcPhase(getPropertyValue("ac_phase").toDouble());
}
