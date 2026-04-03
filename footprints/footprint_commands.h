#ifndef FOOTPRINT_COMMANDS_H
#define FOOTPRINT_COMMANDS_H

#include <QUndoCommand>
#include <QPointer>
#include <QGraphicsItem>
#include "models/footprint_definition.h"

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;

class FootprintEditor;

/**
 * @brief Command to add a primitive to the footprint editor
 */
class AddFootprintPrimitiveCommand : public QUndoCommand {
public:
    AddFootprintPrimitiveCommand(QPointer<FootprintEditor> editor,
                                 const FootprintPrimitive& prim);

    void redo() override;
    void undo() override;

private:
    QPointer<FootprintEditor> m_editor;
    FootprintPrimitive        m_prim;
    int                       m_insertedIndex = -1;
};

/**
 * @brief Command to remove a primitive from the footprint editor
 */
class RemoveFootprintPrimitiveCommand : public QUndoCommand {
public:
    RemoveFootprintPrimitiveCommand(QPointer<FootprintEditor> editor, int index);

    void redo() override;
    void undo() override;

private:
    QPointer<FootprintEditor> m_editor;
    int                       m_index = -1;
    FootprintPrimitive        m_prim;
};

/**
 * @brief Command to update the entire footprint definition
 */
class UpdateFootprintCommand : public QUndoCommand {
public:
    UpdateFootprintCommand(QPointer<FootprintEditor> editor,
                           const FootprintDefinition& oldDef,
                           const FootprintDefinition& newDef,
                           const QString& label = "Update Footprint");

    void undo() override;
    void redo() override;

private:
    QPointer<FootprintEditor> m_editor;
    FootprintDefinition       m_oldDef;
    FootprintDefinition       m_newDef;
};

#endif // FOOTPRINT_COMMANDS_H
