#include "sim_manager.h"
#include "sim_schematic_bridge.h"
#include "../core/sim_net_evaluator.h"
#include "../core/raw_data_parser.h"
#include "../../schematic/items/schematic_item.h"
#include "../../schematic/items/smart_signal_item.h"
#include "simulation_manager.h"
#include "../../schematic/analysis/spice_netlist_generator.h"
#include "../core/sim_value_parser.h"
#include <QDebug>
#include <QFile>
#include <QTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTemporaryFile>
#include <QDir>
#include <QProcess>
#include <QPointer>
#include <QSharedPointer>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent>

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

QString enrichNgspiceFailureMessage(const QString& baseMessage, const QStringList& tail) {
    QString msg = baseMessage;
    if (!tail.isEmpty()) {
        msg += "\n" + tail.join("\n");
    }

    const QString joined = tail.join('\n');
    QStringList hints;

    const bool sawOtaFailure =
        joined.contains("unable to find definition of model", Qt::CaseInsensitive) &&
        joined.contains(QRegularExpression("\\bota\\b", QRegularExpression::CaseInsensitiveOption));
    if (sawOtaFailure) {
        hints << "This library uses LTspice's built-in OTA A-device syntax. Your ngspice build includes XSPICE code models, but the ngspice source tree does not provide an OTA code model, so these macromodels cannot run without translation.";
    }

    if (joined.contains("no such function 'uplim'", Qt::CaseInsensitive) ||
        joined.contains("no such function 'dnlim'", Qt::CaseInsensitive) ||
        joined.contains("can't find model 'noiseless'", Qt::CaseInsensitive)) {
        hints << "The failing library still contains LTspice-specific syntax such as uplim/dnlim or noiseless. Import and run the model through VioSpice's sanitized include path rather than including the raw LTspice file directly.";
    }

    if (!hints.isEmpty()) {
        msg += "\n\nCompatibility hints:\n- " + hints.join("\n- ");
    }
    return msg;
}

QString detectUnsupportedOtaModelUsage(const QString& netlistText) {
    const QRegularExpression includeRe(
        QStringLiteral("^\\s*\\.(?:include|lib)\\s+\"?([^\"\\r\\n]+)\"?"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const QRegularExpression otaLineRe(
        R"(^\s*A\S*.*\bOTA\b)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);

    auto hasUnsupportedOta = [&](const QString& text) {
        return otaLineRe.match(text).hasMatch();
    };

    if (hasUnsupportedOta(netlistText)) {
        return QStringLiteral("The generated netlist uses LTspice OTA A-device syntax, which this ngspice backend does not support directly.");
    }

    QSet<QString> scanned;
    QRegularExpressionMatchIterator it = includeRe.globalMatch(netlistText);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString includePath = QDir::cleanPath(match.captured(1).trimmed());
        if (includePath.isEmpty() || scanned.contains(includePath)) continue;
        scanned.insert(includePath);

        QFile file(includePath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QString content = QString::fromUtf8(file.readAll());
        if (content.contains(QChar::ReplacementCharacter)) {
            file.seek(0);
            content = QString::fromLatin1(file.readAll());
        }
        file.close();

        if (hasUnsupportedOta(content)) {
            return QString("The included model library %1 uses LTspice OTA A-device syntax, which this ngspice backend does not support directly. Import it through a translated model path or use a non-OTA variant.")
                .arg(includePath);
        }
    }

    return QString();
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

void sortFirstStepDimensionValues(QList<StepSpec>* specs) {
    if (!specs || specs->isEmpty()) return;

    QStringList& values = (*specs)[0].values;
    if (values.size() < 2) return;

    struct ParsedValue {
        QString text;
        double numeric = 0.0;
    };

    QList<ParsedValue> parsed;
    parsed.reserve(values.size());
    for (const QString& value : values) {
        ParsedValue item;
        item.text = value;
        if (!SimValueParser::parseSpiceNumber(value, item.numeric)) {
            return;
        }
        parsed.append(item);
    }

    std::stable_sort(parsed.begin(), parsed.end(), [](const ParsedValue& a, const ParsedValue& b) {
        return a.numeric < b.numeric;
    });

    values.clear();
    for (const ParsedValue& item : parsed) values.append(item.text);
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
    QList<StepSpec> specs = parseStepSpecsFromNetlist(netlistContent);
    if (specs.isEmpty()) return runs;
    for (const StepSpec& spec : specs) {
        if (!spec.error.isEmpty()) {
            if (error) *error = spec.error;
            return {};
        }
    }
    sortFirstStepDimensionValues(&specs);
    QString stepStripped = netlistContent;
    stepStripped.replace(QRegularExpression("^\\s*\\*?\\s*\\.step.*$", QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption),
                         "* LTspice .step sweep handled by VioSpice sweep runner");
    buildStepRunCartesian(specs, 0, stepStripped, {}, &runs, error);
    return runs;
}

std::vector<MeasStatement> parseMeasStatementsFromNetlist(const QString& netlistContent) {
    std::vector<MeasStatement> statements;
    QStringList logicalLines;
    for (const QString& rawLine : netlistContent.split('\n')) {
        const QString trimmed = rawLine.trimmed();
        if (!logicalLines.isEmpty() && trimmed.startsWith('+')) {
            logicalLines.last() += " " + trimmed.mid(1).trimmed();
        } else {
            logicalLines.append(rawLine);
        }
    }

    int lineNo = 0;
    for (const QString& rawLine : logicalLines) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;

        std::string parseLine;
        if (line.startsWith(".mean", Qt::CaseInsensitive)) {
            static const QRegularExpression re(
                "^\\s*\\.mean\\s+(?:(avg|max|min|rms)\\s+)?([^\\s]+)(?:\\s+from\\s*=\\s*([^\\s]+))?(?:\\s+to\\s*=\\s*([^\\s]+))?\\s*$",
                QRegularExpression::CaseInsensitiveOption);
            const auto m = re.match(line);
            if (!m.hasMatch()) continue;
            const QString mode = m.captured(1).isEmpty() ? QString("avg") : m.captured(1).toLower();
            const QString signal = m.captured(2).trimmed();
            const QString from = m.captured(3).trimmed();
            const QString to = m.captured(4).trimmed();
            parseLine = QString(".meas tran mean_l%1 %2 %3%4%5")
                            .arg(lineNo)
                            .arg(mode, signal)
                            .arg(from.isEmpty() ? QString() : QString(" from=%1").arg(from))
                            .arg(to.isEmpty() ? QString() : QString(" to=%1").arg(to))
                            .toStdString();
        } else if (line.startsWith(".meas", Qt::CaseInsensitive)) {
            parseLine = line.toStdString();
        } else {
            continue;
        }

        MeasStatement stmt;
        if (SimMeasEvaluator::parse(parseLine, lineNo, "netlist", stmt)) statements.push_back(stmt);
    }
    return statements;
}

std::vector<NetStatement> parseNetStatementsFromNetlist(const QString& netlistContent) {
    std::vector<NetStatement> statements;
    int lineNo = 0;
    for (const QString& rawLine : netlistContent.split('\n')) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;
        NetStatement stmt;
        if (SimNetEvaluator::parse(line.toStdString(), lineNo, "netlist", stmt)) {
            statements.push_back(stmt);
        }
    }
    return statements;
}

std::map<std::string, NetSourceInfo> parseSourceInfosFromNetlist(const QString& netlistContent) {
    std::map<std::string, NetSourceInfo> sources;
    for (const QString& rawLine : netlistContent.split('\n')) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('.') || line.startsWith('*') || line.startsWith(';') || line.startsWith('#')) continue;
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;
        const QString ref = parts.at(0).trimmed().toUpper();
        if (!(ref.startsWith('V') || ref.startsWith('I'))) continue;
        NetSourceInfo info;
        info.positiveNode = parts.at(1).trimmed().toUpper().toStdString();
        info.negativeNode = parts.at(2).trimmed().toUpper().toStdString();
        sources[ref.toStdString()] = info;
    }
    return sources;
}

QString stripMeasStatementsFromNetlist(const QString& netlistContent) {
    QStringList outLines;
    bool previousWasMeas = false;
    for (const QString& rawLine : netlistContent.split('\n')) {
        const QString trimmed = rawLine.trimmed();
        const bool isMeas = trimmed.startsWith(".meas", Qt::CaseInsensitive) || trimmed.startsWith(".measure", Qt::CaseInsensitive)
                            || trimmed.startsWith(".measur", Qt::CaseInsensitive) || trimmed.startsWith(".mean", Qt::CaseInsensitive);
        const bool isCont = trimmed.startsWith('+');
        if (isMeas || (previousWasMeas && isCont)) {
            outLines.append(QString("* VioSpice evaluates this measurement post-simulation: %1").arg(rawLine));
            previousWasMeas = true;
            continue;
        }
        previousWasMeas = false;
        outLines.append(rawLine);
    }
    return outLines.join('\n');
}

QString stripNetStatementsFromNetlist(const QString& netlistContent) {
    QStringList outLines;
    for (const QString& rawLine : netlistContent.split('\n')) {
        const QString trimmed = rawLine.trimmed();
        if (trimmed.startsWith(".net", Qt::CaseInsensitive)) {
            outLines.append(QString("* VioSpice evaluates this .net statement post-simulation: %1").arg(rawLine));
            continue;
        }
        outLines.append(rawLine);
    }
    return outLines.join('\n');
}

std::string measAnalysisToken(SimAnalysisType type) {
    switch (type) {
    case SimAnalysisType::OP: return "op";
    case SimAnalysisType::Transient: return "tran";
    case SimAnalysisType::AC: return "ac";
    case SimAnalysisType::DC: return "dc";
    case SimAnalysisType::Noise: return "noise";
    default: return std::string();
    }
}

void evaluateMeasStatementsIntoResults(const QString& netlistContent, SimAnalysisType analysisType, SimResults* results) {
    if (!results) return;
    const std::vector<MeasStatement> statements = parseMeasStatementsFromNetlist(netlistContent);
    if (statements.empty()) return;
    const std::string atype = measAnalysisToken(analysisType);
    if (atype.empty()) return;
    for (const auto& mr : SimMeasEvaluator::evaluate(statements, *results, atype)) {
        if (mr.valid) {
            results->measurements[mr.name] = mr.value;
            results->measurementMetadata[mr.name] = {mr.quantityLabel, mr.displayUnit};
        }
        else results->diagnostics.push_back(".meas " + mr.name + " failed: " + mr.error);
    }
}

void evaluateNetStatementsIntoResults(const QString& netlistContent, SimAnalysisType analysisType, SimResults* results) {
    if (!results) return;
    const std::vector<NetStatement> statements = parseNetStatementsFromNetlist(netlistContent);
    if (statements.empty()) return;
    const NetEvaluation eval = SimNetEvaluator::evaluate(statements, parseSourceInfosFromNetlist(netlistContent), *results, measAnalysisToken(analysisType));
    for (const SimWaveform& waveform : eval.waveforms) results->waveforms.push_back(waveform);
    for (const std::string& diag : eval.diagnostics) results->diagnostics.push_back(diag);
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
        Q_EMIT errorOccurred(error);
    });

    auto& liveSim = SimulationManager::instance();
    connect(&liveSim, &SimulationManager::outputReceived, this, [this](const QString& text) {
        Q_EMIT logMessage(text);
    });
    connect(&liveSim, &SimulationManager::errorOccurred, this, [this](const QString& error) {
        Q_EMIT logMessage(error);
        Q_EMIT errorOccurred(error);
    });
    connect(&liveSim, &SimulationManager::realTimeDataBatchReceived, this, [this](const std::vector<double>& times,
                                                                                  const std::vector<std::vector<double>>& values,
                                                                                  const QStringList& names) {
        if (m_lastConfig.type == SimAnalysisType::RealTime || m_lastConfig.type == SimAnalysisType::Transient) {
            Q_EMIT realTimeDataBatchReceived(times, values, names);
        }
    });
    connect(&liveSim, &SimulationManager::rawResultsReady, this, [this](const QString& rawPath) {
        const bool sharedTransientRun =
            (m_lastConfig.type == SimAnalysisType::RealTime || m_lastConfig.type == SimAnalysisType::Transient) &&
            !m_ngspiceProcess &&
            (!m_sharedNetlistPath.isEmpty() || m_control || m_stopRequested);
        if (sharedTransientRun) {
            parseRawResultsFile(rawPath, m_activeNetlistText, SimAnalysisType::Transient);
        }
    });
    
    m_resultsPending = false;
}

void SimManager::runDCOP(QGraphicsScene* scene, NetManager* netMgr) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    compileFluxScripts(scene);
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runTransient(QGraphicsScene* scene, NetManager* netMgr, double tStop, double tStep) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStop = tStop;
    config.tStep = tStep;
    compileFluxScripts(scene);
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runAC(QGraphicsScene* scene, NetManager* netMgr, double fStart, double fStop, int points) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = fStart;
    config.fStop = fStop;
    config.fPoints = points;
    compileFluxScripts(scene);
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runRFAnalysis(QGraphicsScene* scene, NetManager* netMgr, double fStart, double fStop, int points, const QString& p1Src, const QString& p2Node, double z0) {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::SParameter;
    config.fStart = fStart;
    config.fStop = fStop;
    config.fPoints = points;
    config.rfPort1Source = p1Src.toStdString();
    config.rfPort2Node = p2Node.toStdString();
    config.rfZ0 = z0;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runMonteCarlo(QGraphicsScene* scene, NetManager* netMgr, int runs) {
    // Ngspice support for Monte Carlo usually involves a control script loop.
    Q_EMIT logMessage("Monte Carlo via Ngspice not fully implemented yet, running OP.");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runParametricSweep(QGraphicsScene* scene, NetManager* netMgr, const QString& /*component*/, const QString& /*param*/, double /*start*/, double /*stop*/, int /*steps*/) {
    // Requires .control script loop
    Q_EMIT logMessage("Parametric Sweep via Ngspice not fully implemented yet, running OP.");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    QString netlist = generateNetlist(scene, netMgr, config);
    runNgspiceSimulation(netlist, config);
}

void SimManager::runSensitivity(QGraphicsScene* scene, NetManager* netMgr, const QString& targetSignal) {
     Q_EMIT logMessage("Sensitivity analysis via Ngspice not fully implemented yet.");
     // Fallback to OP
     SimAnalysisConfig config;
     config.type = SimAnalysisType::OP;
     QString netlist = generateNetlist(scene, netMgr, config);
     runNgspiceSimulation(netlist, config);
}

void SimManager::runNetlistText(const QString& netlistContent) {
    if (m_control) {
        Q_EMIT logMessage("A simulation is already running.");
        return;
    }
    m_stopRequested = false;
    m_paused = false;
    Q_EMIT simulationStarted();
    Q_EMIT logMessage("Running ngspice netlist...");
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    m_lastConfig = config;

    QString stepError;
    m_pendingStepRuns = buildStepRuns(netlistContent, &stepError);
    m_stepSweepResults = SimResults();
    m_activeStepLabel.clear();
    m_completedStepRuns = 0;
    if (!stepError.isEmpty()) {
        Q_EMIT logMessage(QString("LTspice .step emulation error: %1").arg(stepError));
        Q_EMIT errorOccurred(stepError);
        Q_EMIT simulationFinished(SimResults());
        return;
    }
    if (!m_pendingStepRuns.isEmpty()) {
        Q_EMIT logMessage(QString("Running LTspice .step sweep emulation with %1 run(s).").arg(m_pendingStepRuns.size()));
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
        case SimAnalysisType::RealTime:
            params.type = SpiceNetlistGenerator::Transient;
            params.start = "0";
            params.stop = QString::number(config.tStop);
            params.step = QString::number(config.tStep);
            params.transientSteady = config.transientStopAtSteadyState;
            if (config.transientSteadyStateTol > 0.0) {
                params.steadyStateTol = QString::number(config.transientSteadyStateTol, 'g', 12);
            }
            if (config.transientSteadyStateDelay > 0.0) {
                params.steadyStateDelay = QString::number(config.transientSteadyStateDelay, 'g', 12);
            }
            break;
        case SimAnalysisType::AC:
            params.type = SpiceNetlistGenerator::AC;
            params.start = QString::number(config.fStart > 0.0 ? config.fStart : 10.0, 'g', 12);
            params.stop = QString::number(config.fStop > 0.0 ? config.fStop : 1e6, 'g', 12);
            params.step = QString::number(config.fPoints > 0 ? config.fPoints : 10);
            break;
        case SimAnalysisType::SParameter:
            params.type = SpiceNetlistGenerator::SParameter;
            params.start = QString::number(config.fStart > 0.0 ? config.fStart : 10.0, 'g', 12);
            params.stop = QString::number(config.fStop > 0.0 ? config.fStop : 1e6, 'g', 12);
            params.step = QString::number(config.fPoints > 0 ? config.fPoints : 10);
            params.rfPort1Source = QString::fromStdString(config.rfPort1Source);
            params.rfPort2Node = QString::fromStdString(config.rfPort2Node);
            params.rfZ0 = QString::number(config.rfZ0, 'g', 6);
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
        Q_EMIT logMessage("A simulation is already running.");
        return;
    }

    m_stopRequested = false;
    m_paused = false;
    Q_EMIT simulationStarted();
    m_lastConfig = config;

    if (netlist.isEmpty() || netlist.startsWith("* Missing scene")) {
        Q_EMIT logMessage("Ngspice: Data parse error or empty results.");
        Q_EMIT errorOccurred("Netlist generation failed or empty results.");
        Q_EMIT simulationFinished(SimResults());
        return;
    }

    Q_EMIT logMessage(QString("Starting ngspice simulation (Analysis: %1)...").arg(static_cast<int>(config.type)));

    QString stepError;
    m_pendingStepRuns = buildStepRuns(netlist, &stepError);
    m_stepSweepResults = SimResults();
    m_activeStepLabel.clear();
    m_completedStepRuns = 0;
    if (!stepError.isEmpty()) {
        Q_EMIT logMessage(QString("LTspice .step emulation error: %1").arg(stepError));
        Q_EMIT errorOccurred(stepError);
        Q_EMIT simulationFinished(SimResults());
        return;
    }
    if (!m_pendingStepRuns.isEmpty()) {
        Q_EMIT logMessage(QString("Running LTspice .step sweep emulation with %1 run(s).").arg(m_pendingStepRuns.size()));
        startNextStepSweepRun();
        return;
    }

    if (config.type == SimAnalysisType::Transient) {
        m_activeNetlistText = netlist;
        m_control = new SimControl();
        if (!startSharedSimulation(netlist, "Starting ngspice shared transient simulation...")) {
            return;
        }
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
        Q_EMIT simulationFinished(finalResults);
        return;
    }

    const PendingStepRun run = m_pendingStepRuns.takeFirst();
    m_activeStepLabel = run.label;
    Q_EMIT logMessage(QString("Running .step case %1").arg(run.label));
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
        const std::string suffixed = withStepSuffix(QString::fromStdString(name), stepLabel).toStdString();
        m_stepSweepResults.measurements[suffixed] = val;
        const auto metaIt = runResults.measurementMetadata.find(name);
        if (metaIt != runResults.measurementMetadata.end()) {
            m_stepSweepResults.measurementMetadata[suffixed] = metaIt->second;
        }
    }
    m_stepSweepResults.diagnostics.push_back(QString("Step run %1: %2").arg(runIndex).arg(stepLabel).toStdString());
    m_completedStepRuns = runIndex;
}

void SimManager::startNgspiceWithNetlist(const QString& netlistContent) {
    const QString otaCompatibilityError = detectUnsupportedOtaModelUsage(netlistContent);
    if (!otaCompatibilityError.isEmpty()) {
        Q_EMIT logMessage(otaCompatibilityError);
        Q_EMIT errorOccurred(otaCompatibilityError);
        Q_EMIT simulationFinished(SimResults());
        cleanupSimulation();
        return;
    }

    // Create a temporary file that auto-deletes when the object is destroyed.
    // However, we need it to persist until simulation load.
    // We'll manage it via a member or just use a transient one and pass path.
    auto* tempFile = new QTemporaryFile(this);
    tempFile->setAutoRemove(false);
    const QString activeNetlist = stripNetStatementsFromNetlist(stripMeasStatementsFromNetlist(netlistContent));
    if (tempFile->open()) {
        QTextStream out(tempFile);
        out << activeNetlist;
        tempFile->close();
    } else {
        Q_EMIT errorOccurred("Failed to create temporary netlist file.");
        delete tempFile;
        return;
    }

    m_control = new SimControl();
    m_paused = false;
    m_activeNetlistText = netlistContent;

    // Use a QObject parent for the file to ensure it's deleted eventually,
    // but we also use a QPointer to track its lifetime across multiple lambdas.
    QPointer<QTemporaryFile> safeTempFile = tempFile;
    const QFileInfo runInfo(tempFile->fileName());
    const QString rawPath = runInfo.absolutePath() + "/" + runInfo.completeBaseName() + ".raw";
    QFile::remove(rawPath);

    auto parseRawResults = [this, safeTempFile](const QString& path) {
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
            if (m_stopRequested) {
                if (safeTempFile) safeTempFile->deleteLater();
                cleanupSimulation();
                return;
            }
            if (result.first) {
                if (!m_activeStepLabel.isEmpty() || !m_pendingStepRuns.isEmpty()) {
                    mergeStepSweepResults(result.second, m_activeStepLabel, m_completedStepRuns + 1);
                } else {
                    Q_EMIT simulationFinished(result.second);
                }
            } else {
                const QString err = "Ngspice: Data parse error or empty results.";
                Q_EMIT logMessage(err);
                Q_EMIT errorOccurred(err);
                // Still close run-state on editor side even when results are unavailable.
                Q_EMIT simulationFinished(SimResults());
            }
            
            const bool continueStepSweep = result.first && (!m_activeStepLabel.isEmpty() || !m_pendingStepRuns.isEmpty());
            if (safeTempFile) safeTempFile->deleteLater();
            cleanupSimulation();
            if (continueStepSweep) {
                QTimer::singleShot(0, this, &SimManager::startNextStepSweepRun);
            }
        });

        const QString netlistText = m_activeNetlistText;
        const SimAnalysisType analysisType = m_lastConfig.type;
        watcher->setFuture(QtConcurrent::run([path, netlistText, analysisType]() {
            RawData rd;
            QString err;
            if (RawDataParser::loadRawAscii(path.toStdString(), &rd)) {
                SimResults simResults = rd.toSimResults();
                evaluateMeasStatementsIntoResults(netlistText, analysisType, &simResults);
                evaluateNetStatementsIntoResults(netlistText, analysisType, &simResults);
                return std::make_pair(true, simResults);
            }
            qDebug() << "RawDataParser error:" << err;
            return std::make_pair(false, SimResults());
        }));
    };

    auto* proc = new QProcess(this);
    m_ngspiceProcess = proc;
    auto processLogTail = QSharedPointer<QStringList>::create();
    proc->setProgram("ngspice");
    proc->setArguments({"-b", "-r", rawPath, tempFile->fileName()});
    proc->setWorkingDirectory(runInfo.absolutePath());
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, processLogTail]() {
        const QString text = QString::fromLocal8Bit(proc->readAllStandardOutput());
        const QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            processLogTail->append(line);
            while (processLogTail->size() > 80) {
                processLogTail->removeFirst();
            }
            Q_EMIT logMessage(QString("[Ngspice] %1").arg(line));
        }
    });

    connect(proc, &QProcess::errorOccurred, this, [this, safeTempFile, proc, processLogTail](QProcess::ProcessError error) {
        if (proc != m_ngspiceProcess) return;
        const QString msg = enrichNgspiceFailureMessage(
            QString("Failed to start ngspice process (%1).").arg(static_cast<int>(error)),
            *processLogTail);
        Q_EMIT logMessage(msg);
        Q_EMIT errorOccurred(msg);
        if (safeTempFile) safeTempFile->deleteLater();
        cleanupSimulation();
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, safeTempFile, rawPath, proc, parseRawResults, processLogTail](int exitCode, QProcess::ExitStatus exitStatus) {
        if (proc != m_ngspiceProcess) return;
        if (m_stopRequested) {
            if (safeTempFile) safeTempFile->deleteLater();
            cleanupSimulation();
            return;
        }
        const QString trailingText = QString::fromLocal8Bit(proc->readAllStandardOutput());
        if (!trailingText.trimmed().isEmpty()) {
            const QStringList lines = trailingText.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                processLogTail->append(line);
                while (processLogTail->size() > 80) {
                    processLogTail->removeFirst();
                }
                Q_EMIT logMessage(QString("[Ngspice] %1").arg(line));
            }
        }
        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString msg = enrichNgspiceFailureMessage(
                QString("Ngspice process failed (exit code %1).").arg(exitCode),
                *processLogTail);
            Q_EMIT logMessage(msg);
            Q_EMIT errorOccurred(msg);
            if (safeTempFile) safeTempFile->deleteLater();
            cleanupSimulation();
            return;
        }

        if (!QFileInfo::exists(rawPath)) {
            const QString msg = "Ngspice finished without RAW results; closing simulation run.";
            Q_EMIT logMessage(msg);
            if (m_pendingStepRuns.isEmpty() && m_activeStepLabel.isEmpty()) {
                Q_EMIT simulationFinished(SimResults());
            }
            if (safeTempFile) safeTempFile->deleteLater();
            cleanupSimulation();
            return;
        }

        parseRawResults(rawPath);
    });

    proc->start();
}

void SimManager::cleanupSimulation() {
    if (m_ngspiceProcess) {
        QProcess* proc = m_ngspiceProcess;
        m_ngspiceProcess = nullptr;
        proc->disconnect(this);
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(1000);
        }
        proc->deleteLater();
    }
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
    if (!m_sharedNetlistPath.isEmpty()) {
        QFile::remove(m_sharedNetlistPath);
        m_sharedNetlistPath.clear();
    }
    
    SimulationManager::instance().clearFluxScriptTargets();
}

void SimManager::runRealTime(QGraphicsScene* scene, NetManager* netMgr, int intervalMs) {
    if (m_control || m_ngspiceProcess) {
        Q_EMIT logMessage("A simulation is already running.");
        return;
    }

    SimAnalysisConfig config;
    config.type = SimAnalysisType::RealTime;
    config.rtIntervalMs = std::max(10, intervalMs);
    config.rtTimeStep = std::max(1e-6, static_cast<double>(config.rtIntervalMs) / 1000.0);
    config.tStep = std::max(1e-6, config.rtTimeStep / 10.0);
    config.tStop = std::max(config.rtTimeStep * 200.0, 0.1);

    QString netlist = generateNetlist(scene, netMgr, config);
    if (netlist.isEmpty() || netlist.startsWith("* Missing scene")) {
        const QString msg = "Real-time netlist generation failed.";
        Q_EMIT logMessage(msg);
        Q_EMIT errorOccurred(msg);
        Q_EMIT simulationFinished(SimResults());
        return;
    }

    m_lastConfig = config;
    m_activeNetlistText = netlist;
    m_rtScene = scene;
    m_rtNetMgr = netMgr;
    m_rtPending = false;
    m_rtCurrentTime = 0.0;
    m_pendingStepRuns.clear();
    m_activeStepLabel.clear();
    m_completedStepRuns = 0;
    m_stepSweepResults = SimResults();
    m_stopRequested = false;
    m_paused = false;

    m_control = new SimControl();
    Q_EMIT simulationStarted();
    if (!startSharedSimulation(netlist, QString("Starting real-time transient stream (%1 ms update interval)...").arg(config.rtIntervalMs))) {
        return;
    }
}

void SimManager::stopRealTime() {
    const bool sharedRunActive = (m_control && !m_ngspiceProcess);
    m_stopRequested = true;
    m_paused = false;
    if (m_control) {
        m_control->stopRequested = true;
        m_control->pauseRequested = false;
    }
    SimulationManager::instance().stopSimulation();
    if (m_rtTimer) {
        m_rtTimer->stop();
        delete m_rtTimer;
        m_rtTimer = nullptr;
    }
    m_rtScene = nullptr;
    m_rtNetMgr = nullptr;
    if (!sharedRunActive) {
        cleanupSimulation();
    }
    Q_EMIT simulationPaused(false);
    Q_EMIT simulationStopped();
}

void SimManager::onInteractiveStateChanged() {
    // Interactive changes handling
}

void SimManager::onRealTimeTick() {
    // Real-time tick handling
}

bool SimManager::startSharedSimulation(const QString& netlistContent, const QString& startMessage) {
    auto* tempFile = new QTemporaryFile(this);
    tempFile->setAutoRemove(false);
    if (!tempFile->open()) {
        const QString msg = "Failed to create temporary shared ngspice netlist file.";
        Q_EMIT logMessage(msg);
        Q_EMIT errorOccurred(msg);
        tempFile->deleteLater();
        cleanupSimulation();
        Q_EMIT simulationFinished(SimResults());
        return false;
    }

    QTextStream out(tempFile);
    const QString activeNetlist = stripNetStatementsFromNetlist(stripMeasStatementsFromNetlist(netlistContent));
    out << activeNetlist;
    out.flush();
    tempFile->close();

    m_sharedNetlistPath = tempFile->fileName();
    tempFile->deleteLater();

    Q_EMIT logMessage(startMessage);
    SimulationManager::instance().runSimulation(m_sharedNetlistPath, m_control);
    return true;
}

void SimManager::parseRawResultsFile(const QString& path, const QString& netlistText, SimAnalysisType analysisType) {
    m_resultsPending = true;
    auto* watcher = new QFutureWatcher<std::pair<bool, SimResults>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, SimResults>>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_resultsPending = false;

        if (result.first) {
            Q_EMIT simulationFinished(result.second);
        } else {
            const QString err = "Ngspice: Data parse error or empty real-time results.";
            Q_EMIT logMessage(err);
            Q_EMIT errorOccurred(err);
            Q_EMIT simulationFinished(SimResults());
        }

        cleanupSimulation();
    });

    watcher->setFuture(QtConcurrent::run([path, netlistText, analysisType]() {
        RawData rd;
        QString err;
        if (RawDataParser::loadRawAscii(path.toStdString(), &rd)) {
            SimResults simResults = rd.toSimResults();
            evaluateMeasStatementsIntoResults(netlistText, analysisType, &simResults);
            evaluateNetStatementsIntoResults(netlistText, analysisType, &simResults);
            return std::make_pair(true, simResults);
        }
        qDebug() << "RawDataParser error:" << err;
        return std::make_pair(false, SimResults());
    }));
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
     Q_EMIT errorOccurred("Direct SimNetlist execution not supported with Ngspice backend yet. Use UI.");
}

bool SimManager::isRunning() const {
    return m_control != nullptr || (m_ngspiceProcess && m_ngspiceProcess->state() != QProcess::NotRunning) || m_resultsPending;
}

void SimManager::stopAll() {
    const bool sharedRunActive = (m_control && !m_ngspiceProcess);
    m_stopRequested = true;
    m_paused = false;
    if (m_control) {
        m_control->stopRequested = true;
        m_control->pauseRequested = false;
    }
    if (m_ngspiceProcess && m_ngspiceProcess->state() != QProcess::NotRunning) {
        m_ngspiceProcess->kill();
    } else {
        SimulationManager::instance().stopSimulation();
    }
    if (m_rtTimer) {
        m_rtTimer->stop();
    }
    m_rtScene = nullptr;
    m_rtNetMgr = nullptr;
    if (!sharedRunActive) {
        cleanupSimulation();
    }
    Q_EMIT simulationPaused(false);
    Q_EMIT simulationStopped();
    Q_EMIT logMessage("Simulation stopped.");
}

void SimManager::pauseSimulation(bool pause) {
    if (!isRunning()) {
        Q_EMIT logMessage("No active simulation to pause or resume.");
        return;
    }

    if (m_ngspiceProcess && m_ngspiceProcess->state() != QProcess::NotRunning) {
        Q_EMIT logMessage("Pause/resume is not available for batch ngspice runs.");
        return;
    }

    if (m_paused == pause) {
        return;
    }

    if (m_control) {
        m_control->pauseRequested = pause;
    }
    m_paused = pause;
    Q_EMIT simulationPaused(m_paused);
    Q_EMIT logMessage(m_paused ? "Simulation paused." : "Simulation resumed.");
    QtConcurrent::run([pause]() {
        SimulationManager::instance().sendInternalCommand(pause ? "bg_halt" : "bg_resume");
    });
}

#include "../../core/jit_context_manager.h"

void SimManager::compileFluxScripts(QGraphicsScene* scene) {
    if (!scene) return;
    
    m_fluxScriptTargets.clear();
    QStringList targetIds;
    int compiledCount = 0;
    
    for (QGraphicsItem* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->itemType() == SchematicItem::SmartSignalType) {
                if (auto* smart = dynamic_cast<SmartSignalItem*>(si)) {
                    QString ref = smart->reference().trimmed().toUpper();
                    if (ref.isEmpty()) continue;
                    
                    m_fluxScriptTargets[ref] = smart;
                    targetIds << ref;
                    
                    // Compile the script into the JIT
                    QMap<int, QString> errors;
                    if (Flux::JITContextManager::instance().compileAndLoad(ref, smart->fluxCode(), errors)) {
                        compiledCount++;
                    } else {
                        QString err = errors.value(0);
                        Q_EMIT logMessage(QString("[FluxScript] Compilation failed for %1: %2").arg(ref, err));
                    }
                }
            }
        }
    }
    
    if (compiledCount > 0) {
        Q_EMIT logMessage(QString("[FluxScript] Successfully JIT-compiled %1 smart blocks.").arg(compiledCount));
    }
    
    // Push targets to live simulation manager for the feedback loop
    SimulationManager::instance().setFluxScriptTargets(targetIds);
}
