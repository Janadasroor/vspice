#include "schematic_text_properties_dialog.h"
#include "../editor/schematic_commands.h"

SchematicTextPropertiesDialog::SchematicTextPropertiesDialog(SchematicTextItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item) {
    setWindowTitle("Text Properties");
    
    PropertyTab textTab;
    textTab.title = "Text";
    
    PropertyField text;
    text.name = "text";
    text.label = "Text Content";
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
    
    // Initialize values
    setPropertyValue("text", item->text());
    setPropertyValue("fontSize", item->font().pointSize());
    setPropertyValue("rotation", item->rotation());
    
    QString alignStr = "Left";
    if (item->alignment() == Qt::AlignCenter) alignStr = "Center";
    else if (item->alignment() == Qt::AlignRight) alignStr = "Right";
    setPropertyValue("alignment", alignStr);
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
    
    m_undoStack->endMacro();
}

void SchematicTextPropertiesDialog::applyPreview() {
    QString newText = getPropertyValue("text").toString();
    int newSize = getPropertyValue("fontSize").toInt();
    double newRot = getPropertyValue("rotation").toDouble();
    QString newAlign = getPropertyValue("alignment").toString();

    m_item->setText(newText);
    QFont f = m_item->font();
    f.setPointSize(newSize);
    m_item->setFont(f);
    m_item->setRotation(newRot);
    
    Qt::Alignment align = Qt::AlignLeft;
    if (newAlign == "Center") align = Qt::AlignCenter;
    else if (newAlign == "Right") align = Qt::AlignRight;
    m_item->setAlignment(align);
    
    m_item->update();
}
