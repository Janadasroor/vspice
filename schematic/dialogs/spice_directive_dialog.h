#ifndef SPICE_DIRECTIVE_DIALOG_H
#define SPICE_DIRECTIVE_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QUndoStack>
#include <QGraphicsScene>
#include "../items/schematic_spice_directive_item.h"

class SpiceHighlighter;

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
    void validateDirectiveText();
    void updatePreview();

    SchematicSpiceDirectiveItem* m_item;
    QUndoStack* m_undoStack;
    QGraphicsScene* m_scene;

    QPlainTextEdit* m_commandEdit;
    QLabel* m_validationLabel;
    QTextBrowser* m_previewEdit;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    SpiceHighlighter* m_highlighter;
};

#endif // SPICE_DIRECTIVE_DIALOG_H
