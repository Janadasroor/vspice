#include "spice_step_dialog.h"

#include "theme_manager.h"
#include "../../simulator/core/sim_value_parser.h"
#include "../items/current_source_item.h"
#include "../items/transistor_item.h"
#include "../items/voltage_controlled_switch_item.h"
#include "../items/voltage_source_item.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <cmath>

namespace {

bool parseStepValue(const QLineEdit* edit, double& outValue) {
    return edit && SimValueParser::parseSpiceNumber(edit->text().trimmed(), outValue);
}

QStringList tokenizeStepDialogLine(const QString& text) {
    QString normalized = text;
    normalized.replace(',', ' ');
    return normalized.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
}

bool estimateLinearCount(const QStringList& tokens, int idx, int& outCount) {
    double start = 0.0, stop = 0.0, step = 0.0;
    if (tokens.size() < idx + 3) return false;
    if (!SimValueParser::parseSpiceNumber(tokens[idx], start) || !SimValueParser::parseSpiceNumber(tokens[idx + 1], stop) ||
        !SimValueParser::parseSpiceNumber(tokens[idx + 2], step) || step == 0.0) {
        return false;
    }
    const double direction = (stop >= start) ? 1.0 : -1.0;
    if ((direction > 0.0 && step < 0.0) || (direction < 0.0 && step > 0.0)) step = -step;
    const double eps = std::abs(step) * 1e-9 + 1e-18;
    int count = 0;
    for (double v = start; (direction > 0.0) ? (v <= stop + eps) : (v >= stop - eps); v += step) {
        ++count;
        if (count > 1000000) return false;
    }
    outCount = count;
    return count > 0;
}

bool estimateLogCount(const QString& mode, const QStringList& tokens, int idx, int& outCount) {
    double start = 0.0, stop = 0.0, points = 0.0;
    if (tokens.size() < idx + 3) return false;
    if (!SimValueParser::parseSpiceNumber(tokens[idx], start) || !SimValueParser::parseSpiceNumber(tokens[idx + 1], stop) ||
        !SimValueParser::parseSpiceNumber(tokens[idx + 2], points) || points <= 0.0 || start <= 0.0 || stop <= 0.0) {
        return false;
    }
    const double ratioBase = mode.compare("oct", Qt::CaseInsensitive) == 0 ? 2.0 : 10.0;
    const double factor = std::pow(ratioBase, 1.0 / points);
    if (factor <= 1.0) return false;
    int count = 0;
    for (double v = start; v <= stop * (1.0 + 1e-12); v *= factor) {
        ++count;
        if (count > 1000000) return false;
    }
    outCount = count;
    return count > 0;
}

bool estimateStepLineCount(const QString& line, int& outCount) {
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(".temp", Qt::CaseInsensitive)) {
        const QStringList tokens = tokenizeStepDialogLine(trimmed);
        outCount = qMax(0, tokens.size() - 1);
        return outCount > 0;
    }
    if (!trimmed.startsWith(".step", Qt::CaseInsensitive)) return false;
    const QStringList tokens = tokenizeStepDialogLine(trimmed);
    if (tokens.size() < 3) return false;
    int idx = 1;
    QString mode = "lin";
    const QString maybeMode = tokens.value(idx).toLower();
    if (maybeMode == "lin" || maybeMode == "dec" || maybeMode == "oct") {
        mode = maybeMode;
        ++idx;
    }
    if (idx >= tokens.size()) return false;
    if (tokens.value(idx).compare("param", Qt::CaseInsensitive) == 0) idx += 2;
    else if (tokens.value(idx).compare("temp", Qt::CaseInsensitive) == 0) idx += 1;
    else if (idx + 1 < tokens.size() && tokens.value(idx + 1).contains('(') && tokens.value(idx + 1).contains(')')) idx += 2;
    else idx += 1;
    if (idx >= tokens.size()) return false;

    if (tokens.value(idx).compare("list", Qt::CaseInsensitive) == 0) {
        outCount = qMax(0, tokens.size() - idx - 1);
        return outCount > 0;
    }
    if (tokens.value(idx).startsWith("file=", Qt::CaseInsensitive)) {
        QString fileToken = tokens.value(idx).mid(5).trimmed();
        if (fileToken.isEmpty()) return false;
        QFile file(fileToken.startsWith('"') && fileToken.endsWith('"') ? fileToken.mid(1, fileToken.size() - 2) : fileToken);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        int count = 0;
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString fileLine = stream.readLine().trimmed();
            if (fileLine.isEmpty() || fileLine.startsWith('*') || fileLine.startsWith(';') || fileLine.startsWith('#')) continue;
            const int semicolon = fileLine.indexOf(';');
            if (semicolon >= 0) fileLine = fileLine.left(semicolon).trimmed();
            if (fileLine.isEmpty()) continue;
            count += fileLine.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts).size();
        }
        outCount = count;
        return count > 0;
    }
    if (mode == "lin") return estimateLinearCount(tokens, idx, outCount);
    return estimateLogCount(mode, tokens, idx, outCount);
}

QString effectiveReference(const SchematicItem* item) {
    if (!item) return QString();
    const QString ref = item->reference().trimmed();
    if (!ref.isEmpty()) return ref;
    return item->name().trimmed();
}

QStringList gatherSourceCandidates(QGraphicsScene* scene) {
    QStringList values;
    if (!scene) return values;
    for (QGraphicsItem* rawItem : scene->items()) {
        if (auto* voltage = dynamic_cast<VoltageSourceItem*>(rawItem)) {
            const QString ref = effectiveReference(voltage);
            if (!ref.isEmpty()) values << ref;
        } else if (auto* current = dynamic_cast<CurrentSourceItem*>(rawItem)) {
            const QString ref = effectiveReference(current);
            if (!ref.isEmpty()) values << ref;
        }
    }
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    return values;
}

QString transistorTypeName(TransistorItem::TransistorType type) {
    switch (type) {
    case TransistorItem::NPN: return "NPN";
    case TransistorItem::PNP: return "PNP";
    case TransistorItem::NMOS: return "NMOS";
    case TransistorItem::PMOS: return "PMOS";
    }
    return QString();
}

void gatherModelCandidates(QGraphicsScene* scene, QStringList& typeNames, QStringList& modelNames) {
    if (!scene) return;
    for (QGraphicsItem* rawItem : scene->items()) {
        if (auto* transistor = dynamic_cast<TransistorItem*>(rawItem)) {
            const QString type = transistorTypeName(transistor->transistorType());
            const QString model = transistor->value().trimmed();
            if (!type.isEmpty()) typeNames << type;
            if (!model.isEmpty()) modelNames << model;
        } else if (auto* sw = dynamic_cast<VoltageControlledSwitchItem*>(rawItem)) {
            typeNames << "SW";
            const QString model = sw->modelName().trimmed();
            if (!model.isEmpty()) modelNames << model;
        }
    }
    typeNames.removeDuplicates();
    modelNames.removeDuplicates();
    std::sort(typeNames.begin(), typeNames.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
    std::sort(modelNames.begin(), modelNames.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
}

}

SpiceStepDialog::SpiceStepDialog(const QString& initialCommand, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_scene(scene)
{
    setWindowTitle("Step Sweep Builder");
    setMinimumWidth(540);
    const auto* theme = ThemeManager::theme();
    const QString panelBg = theme ? theme->panelBackground().name() : QString("#1f2937");
    const QString windowBg = theme ? theme->windowBackground().name() : QString("#111827");
    const QString border = theme ? theme->panelBorder().name() : QString("#374151");
    const QString textSecondary = theme ? theme->textSecondary().name() : QString("#94a3b8");
    const QString accent = theme ? theme->accentColor().name() : QString("#2563eb");
    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    auto* mainLayout = new QVBoxLayout(this);

    auto* hero = new QLabel(
        "Build LTspice-style <b>.step</b> directives with supported VioSpice sweep forms. "
        "Use this for parameter, temperature, source, or model stepping.",
        this);
    hero->setWordWrap(true);
    hero->setStyleSheet(QString("color: %1; background: %2; border: 1px solid %3; padding: 10px; border-radius: 6px;")
        .arg(theme ? theme->textColor().name() : QString("#d1d5db"), windowBg, border));
    mainLayout->addWidget(hero);

    auto* presetFrame = new QFrame(this);
    presetFrame->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 6px; }")
        .arg(windowBg, border));
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
        button->setStyleSheet(QString("QPushButton { background: %1; color: white; padding: 4px 10px; border-radius: 4px; border: 1px solid %2; }"
                                      "QPushButton:hover { background: %2; }")
                              .arg(accent, accent));
        connect(button, &QPushButton::clicked, this, [this, preset]() { applyPreset(preset.second); });
        presetLayout->addWidget(button);
    }
    presetLayout->addStretch();
    mainLayout->addWidget(presetFrame);

    auto* form = new QFormLayout();
    m_dimensionCountCombo = new QComboBox(this);
    m_dimensionCountCombo->addItems({"1", "2", "3"});
    form->addRow("Sweep Levels:", m_dimensionCountCombo);

    m_editLevelCombo = new QComboBox(this);
    m_editLevelCombo->addItems({"Level 1", "Level 2", "Level 3"});
    form->addRow("Editing Level:", m_editLevelCombo);

    m_targetKindCombo = new QComboBox(this);
    m_targetKindCombo->addItems({"Parameter", "Temperature", "Independent Source", "Model Parameter"});
    form->addRow("Sweep Target:", m_targetKindCombo);

    m_tempSyntaxCombo = new QComboBox(this);
    m_tempSyntaxCombo->addItems({".step temp", ".temp alias"});
    form->addRow("Temp Syntax:", m_tempSyntaxCombo);

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
    tempInfo->setStyleSheet(QString("color: %1;").arg(textSecondary));
    tempLayout->addWidget(tempInfo);
    m_targetStack->addWidget(tempPage);

    auto* sourcePage = new QWidget(this);
    auto* sourceLayout = new QHBoxLayout(sourcePage);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    m_sourceNameEdit = new QComboBox(sourcePage);
    m_sourceNameEdit->setEditable(true);
    m_sourceNameEdit->setInsertPolicy(QComboBox::NoInsert);
    m_sourceNameEdit->setPlaceholderText("V1 or I1");
    m_sourceNameEdit->addItems(gatherSourceCandidates(m_scene));
    sourceLayout->addWidget(m_sourceNameEdit);
    m_targetStack->addWidget(sourcePage);

    auto* modelPage = new QWidget(this);
    auto* modelLayout = new QGridLayout(modelPage);
    QStringList modelTypeNames = {"NPN", "PNP", "NMOS", "PMOS", "SW"};
    QStringList modelNames;
    gatherModelCandidates(m_scene, modelTypeNames, modelNames);
    m_modelTypeEdit = new QComboBox(modelPage);
    m_modelTypeEdit->setEditable(true);
    m_modelTypeEdit->addItems(modelTypeNames);
    m_modelNameEdit = new QComboBox(modelPage);
    m_modelNameEdit->setEditable(true);
    m_modelNameEdit->addItems(modelNames);
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
    m_filePreviewLabel = new QLabel(filePage);
    m_filePreviewLabel->setWordWrap(true);
    m_filePreviewLabel->setStyleSheet("color: #94a3b8; font-size: 11px;");
    m_filePreviewLabel->setText("Preview: pick a values file to parse whitespace/comma separated entries.");
    fileForm->addWidget(m_filePreviewLabel);
    m_modeStack->addWidget(filePage);

    auto* modeFrame = new QFrame(this);
    modeFrame->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 6px; }")
        .arg(panelBg, border));
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
    help->setStyleSheet(QString("color: %1;").arg(textSecondary));
    mainLayout->addWidget(help);

    mainLayout->addWidget(new QLabel("Directive Preview:", this));
    m_commandEdit = new QLineEdit(this);
    m_commandEdit->setStyleSheet(QString("color: %1; font-family: 'Courier New'; font-weight: bold;").arg(accent));
    mainLayout->addWidget(m_commandEdit);

    m_validationLabel = new QLabel(this);
    m_validationLabel->setWordWrap(true);
    m_validationLabel->setStyleSheet("font-size: 11px;");
    mainLayout->addWidget(m_validationLabel);

    m_runEstimateLabel = new QLabel(this);
    m_runEstimateLabel->setWordWrap(true);
    m_runEstimateLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(textSecondary));
    mainLayout->addWidget(m_runEstimateLabel);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (validationMessage().isEmpty() && !m_commandEdit->text().trimmed().isEmpty()) accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_dimensionCountCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::onDimensionCountChanged);
    connect(m_editLevelCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::onEditingLevelChanged);
    connect(m_targetKindCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    connect(m_tempSyntaxCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    connect(m_sweepModeCombo, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updateUiState);
    connect(browseButton, &QPushButton::clicked, this, &SpiceStepDialog::browseStepFile);
    for (QLineEdit* edit : {m_paramNameEdit, m_modelParamEdit,
                            m_linearStartEdit, m_linearStopEdit, m_linearStepEdit,
                            m_listValuesEdit, m_logPointsEdit, m_logStartEdit, m_logStopEdit,
                            m_octPointsEdit, m_octStartEdit, m_octStopEdit, m_filePathEdit}) {
        connect(edit, &QLineEdit::textChanged, this, &SpiceStepDialog::updatePreview);
    }
    connect(m_sourceNameEdit, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updatePreview);
    connect(m_modelTypeEdit, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updatePreview);
    connect(m_modelNameEdit, &QComboBox::currentTextChanged, this, &SpiceStepDialog::updatePreview);
    connect(m_commandEdit, &QLineEdit::editingFinished, this, &SpiceStepDialog::applyCommandText);

    m_levelCommands = QStringList({QString(), QString(), QString()});
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

int SpiceStepDialog::currentLevelIndex() const {
    return m_editLevelCombo ? m_editLevelCombo->currentIndex() : 0;
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
        return m_sourceNameEdit ? m_sourceNameEdit->currentText().trimmed() : QString();
    case TargetKind::ModelParameter:
        return QString("%1 %2(%3)")
            .arg(m_modelTypeEdit ? m_modelTypeEdit->currentText().trimmed() : QString(),
                 m_modelNameEdit ? m_modelNameEdit->currentText().trimmed() : QString(),
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
        {
            QString error;
            const QStringList values = parseFileSweepValues(&error);
            if (!error.isEmpty()) return error;
            if (values.isEmpty()) return "File sweeps need at least one valid numeric value.";
        }
        break;
    }
    return QString();
}

QStringList SpiceStepDialog::parseFileSweepValues(QString* errorMessage) const {
    if (errorMessage) errorMessage->clear();
    const QString path = m_filePathEdit ? m_filePathEdit->text().trimmed() : QString();
    if (path.isEmpty()) return {};

    QFile file(path);
    if (!file.exists()) {
        if (errorMessage) *errorMessage = "Sweep values file does not exist.";
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) *errorMessage = "Unable to open sweep values file.";
        return {};
    }

    QString content = QTextStream(&file).readAll();
    QStringList values;
    const QStringList lines = content.split('\n');
    for (QString line : lines) {
        const int commentPos = line.indexOf(';');
        if (commentPos >= 0) line = line.left(commentPos);
        line = line.trimmed();
        if (line.startsWith('*')) continue;
        if (line.isEmpty()) continue;
        const QStringList tokens = line.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        for (const QString& token : tokens) {
            double parsed = 0.0;
            if (!SimValueParser::parseSpiceNumber(token, parsed)) {
                if (errorMessage) *errorMessage = QString("Invalid file sweep value '%1'.").arg(token);
                return {};
            }
            values << token;
        }
    }
    return values;
}

void SpiceStepDialog::updateFilePreview() {
    if (!m_filePreviewLabel) return;
    QString error;
    const QStringList values = parseFileSweepValues(&error);
    if (!error.isEmpty()) {
        m_filePreviewLabel->setStyleSheet("color: #f59e0b; font-size: 11px;");
        m_filePreviewLabel->setText(error);
        return;
    }
    if (values.isEmpty()) {
        m_filePreviewLabel->setStyleSheet("color: #94a3b8; font-size: 11px;");
        m_filePreviewLabel->setText("Preview: no values parsed yet.");
        return;
    }

    const QString preview = values.mid(0, 8).join(", ");
    const QString suffix = values.size() > 8 ? QString(", ...") : QString();
    m_filePreviewLabel->setStyleSheet("color: #10b981; font-size: 11px;");
    m_filePreviewLabel->setText(QString("Parsed %1 value(s): %2%3")
        .arg(values.size())
        .arg(preview)
        .arg(suffix));
}

QString SpiceStepDialog::buildSingleLevelCommand() const {
    const QString prefix = targetPrefix().trimmed();
    if (prefix.isEmpty()) return QString();

    const bool useTempAlias = currentTargetKind() == TargetKind::Temperature &&
                              currentSweepMode() == SweepMode::List &&
                              m_tempSyntaxCombo && m_tempSyntaxCombo->currentIndex() == 1;
    if (useTempAlias) {
        return QString(".temp %1").arg(m_listValuesEdit->text().trimmed()).trimmed();
    }

    switch (currentSweepMode()) {
    case SweepMode::List:
        return QString(".step %1 list %2").arg(prefix, m_listValuesEdit->text().trimmed()).trimmed();
    case SweepMode::Decade:
        return QString(".step dec %1 %2 %3 %4")
            .arg(prefix, m_logPointsEdit->text().trimmed(), m_logStartEdit->text().trimmed(), m_logStopEdit->text().trimmed()).trimmed();
    case SweepMode::Octave:
        return QString(".step oct %1 %2 %3 %4")
            .arg(prefix, m_octPointsEdit->text().trimmed(), m_octStartEdit->text().trimmed(), m_octStopEdit->text().trimmed()).trimmed();
    case SweepMode::File:
        return QString(".step %1 file=%2").arg(prefix, quotedFilePath(m_filePathEdit->text())).trimmed();
    case SweepMode::LinearRange:
    default:
        return QString(".step %1 %2 %3 %4")
            .arg(prefix, m_linearStartEdit->text().trimmed(), m_linearStopEdit->text().trimmed(), m_linearStepEdit->text().trimmed()).trimmed();
    }
}

void SpiceStepDialog::syncCurrentLevelFromUi() {
    const int idx = currentLevelIndex();
    if (idx < 0 || idx >= m_levelCommands.size()) return;
    m_levelCommands[idx] = buildSingleLevelCommand();
}

bool SpiceStepDialog::parseSingleLevelCommand(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.startsWith(".temp", Qt::CaseInsensitive)) {
        const QStringList tokens = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.size() < 2) return false;
        m_targetKindCombo->setCurrentIndex(1);
        if (m_tempSyntaxCombo) m_tempSyntaxCombo->setCurrentIndex(1);
        m_sweepModeCombo->setCurrentIndex(1);
        m_listValuesEdit->setText(tokens.mid(1).join(' '));
        return true;
    }
    if (!trimmed.startsWith(".step", Qt::CaseInsensitive)) return false;

    const QStringList tokens = trimmed.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (tokens.size() < 3) return false;

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
        if (m_tempSyntaxCombo) m_tempSyntaxCombo->setCurrentIndex(0);
        pos++;
        if (m_paramNameEdit) m_paramNameEdit->clear();
    } else if (pos + 1 < tokens.size() && tokens.value(pos + 1).contains('(') && tokens.value(pos + 1).contains(')')) {
        m_targetKindCombo->setCurrentIndex(3);
        if (m_modelTypeEdit) m_modelTypeEdit->setCurrentText(tokens.value(pos));
        const QString modelToken = tokens.value(pos + 1);
        const int open = modelToken.indexOf('(');
        const int close = modelToken.lastIndexOf(')');
        if (m_modelNameEdit) m_modelNameEdit->setCurrentText(modelToken.left(open));
        if (m_modelParamEdit && open > 0 && close > open) m_modelParamEdit->setText(modelToken.mid(open + 1, close - open - 1));
        pos += 2;
    } else {
        m_targetKindCombo->setCurrentIndex(2);
        if (m_sourceNameEdit) m_sourceNameEdit->setCurrentText(tokens.value(pos));
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

    return true;
}

void SpiceStepDialog::loadLevelIntoUi(int levelIndex) {
    if (levelIndex < 0 || levelIndex >= m_levelCommands.size()) return;
    const QString text = m_levelCommands[levelIndex].trimmed();
    if (text.isEmpty()) return;
    parseSingleLevelCommand(text);
}

void SpiceStepDialog::onDimensionCountChanged() {
    if (m_syncingCommand) return;
    syncCurrentLevelFromUi();
    const int count = m_dimensionCountCombo ? (m_dimensionCountCombo->currentIndex() + 1) : 1;
    if (m_editLevelCombo) {
        for (int i = 0; i < m_editLevelCombo->count(); ++i) {
            m_editLevelCombo->setItemData(i, i < count ? QVariant() : 0, Qt::UserRole - 1);
        }
        m_editLevelCombo->setCurrentIndex(qMin(currentLevelIndex(), count - 1));
    }
    updatePreview();
}

void SpiceStepDialog::onEditingLevelChanged() {
    if (m_syncingCommand) return;
    m_syncingCommand = true;
    loadLevelIntoUi(currentLevelIndex());
    m_syncingCommand = false;
    updateUiState();
}

void SpiceStepDialog::updateUiState() {
    if (m_targetStack) m_targetStack->setCurrentIndex(m_targetKindCombo->currentIndex());
    if (m_tempSyntaxCombo) {
        const bool enableTempAlias = currentTargetKind() == TargetKind::Temperature && currentSweepMode() == SweepMode::List;
        m_tempSyntaxCombo->setEnabled(enableTempAlias);
        if (!enableTempAlias) m_tempSyntaxCombo->setCurrentIndex(0);
    }
    m_modeStack->setCurrentIndex(m_sweepModeCombo->currentIndex());
    updateFilePreview();
    updatePreview();
}

void SpiceStepDialog::updatePreview() {
    if (m_syncingCommand) return;
    m_syncingCommand = true;

    syncCurrentLevelFromUi();
    const int count = m_dimensionCountCombo ? (m_dimensionCountCombo->currentIndex() + 1) : 1;
    QStringList commands;
    for (int i = 0; i < count && i < m_levelCommands.size(); ++i) {
        const QString line = m_levelCommands[i].trimmed();
        if (!line.isEmpty()) commands << line;
    }

    const QString cmd = commands.join("\n");
    m_commandEdit->setText(cmd);

    if (m_runEstimateLabel) {
        int totalRuns = 1;
        QStringList factors;
        bool ok = !commands.isEmpty();
        for (const QString& line : commands) {
            int count = 0;
            if (!estimateStepLineCount(line, count) || count <= 0) {
                ok = false;
                break;
            }
            factors << QString::number(count);
            totalRuns *= count;
        }

        if (!ok) {
            m_runEstimateLabel->setStyleSheet("color: #94a3b8; font-size: 11px;");
            m_runEstimateLabel->setText("Estimated runs: unavailable until all active sweep levels are valid.");
        } else {
            const QString factorText = factors.join(" x ");
            const QString warning = totalRuns > 512
                ? " Large sweep: execution and plotting may take noticeably longer."
                : QString();
            m_runEstimateLabel->setStyleSheet(totalRuns > 512
                ? "color: #f59e0b; font-size: 11px;"
                : "color: #94a3b8; font-size: 11px;");
            m_runEstimateLabel->setText(QString("Estimated runs: %1 (%2).%3")
                .arg(totalRuns)
                .arg(factorText)
                .arg(warning));
        }
    }

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
    if (text.isEmpty()) return;

    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    QStringList stepLines;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(".step", Qt::CaseInsensitive) || line.startsWith(".temp", Qt::CaseInsensitive)) stepLines << line;
    }
    if (stepLines.isEmpty()) return;

    m_syncingCommand = true;
    m_levelCommands = QStringList({QString(), QString(), QString()});
    const int count = qMin(stepLines.size(), 3);
    for (int i = 0; i < count; ++i) m_levelCommands[i] = stepLines[i];
    if (m_dimensionCountCombo) m_dimensionCountCombo->setCurrentIndex(count - 1);
    if (m_editLevelCombo) m_editLevelCombo->setCurrentIndex(0);
    loadLevelIntoUi(0);
    m_syncingCommand = false;
    updateUiState();
    updatePreview();
}

void SpiceStepDialog::browseStepFile() {
    const QString path = QFileDialog::getOpenFileName(this, "Select .step Values File", QString(), "Text Files (*.txt *.lst *.csv);;All Files (*)");
    if (path.isEmpty()) return;
    m_filePathEdit->setText(path);
    updateFilePreview();
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
