#include "spice_step_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QVBoxLayout>

SpiceStepDialog::SpiceStepDialog(const QString& initialCommand, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Step Sweep Builder");
    setMinimumWidth(540);

    auto* mainLayout = new QVBoxLayout(this);

    auto* hero = new QLabel(
        "Build LTspice-style <b>.step</b> directives with supported VioSpice sweep forms. "
        "Use this for parameter, temperature, source, or model stepping.",
        this);
    hero->setWordWrap(true);
    hero->setStyleSheet("color: #d1d5db; background: #111827; border: 1px solid #374151; padding: 10px; border-radius: 6px;");
    mainLayout->addWidget(hero);

    auto* form = new QFormLayout();
    m_targetKindCombo = new QComboBox(this);
    m_targetKindCombo->addItems({"Parameter", "Temperature", "Custom Target"});
    form->addRow("Sweep Target:", m_targetKindCombo);

    m_targetLabel = new QLabel("Name:", this);
    m_targetEdit = new QLineEdit(this);
    m_targetEdit->setPlaceholderText("RLOAD or NPN QDRV(Bf)");
    form->addRow(m_targetLabel, m_targetEdit);

    m_sweepModeCombo = new QComboBox(this);
    m_sweepModeCombo->addItems({"Linear Range", "Value List", "Decade", "Octave", "File List"});
    form->addRow("Sweep Mode:", m_sweepModeCombo);
    mainLayout->addLayout(form);

    m_modeStack = new QStackedWidget(this);

    auto* linearPage = new QWidget(this);
    auto* linearForm = new QGridLayout(linearPage);
    m_linearStartEdit = new QLineEdit(linearPage);
    m_linearStopEdit = new QLineEdit(linearPage);
    m_linearStepEdit = new QLineEdit(linearPage);
    m_linearStartEdit->setPlaceholderText("1k");
    m_linearStopEdit->setPlaceholderText("10k");
    m_linearStepEdit->setPlaceholderText("1k");
    linearForm->addWidget(new QLabel("Start", linearPage), 0, 0);
    linearForm->addWidget(m_linearStartEdit, 0, 1);
    linearForm->addWidget(new QLabel("Stop", linearPage), 1, 0);
    linearForm->addWidget(m_linearStopEdit, 1, 1);
    linearForm->addWidget(new QLabel("Increment", linearPage), 2, 0);
    linearForm->addWidget(m_linearStepEdit, 2, 1);
    m_modeStack->addWidget(linearPage);

    auto* listPage = new QWidget(this);
    auto* listForm = new QVBoxLayout(listPage);
    listForm->addWidget(new QLabel("List values separated by spaces:", listPage));
    m_listValuesEdit = new QLineEdit(listPage);
    m_listValuesEdit->setPlaceholderText("1k 2k 5k 10k");
    listForm->addWidget(m_listValuesEdit);
    m_modeStack->addWidget(listPage);

    auto* decadePage = new QWidget(this);
    auto* decadeForm = new QGridLayout(decadePage);
    m_logPointsEdit = new QLineEdit(decadePage);
    m_logStartEdit = new QLineEdit(decadePage);
    m_logStopEdit = new QLineEdit(decadePage);
    m_logPointsEdit->setPlaceholderText("10");
    m_logStartEdit->setPlaceholderText("1k");
    m_logStopEdit->setPlaceholderText("1Meg");
    decadeForm->addWidget(new QLabel("Points / span", decadePage), 0, 0);
    decadeForm->addWidget(m_logPointsEdit, 0, 1);
    decadeForm->addWidget(new QLabel("Start", decadePage), 1, 0);
    decadeForm->addWidget(m_logStartEdit, 1, 1);
    decadeForm->addWidget(new QLabel("Stop", decadePage), 2, 0);
    decadeForm->addWidget(m_logStopEdit, 2, 1);
    m_modeStack->addWidget(decadePage);

    auto* octavePage = new QWidget(this);
    auto* octaveForm = new QGridLayout(octavePage);
    m_octPointsEdit = new QLineEdit(octavePage);
    m_octStartEdit = new QLineEdit(octavePage);
    m_octStopEdit = new QLineEdit(octavePage);
    m_octPointsEdit->setPlaceholderText("8");
    m_octStartEdit->setPlaceholderText("1k");
    m_octStopEdit->setPlaceholderText("1Meg");
    octaveForm->addWidget(new QLabel("Points / octave", octavePage), 0, 0);
    octaveForm->addWidget(m_octPointsEdit, 0, 1);
    octaveForm->addWidget(new QLabel("Start", octavePage), 1, 0);
    octaveForm->addWidget(m_octStartEdit, 1, 1);
    octaveForm->addWidget(new QLabel("Stop", octavePage), 2, 0);
    octaveForm->addWidget(m_octStopEdit, 2, 1);
    m_modeStack->addWidget(octavePage);

    auto* filePage = new QWidget(this);
    auto* fileForm = new QVBoxLayout(filePage);
    fileForm->addWidget(new QLabel("Path to a file containing one sweep value per line:", filePage));
    m_filePathEdit = new QLineEdit(filePage);
    m_filePathEdit->setPlaceholderText("sweeps/rload_values.txt");
    fileForm->addWidget(m_filePathEdit);
    m_modeStack->addWidget(filePage);

    auto* modeFrame = new QFrame(this);
    modeFrame->setStyleSheet("QFrame { background: #0f172a; border: 1px solid #334155; border-radius: 6px; }");
    auto* modeFrameLayout = new QVBoxLayout(modeFrame);
    modeFrameLayout->addWidget(m_modeStack);
    mainLayout->addWidget(modeFrame);

    auto* help = new QLabel(
        "Examples: <code>.step param RLOAD list 1k 2k 5k</code>, "
        "<code>.step dec param CLOAD 10 1p 1u</code>, "
        "<code>.step temp -40 125 5</code>",
        this);
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);
    help->setStyleSheet("color: #94a3b8;");
    mainLayout->addWidget(help);

    mainLayout->addWidget(new QLabel("Directive Preview:", this));
    m_commandEdit = new QLineEdit(this);
    m_commandEdit->setStyleSheet("color: #3b82f6; font-family: 'Courier New'; font-weight: bold;");
    mainLayout->addWidget(m_commandEdit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!m_commandEdit->text().trimmed().isEmpty()) accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_targetKindCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    connect(m_sweepModeCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    for (QLineEdit* edit : {m_targetEdit, m_linearStartEdit, m_linearStopEdit, m_linearStepEdit,
                            m_listValuesEdit, m_logPointsEdit, m_logStartEdit, m_logStopEdit,
                            m_octPointsEdit, m_octStartEdit, m_octStopEdit, m_filePathEdit}) {
        connect(edit, &QLineEdit::textChanged, this, &SpiceStepDialog::updatePreview);
    }
    connect(m_commandEdit, &QLineEdit::editingFinished, this, &SpiceStepDialog::applyCommandText);

    m_commandEdit->setText(initialCommand.trimmed());
    applyCommandText();
    updateUiState();
    updatePreview();
}

QString SpiceStepDialog::commandText() const {
    return m_commandEdit->text().trimmed();
}

QString SpiceStepDialog::quotedFilePath(const QString& path) {
    QString out = path.trimmed();
    if (out.isEmpty()) return out;
    if (out.startsWith('"') && out.endsWith('"')) return out;
    return QString("\"%1\"").arg(out);
}

SpiceStepDialog::TargetKind SpiceStepDialog::currentTargetKind() const {
    switch (m_targetKindCombo->currentIndex()) {
    case 1: return TargetKind::Temperature;
    case 2: return TargetKind::Custom;
    default: return TargetKind::Parameter;
    }
}

SpiceStepDialog::SweepMode SpiceStepDialog::currentSweepMode() const {
    switch (m_sweepModeCombo->currentIndex()) {
    case 1: return SweepMode::List;
    case 2: return SweepMode::Decade;
    case 3: return SweepMode::Octave;
    case 4: return SweepMode::File;
    default: return SweepMode::LinearRange;
    }
}

QString SpiceStepDialog::targetPrefix() const {
    switch (currentTargetKind()) {
    case TargetKind::Temperature:
        return "temp";
    case TargetKind::Custom:
        return m_targetEdit->text().trimmed();
    case TargetKind::Parameter:
    default:
        return QString("param %1").arg(m_targetEdit->text().trimmed());
    }
}

void SpiceStepDialog::updateUiState() {
    const bool needsTarget = currentTargetKind() != TargetKind::Temperature;
    m_targetLabel->setText(currentTargetKind() == TargetKind::Custom ? "Target:" : "Name:");
    m_targetEdit->setEnabled(needsTarget);
    m_targetEdit->setPlaceholderText(currentTargetKind() == TargetKind::Custom ? "V1 or NPN QDRV(Bf)" : "RLOAD");
    m_modeStack->setCurrentIndex(m_sweepModeCombo->currentIndex());
    updatePreview();
}

void SpiceStepDialog::updatePreview() {
    if (m_syncingCommand) return;
    m_syncingCommand = true;

    const QString prefix = targetPrefix().trimmed();
    QString cmd;
    switch (currentSweepMode()) {
    case SweepMode::List:
        cmd = QString(".step %1 list %2").arg(prefix, m_listValuesEdit->text().trimmed());
        break;
    case SweepMode::Decade:
        cmd = QString(".step dec %1 %2 %3 %4")
                  .arg(prefix, m_logPointsEdit->text().trimmed(), m_logStartEdit->text().trimmed(), m_logStopEdit->text().trimmed());
        break;
    case SweepMode::Octave:
        cmd = QString(".step oct %1 %2 %3 %4")
                  .arg(prefix, m_octPointsEdit->text().trimmed(), m_octStartEdit->text().trimmed(), m_octStopEdit->text().trimmed());
        break;
    case SweepMode::File:
        cmd = QString(".step %1 file=%2").arg(prefix, quotedFilePath(m_filePathEdit->text()));
        break;
    case SweepMode::LinearRange:
    default:
        cmd = QString(".step %1 %2 %3 %4")
                  .arg(prefix, m_linearStartEdit->text().trimmed(), m_linearStopEdit->text().trimmed(), m_linearStepEdit->text().trimmed());
        break;
    }

    m_commandEdit->setText(cmd.trimmed());
    m_syncingCommand = false;
}

void SpiceStepDialog::applyCommandText() {
    if (m_syncingCommand) return;
    const QString text = m_commandEdit->text().trimmed();
    if (!text.startsWith(".step", Qt::CaseInsensitive)) return;

    const QStringList tokens = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (tokens.size() < 3) return;

    m_syncingCommand = true;
    int pos = 1;
    const QString sweepToken = tokens.value(pos).toLower();
    if (sweepToken == "dec") {
        m_sweepModeCombo->setCurrentIndex(2);
        pos++;
    } else if (sweepToken == "oct") {
        m_sweepModeCombo->setCurrentIndex(3);
        pos++;
    } else {
        m_sweepModeCombo->setCurrentIndex(text.contains(" file=", Qt::CaseInsensitive) ? 4
                                          : text.contains(" list ", Qt::CaseInsensitive) ? 1 : 0);
    }

    const QString maybeParam = tokens.value(pos).toLower();
    if (maybeParam == "param") {
        m_targetKindCombo->setCurrentIndex(0);
        pos++;
        m_targetEdit->setText(tokens.value(pos));
        pos++;
    } else if (maybeParam == "temp") {
        m_targetKindCombo->setCurrentIndex(1);
        pos++;
        m_targetEdit->clear();
    } else {
        m_targetKindCombo->setCurrentIndex(2);
        if (m_sweepModeCombo->currentIndex() == 0 && tokens.size() >= pos + 4) {
            m_targetEdit->setText(tokens.mid(pos, tokens.size() - pos - 3).join(' '));
            pos = tokens.size() - 3;
        } else {
            m_targetEdit->setText(tokens.value(pos));
            pos++;
        }
    }

    if (m_sweepModeCombo->currentIndex() == 1) {
        if (tokens.value(pos).compare("list", Qt::CaseInsensitive) == 0) pos++;
        m_listValuesEdit->setText(tokens.mid(pos).join(' '));
    } else if (m_sweepModeCombo->currentIndex() == 4) {
        const QRegularExpression fileRe("file\\s*=\\s*(\"[^\"]+\"|\\S+)", QRegularExpression::CaseInsensitiveOption);
        const auto match = fileRe.match(text);
        if (match.hasMatch()) {
            QString path = match.captured(1).trimmed();
            if (path.startsWith('"') && path.endsWith('"')) path = path.mid(1, path.size() - 2);
            m_filePathEdit->setText(path);
        }
    } else if (m_sweepModeCombo->currentIndex() == 2 || m_sweepModeCombo->currentIndex() == 3) {
        if (m_sweepModeCombo->currentIndex() == 2) {
            m_logPointsEdit->setText(tokens.value(pos));
            m_logStartEdit->setText(tokens.value(pos + 1));
            m_logStopEdit->setText(tokens.value(pos + 2));
        } else {
            m_octPointsEdit->setText(tokens.value(pos));
            m_octStartEdit->setText(tokens.value(pos + 1));
            m_octStopEdit->setText(tokens.value(pos + 2));
        }
    } else {
        m_linearStartEdit->setText(tokens.value(pos));
        m_linearStopEdit->setText(tokens.value(pos + 1));
        m_linearStepEdit->setText(tokens.value(pos + 2));
    }

    m_syncingCommand = false;
    updateUiState();
    updatePreview();
}
