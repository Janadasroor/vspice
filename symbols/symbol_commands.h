#ifndef SYMBOL_COMMANDS_H
#define SYMBOL_COMMANDS_H

#include <QUndoCommand>
#include <QPointer>
#include <QGraphicsItem>
#include "models/symbol_definition.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

class SymbolEditor;

/**
 * @brief Command to add a primitive to the symbol editor
 */
class AddPrimitiveCommand : public QUndoCommand {
public:
    AddPrimitiveCommand(QPointer<SymbolEditor> editor,
                        const SymbolPrimitive& prim,
                        QGraphicsItem* visual);
    ~AddPrimitiveCommand() override;

    void redo() override;
    void undo() override;

private:
    QPointer<SymbolEditor> m_editor;
    SymbolPrimitive        m_prim;
    QGraphicsItem*         m_visual = nullptr;
    int                    m_insertedIndex = -1;
};

/**
 * @brief Command to remove a primitive from the symbol editor
 */
class RemovePrimitiveCommand : public QUndoCommand {
public:
    RemovePrimitiveCommand(QPointer<SymbolEditor> editor, int index);
    ~RemovePrimitiveCommand() override;

    void redo() override;
    void undo() override;

private:
    QPointer<SymbolEditor> m_editor;
    int                    m_index  = -1;
    QGraphicsItem*         m_visual = nullptr;
    SymbolPrimitive        m_prim;
};

/**
 * @brief Command to update the entire symbol definition
 */
class UpdateSymbolCommand : public QUndoCommand {
public:
    UpdateSymbolCommand(QPointer<SymbolEditor> editor,
                        const SymbolDefinition& oldDef,
                        const SymbolDefinition& newDef,
                        const QString& label = "Update Symbol");

    void undo() override;
    void redo() override;

private:
    QPointer<SymbolEditor> m_editor;
    SymbolDefinition       m_oldDef;
    SymbolDefinition       m_newDef;
};

#endif // SYMBOL_COMMANDS_H
