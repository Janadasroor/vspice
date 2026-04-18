#include "switch_properties_dialog.h"
#include "../items/switch_item.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

SwitchPropertiesDialog::SwitchPropertiesDialog(SchematicItem* item, QWidget* parent)
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
        if (auto* sw = dynamic_cast<SwitchItem*>(m_item.data())) {
            m_useModel->setChecked(sw->useModel());
            m_modelName->setText(sw->modelName());
            m_ron->setText(sw->ron());
            m_roff->setText(sw->roff());
            m_vt->setText(sw->vt());
            m_vh->setText(sw->vh());
            m_state->setCurrentText(sw->isOpen() ? "Open" : "Closed");
        } else {
            const QMap<QString, QString> exprs = m_item->paramExpressions();
            const QString useModelExpr = exprs.value("switch.use_model").trimmed().toLower();
            bool useModel = (useModelExpr == "1" || useModelExpr == "true");
            if (useModelExpr.isEmpty() && m_item->connectionPoints().size() >= 4) {
                useModel = true; // Default voltage-controlled behavior for 4-pin symbols.
            }
            m_useModel->setChecked(useModel);
            m_modelName->setText(exprs.value("switch.model_name").trimmed().isEmpty() ? "MySwitchName" : exprs.value("switch.model_name").trimmed());
            m_ron->setText(exprs.value("switch.ron").trimmed().isEmpty() ? "0.1" : exprs.value("switch.ron").trimmed());
            m_roff->setText(exprs.value("switch.roff").trimmed().isEmpty() ? "1Meg" : exprs.value("switch.roff").trimmed());
            m_vt->setText(exprs.value("switch.vt").trimmed().isEmpty() ? "0.5" : exprs.value("switch.vt").trimmed());
            m_vh->setText(exprs.value("switch.vh").trimmed().isEmpty() ? "0.1" : exprs.value("switch.vh").trimmed());
            const QString stateExpr = exprs.value("switch.state").trimmed().toLower();
            m_state->setCurrentText(stateExpr == "closed" ? "Closed" : "Open");
        }
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

    if (auto* sw = dynamic_cast<SwitchItem*>(m_item.data())) {
        sw->setUseModel(m_useModel->isChecked());
        sw->setModelName(m_modelName->text());
        sw->setRon(m_ron->text());
        sw->setRoff(m_roff->text());
        sw->setVt(m_vt->text());
        sw->setVh(m_vh->text());
        sw->setOpen(m_state->currentText() == "Open");
        sw->update();
        return;
    }

    // Generic symbol: store in paramExpressions for netlist generation.
    m_item->setParamExpression("switch.use_model", m_useModel->isChecked() ? "1" : "0");
    m_item->setParamExpression("switch.model_name", m_modelName->text().trimmed());
    m_item->setParamExpression("switch.ron", m_ron->text().trimmed());
    m_item->setParamExpression("switch.roff", m_roff->text().trimmed());
    m_item->setParamExpression("switch.vt", m_vt->text().trimmed());
    m_item->setParamExpression("switch.vh", m_vh->text().trimmed());
    const bool isOpen = (m_state->currentText() == "Open");
    m_item->setParamExpression("switch.state", isOpen ? "open" : "closed");

    // Keep value in sync for non-model switch behavior.
    if (!m_useModel->isChecked()) {
        m_item->setValue(isOpen ? "1e12" : "0.001");
        m_item->setParamExpression("resistance", isOpen ? "1e12" : "0.001");
    }
    m_item->update();
}
