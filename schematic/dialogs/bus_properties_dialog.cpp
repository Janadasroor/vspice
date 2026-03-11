#include "bus_properties_dialog.h"
#include "../editor/schematic_commands.h"

BusPropertiesDialog::BusPropertiesDialog(BusItem* bus, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({bus}, undoStack, scene, parent), m_bus(bus) {
    
    setWindowTitle("Bus Properties");
    
    PropertyTab generalTab;
    generalTab.title = "General";
    
    generalTab.fields.append({"busName", "Bus Name", PropertyField::Text});
    generalTab.fields.append({"width", "Line Width", PropertyField::Double, 0.5, {}, "mm"});
    
    addTab(generalTab);
    
    setPropertyValue("busName", bus->netName());
    setPropertyValue("width", bus->pen().widthF());
}

void BusPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    m_undoStack->beginMacro("Update Bus Properties");
    
    QString newName = getPropertyValue("busName").toString();
    double newWidth = getPropertyValue("width").toDouble();

    for (auto* item : m_items) {
        if (auto* bus = dynamic_cast<BusItem*>(item)) {
            if (newName != bus->netName()) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, bus, "netName", bus->netName(), newName));
            }
            if (std::abs(newWidth - bus->pen().widthF()) > 0.001) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, bus, "Width", bus->pen().widthF(), newWidth));
            }
        }
    }
    
    m_undoStack->endMacro();
}

void BusPropertiesDialog::applyPreview() {
    QString newName = getPropertyValue("busName").toString();
    double newWidth = getPropertyValue("width").toDouble();
    
    for (auto* item : m_items) {
        if (auto* bus = dynamic_cast<BusItem*>(item)) {
            bus->setNetName(newName);
            QPen p = bus->pen();
            p.setWidthF(newWidth);
            bus->setPen(p);
            bus->update();
        }
    }
}
