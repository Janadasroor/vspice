#include "vcvs_properties_dialog.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

VCVSPropertiesDialog::VCVSPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("VCVS Properties - " + item->reference());
    setModal(true);
    setMinimumWidth(350);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void VCVSPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_gainEdit = new QLineEdit();
    m_gainEdit->setPlaceholderText("e.g. 5, 1k, {V(ctrl)*2}");
    form->addRow("Voltage Gain:", m_gainEdit);

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &VCVSPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_gainEdit, &QLineEdit::textChanged, this, &VCVSPropertiesDialog::updateCommandPreview);
}

void VCVSPropertiesDialog::loadValues() {
    if (!m_item) return;

    QString gain = m_item->value().trimmed();
    if (gain.isEmpty()) gain = "1";
    m_gainEdit->setText(gain);
}

void VCVSPropertiesDialog::updateCommandPreview() {
    QString ref = m_item ? m_item->reference() : "E1";
    QString gain = m_gainEdit ? m_gainEdit->text().trimmed() : "1";
    if (gain.isEmpty()) gain = "1";

    QString cmd = QString("%1 out+ out- ctrl+ ctrl- %2").arg(ref, gain);
    m_commandPreview->setText(cmd);
}

void VCVSPropertiesDialog::applyChanges() {
    accept();
}

QString VCVSPropertiesDialog::gainValue() const {
    return m_gainEdit ? m_gainEdit->text().trimmed() : "1";
}
