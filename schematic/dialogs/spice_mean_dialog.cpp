#include "spice_mean_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

SpiceMeanDialog::SpiceMeanDialog(const QString& initialCommand, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Edit .mean Command");
    setMinimumWidth(420);

    auto* mainLayout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({"avg", "max", "min", "rms"});
    m_signalEdit = new QLineEdit(this);
    m_fromEdit = new QLineEdit(this);
    m_toEdit = new QLineEdit(this);
    m_commandEdit = new QLineEdit(this);
    m_commandEdit->setStyleSheet("color: #3b82f6; font-family: 'Courier New';");

    auto* syntaxLabel = new QLabel("Syntax: .mean [avg|max|min|rms] <signal> [from=<start>] [to=<stop>]", this);
    syntaxLabel->setStyleSheet("color: #6b7280;");
    mainLayout->addWidget(syntaxLabel);

    form->addRow("Statistic:", m_modeCombo);
    form->addRow("Signal:", m_signalEdit);
    form->addRow("From (optional):", m_fromEdit);
    form->addRow("To (optional):", m_toEdit);
    mainLayout->addLayout(form);

    mainLayout->addWidget(new QLabel("Command (editable):", this));
    mainLayout->addWidget(m_commandEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_commandEdit->text().trimmed().isEmpty()) return;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_modeCombo, &QComboBox::currentTextChanged, this, &SpiceMeanDialog::updatePreview);
    connect(m_signalEdit, &QLineEdit::textChanged, this, &SpiceMeanDialog::updatePreview);
    connect(m_fromEdit, &QLineEdit::textChanged, this, &SpiceMeanDialog::updatePreview);
    connect(m_toEdit, &QLineEdit::textChanged, this, &SpiceMeanDialog::updatePreview);
    connect(m_commandEdit, &QLineEdit::editingFinished, this, &SpiceMeanDialog::applyCommandText);

    m_commandEdit->setText(initialCommand.trimmed());
    applyCommandText();
    updatePreview();
}

QString SpiceMeanDialog::commandText() const {
    const QString typed = m_commandEdit->text().trimmed();
    if (!typed.isEmpty()) return typed;

    QString cmd = QString(".mean %1 %2")
                      .arg(m_modeCombo->currentText().trimmed())
                      .arg(m_signalEdit->text().trimmed());
    if (!m_fromEdit->text().trimmed().isEmpty()) {
        cmd += QString(" from=%1").arg(m_fromEdit->text().trimmed());
    }
    if (!m_toEdit->text().trimmed().isEmpty()) {
        cmd += QString(" to=%1").arg(m_toEdit->text().trimmed());
    }
    return cmd;
}

QRegularExpression SpiceMeanDialog::meanRegex() {
    return QRegularExpression(
        "^\\s*\\.mean\\s+(?:(avg|max|min|rms)\\s+)?([^\\s]+)(?:\\s+from\\s*=\\s*([^\\s]+))?(?:\\s+to\\s*=\\s*([^\\s]+))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
}

void SpiceMeanDialog::updatePreview() {
    if (m_syncingCommand) return;
    m_syncingCommand = true;
    QString cmd = QString(".mean %1 %2")
                      .arg(m_modeCombo->currentText().trimmed())
                      .arg(m_signalEdit->text().trimmed());
    if (!m_fromEdit->text().trimmed().isEmpty()) {
        cmd += QString(" from=%1").arg(m_fromEdit->text().trimmed());
    }
    if (!m_toEdit->text().trimmed().isEmpty()) {
        cmd += QString(" to=%1").arg(m_toEdit->text().trimmed());
    }
    m_commandEdit->setText(cmd.trimmed());
    m_syncingCommand = false;
}

void SpiceMeanDialog::applyCommandText() {
    if (m_syncingCommand) return;
    const QRegularExpressionMatch match = meanRegex().match(m_commandEdit->text().trimmed());
    if (!match.hasMatch()) return;
    m_syncingCommand = true;
    const QString mode = match.captured(1).toLower();
    m_modeCombo->setCurrentText(mode.isEmpty() ? "avg" : mode);
    m_signalEdit->setText(match.captured(2));
    m_fromEdit->setText(match.captured(3));
    m_toEdit->setText(match.captured(4));
    m_syncingCommand = false;
}
