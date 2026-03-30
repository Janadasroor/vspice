#include "spice_step_dialog.h"

#include "../../simulator/core/sim_value_parser.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

bool parseStepValue(const QLineEdit* edit, double& outValue) {
    return edit && SimValueParser::parseSpiceNumber(edit->text().trimmed(), outValue);
}

}

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

    auto* presetFrame = new QFrame(this);
    presetFrame->setStyleSheet("QFrame { background: #111827; border: 1px solid #334155; border-radius: 6px; }");
    auto* presetLayout = new QHBoxLayout(presetFrame);
    presetLayout->setContentsMargins(10, 8, 10, 8);
    presetLayout->setSpacing(6);
    presetLayout->addWidget(new QLabel("Quick Presets:", presetFrame));
    const QList<QPair<QString, QString>> presets = {
        {"Resistor Sweep", "resistor"},
        {"Capacitor Decade", "capacitor"},
        {"Temperature Range", "temperature"}
    };
    for (const auto& preset : presets) {
        auto* button = new QPushButton(preset.first, presetFrame);
        button->setStyleSheet("QPushButton { background: #1d4ed8; color: white; padding: 4px 10px; border-radius: 4px; }"
                              "QPushButton:hover { background: #2563eb; }");
        connect(button, &QPushButton::clicked, this, [this, preset]() { applyPreset(preset.second); });
        presetLayout->addWidget(button);
    }
    presetLayout->addStretch();
    mainLayout->addWidget(presetFrame);

    auto* form = new QFormLayout();
    m_targetKindCombo = new QComboBox(this);
    m_targetKindCombo->addItems({"Parameter", "Temperature", "Independent Source", "Model Parameter"});
    form->addRow("Sweep Target:", m_targetKindCombo);

    m_targetStack = new QStackedWidget(this);

    auto* paramPage = new QWidget(this);
    auto* paramLayout = new QHBoxLayout(paramPage);
    paramLayout->setContentsMargins(0, 0, 0, 0);
    m_paramNameEdit = new QLineEdit(paramPage);
    m_paramNameEdit->setPlaceholderText("RLOAD");
    paramLayout->addWidget(m_paramNameEdit);
    m_targetStack->addWidget(paramPage);

    auto* tempPage = new QWidget(this);
    auto* tempLayout = new QHBoxLayout(tempPage);
    tempLayout->setContentsMargins(0, 0, 0, 0);
    auto* tempInfo = new QLabel("Temperature sweeps target LTspice 'temp'.", tempPage);
    tempInfo->setStyleSheet("color: #94a3b8;");
    tempLayout->addWidget(tempInfo);
    m_targetStack->addWidget(tempPage);

    auto* sourcePage = new QWidget(this);
    auto* sourceLayout = new QHBoxLayout(sourcePage);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    m_sourceNameEdit = new QLineEdit(sourcePage);
    m_sourceNameEdit->setPlaceholderText("V1 or I1");
    sourceLayout->addWidget(m_sourceNameEdit);
    m_targetStack->addWidget(sourcePage);

    auto* modelPage = new QWidget(this);
    auto* modelLayout = new QGridLayout(modelPage);
    m_modelTypeEdit = new QLineEdit(modelPage);
    m_modelNameEdit = new QLineEdit(modelPage);
    m_modelParamEdit = new QLineEdit(modelPage);
    m_modelTypeEdit->setPlaceholderText("NPN");
    m_modelNameEdit->setPlaceholderText("2N2222");
    m_modelParamEdit->setPlaceholderText("VAF");
    modelLayout->addWidget(new QLabel("Device Type", modelPage), 0, 0);
    modelLayout->addWidget(m_modelTypeEdit, 0, 1);
    modelLayout->addWidget(new QLabel("Model Name", modelPage), 1, 0);
    modelLayout->addWidget(m_modelNameEdit, 1, 1);
    modelLayout->addWidget(new QLabel("Parameter", modelPage), 2, 0);
    modelLayout->addWidget(m_modelParamEdit, 2, 1);
    m_targetStack->addWidget(modelPage);

    form->addRow("Target Details:", m_targetStack);

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
    auto* fileRow = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(filePage);
    m_filePathEdit->setPlaceholderText("sweeps/rload_values.txt");
    auto* browseButton = new QPushButton("Browse...", filePage);
    fileRow->addWidget(m_filePathEdit, 1);
    fileRow->addWidget(browseButton);
    fileForm->addLayout(fileRow);
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

    m_validationLabel = new QLabel(this);
    m_validationLabel->setWordWrap(true);
    m_validationLabel->setStyleSheet("color: #f59e0b; font-size: 11px;");
    mainLayout->addWidget(m_validationLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (validationMessage().isEmpty() && !m_commandEdit->text().trimmed().isEmpty()) accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_targetKindCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    connect(m_sweepModeCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    connect(browseButton, &QPushButton::clicked, this, &SpiceStepDialog::browseStepFile);
    for (QLineEdit* edit : {m_paramNameEdit, m_sourceNameEdit, m_modelTypeEdit, m_modelNameEdit, m_modelParamEdit,
                            m_linearStartEdit, m_linearStopEdit, m_linearStepEdit,
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
    case 2: return TargetKind::Source;
    case 3: return TargetKind::ModelParameter;
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
    case TargetKind::Source:
        return m_sourceNameEdit ? m_sourceNameEdit->text().trimmed() : QString();
    case TargetKind::ModelParameter:
        return QString("%1 %2(%3)")
            .arg(m_modelTypeEdit ? m_modelTypeEdit->text().trimmed() : QString(),
                 m_modelNameEdit ? m_modelNameEdit->text().trimmed() : QString(),
                 m_modelParamEdit ? m_modelParamEdit->text().trimmed() : QString()).trimmed();
    case TargetKind::Parameter:
    default:
        return QString("param %1").arg(m_paramNameEdit ? m_paramNameEdit->text().trimmed() : QString());
    }
}

QString SpiceStepDialog::validationMessage() const {
    const QString prefix = targetPrefix().trimmed();
    if (prefix.isEmpty()) return "Sweep target is required.";

    switch (currentSweepMode()) {
    case SweepMode::LinearRange:
        if (m_linearStartEdit->text().trimmed().isEmpty() || m_linearStopEdit->text().trimmed().isEmpty() ||
            m_linearStepEdit->text().trimmed().isEmpty()) {
            return "Linear sweeps need start, stop, and increment values.";
        }
        {
            double start = 0.0, stop = 0.0, step = 0.0;
            if (!parseStepValue(m_linearStartEdit, start) || !parseStepValue(m_linearStopEdit, stop) ||
                !parseStepValue(m_linearStepEdit, step)) {
                return "Linear sweep values must be valid SPICE numbers.";
            }
            if (step == 0.0) return "Linear sweep increment cannot be zero.";
            if ((stop > start && step < 0.0) || (stop < start && step > 0.0)) {
                return "Linear sweep increment must move from start toward stop.";
            }
            if (start == stop) return "Linear sweep start and stop should not be equal.";
        }
        break;
    case SweepMode::List:
        if (m_listValuesEdit->text().trimmed().isEmpty()) return "Value list sweeps need at least one value.";
        {
            const QStringList values = m_listValuesEdit->text().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (const QString& value : values) {
                double parsed = 0.0;
                if (!SimValueParser::parseSpiceNumber(value, parsed)) {
                    return QString("List value '%1' is not a valid SPICE number.").arg(value);
                }
            }
        }
        break;
    case SweepMode::Decade:
        if (m_logPointsEdit->text().trimmed().isEmpty() || m_logStartEdit->text().trimmed().isEmpty() ||
            m_logStopEdit->text().trimmed().isEmpty()) {
            return "Decade sweeps need points, start, and stop values.";
        }
        {
            double points = 0.0, start = 0.0, stop = 0.0;
            if (!parseStepValue(m_logPointsEdit, points) || !parseStepValue(m_logStartEdit, start) ||
                !parseStepValue(m_logStopEdit, stop)) {
                return "Decade sweep values must be valid SPICE numbers.";
            }
            if (points <= 0.0) return "Decade sweep points must be greater than zero.";
            if (start <= 0.0 || stop <= 0.0) return "Decade sweeps require positive start and stop values.";
            if (start == stop) return "Decade sweep start and stop should not be equal.";
        }
        break;
    case SweepMode::Octave:
        if (m_octPointsEdit->text().trimmed().isEmpty() || m_octStartEdit->text().trimmed().isEmpty() ||
            m_octStopEdit->text().trimmed().isEmpty()) {
            return "Octave sweeps need points, start, and stop values.";
        }
        {
            double points = 0.0, start = 0.0, stop = 0.0;
            if (!parseStepValue(m_octPointsEdit, points) || !parseStepValue(m_octStartEdit, start) ||
                !parseStepValue(m_octStopEdit, stop)) {
                return "Octave sweep values must be valid SPICE numbers.";
            }
            if (points <= 0.0) return "Octave sweep points must be greater than zero.";
            if (start <= 0.0 || stop <= 0.0) return "Octave sweeps require positive start and stop values.";
            if (start == stop) return "Octave sweep start and stop should not be equal.";
        }
        break;
    case SweepMode::File:
        if (m_filePathEdit->text().trimmed().isEmpty()) return "File sweeps need a values file path.";
        break;
    }
    return QString();
}

void SpiceStepDialog::updateUiState() {
    if (m_targetStack) m_targetStack->setCurrentIndex(m_targetKindCombo->currentIndex());
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
    const QString validation = validationMessage();
    if (m_validationLabel) {
        m_validationLabel->setText(validation.isEmpty()
            ? QString("Supported by VioSpice LTspice emulation. The generated directive is ready to use.")
            : validation);
        m_validationLabel->setStyleSheet(validation.isEmpty()
            ? "color: #10b981; font-size: 11px;"
            : "color: #f59e0b; font-size: 11px;");
    }
    if (m_buttonBox && m_buttonBox->button(QDialogButtonBox::Ok)) {
        m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(validation.isEmpty() && !cmd.trimmed().isEmpty());
    }
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
        if (m_paramNameEdit) m_paramNameEdit->setText(tokens.value(pos));
        pos++;
    } else if (maybeParam == "temp") {
        m_targetKindCombo->setCurrentIndex(1);
        pos++;
        if (m_paramNameEdit) m_paramNameEdit->clear();
    } else if (pos + 1 < tokens.size() && tokens.value(pos + 1).contains('(') && tokens.value(pos + 1).contains(')')) {
        m_targetKindCombo->setCurrentIndex(3);
        if (m_modelTypeEdit) m_modelTypeEdit->setText(tokens.value(pos));
        const QString modelToken = tokens.value(pos + 1);
        const int open = modelToken.indexOf('(');
        const int close = modelToken.lastIndexOf(')');
        if (m_modelNameEdit) m_modelNameEdit->setText(modelToken.left(open));
        if (m_modelParamEdit && open > 0 && close > open) m_modelParamEdit->setText(modelToken.mid(open + 1, close - open - 1));
        pos += 2;
    } else {
        m_targetKindCombo->setCurrentIndex(2);
        if (m_sourceNameEdit) m_sourceNameEdit->setText(tokens.value(pos));
        pos++;
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

void SpiceStepDialog::browseStepFile() {
    const QString path = QFileDialog::getOpenFileName(this, "Select .step Values File", QString(), "Text Files (*.txt *.lst *.csv);;All Files (*)");
    if (path.isEmpty()) return;
    m_filePathEdit->setText(path);
}

void SpiceStepDialog::applyPreset(const QString& presetId) {
    m_syncingCommand = true;
    if (presetId == "resistor") {
        m_targetKindCombo->setCurrentIndex(0);
        if (m_paramNameEdit) m_paramNameEdit->setText("RLOAD");
        m_sweepModeCombo->setCurrentIndex(1);
        m_listValuesEdit->setText("1k 2k 5k 10k");
    } else if (presetId == "capacitor") {
        m_targetKindCombo->setCurrentIndex(0);
        if (m_paramNameEdit) m_paramNameEdit->setText("CLOAD");
        m_sweepModeCombo->setCurrentIndex(2);
        m_logPointsEdit->setText("10");
        m_logStartEdit->setText("1p");
        m_logStopEdit->setText("1u");
    } else if (presetId == "temperature") {
        m_targetKindCombo->setCurrentIndex(1);
        if (m_paramNameEdit) m_paramNameEdit->clear();
        m_sweepModeCombo->setCurrentIndex(0);
        m_linearStartEdit->setText("-40");
        m_linearStopEdit->setText("125");
        m_linearStepEdit->setText("5");
    }
    m_syncingCommand = false;
    updateUiState();
    updatePreview();
}
