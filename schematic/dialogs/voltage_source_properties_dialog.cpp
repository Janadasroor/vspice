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
    typeField.choices = {"DC", "Sine", "Pulse", "Behavioral"};
    typeField.defaultValue = (int)item->sourceType();
    mainTab.fields.append(typeField);

    mainTab.fields.append({"exclude_simulation", "Exclude from Simulation", PropertyField::Boolean, item->excludeFromSimulation()});
    mainTab.fields.append({"exclude_pcb", "Exclude from PCB Editor", PropertyField::Boolean, item->excludeFromPcb()});
    
    addTab(mainTab);

    // DC Settings
    PropertyTab dcTab;
    dcTab.title = "DC Value";
    dcTab.fields.append({"dcVoltage", "DC Offset / Voltage", PropertyField::EngineeringValue, item->dcVoltage(), {}, "V"});
    addTab(dcTab);

    // Sine Settings
    PropertyTab sineTab;
    sineTab.title = "Sine";
    sineTab.fields.append({"sineAmplitude", "Amplitude (pk)", PropertyField::EngineeringValue, item->sineAmplitude(), {}, "V"});
    sineTab.fields.append({"sineFrequency", "Frequency", PropertyField::EngineeringValue, item->sineFrequency(), {}, "Hz"});
    sineTab.fields.append({"sineOffset", "DC Offset", PropertyField::EngineeringValue, item->sineOffset(), {}, "V"});
    addTab(sineTab);

    // Pulse Settings
    PropertyTab pulseTab;
    pulseTab.title = "Pulse";
    pulseTab.fields.append({"pulseV1", "Initial Value (V1)", PropertyField::EngineeringValue, item->pulseV1(), {}, "V"});
    pulseTab.fields.append({"pulseV2", "Pulsed Value (V2)", PropertyField::EngineeringValue, item->pulseV2(), {}, "V"});
    pulseTab.fields.append({"pulseDelay", "Delay Time", PropertyField::EngineeringValue, item->pulseDelay(), {}, "s"});
    pulseTab.fields.append({"pulseRise", "Rise Time", PropertyField::EngineeringValue, item->pulseRise(), {}, "s"});
    pulseTab.fields.append({"pulseFall", "Fall Time", PropertyField::EngineeringValue, item->pulseFall(), {}, "s"});
    pulseTab.fields.append({"pulseWidth", "On Time (Twidth)", PropertyField::EngineeringValue, item->pulseWidth(), {}, "s"});
    pulseTab.fields.append({"pulsePeriod", "Period (Tperiod)", PropertyField::EngineeringValue, item->pulsePeriod(), {}, "s"});
    addTab(pulseTab);

    // Behavioral Settings (BV)
    PropertyTab behavioralTab;
    behavioralTab.title = "Behavioral (BV)";
    PropertyField exprField;
    exprField.name = "behavioralExpr";
    exprField.label = "Expression (V=...)";
    exprField.type = PropertyField::MultilineText;
    exprField.defaultValue = item->value();
    exprField.tooltip = "Behavioral voltage: V=<expression>, e.g. V=V(in)*2 or V=if(V(in)>0,1,0)";
    exprField.validator = [](const QVariant& value) -> QString {
        const QString v = value.toString().trimmed();
        if (v.isEmpty()) return "Expression is empty.";
        int depth = 0;
        for (const QChar& c : v) {
            if (c == '(') depth++;
            else if (c == ')') depth--;
            if (depth < 0) break;
        }
        if (depth != 0) return "Unbalanced parentheses.";
        return QString();
    };
    behavioralTab.fields.append(exprField);
    addTab(behavioralTab);

    // Sync initial visibility
    updateTabVisibility();
}

void VoltageSourcePropertiesDialog::onFieldChanged() {
    SmartPropertiesDialog::onFieldChanged();
    updateTabVisibility();
}

void VoltageSourcePropertiesDialog::updateTabVisibility() {
    QString type = getPropertyValue("sourceType").toString();
    
    // DC tab is index 1, Sine is 2, Pulse is 3, Behavioral is 4
    setTabVisible(1, type == "DC");
    setTabVisible(2, type == "Sine");
    setTabVisible(3, type == "Pulse");
    setTabVisible(4, type == "Behavioral");
}

void VoltageSourcePropertiesDialog::onApply() {
    m_undoStack->beginMacro("Update Voltage Source");
    
    QJsonObject newState = m_item->toJson();
    newState["sourceType"] = getPropertyValue("sourceType").toInt();
    newState["dcVoltage"] = getPropertyValue("dcVoltage").toString();
    
    newState["excludeFromSim"] = getPropertyValue("exclude_simulation").toBool();
    newState["excludeFromPcb"] = getPropertyValue("exclude_pcb").toBool();

    newState["sineAmplitude"] = getPropertyValue("sineAmplitude").toString();
    newState["sineFrequency"] = getPropertyValue("sineFrequency").toString();
    newState["sineOffset"] = getPropertyValue("sineOffset").toString();
    
    newState["pulseV1"] = getPropertyValue("pulseV1").toString();
    newState["pulseV2"] = getPropertyValue("pulseV2").toString();
    newState["pulseDelay"] = getPropertyValue("pulseDelay").toString();
    newState["pulseRise"] = getPropertyValue("pulseRise").toString();
    newState["pulseFall"] = getPropertyValue("pulseFall").toString();
    newState["pulseWidth"] = getPropertyValue("pulseWidth").toString();
    newState["pulsePeriod"] = getPropertyValue("pulsePeriod").toString();

    if (getPropertyValue("sourceType").toString() == "Behavioral") {
        QString expr = getPropertyValue("behavioralExpr").toString().trimmed();
        if (expr.isEmpty()) expr = "V=0";
        if (!expr.startsWith("V=", Qt::CaseInsensitive)) expr = "V=" + expr;
        newState["value"] = expr;
    }

    m_undoStack->push(new BulkChangePropertyCommand(m_scene, m_item, newState));
    m_undoStack->endMacro();
}

void VoltageSourcePropertiesDialog::applyPreview() {
    auto type = static_cast<VoltageSourceItem::SourceType>(getPropertyValue("sourceType").toInt());
    m_item->setSourceType(type);
    
    m_item->setExcludeFromSimulation(getPropertyValue("exclude_simulation").toBool());
    m_item->setExcludeFromPcb(getPropertyValue("exclude_pcb").toBool());

    if (type == VoltageSourceItem::DC) {
        m_item->setDcVoltage(getPropertyValue("dcVoltage").toString());
    } else if (type == VoltageSourceItem::Sine) {
        m_item->setSineAmplitude(getPropertyValue("sineAmplitude").toString());
        m_item->setSineFrequency(getPropertyValue("sineFrequency").toString());
        m_item->setSineOffset(getPropertyValue("sineOffset").toString());
    } else if (type == VoltageSourceItem::Pulse) {
        m_item->setPulseV1(getPropertyValue("pulseV1").toString());
        m_item->setPulseV2(getPropertyValue("pulseV2").toString());
        m_item->setPulseDelay(getPropertyValue("pulseDelay").toString());
        m_item->setPulseRise(getPropertyValue("pulseRise").toString());
        m_item->setPulseFall(getPropertyValue("pulseFall").toString());
        m_item->setPulseWidth(getPropertyValue("pulseWidth").toString());
        m_item->setPulsePeriod(getPropertyValue("pulsePeriod").toString());
    } else if (type == VoltageSourceItem::Behavioral) {
        QString expr = getPropertyValue("behavioralExpr").toString().trimmed();
        if (expr.isEmpty()) expr = "V=0";
        if (!expr.startsWith("V=", Qt::CaseInsensitive)) expr = "V=" + expr;
        m_item->setValue(expr);
    }
    
    m_item->update();
}
