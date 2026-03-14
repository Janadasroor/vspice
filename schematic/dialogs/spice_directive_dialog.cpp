#include "spice_directive_dialog.h"
#include "../editor/schematic_commands.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

SpiceDirectiveDialog::SpiceDirectiveDialog(SchematicSpiceDirectiveItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene)
{
    setupUi();
    loadFromItem();

    connect(m_okButton, &QPushButton::clicked, this, &SpiceDirectiveDialog::onAccepted);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void SpiceDirectiveDialog::setupUi() {
    setWindowTitle("Edit SPICE Directive");
    resize(500, 300);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* infoLabel = new QLabel("Enter SPICE commands (e.g., .tran 10n 1u, .model, .param):", this);
    mainLayout->addWidget(infoLabel);

    m_commandEdit = new QPlainTextEdit(this);
    m_commandEdit->setPlaceholderText(".tran 0 10m 10u");
    QFont font("Courier New");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    m_commandEdit->setFont(font);
    mainLayout->addWidget(m_commandEdit);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel", this);
    m_okButton = new QPushButton("OK", this);
    m_okButton->setDefault(true);

    btnLayout->addWidget(m_cancelButton);
    btnLayout->addWidget(m_okButton);

    mainLayout->addLayout(btnLayout);
    
    // Apply styling if a theme is available would be nice, but QDialog usually inherits
}

void SpiceDirectiveDialog::loadFromItem() {
    if (m_item) {
        m_commandEdit->setPlainText(m_item->text());
    }
}

void SpiceDirectiveDialog::onAccepted() {
    saveToItem();
    accept();
}

void SpiceDirectiveDialog::saveToItem() {
    if (!m_item) return;

    QString newText = m_commandEdit->toPlainText().trimmed();
    QString oldText = m_item->text();

    if (newText != oldText) {
        if (m_undoStack && m_scene) {
            m_undoStack->push(new ChangePropertyCommand(m_scene, m_item, "Text", oldText, newText));
        } else {
            m_item->setText(newText);
            m_item->update();
        }
    }
}
