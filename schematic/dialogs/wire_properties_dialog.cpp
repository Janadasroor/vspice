#include "wire_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include <QGraphicsScene>

WirePropertiesDialog::WirePropertiesDialog(WireItem* wire, NetManager* netManager, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({wire}, undoStack, scene, parent), m_wire(wire), m_netManager(netManager) {
    setWindowTitle("Wire Properties");
    
    PropertyTab electricalTab;
    electricalTab.title = "Electrical";
    
    PropertyField netName;
    netName.name = "netName";
    netName.label = "Net Name";
    netName.type = PropertyField::Text;
    netName.tooltip = "The electrical net name for this wire.";
    electricalTab.fields.append(netName);
    
    addTab(electricalTab);
    
    PropertyTab visualTab;
    visualTab.title = "Visual";
    
    PropertyField width;
    width.name = "width";
    width.label = "Line Width";
    width.type = PropertyField::Double;
    width.unit = "mm";
    visualTab.fields.append(width);
    
    PropertyField style;
    style.name = "style";
    style.label = "Line Style";
    style.type = PropertyField::Choice;
    style.choices = {"Solid", "Dash", "Dot"};
    visualTab.fields.append(style);
    
    addTab(visualTab);
    
    // Initialize values
    QString currentNet = m_netManager ? m_netManager->findNetAtPoint(wire->startPoint()) : "N/A";
    setPropertyValue("netName", currentNet);
    setPropertyValue("width", wire->pen().widthF());
    
    QString styleStr = "Solid";
    if (wire->pen().style() == Qt::DashLine) styleStr = "Dash";
    else if (wire->pen().style() == Qt::DotLine) styleStr = "Dot";
    setPropertyValue("style", styleStr);
}

void WirePropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    m_undoStack->beginMacro("Update Wire Properties");
    
    double newWidth = getPropertyValue("width").toDouble();
    QString newStyle = getPropertyValue("style").toString();

    for (auto* item : m_items) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            if (std::abs(newWidth - wire->pen().widthF()) > 0.001) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, wire, "Width", wire->pen().widthF(), newWidth));
            }
            m_undoStack->push(new ChangePropertyCommand(m_scene, wire, "Line Style", "", newStyle));
        }
    }
    
    m_undoStack->endMacro();
}

void WirePropertiesDialog::applyPreview() {
    double newWidth = getPropertyValue("width").toDouble();
    QString newStyle = getPropertyValue("style").toString();

    for (auto* item : m_items) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            QPen p = wire->pen();
            p.setWidthF(newWidth);
            if (newStyle == "Dash") p.setStyle(Qt::DashLine);
            else if (newStyle == "Dot") p.setStyle(Qt::DotLine);
            else p.setStyle(Qt::SolidLine);
            wire->setPen(p);
            wire->update();
        }
    }
}
