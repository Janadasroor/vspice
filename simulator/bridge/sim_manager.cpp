#include "sim_manager.h"
#include "sim_schematic_bridge.h"
#include "../core/raw_data_parser.h"
#include "../../schematic/items/schematic_item.h"
#include "../../schematic/items/smart_signal_item.h"
#include "../../core/simulation_manager.h"
#include "../../schematic/analysis/spice_netlist_generator.h"
#include "../core/sim_value_parser.h"
#include <QDebug>
#include <QFile>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <QDir>
#include <QRegularExpression>

namespace {

struct StepSpec {
    enum class Kind {
        Param,
        Temp,
        Source,
        ModelParam,
        Unsupported
    };

    Kind kind = Kind::Unsupported;
    QString target;
    QString modelType;
    QString modelName;
    QString modelParam;
    QString rawLine;
    QStringList values;
    QString error;
};

QStringList tokenizePreservingQuotes(const QString& text) {
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    for (QChar ch : text) {
        if (ch == '"') {
            inQuotes = !inQuotes;
            current += ch;
            continue;
        }
        if (ch.isSpace() && !inQuotes) {
            if (!current.isEmpty()) {
                tokens << current;
                current.clear();
            }
            continue;
        }
        current += ch;
    }
    if (!current.isEmpty()) tokens << current;
    return tokens;
}

QString stripCommentPrefix(const QString& line) {
    QString trimmed = line.trimmed();
    if (trimmed.startsWith('*')) {
        trimmed.remove(0, 1);
        trimmed = trimmed.trimmed();
    }
    return trimmed;
}

QString stripOuterQuotes(const QString& text) {
    if (text.size() >= 2 && ((text.startsWith('"') && text.endsWith('"')) || (text.startsWith('\'') && text.endsWith('\'')))) {
        return text.mid(1, text.size() - 2);
    }
    return text;
}

QStringList tokenizeStepFileLine(const QString& line) {
    QString normalized = line;
    normalized.replace(',', ' ');
    return tokenizePreservingQuotes(normalized);
}

bool loadStepValuesFromFile(const QString& fileToken, QStringList* values, QString* error) {
    if (!values) return false;

    const QString fileName = stripOuterQuotes(fileToken.trimmed());
    const QString filePath = QDir::current().absoluteFilePath(fileName);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Unable to open .step file '%1'.").arg(fileName);
        return false;
    }

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;
        const int semicolon = line.indexOf(';');
        if (semicolon >= 0) line = line.left(semicolon).trimmed();
        if (line.isEmpty()) continue;
        const QStringList lineTokens = tokenizeStepFileLine(line);
        for (const QString& token : lineTokens) {
            const QString trimmed = token.trimmed();
            if (!trimmed.isEmpty()) values->append(trimmed);
        }
    }

    if (values->isEmpty()) {
        if (error) *error = QString("No sweep values found in .step file '%1'.").arg(fileName);
        return false;
    }
    if (values->size() > 512) {
        if (error) *error = QString("Refusing to expand .step file '%1' with more than 512 points.").arg(fileName);
        return false;
    }
    return true;
}

bool buildLinearRange(const QString& startText, const QString& stopText, const QString& stepText, QStringList* values, QString* error) {
    double start = 0.0, stop = 0.0, step = 0.0;
    if (!SimValueParser::parseSpiceNumber(startText, start) || !SimValueParser::parseSpiceNumber(stopText, stop) ||
        !SimValueParser::parseSpiceNumber(stepText, step) || step == 0.0) {
        if (error) *error = QString("Unable to parse linear .step range: %1 %2 %3").arg(startText, stopText, stepText);
        return false;
    }
    const double direction = (stop >= start) ? 1.0 : -1.0;
    if ((direction > 0.0 && step < 0.0) || (direction < 0.0 && step > 0.0)) step = -step;
    const double eps = std::abs(step) * 1e-9 + 1e-18;
    for (double v = start; (direction > 0.0) ? (v <= stop + eps) : (v >= stop - eps); v += step) {
        values->append(QString::number(v, 'g', 12));
        if (values->size() > 512) {
            if (error) *error = "Refusing to expand .step with more than 512 points.";
            return false;
        }
    }
    return !values->isEmpty();
}

bool buildLogRange(const QString& mode, const QString& startText, const QString& stopText, const QString& countText, QStringList* values, QString* error) {
    double start = 0.0, stop = 0.0, count = 0.0;
    if (!SimValueParser::parseSpiceNumber(startText, start) || !SimValueParser::parseSpiceNumber(stopText, stop) ||
        !SimValueParser::parseSpiceNumber(countText, count) || count <= 0.0 || start <= 0.0 || stop <= 0.0) {
        if (error) *error = QString("Unable to parse logarithmic .step range: %1 %2 %3").arg(startText, stopText, countText);
        return false;
    }
    const double ratioBase = mode.compare("oct", Qt::CaseInsensitive) == 0 ? 2.0 : 10.0;
    const double pointsPer = count;
    const double factor = std::pow(ratioBase, 1.0 / pointsPer);
    if (factor <= 1.0) {
        if (error) *error = "Invalid .step logarithmic increment.";
        return false;
    }
    for (double v = start; v <= stop * (1.0 + 1e-12); v *= factor) {
        values->append(QString::number(v, 'g', 12));
        if (values->size() > 512) {
            if (error) *error = "Refusing to expand .step with more than 512 points.";
            return false;
        }
    }
    return !values->isEmpty();
}

StepSpec parseStepLine(const QString& rawLine) {
    StepSpec spec;
    spec.rawLine = rawLine.trimmed();
    const QString stepLine = stripCommentPrefix(rawLine);
    if (!stepLine.startsWith(".step", Qt::CaseInsensitive)) {
        spec.error = "Not a .step line.";
        return spec;
    }

    QStringList tokens = tokenizePreservingQuotes(stepLine);
    if (tokens.size() < 3) {
        spec.error = "Incomplete .step syntax.";
        return spec;
    }

    int idx = 1;
    QString mode = "lin";
    const QString maybeMode = tokens.value(idx).toLower();
    if (maybeMode == "lin" || maybeMode == "dec" || maybeMode == "oct") {
        mode = maybeMode;
        ++idx;
    }
    if (idx >= tokens.size()) {
        spec.error = "Missing .step target.";
        return spec;
    }

    if (tokens.value(idx).compare("param", Qt::CaseInsensitive) == 0) {
        spec.kind = StepSpec::Kind::Param;
        ++idx;
        spec.target = tokens.value(idx++);
    } else {
        spec.target = tokens.value(idx++);
        if (spec.target.compare("temp", Qt::CaseInsensitive) == 0) {
            spec.kind = StepSpec::Kind::Temp;
        } else if (QRegularExpression("^[VI]\\S*$", QRegularExpression::CaseInsensitiveOption).match(spec.target).hasMatch()) {
            spec.kind = StepSpec::Kind::Source;
        } else if (idx < tokens.size()) {
            const QRegularExpression modelParamRe("^([^()]+)\\(([^()]+)\\)$");
            const QRegularExpressionMatch modelMatch = modelParamRe.match(tokens.value(idx));
            if (modelMatch.hasMatch()) {
                spec.kind = StepSpec::Kind::ModelParam;
                spec.modelType = spec.target;
                spec.modelName = modelMatch.captured(1).trimmed();
                spec.modelParam = modelMatch.captured(2).trimmed();
                spec.target = QString("%1(%2)").arg(spec.modelName, spec.modelParam);
                ++idx;
            } else {
                spec.kind = StepSpec::Kind::Unsupported;
            }
        } else {
            spec.kind = StepSpec::Kind::Unsupported;
        }
    }

    if (spec.kind == StepSpec::Kind::Unsupported) {
        spec.error = QString("Unsupported .step target '%1'.").arg(spec.target);
        return spec;
    }
    if (idx >= tokens.size()) {
        spec.error = "Missing .step range or list.";
        return spec;
    }

    const QString next = tokens.value(idx);
    if (next.compare("list", Qt::CaseInsensitive) == 0) {
        spec.values = tokens.mid(idx + 1);
        if (spec.values.isEmpty()) spec.error = "Empty .step list.";
        return spec;
    }
    if (next.startsWith("file=", Qt::CaseInsensitive)) {
        QString error;
        const QString fileToken = next.mid(5).trimmed();
        if (fileToken.isEmpty() || !loadStepValuesFromFile(fileToken, &spec.values, &error)) {
            spec.error = error.isEmpty() ? "Invalid .step file= syntax." : error;
        }
        return spec;
    }
    if (tokens.size() < idx + 3) {
        spec.error = "Incomplete .step range syntax.";
        return spec;
    }

    QString error;
    bool ok = false;
    if (mode == "lin") ok = buildLinearRange(tokens.value(idx), tokens.value(idx + 1), tokens.value(idx + 2), &spec.values, &error);
    else ok = buildLogRange(mode, tokens.value(idx), tokens.value(idx + 1), tokens.value(idx + 2), &spec.values, &error);
    if (!ok) spec.error = error;
    return spec;
}

QList<StepSpec> parseStepSpecsFromNetlist(const QString& netlistContent) {
    QList<StepSpec> specs;
    const QStringList lines = netlistContent.split('\n');
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(".step", Qt::CaseInsensitive) || trimmed.startsWith("* .step", Qt::CaseInsensitive)) {
            specs.append(parseStepLine(trimmed));
        }
    }
    return specs;
}

bool replaceOrInjectParam(QString* netlist, const QString& name, const QString& value) {
    if (!netlist) return false;
    QStringList lines = netlist->split('\n');
    const QRegularExpression assignRe(QString("\\b%1\\s*=\\s*([^\\s]+)").arg(QRegularExpression::escape(name)), QRegularExpression::CaseInsensitiveOption);
    bool replaced = false;
    for (QString& line : lines) {
        if (!line.trimmed().startsWith(".param", Qt::CaseInsensitive)) continue;
        if (line.contains(assignRe)) {
            line.replace(assignRe, QString("%1=%2").arg(name, value));
            replaced = true;
        }
    }
    if (!replaced) {
        int insertAt = 0;
        if (!lines.isEmpty()) insertAt = 1;
        lines.insert(insertAt, QString(".param %1=%2").arg(name, value));
    }
    *netlist = lines.join('\n');
    return true;
}

bool replaceOrInjectTemp(QString* netlist, const QString& value) {
    if (!netlist) return false;
    QStringList lines = netlist->split('\n');
    bool replaced = false;
    for (QString& line : lines) {
        if (line.trimmed().startsWith(".temp", Qt::CaseInsensitive)) {
            line = QString(".temp %1").arg(value);
            replaced = true;
        }
    }
    if (!replaced) lines.insert(1, QString(".temp %1").arg(value));
    *netlist = lines.join('\n');
    return true;
}

bool replaceSimpleSourceValue(QString* netlist, const QString& ref, const QString& value) {
    if (!netlist) return false;
    QStringList lines = netlist->split('\n');
    const QRegularExpression sourceRe(QString("^\\s*(%1)\\s+(\\S+)\\s+(\\S+)\\s+(?:DC\\s+)?(\\{[^}]+\\}|[-+]?\\d+(?:\\.\\d*)?(?:[eE][-+]?\\d+)?[A-Za-z]*)\\s*$")
                                          .arg(QRegularExpression::escape(ref)),
                                      QRegularExpression::CaseInsensitiveOption);
    for (QString& line : lines) {
        const QRegularExpressionMatch m = sourceRe.match(line);
        if (!m.hasMatch()) continue;
        line = QString("%1 %2 %3 %4").arg(m.captured(1), m.captured(2), m.captured(3), value);
        *netlist = lines.join('\n');
        return true;
    }
    return false;
}

bool replaceOrInjectModelParam(QString* netlist, const StepSpec& spec, const QString& value, QString* error) {
    if (!netlist) return false;

    QStringList lines = netlist->split('\n');
    const QRegularExpression modelRe(
        QString("^\\s*\\.model\\s+(%1)\\s+(%2)\\s*\\((.*)\\)\\s*$")
            .arg(QRegularExpression::escape(spec.modelName), QRegularExpression::escape(spec.modelType)),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression assignRe(
        QString("\\b%1\\s*=\\s*(\\{[^}]+\\}|[^\\s,)]+)")
            .arg(QRegularExpression::escape(spec.modelParam)),
        QRegularExpression::CaseInsensitiveOption);

    for (int i = 0; i < lines.size(); ++i) {
        const QString firstLine = lines.at(i);
        if (!firstLine.trimmed().startsWith(".model", Qt::CaseInsensitive)) continue;

        QStringList cardLines;
        cardLines << firstLine;
        int lastLine = i;
        while (lastLine + 1 < lines.size() && lines.at(lastLine + 1).trimmed().startsWith('+')) {
            ++lastLine;
            cardLines << lines.at(lastLine);
        }

        QString combined = cardLines.takeFirst().trimmed();
        for (const QString& continuation : cardLines) {
            QString part = continuation.trimmed();
            if (part.startsWith('+')) part.remove(0, 1);
            combined += " ";
            combined += part.trimmed();
        }

        const QRegularExpressionMatch modelMatch = modelRe.match(combined);
        if (!modelMatch.hasMatch()) continue;

        QString body = modelMatch.captured(3).trimmed();
        if (body.contains(assignRe)) {
            body.replace(assignRe, QString("%1=%2").arg(spec.modelParam, value));
        } else if (body.isEmpty()) {
            body = QString("%1=%2").arg(spec.modelParam, value);
        } else {
            body += QString(" %1=%2").arg(spec.modelParam, value);
        }

        lines[i] = QString(".model %1 %2(%3)").arg(spec.modelName, spec.modelType, body);
        for (int removeAt = lastLine; removeAt > i; --removeAt) {
            lines.removeAt(removeAt);
        }
        *netlist = lines.join('\n');
        return true;
    }

    if (error) {
        *error = QString("Could not find .model %1 %2(...) while stepping %3.")
                     .arg(spec.modelName, spec.modelType, spec.target);
    }
    return false;
}

bool applyStepValue(QString* netlist, const StepSpec& spec, const QString& value, QString* error) {
    switch (spec.kind) {
    case StepSpec::Kind::Param:
        return replaceOrInjectParam(netlist, spec.target, value);
    case StepSpec::Kind::Temp:
        return replaceOrInjectTemp(netlist, value);
    case StepSpec::Kind::Source:
        if (replaceSimpleSourceValue(netlist, spec.target, value)) return true;
        if (error) *error = QString("Only simple independent source stepping is currently supported for %1.").arg(spec.target);
        return false;
    case StepSpec::Kind::ModelParam:
        return replaceOrInjectModelParam(netlist, spec, value, error);
    default:
        if (error) *error = spec.error;
        return false;
    }
}

void buildStepRunCartesian(const QList<StepSpec>& specs, int index, QString currentNetlist, QStringList currentLabels,
                           QList<SimManager::PendingStepRun>* runs, QString* error) {
    if (!runs) return;
    if (index >= specs.size()) {
        runs->append({currentNetlist, currentLabels.join(", ")});
        return;
    }
    const StepSpec& spec = specs.at(index);
    for (const QString& value : spec.values) {
        QString runNetlist = currentNetlist;
        QString applyError;
        if (!applyStepValue(&runNetlist, spec, value, &applyError)) {
            if (error) *error = applyError;
            return;
        }
        QStringList labels = currentLabels;
        labels << QString("%1=%2").arg(spec.target, value);
        if (runs->size() > 512) {
            if (error) *error = "Refusing to build more than 512 sweep runs.";
            return;
        }
        buildStepRunCartesian(specs, index + 1, runNetlist, labels, runs, error);
        if (error && !error->isEmpty()) return;
    }
}

QList<SimManager::PendingStepRun> buildStepRuns(const QString& netlistContent, QString* error) {
    QList<SimManager::PendingStepRun> runs;
    const QList<StepSpec> specs = parseStepSpecsFromNetlist(netlistContent);
    if (specs.isEmpty()) return runs;
    for (const StepSpec& spec : specs) {
        if (!spec.error.isEmpty()) {
            if (error) *error = spec.error;
            return {};
        }
    }
    QString stepStripped = netlistContent;
    stepStripped.replace(QRegularExpression("^\\s*\\*?\\s*\\.step.*$", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption),
                         "* LTspice .step sweep handled by VioSpice sweep runner");
    buildStepRunCartesian(specs, 0, stepStripped, {}, &runs, error);
    return runs;
}

QString withStepSuffix(const QString& name, const QString& stepLabel) {
    return stepLabel.trimmed().isEmpty() ? name : QString("%1 [%2]").arg(name, stepLabel);
}

} // namespace

SimManager& SimManager::instance() {
    static SimManager inst;
    return inst;
}

SimManager::~SimManager() = default;

SimManager::SimManager(QObject* parent) : QObject(parent) {
    connect(this, &SimManager::netlistGenerated, this, [this](const QString& netlist, const SimAnalysisConfig& /*config*/) {
        startNgspiceWithNetlist(netlist);
    });
    
    connect(this, &SimManager::generationFailed, this, [this](const QString& error) {
        emit errorOccurred(error);
    });
    
    m_resultsPending = false;
}

void SimManager::runDCOP(QGraphicsScene* scene, NetManager* netMgr) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runTransient(QGraphicsScene* scene, NetManager* netMgr, double tStop, double tStep) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStop = tStop;
    config.tStep = tStep;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runAC(QGraphicsScene* scene, NetManager* netMgr, double fStart, double fStop, int points) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = fStart;
    config.fStop = fStop;
    config.fPoints = points;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runMonteCarlo(QGraphicsScene* scene, NetManager* netMgr, int runs) {
    // Ngspice support for Monte Carlo usually involves a control script loop.
    emit logMessage("Monte Carlo via Ngspice not fully implemented yet, running OP.");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runParametricSweep(QGraphicsScene* scene, NetManager* netMgr, const QString& /*component*/, const QString& /*param*/, double /*start*/, double /*stop*/, int /*steps*/) {
    // Requires .control script loop
    emit logMessage("Parametric Sweep via Ngspice not fully implemented yet, running OP.");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runSensitivity(QGraphicsScene* scene, NetManager* netMgr, const QString& targetSignal) {
     emit logMessage("Sensitivity analysis via Ngspice not fully implemented yet.");
     // Fallback to OP
     SimAnalysisConfig config;
     config.type = SimAnalysisType::OP;
     QString netlist = generateNetlist(scene, netMgr, config);
     runNgspiceSimulation(netlist, config);
}

void SimManager::runNetlistText(const QString& netlistContent) {
    if (m_control) {
        emit logMessage("A simulation is already running.");
        return;
    }
    emit simulationStarted();
    emit logMessage("Running ngspice netlist...");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    m_lastConfig = config;

    QString stepError;
    m_pendingStepRuns = buildStepRuns(netlistContent, &stepError);
    m_stepSweepResults = SimResults();
    m_activeStepLabel.clear();
    m_completedStepRuns = 0;
    if (!stepError.isEmpty()) {
        emit logMessage(QString("LTspice .step emulation error: %1").arg(stepError));
        emit errorOccurred(stepError);
        emit simulationFinished(SimResults());
        return;
    }
    if (!m_pendingStepRuns.isEmpty()) {
        emit logMessage(QString("Running LTspice .step sweep emulation with %1 run(s).").arg(m_pendingStepRuns.size()));
        startNextStepSweepRun();
        return;
    }

    startNgspiceWithNetlist(netlistContent);
}

QString SimManager::generateNetlist(QGraphicsScene* scene, NetManager* netMgr, const SimAnalysisConfig& config, const QString& projectDir) {
    if (!scene) return "* Error: No scene provided for netlist generation\n";

    // Map SimAnalysisConfig to SpiceNetlistGenerator::SimulationParams
    SpiceNetlistGenerator::SimulationParams params;
    switch (config.type) {
        case SimAnalysisType::Transient:
            params.type = SpiceNetlistGenerator::Transient;
            params.start = "0";
            params.stop = QString::number(config.tStop);
            params.step = QString::number(config.tStep);
            break;
        case SimAnalysisType::AC:
            params.type = SpiceNetlistGenerator::AC;
            params.start = QString::number(config.fStart > 0.0 ? config.fStart : 10.0, 'g', 12);
            params.stop = QString::number(config.fStop > 0.0 ? config.fStop : 1e6, 'g', 12);
            params.step = QString::number(config.fPoints > 0 ? config.fPoints : 10);
            break;
        case SimAnalysisType::OP:
        default:
            params.type = SpiceNetlistGenerator::OP;
            break;
    }

    return SpiceNetlistGenerator::generate(scene, projectDir, netMgr, params);
}

void SimManager::runNgspiceSimulation(const QString& netlist, const SimAnalysisConfig& config) {
    if (m_control) {
        emit logMessage("A simulation is already running.");
        return;
    }

    emit simulationStarted();
    m_lastConfig = config;

    if (netlist.isEmpty() || netlist.startsWith("* Missing scene")) {
        emit logMessage("Ngspice: Data parse error or empty results.");
        emit errorOccurred("Netlist generation failed or empty results.");
        emit simulationFinished(SimResults());
        return;
    }

    emit logMessage(QString("Starting ngspice simulation (Analysis: %1)...").arg(static_cast<int>(config.type)));

    QString stepError;
    m_pendingStepRuns = buildStepRuns(netlist, &stepError);
    m_stepSweepResults = SimResults();
    m_activeStepLabel.clear();
    m_completedStepRuns = 0;
    if (!stepError.isEmpty()) {
        emit logMessage(QString("LTspice .step emulation error: %1").arg(stepError));
        emit errorOccurred(stepError);
        emit simulationFinished(SimResults());
        return;
    }
    if (!m_pendingStepRuns.isEmpty()) {
        emit logMessage(QString("Running LTspice .step sweep emulation with %1 run(s).").arg(m_pendingStepRuns.size()));
        startNextStepSweepRun();
        return;
    }

    startNgspiceWithNetlist(netlist);
}

void SimManager::startNextStepSweepRun() {
    if (m_pendingStepRuns.isEmpty()) {
        SimResults finalResults = m_stepSweepResults;
        m_stepSweepResults = SimResults();
        m_activeStepLabel.clear();
        m_completedStepRuns = 0;
        emit simulationFinished(finalResults);
        return;
    }

    const PendingStepRun run = m_pendingStepRuns.takeFirst();
    m_activeStepLabel = run.label;
    emit logMessage(QString("Running .step case %1").arg(run.label));
    startNgspiceWithNetlist(run.netlist);
}

void SimManager::mergeStepSweepResults(const SimResults& runResults, const QString& stepLabel, int runIndex) {
    if (!m_stepSweepResults.isSchemaCompatible()) {
        m_stepSweepResults = SimResults();
    }
    if (m_stepSweepResults.waveforms.empty() && m_stepSweepResults.nodeVoltages.empty() && m_stepSweepResults.branchCurrents.empty() && m_stepSweepResults.measurements.empty()) {
        m_stepSweepResults.analysisType = runResults.analysisType;
        m_stepSweepResults.xAxisName = runResults.xAxisName;
        m_stepSweepResults.yAxisName = runResults.yAxisName;
    }

    for (const auto& wave : runResults.waveforms) {
        SimWaveform labeled = wave;
        labeled.name = withStepSuffix(QString::fromStdString(wave.name), stepLabel).toStdString();
        m_stepSweepResults.waveforms.push_back(std::move(labeled));
    }
    for (const auto& [name, val] : runResults.nodeVoltages) {
        m_stepSweepResults.nodeVoltages[withStepSuffix(QString::fromStdString(name), stepLabel).toStdString()] = val;
    }
    for (const auto& [name, val] : runResults.branchCurrents) {
        m_stepSweepResults.branchCurrents[withStepSuffix(QString::fromStdString(name), stepLabel).toStdString()] = val;
    }
    for (const auto& [name, val] : runResults.measurements) {
        m_stepSweepResults.measurements[withStepSuffix(QString::fromStdString(name), stepLabel).toStdString()] = val;
    }
    m_stepSweepResults.diagnostics.push_back(QString("Step run %1: %2").arg(runIndex).arg(stepLabel).toStdString());
    m_completedStepRuns = runIndex;
}

void SimManager::startNgspiceWithNetlist(const QString& netlistContent) {
    // Create a temporary file that auto-deletes when the object is destroyed.
    // However, we need it to persist until simulation load.
    // We'll manage it via a member or just use a transient one and pass path.
    auto* tempFile = new QTemporaryFile(this);
    tempFile->setAutoRemove(false);
    if (tempFile->open()) {
        QTextStream out(tempFile);
        out << netlistContent;
        tempFile->close();
    } else {
        emit errorOccurred("Failed to create temporary netlist file.");
        delete tempFile;
        return;
    }

    m_control = new SimControl();
    m_paused = false;

    auto& sm = SimulationManager::instance();
    
    // Disconnect previous connections to avoid duplicates
    sm.disconnect(this);

    // Use a QObject parent for the file to ensure it's deleted eventually,
    // but we also use a QPointer to track its lifetime across multiple lambdas.
    QPointer<QTemporaryFile> safeTempFile = tempFile;

    connect(&sm, &SimulationManager::outputReceived, this, &SimManager::logMessage, Qt::QueuedConnection);
    connect(&sm, &SimulationManager::errorOccurred, this, [this, safeTempFile](const QString& msg) {
        emit logMessage(QString("Ngspice error: %1").arg(msg));
        emit errorOccurred(msg);
        if (safeTempFile) safeTempFile->deleteLater();
        cleanupSimulation();
    }, Qt::QueuedConnection);

    connect(&sm, &SimulationManager::realTimeDataBatchReceived, this, &SimManager::realTimeDataBatchReceived, Qt::QueuedConnection);
    
    connect(&sm, &SimulationManager::rawResultsReady, this, [this, safeTempFile](const QString& path) {
        m_resultsPending = true;
        auto* watcher = new QFutureWatcher<std::pair<bool, SimResults>>(this);
        connect(watcher, &QFutureWatcher<std::pair<bool, SimResults>>::finished, this, [this, watcher, safeTempFile]() {
            if (!m_control && !m_resultsPending) {
                watcher->deleteLater();
                return; // Already cleaned up or another watcher finished?
            }
            
            auto result = watcher->result();
            watcher->deleteLater();
            
            m_resultsPending = false;
            if (result.first) {
                if (!m_activeStepLabel.isEmpty() || !m_pendingStepRuns.isEmpty()) {
                    mergeStepSweepResults(result.second, m_activeStepLabel, m_completedStepRuns + 1);
                } else {
                    emit simulationFinished(result.second);
                }
            } else {
                const QString err = "Ngspice: Data parse error or empty results.";
                emit logMessage(err);
                emit errorOccurred(err);
                // Still close run-state on editor side even when results are unavailable.
                emit simulationFinished(SimResults());
            }
            
            const bool continueStepSweep = result.first && (!m_activeStepLabel.isEmpty() || !m_pendingStepRuns.isEmpty());
            if (safeTempFile) safeTempFile->deleteLater();
            cleanupSimulation();
            if (continueStepSweep) {
                QTimer::singleShot(0, this, &SimManager::startNextStepSweepRun);
            }
        });

        watcher->setFuture(QtConcurrent::run([path]() {
            RawData rd;
            QString err;
            if (RawDataParser::loadRawAscii(path, &rd, &err)) {
                return std::make_pair(true, rd.toSimResults());
            }
            qDebug() << "RawDataParser error:" << err;
            return std::make_pair(false, SimResults());
        }));
    }, Qt::QueuedConnection);
    
    // Handle the simulation engine finishing (it fires regardless of result parsing)
    connect(&sm, &SimulationManager::simulationFinished, this, [this, safeTempFile]() {
        // Results might already be in flight via rawResultsReady handler.
        // We wait a bit to see if they arrive. If not, we cleanup.
        QTimer::singleShot(2000, this, [this, safeTempFile]() {
            if (m_control && !m_resultsPending) {
                emit logMessage("Ngspice finished without RAW results; closing simulation run.");
                if (m_pendingStepRuns.isEmpty() && m_activeStepLabel.isEmpty()) {
                    emit simulationFinished(SimResults());
                }
                if (safeTempFile) safeTempFile->deleteLater();
                cleanupSimulation();
            } else if (m_control && m_resultsPending) {
                // Still waiting for the heavy parser? Let it finish.
                // But if it takes TOO long, we might still want a timeout.
            }
        });
    }, Qt::QueuedConnection);

    sm.runSimulation(tempFile->fileName(), m_control);
}

void SimManager::cleanupSimulation() {
    if (m_control) {
        SimControl* ctrl = m_control;
        m_control = nullptr;
        // Delay deletion to ensure any pending signals/threads are done
        QTimer::singleShot(100, [ctrl]() {
            if (ctrl) delete ctrl;
        });
    }
    if (m_pendingStepRuns.isEmpty()) {
        m_activeStepLabel.clear();
    }
}

void SimManager::runRealTime(QGraphicsScene* scene, NetManager* netMgr, int intervalMs) {
    // Stub for now or implement via repeated OP
    emit logMessage("Real-time simulation via Ngspice not yet optimized. Running single-shot OP.");
    runDCOP(scene, netMgr);
}

void SimManager::stopRealTime() {
    if (m_rtTimer) {
        m_rtTimer->stop();
        delete m_rtTimer;
        m_rtTimer = nullptr;
    }
    m_rtScene = nullptr;
}

void SimManager::onInteractiveStateChanged() {
    // Interactive changes handling
}

void SimManager::onRealTimeTick() {
    // Real-time tick handling
}

QStringList SimManager::preflightCheck(QGraphicsScene* scene, NetManager* netMgr, SimNetlist& outNetlist) {
    // Generate netlist via bridge just to check structure/connectivity
    outNetlist = SimSchematicBridge::buildNetlist(scene, netMgr);
    outNetlist.flatten();
    
    QStringList diag;
    for (const auto& d : outNetlist.diagnostics()) {
        QString qd = QString::fromStdString(d);
        if (!qd.trimmed().isEmpty()) diag << qd;
    }
    return diag;
}

void SimManager::runWithNetlist(const SimNetlist& netlist) {
     emit errorOccurred("Direct SimNetlist execution not supported with Ngspice backend yet. Use UI.");
}

void SimManager::stopAll() {
    SimulationManager::instance().stopSimulation();
    cleanupSimulation();
    emit logMessage("Simulation stopped.");
}

void SimManager::pauseSimulation(bool pause) {
    // Ngspice bg_halt / bg_resume could be used
    emit logMessage("Pause not fully implemented for Ngspice.");
}
