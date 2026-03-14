#ifndef SPICE_DIRECTIVE_DIALOG_H
#define SPICE_DIRECTIVE_DIALOG_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUndoStack>
#include <QGraphicsScene>
#include "../items/schematic_spice_directive_item.h"

class SpiceDirectiveDialog : public QDialog {
    Q_OBJECT

public:
    SpiceDirectiveDialog(SchematicSpiceDirectiveItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);

private slots:
    void onAccepted();

private:
    void setupUi();
    void loadFromItem();
    void saveToItem();

    SchematicSpiceDirectiveItem* m_item;
    QUndoStack* m_undoStack;
    QGraphicsScene* m_scene;

    QPlainTextEdit* m_commandEdit;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
};

#endif // SPICE_DIRECTIVE_DIALOG_H
