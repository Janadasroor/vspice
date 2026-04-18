#include "cccs_properties_dialog.h"

#include "../items/schematic_item.h"
#include "theme_manager.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

CCCSPropertiesDialog::CCCSPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("CCCS Properties - " + item->reference());
    setModal(true);
    setMinimumWidth(420);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void CCCSPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_controlSourceEdit = new QLineEdit();
    m_controlSourceEdit->setPlaceholderText("e.g. V1 (sense voltage source name)");
    form->addRow("Control Source:", m_controlSourceEdit);

    m_gainEdit = new QLineEdit();
    m_gainEdit->setPlaceholderText("e.g. 2, 1m, {k}");
    form->addRow("Current Gain:", m_gainEdit);

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &CCCSPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_controlSourceEdit, &QLineEdit::textChanged, this, &CCCSPropertiesDialog::updateCommandPreview);
    connect(m_gainEdit, &QLineEdit::textChanged, this, &CCCSPropertiesDialog::updateCommandPreview);
}

void CCCSPropertiesDialog::loadValues() {
    if (!m_item) return;

    QString value = m_item->value().trimmed();
    QString ctrl;
    QString gain = "1";

    if (!value.isEmpty()) {
        const QStringList parts = value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            ctrl = parts.at(0);
            if (parts.size() > 1) gain = parts.at(1);
        }
    }

    if (ctrl.compare("F", Qt::CaseInsensitive) == 0 || ctrl.isEmpty()) ctrl = "V1";
    if (gain.compare("F", Qt::CaseInsensitive) == 0 || gain.isEmpty()) gain = "1";

    m_controlSourceEdit->setText(ctrl);
    m_gainEdit->setText(gain);
}

void CCCSPropertiesDialog::updateCommandPreview() {
    const QString ref = m_item ? m_item->reference() : "F1";
    QString ctrl = m_controlSourceEdit ? m_controlSourceEdit->text().trimmed() : "V1";
    QString gain = m_gainEdit ? m_gainEdit->text().trimmed() : "1";

    if (ctrl.isEmpty()) ctrl = "V1";
    if (gain.isEmpty()) gain = "1";

    QString ctrlShown = ctrl;
    if (!ctrlShown.startsWith("V", Qt::CaseInsensitive)) ctrlShown = "V" + ctrlShown;

    m_commandPreview->setText(QString("%1 out+ out- %2 %3").arg(ref, ctrlShown, gain));
}

void CCCSPropertiesDialog::applyChanges() {
    accept();
}

QString CCCSPropertiesDialog::controlSource() const {
    QString ctrl = m_controlSourceEdit ? m_controlSourceEdit->text().trimmed() : QString();
    if (ctrl.isEmpty()) ctrl = "V1";
    return ctrl;
}

QString CCCSPropertiesDialog::gainValue() const {
    QString gain = m_gainEdit ? m_gainEdit->text().trimmed() : QString();
    if (gain.isEmpty()) gain = "1";
    return gain;
}
