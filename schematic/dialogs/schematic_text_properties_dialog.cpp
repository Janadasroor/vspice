#include "schematic_text_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../../simulator/bridge/model_library_manager.h"
#include "../items/generic_component_item.h"
#include <QCompleter>

SchematicTextPropertiesDialog::SchematicTextPropertiesDialog(SchematicTextItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle("Text Properties");

    // Detect if this label belongs to a component
    m_parentComponent = dynamic_cast<GenericComponentItem*>(item->parentItem());

    PropertyTab textTab;
    textTab.title = "Text";

    PropertyField text;
    text.name = "text";
    text.label = m_parentComponent ? "Label Text" : "Text Content";
    text.type = PropertyField::Text;
    textTab.fields.append(text);

    PropertyField fontSize;
    fontSize.name = "fontSize";
    fontSize.label = "Font Size";
    fontSize.type = PropertyField::Integer;
    fontSize.unit = "pt";
    textTab.fields.append(fontSize);

    addTab(textTab);

    PropertyTab styleTab;
    styleTab.title = "Style";

    PropertyField alignment;
    alignment.name = "alignment";
    alignment.label = "Alignment";
    alignment.type = PropertyField::Choice;
    alignment.choices = {"Left", "Center", "Right"};
    styleTab.fields.append(alignment);

    PropertyField rotation;
    rotation.name = "rotation";
    rotation.label = "Rotation";
    rotation.type = PropertyField::Double;
    rotation.unit = "°";
    styleTab.fields.append(rotation);

    addTab(styleTab);

    // Model autocomplete tab
    PropertyTab modelTab;
    modelTab.title = "Model";

    PropertyField modelName;
    modelName.name = "model";
    modelName.label = m_parentComponent ? "Component Model Name" : "SPICE Model Name";
    modelName.type = PropertyField::Text;
    modelTab.fields.append(modelName);

    addTab(modelTab);

    // Initialize values
    setPropertyValue("text", item->text());
    setPropertyValue("fontSize", item->font().pointSize());
    setPropertyValue("rotation", item->rotation());

    QString alignStr = "Left";
    if (item->alignment() == Qt::AlignCenter) alignStr = "Center";
    else if (item->alignment() == Qt::AlignRight) alignStr = "Right";
    setPropertyValue("alignment", alignStr);

    // For component labels, show the parent component's model
    if (m_parentComponent) {
        setPropertyValue("model", m_parentComponent->spiceModel());
    } else {
        setPropertyValue("model", item->spiceModel());
    }

    // Add autocomplete completer to model field
    if (auto* modelEdit = qobject_cast<QLineEdit*>(m_widgets.value("model"))) {
        modelEdit->setPlaceholderText("e.g. 2N7000, 1N4148, 2N2222");

        QStringList allModels;
        for (const auto& info : ModelLibraryManager::instance().allModels()) {
            allModels.append(info.name);
        }
        allModels.sort(Qt::CaseInsensitive);
        allModels.removeDuplicates();
        auto* completer = new QCompleter(allModels, this);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        modelEdit->setCompleter(completer);
    }
}

void SchematicTextPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene) return;

    m_undoStack->beginMacro("Update Text Properties");

    QString newText = getPropertyValue("text").toString();
    if (newText != m_item->text()) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Text", m_item->text(), newText));
    }

    int newSize = getPropertyValue("fontSize").toInt();
    if (newSize != m_item->font().pointSize()) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Font Size", m_item->font().pointSize(), newSize));
    }

    double newRot = getPropertyValue("rotation").toDouble();
    if (std::abs(newRot - m_item->rotation()) > 0.001) {
        m_undoStack->push(new RotateItemCommand(m_scene, {m_item}, newRot - m_item->rotation()));
    }

    QString newAlign = getPropertyValue("alignment").toString();
    m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Alignment", "", newAlign));

    // Apply model to parent component if this is a component label
    QString newModel = getPropertyValue("model").toString().trimmed();
    if (m_parentComponent) {
        if (newModel != m_parentComponent->spiceModel()) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, m_parentComponent, "Model", m_parentComponent->spiceModel(), newModel));
        }
    } else if (newModel != m_item->spiceModel()) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Model", m_item->spiceModel(), newModel));
    }

    m_undoStack->endMacro();
}

void SchematicTextPropertiesDialog::applyPreview() {
    QString newText = getPropertyValue("text").toString();
    int newSize = getPropertyValue("fontSize").toInt();
    double newRot = getPropertyValue("rotation").toDouble();
    QString newAlign = getPropertyValue("alignment").toString();
    QString newModel = getPropertyValue("model").toString().trimmed();

    m_item->setText(newText);
    QFont f = m_item->font();
    f.setPointSize(newSize);
    m_item->setFont(f);
    m_item->setRotation(newRot);

    Qt::Alignment align = Qt::AlignLeft;
    if (newAlign == "Center") align = Qt::AlignCenter;
    else if (newAlign == "Right") align = Qt::AlignRight;
    m_item->setAlignment(align);

    // Apply model to parent component if this is a component label
    if (m_parentComponent) {
        m_parentComponent->setSpiceModel(newModel);
    } else {
        m_item->setSpiceModel(newModel);
    }

    m_item->update();
}
