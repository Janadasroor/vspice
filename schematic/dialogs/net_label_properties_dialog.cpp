#include "net_label_properties_dialog.h"
#include "../editor/schematic_commands.h"

NetLabelPropertiesDialog::NetLabelPropertiesDialog(NetLabelItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    
    setWindowTitle("Net Label Properties");
    
    PropertyTab generalTab;
    generalTab.title = "General";
    
    generalTab.fields.append({"label", "Net Name", PropertyField::Text});
    
    PropertyField scope;
    scope.name = "scope";
    scope.label = "Scope";
    scope.type = PropertyField::Choice;
    scope.choices = {"Local", "Global"};
    generalTab.fields.append(scope);
    
    addTab(generalTab);
    
    PropertyTab visualTab;
    visualTab.title = "Appearance";
    visualTab.fields.append({"rotation", "Rotation", PropertyField::Double, 0.0, {}, "°"});
    
    addTab(visualTab);
    
    setPropertyValue("label", item->label());
    setPropertyValue("scope", item->labelScope() == NetLabelItem::Local ? "Local" : "Global");
    setPropertyValue("rotation", item->rotation());
}

void NetLabelPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    m_undoStack->beginMacro("Update Net Label Properties");
    
    QString newLabel = getPropertyValue("label").toString();
    QString newScopeStr = getPropertyValue("scope").toString();
    NetLabelItem::LabelScope scope = (newScopeStr == "Local") ? NetLabelItem::Local : NetLabelItem::Global;
    double newRot = getPropertyValue("rotation").toDouble();

    for (auto* item : m_items) {
        if (auto* nl = dynamic_cast<NetLabelItem*>(item)) {
            if (newLabel != nl->label()) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, nl, "value", nl->label(), newLabel));
            }
            if (scope != nl->labelScope()) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, nl, "Scope", (int)nl->labelScope(), (int)scope));
            }
            if (std::abs(newRot - nl->rotation()) > 0.001) {
                m_undoStack->push(new RotateItemCommand(m_scene, {nl}, newRot - nl->rotation()));
            }
        }
    }
    
    m_undoStack->endMacro();
}

void NetLabelPropertiesDialog::applyPreview() {
    QString newLabel = getPropertyValue("label").toString();
    QString newScopeStr = getPropertyValue("scope").toString();
    NetLabelItem::LabelScope scope = (newScopeStr == "Local") ? NetLabelItem::Local : NetLabelItem::Global;
    double newRot = getPropertyValue("rotation").toDouble();
    
    for (auto* item : m_items) {
        if (auto* nl = dynamic_cast<NetLabelItem*>(item)) {
            nl->setLabel(newLabel);
            nl->setLabelScope(scope);
            nl->setRotation(newRot);
            nl->update();
        }
    }
}
