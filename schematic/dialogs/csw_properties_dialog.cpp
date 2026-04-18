#include "csw_properties_dialog.h"
#include "../items/schematic_item.h"
#include "theme_manager.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

CSWPropertiesDialog::CSWPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("CSW Switch Properties");
    setModal(true);
    setMinimumWidth(400);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void CSWPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_modelNameEdit = new QLineEdit();
    form->addRow("Model Name:", m_modelNameEdit);

    m_itEdit = new QLineEdit();
    form->addRow("Threshold Current (IT):", m_itEdit);

    m_ihEdit = new QLineEdit();
    form->addRow("Hysteresis Current (IH):", m_ihEdit);

    m_ronEdit = new QLineEdit();
    form->addRow("On Resistance (RON):", m_ronEdit);

    m_roffEdit = new QLineEdit();
    form->addRow("Off Resistance (ROFF):", m_roffEdit);

    m_ctrlSourceEdit = new QLineEdit();
    m_ctrlSourceEdit->setPlaceholderText("e.g. V1, VCTRL");
    form->addRow("Controlling Source (V...):", m_ctrlSourceEdit);

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("Generated Command:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &CSWPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_modelNameEdit, &QLineEdit::textChanged, this, &CSWPropertiesDialog::updateCommandPreview);
    connect(m_itEdit, &QLineEdit::textChanged, this, &CSWPropertiesDialog::updateCommandPreview);
    connect(m_ihEdit, &QLineEdit::textChanged, this, &CSWPropertiesDialog::updateCommandPreview);
    connect(m_ronEdit, &QLineEdit::textChanged, this, &CSWPropertiesDialog::updateCommandPreview);
    connect(m_roffEdit, &QLineEdit::textChanged, this, &CSWPropertiesDialog::updateCommandPreview);
    connect(m_ctrlSourceEdit, &QLineEdit::textChanged, this, &CSWPropertiesDialog::updateCommandPreview);
}

void CSWPropertiesDialog::loadValues() {
    if (!m_item) return;

    QString modelName = m_item->value().trimmed();
    if (modelName.isEmpty()) modelName = "MySwitch";
    m_modelNameEdit->setText(modelName);

    const QMap<QString, QString> exprs = m_item->paramExpressions();
    
    auto getVal = [&](const QString& sKey, const QString& cKey, const QString& def) {
        QString v = exprs.value(sKey).trimmed();
        if (v.isEmpty()) v = exprs.value(cKey).trimmed();
        return v.isEmpty() ? def : v;
    };

    m_itEdit->setText(getVal("switch.it", "csw.it", "0.5"));
    m_ihEdit->setText(getVal("switch.ih", "csw.ih", "0.1"));
    m_ronEdit->setText(getVal("switch.ron", "csw.ron", "1"));
    m_roffEdit->setText(getVal("switch.roff", "csw.roff", "1Meg"));
    m_ctrlSourceEdit->setText(exprs.value("switch.control_source").trimmed());
}

void CSWPropertiesDialog::updateCommandPreview() {
    QString name = m_modelNameEdit->text().trimmed();
    if (name.isEmpty()) name = "MySwitch";
    
    QString cmd = QString(".model %1 CSW(IT=%2 IH=%3 RON=%4 ROFF=%5)")
        .arg(name)
        .arg(m_itEdit->text().trimmed())
        .arg(m_ihEdit->text().trimmed())
        .arg(m_ronEdit->text().trimmed())
        .arg(m_roffEdit->text().trimmed());
        
    m_commandPreview->setText(cmd);
}

void CSWPropertiesDialog::applyChanges() {
    if (m_item) {
        m_item->setParamExpression("switch.it", m_itEdit->text().trimmed());
        m_item->setParamExpression("switch.ih", m_ihEdit->text().trimmed());
        m_item->setParamExpression("switch.ron", m_ronEdit->text().trimmed());
        m_item->setParamExpression("switch.roff", m_roffEdit->text().trimmed());
        m_item->setParamExpression("switch.control_source", m_ctrlSourceEdit->text().trimmed());
        // Value field will be updated via UndoStack in schematic_editor_actions.cpp
    }
    accept();
}

QString CSWPropertiesDialog::modelName() const {
    return m_modelNameEdit->text().trimmed();
}

QString CSWPropertiesDialog::commandText() const {
    return m_commandPreview->text().trimmed();
}
