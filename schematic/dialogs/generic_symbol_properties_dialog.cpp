#include "generic_symbol_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../core/assignment_validator.h"
#include "../../symbols/symbol_library.h"
#include <QMessageBox>

namespace {

Flux::Model::SymbolDefinition symbolForValidation(const SchematicItem* item) {
    const QString symbolName = item->name().trimmed();
    if (!symbolName.isEmpty()) {
        if (auto* found = SymbolLibraryManager::instance().findSymbol(symbolName)) {
            return found->clone();
        }
    }

    Flux::Model::SymbolDefinition symbol(symbolName.isEmpty() ? item->itemTypeName() : symbolName);
    symbol.setReferencePrefix(item->referencePrefix());
    symbol.setIsPowerSymbol(item->itemType() == SchematicItem::PowerType);
    if (item->itemType() == SchematicItem::ICType) {
        symbol.setCategory("Integrated Circuits");
    }

    const QList<QPointF> pins = item->connectionPoints();
    for (int i = 0; i < pins.size(); ++i) {
        symbol.addPrimitive(Flux::Model::SymbolPrimitive::createPin(pins[i], i + 1, QString::number(i + 1)));
    }

    return symbol;
}

}

GenericSymbolPropertiesDialog::GenericSymbolPropertiesDialog(SchematicItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle(item->itemTypeName() + " Properties");
    
    PropertyTab identityTab;
    identityTab.title = "Identity";
    
    identityTab.fields.append({"reference", "Reference", PropertyField::Text});
    identityTab.fields.append({"value", "Value", PropertyField::Text});
    identityTab.fields.append({"name", "Component Name", PropertyField::Text});
    
    addTab(identityTab);
    
    PropertyTab mfrTab;
    mfrTab.title = "Manufacturer";
    mfrTab.fields.append({"manufacturer", "Manufacturer", PropertyField::Text});
    mfrTab.fields.append({"mpn", "MPN", PropertyField::Text});
    mfrTab.fields.append({"description", "Description", PropertyField::Text});
    
    addTab(mfrTab);
    
    PropertyTab visualTab;
    visualTab.title = "Appearance";
    visualTab.fields.append({"rotation", "Rotation", PropertyField::Double, 0.0, {}, "°"});
    
    addTab(visualTab);
    
    // Initialize values
    setPropertyValue("reference", item->reference());
    setPropertyValue("value", item->value());
    setPropertyValue("name", item->name());
    setPropertyValue("manufacturer", item->manufacturer());
    setPropertyValue("mpn", item->mpn());
    setPropertyValue("description", item->description());
    setPropertyValue("rotation", item->rotation());
}

void GenericSymbolPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    m_undoStack->beginMacro("Update Component Properties");
    
    QString newRef = getPropertyValue("reference").toString();
    QString newVal = getPropertyValue("value").toString();
    double newRot = getPropertyValue("rotation").toDouble();

    for (auto* item : m_items) {
        if (newRef != item->reference() && m_items.size() == 1) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, item, "reference", item->reference(), newRef));
        }
        if (newVal != item->value()) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, item, "value", item->value(), newVal));
        }
        if (std::abs(newRot - item->rotation()) > 0.001) {
            m_undoStack->push(new RotateItemCommand(m_scene, {item}, newRot - item->rotation()));
        }
    }
    
    m_undoStack->endMacro();
}
