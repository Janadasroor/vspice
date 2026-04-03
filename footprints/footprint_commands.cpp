#include "footprint_commands.h"
#include "footprint_editor.h"

// ----------------------------------------------------------------------------
// AddFootprintPrimitiveCommand
// ----------------------------------------------------------------------------

AddFootprintPrimitiveCommand::AddFootprintPrimitiveCommand(QPointer<FootprintEditor> editor,
                                                           const FootprintPrimitive& prim)
    : m_editor(editor), m_prim(prim) {
    setText("Add Primitive");
}

void AddFootprintPrimitiveCommand::redo() {
    if (m_editor) {
        FootprintDefinition def = m_editor->footprintDefinition();
        def.addPrimitive(m_prim);
        m_insertedIndex = def.primitives().size() - 1;
        m_editor->setFootprintDefinition(def);
    }
}

void AddFootprintPrimitiveCommand::undo() {
    if (m_editor && m_insertedIndex != -1) {
        FootprintDefinition def = m_editor->footprintDefinition();
        if (m_insertedIndex < def.primitives().size()) {
            def.primitives().removeAt(m_insertedIndex);
            m_editor->setFootprintDefinition(def);
        }
    }
}

// ----------------------------------------------------------------------------
// RemoveFootprintPrimitiveCommand
// ----------------------------------------------------------------------------

RemoveFootprintPrimitiveCommand::RemoveFootprintPrimitiveCommand(QPointer<FootprintEditor> editor, int index)
    : m_editor(editor), m_index(index) {
    setText("Remove Primitive");
    if (m_editor) {
        FootprintDefinition def = m_editor->footprintDefinition();
        if (index >= 0 && index < def.primitives().size()) {
            m_prim = def.primitives().at(index);
        }
    }
}

void RemoveFootprintPrimitiveCommand::redo() {
    if (m_editor && m_index != -1) {
        FootprintDefinition def = m_editor->footprintDefinition();
        if (m_index < def.primitives().size()) {
            def.primitives().removeAt(m_index);
            m_editor->setFootprintDefinition(def);
        }
    }
}

void RemoveFootprintPrimitiveCommand::undo() {
    if (m_editor && m_index != -1) {
        FootprintDefinition def = m_editor->footprintDefinition();
        def.primitives().insert(m_index, m_prim);
        m_editor->setFootprintDefinition(def);
    }
}

// ----------------------------------------------------------------------------
// UpdateFootprintCommand
// ----------------------------------------------------------------------------

UpdateFootprintCommand::UpdateFootprintCommand(QPointer<FootprintEditor> editor,
                                               const FootprintDefinition& oldDef,
                                               const FootprintDefinition& newDef,
                                               const QString& label)
    : m_editor(editor), m_oldDef(oldDef), m_newDef(newDef) {
    setText(label);
}

void UpdateFootprintCommand::undo() {
    if (m_editor) {
        m_editor->setFootprintDefinition(m_oldDef);
    }
}

void UpdateFootprintCommand::redo() {
    if (m_editor) {
        m_editor->setFootprintDefinition(m_newDef);
    }
}
