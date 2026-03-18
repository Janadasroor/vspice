#include "switch_properties_dialog.h"
#include "../items/switch_item.h"
#include "../../core/theme_manager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

SwitchPropertiesDialog::SwitchPropertiesDialog(SwitchItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("Switch Properties");
    setModal(true);
    resize(380, 240);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_useModel = new QCheckBox("Use SPICE switch model");
    form->addRow("", m_useModel);

    m_modelName = new QLineEdit();
    form->addRow("Model Name:", m_modelName);

    m_ron = new QLineEdit();
    form->addRow("Ron:", m_ron);

    m_roff = new QLineEdit();
    form->addRow("Roff:", m_roff);

    m_vt = new QLineEdit();
    form->addRow("Vt:", m_vt);

    m_vh = new QLineEdit();
    form->addRow("Vh:", m_vh);

    m_state = new QComboBox();
    m_state->addItems({"Open", "Closed"});
    form->addRow("Initial State:", m_state);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyChanges();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (m_item) {
        m_useModel->setChecked(m_item->useModel());
        m_modelName->setText(m_item->modelName());
        m_ron->setText(m_item->ron());
        m_roff->setText(m_item->roff());
        m_vt->setText(m_item->vt());
        m_vh->setText(m_item->vh());
        m_state->setCurrentText(m_item->isOpen() ? "Open" : "Closed");
    }

    connect(m_useModel, &QCheckBox::toggled, this, [this](bool) {
        updateEnabledState();
    });
    updateEnabledState();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void SwitchPropertiesDialog::updateEnabledState() {
    const bool on = m_useModel && m_useModel->isChecked();
    m_modelName->setEnabled(on);
    m_ron->setEnabled(on);
    m_roff->setEnabled(on);
    m_vt->setEnabled(on);
    m_vh->setEnabled(on);
}

void SwitchPropertiesDialog::applyChanges() {
    if (!m_item) return;

    m_item->setUseModel(m_useModel->isChecked());
    m_item->setModelName(m_modelName->text());
    m_item->setRon(m_ron->text());
    m_item->setRoff(m_roff->text());
    m_item->setVt(m_vt->text());
    m_item->setVh(m_vh->text());
    m_item->setOpen(m_state->currentText() == "Open");
    m_item->update();
}
