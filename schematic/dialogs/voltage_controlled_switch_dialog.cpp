#include "voltage_controlled_switch_dialog.h"
#include "../items/voltage_controlled_switch_item.h"
#include "theme_manager.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

VoltageControlledSwitchDialog::VoltageControlledSwitchDialog(VoltageControlledSwitchItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("Voltage Controlled Switch Properties");
    setModal(true);
    resize(380, 220);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

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

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyChanges();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (m_item) {
        m_modelName->setText(m_item->modelName());
        m_ron->setText(m_item->ron());
        m_roff->setText(m_item->roff());
        m_vt->setText(m_item->vt());
        m_vh->setText(m_item->vh());
    }

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void VoltageControlledSwitchDialog::applyChanges() {
    if (!m_item) return;
    m_item->setModelName(m_modelName->text());
    m_item->setRon(m_ron->text());
    m_item->setRoff(m_roff->text());
    m_item->setVt(m_vt->text());
    m_item->setVh(m_vh->text());
    m_item->update();
}
