#include "ccvs_properties_dialog.h"

#include "../items/schematic_item.h"
#include "theme_manager.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

CCVSPropertiesDialog::CCVSPropertiesDialog(SchematicItem* item, QWidget* parent)
    : QDialog(parent), m_item(item) {
    setWindowTitle("CCVS Properties - " + item->reference());
    setModal(true);
    setMinimumWidth(420);

    setupUI();
    loadValues();
    updateCommandPreview();

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
}

void CCVSPropertiesDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_controlSourceEdit = new QLineEdit();
    m_controlSourceEdit->setPlaceholderText("e.g. V1 (sense voltage source name)");
    form->addRow("Control Source:", m_controlSourceEdit);

    m_gainEdit = new QLineEdit();
    m_gainEdit->setPlaceholderText("e.g. 1, 10, {Rm}");
    form->addRow("Transresistance:", m_gainEdit);

    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("SPICE Preview:"));
    m_commandPreview = new QLineEdit();
    m_commandPreview->setReadOnly(true);
    m_commandPreview->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");
    mainLayout->addWidget(m_commandPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &CCVSPropertiesDialog::applyChanges);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_controlSourceEdit, &QLineEdit::textChanged, this, &CCVSPropertiesDialog::updateCommandPreview);
    connect(m_gainEdit, &QLineEdit::textChanged, this, &CCVSPropertiesDialog::updateCommandPreview);
}

void CCVSPropertiesDialog::loadValues() {
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

    if (ctrl.compare("H", Qt::CaseInsensitive) == 0 || ctrl.isEmpty()) ctrl = "V1";
    if (gain.compare("H", Qt::CaseInsensitive) == 0 || gain.isEmpty()) gain = "1";

    m_controlSourceEdit->setText(ctrl);
    m_gainEdit->setText(gain);
}

void CCVSPropertiesDialog::updateCommandPreview() {
    const QString ref = m_item ? m_item->reference() : "H1";
    QString ctrl = m_controlSourceEdit ? m_controlSourceEdit->text().trimmed() : "V1";
    QString gain = m_gainEdit ? m_gainEdit->text().trimmed() : "1";

    if (ctrl.isEmpty()) ctrl = "V1";
    if (gain.isEmpty()) gain = "1";

    QString ctrlShown = ctrl;
    if (!ctrlShown.startsWith("V", Qt::CaseInsensitive)) ctrlShown = "V" + ctrlShown;

    m_commandPreview->setText(QString("%1 out+ out- %2 %3").arg(ref, ctrlShown, gain));
}

void CCVSPropertiesDialog::applyChanges() {
    accept();
}

QString CCVSPropertiesDialog::controlSource() const {
    QString ctrl = m_controlSourceEdit ? m_controlSourceEdit->text().trimmed() : QString();
    if (ctrl.isEmpty()) ctrl = "V1";
    return ctrl;
}

QString CCVSPropertiesDialog::transresistance() const {
    QString gain = m_gainEdit ? m_gainEdit->text().trimmed() : QString();
    if (gain.isEmpty()) gain = "1";
    return gain;
}
