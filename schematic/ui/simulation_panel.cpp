#include "simulation_panel.h"
#include "../items/voltage_source_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../items/schematic_page_item.h"
#include "../../simulator/core/sim_value_parser.h"
#include "simulator/core/raw_data_parser.h"
#include "waveform_viewer.h"
#include "simulation_log_dialog.h"
#include "../../simulator/bridge/sim_audio_engine.h"
#include "../../core/theme_manager.h"
#include "../../core/simulation_manager.h"
#include "../io/netlist_generator.h"
#include "../items/schematic_item.h"
#include "../items/voltage_source_item.h"
#include "../analysis/spice_netlist_generator.h"
#include "../io/schematic_file_io.h"
#include "../../simulator/bridge/sim_schematic_bridge.h"
#include "../analysis/net_manager.h"
#include "../editor/schematic_editor.h"
#include "../dialogs/spice_step_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDebug>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QLogValueAxis>
#include <QCategoryAxis>
#include <QListWidget>
#include <QSplitter>
#include <QGraphicsView>
#include <QCheckBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QToolBar>
#include <QTabWidget>
#include <QScrollArea>
#include <QFileDialog>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QShortcut>

namespace {
struct SimBuildResult {
    bool ok = false;
    QString error;
    SimNetlist netlist;
    QStringList diagnostics;
    QString netlistText;
};
}
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTableWidget>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include "virtual_instruments.h"
#include "../../core/si_formatter.h"
#include "../../simulator/core/sim_math.h"
#include "../../simulator/core/sim_value_parser.h"
#include "../../simulator/bridge/sim_schematic_bridge.h"

namespace {
QString axisLabelFromSchema(const std::string& axisName) {
    if (axisName == "time_s") return "Time (s)";
    if (axisName == "frequency_hz") return "Frequency (Hz)";
    if (axisName == "run") return "Run Number";
    if (axisName == "component") return "Component";
    if (axisName == "sweep_point") return "Sweep Point";
    if (axisName == "magnitude") return "Magnitude";
    if (axisName == "sensitivity") return "Sensitivity";
    if (axisName == "index") return "Index";
    if (axisName == "value") return "Value";
    return QString::fromStdString(axisName);
}

QVector<QPointF> makePoints(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = std::min(x.size(), y.size());
    QVector<QPointF> points;
    points.reserve(static_cast<int>(n));
    for (size_t i = 0; i < n; ++i) {
        points.append(QPointF(x[i], y[i]));
    }
    return points;
}

QVector<QPointF> decimateMinMaxBuckets(const std::vector<double>& x, const std::vector<double>& y, int maxPoints) {
    const size_t n = std::min(x.size(), y.size());
    if (n == 0) return {};
    if (maxPoints < 8) maxPoints = 8;
    if (static_cast<int>(n) <= maxPoints) return makePoints(x, y);

    QVector<QPointF> out;
    out.reserve(maxPoints + 2);
    out.append(QPointF(x.front(), y.front()));

    const size_t interiorStart = 1;
    const size_t interiorEnd = n - 1;
    const size_t interiorCount = interiorEnd - interiorStart;
    const int buckets = std::max(1, maxPoints / 2 - 1);
    const double bucketSpan = static_cast<double>(interiorCount) / static_cast<double>(buckets);

    for (int b = 0; b < buckets; ++b) {
        const size_t begin = interiorStart + static_cast<size_t>(std::floor(b * bucketSpan));
        size_t end = interiorStart + static_cast<size_t>(std::floor((b + 1) * bucketSpan));
        if (b == buckets - 1 || end > interiorEnd) end = interiorEnd;
        if (begin >= end) continue;

        size_t minIdx = begin;
        size_t maxIdx = begin;
        for (size_t i = begin + 1; i < end; ++i) {
            if (y[i] < y[minIdx]) minIdx = i;
            if (y[i] > y[maxIdx]) maxIdx = i;
        }

        if (minIdx == maxIdx) {
            out.append(QPointF(x[minIdx], y[minIdx]));
        } else if (minIdx < maxIdx) {
            out.append(QPointF(x[minIdx], y[minIdx]));
            out.append(QPointF(x[maxIdx], y[maxIdx]));
        } else {
            out.append(QPointF(x[maxIdx], y[maxIdx]));
            out.append(QPointF(x[minIdx], y[minIdx]));
        }
    }

    out.append(QPointF(x.back(), y.back()));
    return out;
}

int sampleStride(size_t pointCount, int targetPoints) {
    if (targetPoints <= 0 || pointCount <= static_cast<size_t>(targetPoints)) return 1;
    return static_cast<int>(std::ceil(static_cast<double>(pointCount) / static_cast<double>(targetPoints)));
}

std::string measAnalysisToken(SimAnalysisType type) {
    switch (type) {
    case SimAnalysisType::Transient: return "tran";
    case SimAnalysisType::AC: return "ac";
    case SimAnalysisType::DC: return "dc";
    case SimAnalysisType::OP: return "op";
    case SimAnalysisType::Noise: return "noise";
    default: return "";
    }
}

QString inferredMeasurementUnit(const QString& name) {
    const QString lower = name.trimmed().toLower();
    if (lower.contains("db")) return "dB";
    if (lower.contains("freq") || lower == "bw" || lower.endsWith("_bw") || lower.contains("bandwidth")) return "Hz";
    if (lower.contains("time") || lower.contains("delay") || lower.contains("period") ||
        lower.contains("rise") || lower.contains("fall") || lower.contains("when") ||
        lower.contains("cross") || lower.contains("trig") || lower.contains("targ") ||
        lower.contains("width") || lower.contains("span")) {
        return "s";
    }
    return QString();
}

struct MeasurementDisplayEntry {
    QString fullName;
    QString baseName;
    QString stepLabel;
    double value = 0.0;
};

MeasurementDisplayEntry makeMeasurementDisplayEntry(const std::string& name, double value) {
    MeasurementDisplayEntry entry;
    entry.fullName = QString::fromStdString(name);
    entry.baseName = entry.fullName;
    entry.value = value;

    const int bracketPos = entry.fullName.lastIndexOf(" [");
    if (bracketPos > 0 && entry.fullName.endsWith(']')) {
        entry.baseName = entry.fullName.left(bracketPos).trimmed();
        entry.stepLabel = entry.fullName.mid(bracketPos + 2, entry.fullName.length() - bracketPos - 3).trimmed();
    }
    return entry;
}

QList<MeasurementDisplayEntry> buildMeasurementDisplayEntries(const std::map<std::string, double>& measurements) {
    QList<MeasurementDisplayEntry> entries;
    for (const auto& [name, value] : measurements) entries.append(makeMeasurementDisplayEntry(name, value));
    return entries;
}

QMap<QString, QList<MeasurementDisplayEntry>> groupSteppedMeasurementEntries(const std::map<std::string, double>& measurements) {
    QMap<QString, QList<MeasurementDisplayEntry>> grouped;
    for (const MeasurementDisplayEntry& entry : buildMeasurementDisplayEntries(measurements)) {
        if (entry.stepLabel.isEmpty()) continue;
        grouped[entry.baseName].append(entry);
    }
    return grouped;
}

bool hasPlottableSteppedMeasurements(const std::map<std::string, double>& measurements) {
    const auto grouped = groupSteppedMeasurementEntries(measurements);
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
        if (it.value().size() >= 2) return true;
    }
    return false;
}

bool parseSweepCoordinate(const QString& stepLabel, double& outX, QString& outAxisLabel) {
    const QString trimmed = stepLabel.trimmed();
    if (trimmed.isEmpty()) return false;

    const QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
    const QString firstPart = parts.isEmpty() ? trimmed : parts.first().trimmed();
    const int eqPos = firstPart.indexOf('=');
    const QString key = (eqPos > 0) ? firstPart.left(eqPos).trimmed() : QString();
    const QString valueText = (eqPos >= 0) ? firstPart.mid(eqPos + 1).trimmed() : firstPart;

    double parsed = 0.0;
    if (!SimValueParser::parseSpiceNumber(valueText, parsed)) return false;
    outX = parsed;
    outAxisLabel = key.isEmpty() ? QString("Sweep Value") : key;
    return true;
}

QList<QPair<QString, double>> parseSweepAssignments(const QString& stepLabel) {
    QList<QPair<QString, double>> assignments;
    const QStringList parts = stepLabel.split(',', Qt::SkipEmptyParts);
    for (const QString& rawPart : parts) {
        const QString part = rawPart.trimmed();
        if (part.isEmpty()) continue;
        const int eqPos = part.indexOf('=');
        const QString key = (eqPos > 0) ? part.left(eqPos).trimmed() : QString();
        const QString valueText = (eqPos >= 0) ? part.mid(eqPos + 1).trimmed() : part;
        double parsed = 0.0;
        if (!SimValueParser::parseSpiceNumber(valueText, parsed)) continue;
        assignments.append({key.isEmpty() ? QString("Sweep Value") : key, parsed});
    }
    return assignments;
}

struct SweepAxisSelection {
    bool valid = false;
    QString axisLabel = "Sweep Point";
    QMap<QString, double> valuesByStepLabel;
};

SweepAxisSelection chooseSweepAxis(const QList<MeasurementDisplayEntry>& entries) {
    struct CandidateData {
        QMap<QString, double> valuesByStepLabel;
        QSet<QString> distinctStepLabels;
        QSet<QString> distinctValues;
        bool conflict = false;
    };

    QMap<QString, CandidateData> candidates;
    for (const MeasurementDisplayEntry& entry : entries) {
        if (entry.stepLabel.isEmpty()) continue;
        for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
            CandidateData& data = candidates[assignment.first];
            data.distinctStepLabels.insert(entry.stepLabel);
            data.distinctValues.insert(QString::number(assignment.second, 'g', 12));
            if (data.valuesByStepLabel.contains(entry.stepLabel) &&
                !qFuzzyCompare(data.valuesByStepLabel.value(entry.stepLabel) + 1.0, assignment.second + 1.0)) {
                data.conflict = true;
            } else {
                data.valuesByStepLabel[entry.stepLabel] = assignment.second;
            }
        }
    }

    QString bestKey;
    int bestCoverage = -1;
    int bestDistinct = -1;
    for (auto it = candidates.cbegin(); it != candidates.cend(); ++it) {
        if (it.value().conflict) continue;
        const int coverage = it.value().distinctStepLabels.size();
        const int distinct = it.value().distinctValues.size();
        if (coverage < 2 || distinct < 2) continue;
        if (coverage > bestCoverage || (coverage == bestCoverage && distinct > bestDistinct)) {
            bestKey = it.key();
            bestCoverage = coverage;
            bestDistinct = distinct;
        }
    }

    SweepAxisSelection selection;
    if (bestKey.isEmpty()) return selection;
    selection.valid = true;
    selection.axisLabel = bestKey;
    selection.valuesByStepLabel = candidates.value(bestKey).valuesByStepLabel;
    return selection;
}

QStringList availableSweepAxes(const QList<MeasurementDisplayEntry>& entries) {
    QStringList axes;
    for (const MeasurementDisplayEntry& entry : entries) {
        for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
            if (!axes.contains(assignment.first)) axes.append(assignment.first);
        }
    }
    return axes;
}

QMap<QString, double> sweepAxisValues(const QList<MeasurementDisplayEntry>& entries, const QString& axisName) {
    QMap<QString, double> values;
    for (const MeasurementDisplayEntry& entry : entries) {
        for (const auto& assignment : parseSweepAssignments(entry.stepLabel)) {
            if (assignment.first.compare(axisName, Qt::CaseInsensitive) == 0) {
                values[entry.stepLabel] = assignment.second;
                break;
            }
        }
    }
    return values;
}

QString formatMeasuredNumber(const QString& name, double value) {
    const QString unit = inferredMeasurementUnit(name);
    if (!unit.isEmpty()) return SiFormatter::format(value, unit);
    return QString::number(value, 'g', 12);
}

QString formatMeasuredNumber(const SimResults& results, const QString& fullName, const QString& baseName, double value) {
    const auto it = results.measurementMetadata.find(fullName.toStdString());
    if (it != results.measurementMetadata.end() && !it->second.displayUnit.empty()) {
        return SiFormatter::format(value, QString::fromStdString(it->second.displayUnit));
    }
    return formatMeasuredNumber(baseName, value);
}

QString measurementYAxisTitle(const SimResults& results, const QString& fullName) {
    const auto it = results.measurementMetadata.find(fullName.toStdString());
    if (it == results.measurementMetadata.end()) return "Measurement Value";
    const QString label = QString::fromStdString(it->second.quantityLabel);
    const QString unit = QString::fromStdString(it->second.displayUnit);
    if (label.isEmpty() && unit.isEmpty()) return "Measurement Value";
    if (unit.isEmpty()) return label;
    if (label.isEmpty()) return QString("Measurement Value (%1)").arg(unit);
    return QString("%1 (%2)").arg(label, unit);
}

void appendMeasurementLogBlock(QTextEdit* logOutput, const SimResults& results) {
    const auto& measurements = results.measurements;
    if (!logOutput || measurements.empty()) return;
    logOutput->append("\n--- Measurements (.meas/.mean) ---");
    for (const MeasurementDisplayEntry& entry : buildMeasurementDisplayEntries(measurements)) {
        if (entry.stepLabel.isEmpty()) {
            logOutput->append(QString("%1 = %2")
                .arg(entry.baseName, formatMeasuredNumber(results, entry.fullName, entry.baseName, entry.value)));
        } else {
            logOutput->append(QString("%1 [%2] = %3")
                .arg(entry.baseName, entry.stepLabel, formatMeasuredNumber(results, entry.fullName, entry.baseName, entry.value)));
        }
    }
}
}

SimulationPanel::SimulationPanel(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir, QWidget* parent)
    : QWidget(parent), m_scene(scene), m_netManager(netManager), m_projectDir(projectDir), m_acceptRealTimeStream(false) {
    setupUI();
    
    auto& builtin = SimManager::instance();
    connect(&builtin, &SimManager::logMessage, this, &SimulationPanel::onLogReceived);
    connect(&builtin, &SimManager::simulationFinished, this, &SimulationPanel::onSimResultsReady);
    connect(&builtin, &SimManager::realTimeDataBatchReceived, this, &SimulationPanel::onRealTimeDataBatchReceived);
    connect(&builtin, &SimManager::errorOccurred, this, &SimulationPanel::onLogReceived);

    m_logFlushTimer = new QTimer(this);
    m_logFlushTimer->setInterval(100);
    connect(m_logFlushTimer, &QTimer::timeout, this, [this]() {
        if (m_logBuffer.isEmpty()) {
            m_logFlushTimer->stop();
            return;
        }
        const QStringList batch = m_logBuffer;
        m_logBuffer.clear();
        if (m_logOutput) {
            m_logOutput->append(batch.join("\n"));
        }
        for (const QString& msg : batch) {
            appendIssueItem(msg);
        }
    });
}

SimulationPanel::~SimulationPanel() {
    SimManager::instance().stopRealTime();
    // Explicitly disconnect to avoid signals hitting a partially destroyed objective or another instance
    SimManager::instance().disconnect(this);
}

namespace {
bool signalMatches(const QString& itemText, const QString& signalName) {
    if (itemText.compare(signalName, Qt::CaseInsensitive) == 0) {
        return true;
    }

    // Check V(net) <-> net equivalence
    if (signalName.startsWith("V(", Qt::CaseInsensitive) && signalName.endsWith(")")) {
        const QString bare = signalName.mid(2, signalName.length() - 3);
        if (itemText.compare(bare, Qt::CaseInsensitive) == 0) {
            return true;
        }
    } else {
        const QString vName = QString("V(%1)").arg(signalName);
        if (itemText.compare(vName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }

    return false;
}
} // namespace

QStringList SimulationPanel::connectedNetsForItem(SchematicItem* item, bool updateNets) const {
    QStringList nets;
    if (!item || !m_netManager) return nets;
    if (updateNets) m_netManager->updateNets(m_scene);

    QSet<QString> seen;
    const qreal pinTolerance = 2.0;

    const QList<QPointF> pins = item->connectionPoints();
    for (int i = 0; i < pins.size(); ++i) {
        const QPointF pinScene = item->mapToScene(pins[i]);
        QString net = m_netManager->findNetAtPoint(pinScene).trimmed();

        // Fallback: use pinNet if findNetAtPoint returned nothing
        if (net.isEmpty()) {
            net = item->pinNet(i).trimmed();
        }
        if (net.isEmpty()) continue;

        // Verify this pin belongs to the item
        const QList<NetConnection> conns = m_netManager->getConnections(net);
        bool pinBelongsToItem = false;
        for (const auto& conn : conns) {
            if (conn.item != item) continue;
            if (QLineF(conn.connectionPoint, pinScene).length() <= pinTolerance) {
                pinBelongsToItem = true;
                break;
            }
        }
        // Fallback: trust findNetAtPoint/pinNet even if connection verification fails
        if (!pinBelongsToItem && !net.isEmpty()) {
            pinBelongsToItem = true;
        }

        if (!pinBelongsToItem) continue;

        const QString canonicalNet = net.toUpper();
        if (seen.contains(canonicalNet)) continue;
        seen.insert(canonicalNet);
        nets.append(net);
    }
    return nets;
}

bool SimulationPanel::buildDerivedPowerWaveform(const QString& signalName, QVector<double>& time, QVector<double>& values) const {
    if (!signalName.startsWith("P(", Qt::CaseInsensitive) || !signalName.endsWith(")")) return false;
    if (!m_scene) { qWarning() << "buildDerivedPowerWaveform: no scene"; return false; }

    const QString ref = signalName.mid(2, signalName.length() - 3).trimmed();
    if (ref.isEmpty()) return false;

    SchematicItem* targetItem = nullptr;
    for (QGraphicsItem* gi : m_scene->items()) {
        auto* item = dynamic_cast<SchematicItem*>(gi);
        if (!item) continue;
        if (item->reference().compare(ref, Qt::CaseInsensitive) == 0) {
            targetItem = item;
            break;
        }
    }
    if (!targetItem) { qWarning() << "buildDerivedPowerWaveform: no item with ref" << ref; return false; }

    const QStringList nets = connectedNetsForItem(targetItem);
    if (nets.size() < 2) { qWarning() << "buildDerivedPowerWaveform:" << ref << "has" << nets.size() << "nets, need >= 2"; return false; }

    const SimWaveform* currentWave = nullptr;
    const SimWaveform* posWave = nullptr;
    const SimWaveform* negWave = nullptr;
    const QString currentName = QString("I(%1)").arg(ref);
    const QString posName = QString("V(%1)").arg(nets.value(0));
    const QString negName = QString("V(%1)").arg(nets.value(1));

    for (const auto& w : m_lastResults.waveforms) {
        const QString wName = QString::fromStdString(w.name);
        if (!currentWave && wName.compare(currentName, Qt::CaseInsensitive) == 0) currentWave = &w;
        if (!posWave && (wName.compare(posName, Qt::CaseInsensitive) == 0 || wName.compare(nets.value(0), Qt::CaseInsensitive) == 0)) posWave = &w;
        if (!negWave && (wName.compare(negName, Qt::CaseInsensitive) == 0 || wName.compare(nets.value(1), Qt::CaseInsensitive) == 0)) negWave = &w;
    }
    if (!currentWave || !posWave || !negWave) {
        qWarning() << "buildDerivedPowerWaveform:" << ref
                   << "current=" << (currentWave ? "OK" : "MISSING")
                   << "pos=" << (posWave ? "OK" : "MISSING") << "(" << posName << ")"
                   << "neg=" << (negWave ? "OK" : "MISSING") << "(" << negName << ")";
        return false;
    }

    const size_t count = std::min({currentWave->xData.size(), currentWave->yData.size(), posWave->yData.size(), negWave->yData.size()});
    if (count == 0) return false;

    time.reserve(static_cast<int>(count));
    values.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        time.append(currentWave->xData[i]);
        values.append((posWave->yData[i] - negWave->yData[i]) * currentWave->yData[i]);
    }
    return true;
}

void SimulationPanel::appendDerivedPowerWaveforms(SimResults& results) const {
    if (!m_scene || !m_netManager) return;
    m_netManager->updateNets(m_scene);

    auto findWave = [&](const QString& nameA, const QString& nameB = QString()) -> const SimWaveform* {
        for (const auto& w : results.waveforms) {
            const QString wName = QString::fromStdString(w.name);
            if (wName.compare(nameA, Qt::CaseInsensitive) == 0) return &w;
            if (!nameB.isEmpty() && wName.compare(nameB, Qt::CaseInsensitive) == 0) return &w;
        }
        return nullptr;
    };

    QSet<QString> existing;
    for (const auto& w : results.waveforms) {
        existing.insert(QString::fromStdString(w.name).toUpper());
    }

    std::vector<SimWaveform> powerWaves;
    for (QGraphicsItem* gi : m_scene->items()) {
        auto* item = dynamic_cast<SchematicItem*>(gi);
        if (!item) continue;

        const QString ref = item->reference().trimmed();
        if (ref.isEmpty()) continue;

        const QString powerName = QString("P(%1)").arg(ref);
        if (existing.contains(powerName.toUpper())) continue;

        const QStringList nets = connectedNetsForItem(item, false);
        if (nets.size() < 2) continue;

        const SimWaveform* currentWave = findWave(QString("I(%1)").arg(ref));
        const SimWaveform* posWave = findWave(QString("V(%1)").arg(nets[0]), nets[0]);
        const SimWaveform* negWave = findWave(QString("V(%1)").arg(nets[1]), nets[1]);
        if (!currentWave || !posWave || !negWave) continue;

        const size_t count = std::min({currentWave->xData.size(), currentWave->yData.size(), posWave->yData.size(), negWave->yData.size()});
        if (count == 0) continue;

        SimWaveform pWave;
        pWave.name = powerName.toStdString();
        pWave.xData.reserve(count);
        pWave.yData.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            pWave.xData.push_back(currentWave->xData[i]);
            pWave.yData.push_back((posWave->yData[i] - negWave->yData[i]) * currentWave->yData[i]);
        }
        powerWaves.push_back(std::move(pWave));
    }
    
    if (!powerWaves.empty()) {
        results.waveforms.insert(results.waveforms.end(), 
                                 std::make_move_iterator(powerWaves.begin()), 
                                 std::make_move_iterator(powerWaves.end()));
    }
}

void SimulationPanel::addProbe(const QString& signalName) {
    if (signalName.isEmpty()) return;
    
    // Check if it already exists (case-insensitive)
    QListWidgetItem* targetItem = nullptr;
    for (int i = 0; i < m_signalList->count(); ++i) {
        if (m_signalList->item(i)->text().compare(signalName, Qt::CaseInsensitive) == 0) {
            targetItem = m_signalList->item(i);
            break;
        }
    }
    
    if (targetItem) {
        targetItem->setCheckState(Qt::Checked);
    } else {
        // Not found exactly, try finding without V() if signalName has it, or vice versa
        QString alternative = signalName;
        if (signalName.startsWith("V(", Qt::CaseInsensitive) && signalName.endsWith(")")) {
            alternative = signalName.mid(2, signalName.length() - 3);
        } else {
            alternative = QString("V(%1)").arg(signalName);
        }
        
        for (int i = 0; i < m_signalList->count(); ++i) {
            if (m_signalList->item(i)->text().compare(alternative, Qt::CaseInsensitive) == 0) {
                targetItem = m_signalList->item(i);
                break;
            }
        }
        
        if (targetItem) {
            targetItem->setCheckState(Qt::Checked);
        } else {
            // Truly new signal, add it
            targetItem = new QListWidgetItem(signalName);
            targetItem->setFlags(targetItem->flags() | Qt::ItemIsUserCheckable);
            targetItem->setCheckState(Qt::Checked);
            m_signalList->addItem(targetItem);
        }
    }
    
    const QString matchedName = (targetItem) ? targetItem->text() : signalName;
    
    // Sync with WaveformViewer
    if (m_waveformViewer) {
        auto findWaveform = [&](const QString& name) -> const SimWaveform* {
            if (name.isEmpty()) return nullptr;
            QString bare = name;
            QString vName;
            if (name.startsWith("V(", Qt::CaseInsensitive) && name.endsWith(")")) {
                bare = name.mid(2, name.length() - 3);
                vName = name;
            } else {
                vName = QString("V(%1)").arg(name);
            }
            for (const auto& w : m_lastResults.waveforms) {
                const QString wName = QString::fromStdString(w.name);
                if (wName.compare(name, Qt::CaseInsensitive) == 0 ||
                    wName.compare(bare, Qt::CaseInsensitive) == 0 ||
                    wName.compare(vName, Qt::CaseInsensitive) == 0) {
                    return &w;
                }
            }
            return nullptr;
        };

        auto addWaveform = [&](const SimWaveform* w) {
            QVector<double> time;
            QVector<double> values;
            time.reserve(static_cast<int>(w->xData.size()));
            values.reserve(static_cast<int>(w->yData.size()));
            for (size_t i = 0; i < w->xData.size(); ++i) time.append(w->xData[i]);
            for (size_t i = 0; i < w->yData.size(); ++i) values.append(w->yData[i]);
            if (m_analysisType->currentIndex() == 2 && !w->yPhase.empty()) {
                QVector<double> phase;
                phase.reserve(static_cast<int>(w->yPhase.size()));
                for (size_t i = 0; i < w->yPhase.size(); ++i) phase.append(w->yPhase[i]);
                m_waveformViewer->addSignal(matchedName, time, values, phase);
            } else {
                qDebug() << "SimulationPanel: addProbe - Found waveform for" << matchedName << "Points:" << values.size();
                m_waveformViewer->addSignal(matchedName, time, values);
            }
        };

        bool found = false;
        if (const SimWaveform* w = findWaveform(signalName)) {
            addWaveform(w);
            found = true;
        } else if (matchedName != signalName) {
            if (const SimWaveform* w = findWaveform(matchedName)) {
                addWaveform(w);
                found = true;
            }
        }

        // If not found, check if it's a differential probe V(A,B) and try to calculate it
        if (!found && signalName.startsWith("V(", Qt::CaseInsensitive) && signalName.contains(",") && signalName.endsWith(")")) {
            QString core = signalName.mid(2, signalName.length() - 3);
            QStringList parts = core.split(",", Qt::SkipEmptyParts);
            if (parts.size() == 2) {
                QString pNet = parts[0].trimmed();
                QString nNet = parts[1].trimmed();
                
                auto findW = [&](const QString& net) -> const SimWaveform* {
                    QString vNet = QString("V(%1)").arg(net);
                    for (const auto& w : m_lastResults.waveforms) {
                        QString name = QString::fromStdString(w.name);
                        if (name.compare(net, Qt::CaseInsensitive) == 0 || 
                            name.compare(vNet, Qt::CaseInsensitive) == 0) {
                            return &w;
                        }
                    }
                    return nullptr;
                };
                
                const SimWaveform* pWave = findW(pNet);
                const SimWaveform* nWave = findW(nNet);
                
                if (pWave && nWave) {
                    // Calculate V(p) - V(n)
                    // Note: assume same time axis for simplistic implementation
                    size_t count = std::min(pWave->yData.size(), nWave->yData.size());
                    if (count > 0) {
                        QVector<double> time;
                        QVector<double> values;
                        time.reserve(count);
                        values.reserve(count);
                        
                        for (size_t i = 0; i < count; ++i) {
                            time.append(pWave->xData[i]);
                            values.append(pWave->yData[i] - nWave->yData[i]);
                        }
                        
                        m_waveformViewer->addSignal(matchedName, time, values);
                        found = true;
                    }
                }
            }
        }

        if (!found && signalName.startsWith("P(", Qt::CaseInsensitive)) {
            QVector<double> time;
            QVector<double> values;
            if (buildDerivedPowerWaveform(signalName, time, values) ||
                (matchedName != signalName && buildDerivedPowerWaveform(matchedName, time, values))) {
                bool existsInResults = false;
                for (auto& wave : m_lastResults.waveforms) {
                    if (QString::fromStdString(wave.name).compare(matchedName, Qt::CaseInsensitive) == 0) {
                        wave.xData.assign(time.begin(), time.end());
                        wave.yData.assign(values.begin(), values.end());
                        existsInResults = true;
                        break;
                    }
                }
                if (!existsInResults) {
                    SimWaveform wave;
                    wave.name = matchedName.toStdString();
                    wave.xData.assign(time.begin(), time.end());
                    wave.yData.assign(values.begin(), values.end());
                    m_lastResults.waveforms.push_back(std::move(wave));
                }

                m_waveformViewer->addSignal(matchedName, time, values);

                bool chartHasSeries = false;
                if (m_chart) {
                    for (auto* series : m_chart->series()) {
                        if (series && series->name().compare(matchedName, Qt::CaseInsensitive) == 0) {
                            chartHasSeries = true;
                            break;
                        }
                    }
                    if (!chartHasSeries) {
                        auto axesX = m_chart->axes(Qt::Horizontal);
                        auto axesY = m_chart->axes(Qt::Vertical);
                        if (!axesX.isEmpty() && !axesY.isEmpty()) {
                            auto* series = new QLineSeries();
                            series->setName(matchedName);
                            series->setPen(QPen(Qt::red, 1.5));
                            QList<QPointF> points;
                            points.reserve(time.size());
                            for (int i = 0; i < time.size() && i < values.size(); ++i) {
                                points.append(QPointF(time[i], values[i]));
                            }
                            series->append(points);
                            m_chart->addSeries(series);
                            series->attachAxis(axesX[0]);
                            series->attachAxis(axesY[0]);
                        }
                    }
                }
                found = true;
            }
        }

        m_waveformViewer->setSignalChecked(matchedName, true);
        m_waveformViewer->updatePlot(true);
    }
    
    // If a transient simulation is running, add to real-time series
    if (m_analysisType->currentIndex() == 0 && m_chart && !m_realTimeSeries.contains(signalName)) {
        auto axesX = m_chart->axes(Qt::Horizontal);
        auto axesY = m_chart->axes(Qt::Vertical);
        if (!axesX.isEmpty() && !axesY.isEmpty()) {
            const QList<QColor> colors = {Qt::red, Qt::blue, QColor("#00aa00"), Qt::magenta, Qt::darkCyan};
            QLineSeries* series = new QLineSeries();
            series->setName(signalName);
            series->setPen(QPen(colors[m_realTimeSeries.size() % colors.size()], 1.5));
            m_chart->addSeries(series);
            series->attachAxis(axesX[0]);
            series->attachAxis(axesY[0]);
            m_realTimeSeries[signalName] = series;
        }
    }
    
    m_logOutput->append(QString("Probed signal: %1").arg(signalName));
}

bool SimulationPanel::hasProbe(const QString& signalName) const {
    if (signalName.isEmpty() || !m_signalList) return false;
    for (int i = 0; i < m_signalList->count(); ++i) {
        QListWidgetItem* item = m_signalList->item(i);
        if (!item) continue;
        if (signalMatches(item->text(), signalName)) {
            return true;
        }
    }
    return false;
}

void SimulationPanel::addDifferentialProbe(const QString& pNet, const QString& nNet) {
    if (pNet.isEmpty() || nNet.isEmpty()) return;
    const QString signalName = QString("V(%1,%2)").arg(pNet, nNet);
    addProbe(signalName);
}

QString SimulationPanel::generateMeasLine(int row) const {
    if (!m_measListTable || row < 0 || row >= m_measListTable->rowCount()) return QString();
    QTableWidgetItem* item = m_measListTable->item(row, 1);
    return item ? item->text() : QString();
}

void SimulationPanel::onMeasAdd() {
    if (!m_measName || !m_measFunction || !m_measAnalysisType) return;
    const QString atype = m_measAnalysisType->currentText();
    const QString name = m_measName->text().trimmed();
    const QString func = m_measFunction->currentText();
    if (name.isEmpty()) { m_logOutput->append("[.meas] Name is required."); return; }

    QString measLine;
    if (func == "TRIG/TARG") {
        const QString trigSig = m_measTrigSignal->text().trimmed();
        const QString trigVal = m_measTrigVal->text().trimmed();
        const QString trigEdge = m_measTrigEdge->currentText();
        const QString targSig = m_measTargSignal->text().trimmed();
        const QString targVal = m_measTargVal->text().trimmed();
        const QString targEdge = m_measTargEdge->currentText();
        if (trigSig.isEmpty() || targSig.isEmpty()) { m_logOutput->append("[.meas] TRIG/TARG signals required."); return; }
        measLine = QString(".meas %1 %2 TRIG %3 VAL=%4 %5 TARG %6 VAL=%7 %8").arg(atype, name, trigSig, trigVal, trigEdge, targSig, targVal, targEdge);
    } else {
        const QString signal = m_measSignal->text().trimmed();
        if (signal.isEmpty()) { m_logOutput->append("[.meas] Signal is required."); return; }
        measLine = QString(".meas %1 %2 %3 %4").arg(atype, name, func, signal);
    }
    int row = m_measListTable->rowCount();
    m_measListTable->insertRow(row);
    m_measListTable->setItem(row, 0, new QTableWidgetItem(name));
    m_measListTable->setItem(row, 1, new QTableWidgetItem(measLine));
    m_measListTable->item(row, 0)->setForeground(QColor("#00ccff"));
    MeasStatement meas;
    if (SimMeasEvaluator::parse(measLine.toStdString(), row + 1, "Editor", meas)) m_measStatements.push_back(meas);
    QRegularExpression numRe("(\\d+)$");
    auto match = numRe.match(name);
    if (match.hasMatch()) { int num = match.captured(1).toInt() + 1; m_measName->setText(name.left(match.capturedStart(1)) + QString::number(num)); }
    m_logOutput->append(QString("[.meas] Added: %1").arg(measLine));
}

void SimulationPanel::onMeasRemove() {
    if (!m_measListTable) return;
    int row = m_measListTable->currentRow();
    if (row < 0 || row >= m_measListTable->rowCount()) return;
    QString name = m_measListTable->item(row, 0) ? m_measListTable->item(row, 0)->text() : "";
    m_measListTable->removeRow(row);
    auto it = std::remove_if(m_measStatements.begin(), m_measStatements.end(),
        [&name](const MeasStatement& ms) { return QString::fromStdString(ms.name) == name; });
    m_measStatements.erase(it, m_measStatements.end());
    m_logOutput->append(QString("[.meas] Removed: %1").arg(name));
}

void SimulationPanel::rebuildMeasFromTable() {
    m_measStatements.clear();
    if (!m_measListTable) return;
    for (int row = 0; row < m_measListTable->rowCount(); ++row) {
        QString line = generateMeasLine(row);
        if (line.isEmpty()) continue;
        MeasStatement meas;
        if (SimMeasEvaluator::parse(line.toStdString(), row + 1, "Editor", meas)) m_measStatements.push_back(meas);
    }
}

void SimulationPanel::onMeasFunctionChanged(int index) {
    Q_UNUSED(index)
    if (!m_measFunction) return;
    m_measTrigTargWidget->setVisible(m_measFunction->currentText() == "TRIG/TARG");
}

void SimulationPanel::removeProbe(const QString& signalName) {
    if (signalName.isEmpty() || !m_signalList) return;

    for (int i = 0; i < m_signalList->count(); ++i) {
        QListWidgetItem* item = m_signalList->item(i);
        if (!item || item->text() != signalName) continue;
        delete m_signalList->takeItem(i);

        if (m_chart) {
            const auto allSeries = m_chart->series();
            for (auto* series : allSeries) {
                if (series && series->name() == signalName) {
                    m_chart->removeSeries(series);
                    delete series;
                    break;
                }
            }
        }
        if (m_spectrumChart) {
            const auto spectrumSeries = m_spectrumChart->series();
            for (auto* series : spectrumSeries) {
                if (series && series->name() == signalName) {
                    m_spectrumChart->removeSeries(series);
                    delete series;
                    break;
                }
            }
        }
        if (m_scopeChannelCombo) {
            const int idx = m_scopeChannelCombo->findText(signalName);
            if (idx >= 0) {
                m_scopeChannelCombo->removeItem(idx);
            }
        }

        if (m_waveformViewer) {
            m_waveformViewer->setSignalChecked(signalName, false);
        }

        m_logOutput->append(QString("Unprobed signal: %1").arg(signalName));
        return;
    }
}

void SimulationPanel::clearAllProbes() {
    if (!m_signalList) return;
    const int count = m_signalList->count();
    m_signalList->clear();
    if (m_chart) {
        m_chart->removeAllSeries();
    }
    if (m_spectrumChart) {
        m_spectrumChart->removeAllSeries();
    }
    if (m_scopeChannelCombo) {
        m_scopeChannelCombo->clear();
    }
    if (m_waveformViewer) {
        m_waveformViewer->clear();
    }
    m_persistentCheckedSignals.clear();
    if (m_logOutput) {
        m_logOutput->append(QString("Cleared %1 probe(s).").arg(count));
    }
}

void SimulationPanel::onClearFocusedPaneProbes() {
    if (!m_waveformViewer || !m_signalList) return;
    
    int focusedIndex = m_waveformViewer->focusedPaneIndex();
    if (focusedIndex < 0) focusedIndex = 0; 
    
    QStringList paneSignals = m_waveformViewer->getSignalsInPane(focusedIndex);
    
    m_signalList->blockSignals(true);
    for (const QString& sig : paneSignals) {
        for (int i = 0; i < m_signalList->count(); ++i) {
            auto* item = m_signalList->item(i);
            if (item->text() == sig) {
                delete m_signalList->takeItem(i);
                break;
            }
        }
        m_persistentCheckedSignals.remove(sig);
        m_realTimeSeries.remove(sig); // Avoid dangling pointers
        if (m_editor) {
            m_editor->removeProbeMarkerBySignalName(sig);
        }
    }
    m_signalList->blockSignals(false);
    
    m_waveformViewer->clearPane(focusedIndex);
    if (m_logOutput) {
        m_logOutput->append(QString("Cleared focused pane (%1 signals removed).").arg(paneSignals.size()));
    }
}

void SimulationPanel::clearAllProbesPreserveX() {
    if (m_waveformViewer) {
        double minX = 0.0, maxX = 0.0;
        if (m_waveformViewer->currentXRange(minX, maxX)) {
            m_waveformViewer->preserveXRangeOnce(minX, maxX);
        }
    }
    clearAllProbes();
}

void SimulationPanel::clearResults() {
    if (m_chart) {
        m_chart->removeAllSeries();
    }
    if (m_spectrumChart) {
        m_spectrumChart->removeAllSeries();
    }
    if (m_waveformViewer) {
        m_waveformViewer->clear();
    }
    m_lastResults = SimResults();
    m_previousResults = SimResults();
    m_hasLastResults = false;
    m_hasPreviousResults = false;
    m_realTimeSeries.clear();
    m_realTimePointCounter = 0;
    if (m_timelineSlider) m_timelineSlider->setValue(0);
    if (m_timelineLabel) m_timelineLabel->setText("t = 0");
}

void SimulationPanel::setTargetScene(QGraphicsScene* scene, NetManager* netManager, const QString& projectDir, bool clearState) {
    if (!scene || !netManager) return;
    if (m_scene == scene && m_netManager == netManager && m_projectDir == projectDir && !clearState) return;

    // Save state of the current tab before switching
    QGraphicsScene* oldScene = m_scene;
    if (clearState && oldScene && oldScene != scene) {
        TabOscilloscopeState saved = saveCurrentTabState();
        if (saved.hasLastResults || !saved.waveformSignals.isEmpty()) {
            m_tabStates[oldScene] = saved;
        }
    }

    m_scene = scene;
    m_netManager = netManager;
    m_projectDir = projectDir;

    if (clearState) {
        clearAllProbes();
        if (m_issueList) m_issueList->clear();
        if (m_chart) m_chart->removeAllSeries();
        if (m_spectrumChart) m_spectrumChart->removeAllSeries();
        m_lastResults = SimResults();
        m_previousResults = SimResults();
        m_hasLastResults = false;
        m_hasPreviousResults = false;
        m_realTimeSeries.clear();
        m_realTimePointCounter = 0;
        if (m_timelineSlider) m_timelineSlider->setValue(0);
        if (m_timelineLabel) m_timelineLabel->setText("t = 0");
        if (m_measurementsTable) m_measurementsTable->clearContents();

        // Restore state for the new tab if we have saved data
        auto it = m_tabStates.find(scene);
        if (it != m_tabStates.end()) {
            restoreTabState(it.value());
        }
    }
}

void SimulationPanel::removeTabState(QGraphicsScene* scene) {
    m_tabStates.remove(scene);
}

void SimulationPanel::updateSchematicDirective() {
    if (!m_scene || m_buildInProgress) return;

    SpiceNetlistGenerator::SimulationParams cmdParams;
    const int idx = m_analysisType ? m_analysisType->currentIndex() : 0;

    if (idx == 0) {
        cmdParams.type = SpiceNetlistGenerator::Transient;
        cmdParams.start = "0";
        // Use raw text to preserve suffixes like 'u', 'm' etc.
        cmdParams.stop = m_param2 ? m_param2->text().trimmed() : "10m";
        cmdParams.step = m_param1 ? m_param1->text().trimmed() : "1u";
        
        // Fallbacks if empty
        if (cmdParams.stop.isEmpty()) cmdParams.stop = "10m";
        if (cmdParams.step.isEmpty()) cmdParams.step = "1u";
    } else if (idx == 1) {
        cmdParams.type = SpiceNetlistGenerator::OP;
    } else if (idx == 2) {
        cmdParams.type = SpiceNetlistGenerator::AC;
        cmdParams.start = m_param1 ? (m_param1->text().trimmed().isEmpty() ? "10" : m_param1->text().trimmed()) : "10";
        cmdParams.stop = m_param2 ? (m_param2->text().trimmed().isEmpty() ? "1Meg" : m_param2->text().trimmed()) : "1Meg";
        cmdParams.step = m_param3 ? (m_param3->text().trimmed().isEmpty() ? "10" : m_param3->text().trimmed()) : "10";
    } else {
        cmdParams.type = SpiceNetlistGenerator::OP;
    }

    const QString cmdText = SpiceNetlistGenerator::buildCommand(cmdParams);

    // Find existing simulation command directive
    SchematicSpiceDirectiveItem* found = nullptr;
    QList<SchematicSpiceDirectiveItem*> toRemove;
    
    for (auto* gi : m_scene->items()) {
        if (auto* existing = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
            if (existing->text().startsWith('.') &&
                (existing->text().startsWith(".tran", Qt::CaseInsensitive) ||
                 existing->text().startsWith(".ac", Qt::CaseInsensitive) ||
                 existing->text().startsWith(".dc", Qt::CaseInsensitive) ||
                 existing->text().startsWith(".op", Qt::CaseInsensitive))) {
                if (!found) {
                    found = existing;
                } else {
                    toRemove.append(existing);
                }
            }
        }
    }

    if (found) {
        found->setText(cmdText);
        found->update();
        // Remove duplicates if any
        for (auto* r : toRemove) {
            m_scene->removeItem(r);
            delete r;
        }
    } else {
        // Prefer current view center so the directive is visible
        QPointF cmdPos(100, 200);
        if (!m_scene->views().isEmpty()) {
            if (auto* view = m_scene->views().first()) {
                cmdPos = view->mapToScene(view->viewport()->rect().center());
            }
        } else {
            // Fallback: place inside page margins
            for (auto* gi : m_scene->items()) {
                if (auto* page = dynamic_cast<SchematicPageItem*>(gi)) {
                    qreal w = page->boundingRect().width() - 8;
                    qreal h = page->boundingRect().height() - 8;
                    cmdPos = page->mapToScene(QPointF(-w/2 + 120, h/2 - 150));
                    break;
                }
            }
        }
        auto* cmdItem = new SchematicSpiceDirectiveItem(cmdText, cmdPos);
        m_scene->addItem(cmdItem);
    }
}

void SimulationPanel::updateCommandDisplay() {
    if (!m_commandLine) return;

    SpiceNetlistGenerator::SimulationParams cmdParams;
    const int idx = m_analysisType ? m_analysisType->currentIndex() : 0;

    if (idx == 0) {
        cmdParams.type = SpiceNetlistGenerator::Transient;
        cmdParams.start = "0";
        cmdParams.stop = m_param2 ? m_param2->text() : QString("10m");
        cmdParams.step = m_param1 ? m_param1->text() : QString("1u");
    } else if (idx == 1) {
        cmdParams.type = SpiceNetlistGenerator::OP;
    } else if (idx == 2) {
        cmdParams.type = SpiceNetlistGenerator::AC;
        cmdParams.start = m_param1 ? m_param1->text() : QString("10");
        cmdParams.stop = m_param2 ? m_param2->text() : QString("1Meg");
        cmdParams.step = m_param3 ? m_param3->text() : QString("10");
    } else if (idx == 3) {
        cmdParams.type = SpiceNetlistGenerator::SParameter;
        cmdParams.start = m_param1 ? m_param1->text() : QString("10");
        cmdParams.stop = m_param2 ? m_param2->text() : QString("1Meg");
        cmdParams.step = m_param3 ? m_param3->text() : QString("10");
        cmdParams.rfPort1Source = m_param4 ? m_param4->text() : QString("V1");
        cmdParams.rfPort2Node = m_param5 ? m_param5->text() : QString("OUT");
        cmdParams.rfZ0 = m_param6 ? m_param6->text().trimmed() : "50";
    } else {
        cmdParams.type = SpiceNetlistGenerator::OP;
    }

    m_commandLine->setText(SpiceNetlistGenerator::buildCommand(cmdParams));
}

void SimulationPanel::parseCommandText(const QString& command) {
    if (!m_analysisType || !m_param1 || !m_param2 || !m_param3) return;

    QString cmd = command.trimmed().toLower();
    if (cmd.startsWith(".tran")) {
        m_analysisType->blockSignals(true);
        m_analysisType->setCurrentIndex(0);
        m_analysisType->blockSignals(false);

        QStringList parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            m_param1->blockSignals(true);
            m_param2->blockSignals(true);
            m_param1->setText(parts[1]);
            m_param2->setText(parts[2]);
            m_param1->blockSignals(false);
            m_param2->blockSignals(false);
        }
    } else if (cmd.startsWith(".ac")) {
        m_analysisType->blockSignals(true);
        m_analysisType->setCurrentIndex(2);
        m_analysisType->blockSignals(false);

        QStringList parts = cmd.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 4) {
            m_param1->blockSignals(true);
            m_param2->blockSignals(true);
            m_param3->blockSignals(true);
            m_param1->setText(parts[2]);  // start freq
            m_param2->setText(parts[3]);  // stop freq
            m_param3->setText(parts[1]);  // points/decade
            m_param1->blockSignals(false);
            m_param2->blockSignals(false);
            m_param3->blockSignals(false);
        }
    } else if (cmd.startsWith(".op")) {
        m_analysisType->blockSignals(true);
        m_analysisType->setCurrentIndex(1);
        m_analysisType->blockSignals(false);
    } else if (cmd.startsWith(".dc")) {
        m_analysisType->blockSignals(true);
        m_analysisType->setCurrentIndex(3);  // Monte Carlo placeholder
        m_analysisType->blockSignals(false);
    }

    updateSchematicDirective();
}

void SimulationPanel::updateSchematicDirectiveFromCommand(const QString& commandText) {
    if (!m_scene) return;

    // Parse command and update form fields
    parseCommandText(commandText);
    
    // Find existing simulation command directive
    SchematicSpiceDirectiveItem* found = nullptr;
    for (auto* gi : m_scene->items()) {
        if (auto* existing = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
            if (existing->text().startsWith('.') &&
                (existing->text().startsWith(".tran", Qt::CaseInsensitive) ||
                 existing->text().startsWith(".ac", Qt::CaseInsensitive) ||
                 existing->text().startsWith(".dc", Qt::CaseInsensitive) ||
                 existing->text().startsWith(".op", Qt::CaseInsensitive))) {
                found = existing;
                break;
            }
        }
    }

    if (found) {
        found->setText(commandText);
        found->update();
    } else {
        // Determine position
        QPointF cmdPos(100, 200);
        if (!m_scene->views().isEmpty()) {
            if (auto* view = m_scene->views().first()) {
                cmdPos = view->mapToScene(view->viewport()->rect().center());
            }
        } else {
            for (auto* gi : m_scene->items()) {
                if (auto* page = dynamic_cast<SchematicPageItem*>(gi)) {
                    qreal w = page->boundingRect().width() - 8;
                    qreal h = page->boundingRect().height() - 8;
                    cmdPos = page->mapToScene(QPointF(-w/2 + 120, h/2 - 150));
                    break;
                }
            }
        }

        // Create and add the directive
        auto* cmdItem = new SchematicSpiceDirectiveItem(commandText, cmdPos);
        m_scene->addItem(cmdItem);
    }
}

QString SimulationPanel::tabStateKey(QGraphicsScene* scene) {
    return QString::number(reinterpret_cast<quintptr>(scene), 16);
}

SimulationPanel::TabOscilloscopeState SimulationPanel::saveCurrentTabState() const {
    TabOscilloscopeState state;
    state.lastResults = m_lastResults;
    state.previousResults = m_previousResults;
    state.hasLastResults = m_hasLastResults;
    state.hasPreviousResults = m_hasPreviousResults;

    if (m_waveformViewer) {
        state.waveformSignals = m_waveformViewer->exportSignals();
    }

    if (m_signalList) {
        for (int i = 0; i < m_signalList->count(); ++i) {
            auto* item = m_signalList->item(i);
            TabOscilloscopeState::SignalListItem si;
            si.name = item->text();
            si.checked = (item->checkState() == Qt::Checked);
            si.color = item->foreground().color();
            state.signalListItems.append(si);
        }
    }

    if (m_chart) {
        for (auto* series : m_chart->series()) {
            auto* line = qobject_cast<QLineSeries*>(series);
            if (!line) continue;
            TabOscilloscopeState::ChartSeriesData sd;
            sd.name = line->name();
            sd.points = line->points();
            sd.color = line->pen().color();
            sd.penWidth = line->pen().widthF();
            state.chartSeries.append(sd);
        }
    }

    if (m_spectrumChart) {
        for (auto* series : m_spectrumChart->series()) {
            auto* line = qobject_cast<QLineSeries*>(series);
            if (!line) continue;
            TabOscilloscopeState::ChartSeriesData sd;
            sd.name = line->name();
            sd.points = line->points();
            sd.color = line->pen().color();
            sd.penWidth = line->pen().widthF();
            state.spectrumSeries.append(sd);
        }
    }

    state.analysisConfig = getAnalysisConfig();
    state.commandText = m_commandLine ? m_commandLine->text() : QString();

    return state;
}

void SimulationPanel::restoreTabState(const TabOscilloscopeState& state) {
    m_lastResults = state.lastResults;
    m_previousResults = state.previousResults;
    m_hasLastResults = state.hasLastResults;
    m_hasPreviousResults = state.hasPreviousResults;

    if (m_waveformViewer && !state.waveformSignals.isEmpty()) {
        m_waveformViewer->beginBatchUpdate();
        m_waveformViewer->importSignals(state.waveformSignals);
        m_waveformViewer->endBatchUpdate();
    }

    if (m_signalList) {
        m_signalList->clear();
        for (const auto& si : state.signalListItems) {
            auto* item = new QListWidgetItem(si.name);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            item->setCheckState(si.checked ? Qt::Checked : Qt::Unchecked);
            item->setForeground(si.color);
            m_signalList->addItem(item);
        }
    }

    if (m_chart) {
        m_chart->removeAllSeries();
        if (!state.chartSeries.isEmpty()) {
            double minT = 0, maxT = 0, minV = 0, maxV = 0;
            bool first = true;
            for (const auto& sd : state.chartSeries) {
                auto* series = new QLineSeries();
                series->setName(sd.name);
                QPen pen(sd.color);
                pen.setWidthF(sd.penWidth);
                series->setPen(pen);
                series->replace(sd.points);
                m_chart->addSeries(series);

                for (const auto& pt : sd.points) {
                    if (first) { minT = maxT = pt.x(); minV = maxV = pt.y(); first = false; }
                    else {
                        if (pt.x() < minT) minT = pt.x();
                        if (pt.x() > maxT) maxT = pt.x();
                        if (pt.y() < minV) minV = pt.y();
                        if (pt.y() > maxV) maxV = pt.y();
                    }
                }
            }
            if (!first) {
                auto* axisX = new QValueAxis();
                axisX->setRange(minT, maxT);
                auto* axisY = new QValueAxis();
                double margin = (maxV - minV) * 0.05 + 1e-12;
                axisY->setRange(minV - margin, maxV + margin);
                m_chart->addAxis(axisX, Qt::AlignBottom);
                m_chart->addAxis(axisY, Qt::AlignLeft);
                for (auto* s : m_chart->series()) {
                    s->attachAxis(axisX);
                    s->attachAxis(axisY);
                }
            }
        }
    }

    if (m_spectrumChart) {
        m_spectrumChart->removeAllSeries();
        if (!state.spectrumSeries.isEmpty()) {
            double minT = 0, maxT = 0, minV = 0, maxV = 0;
            bool first = true;
            for (const auto& sd : state.spectrumSeries) {
                auto* series = new QLineSeries();
                series->setName(sd.name);
                QPen pen(sd.color);
                pen.setWidthF(sd.penWidth);
                series->setPen(pen);
                series->replace(sd.points);
                m_spectrumChart->addSeries(series);

                for (const auto& pt : sd.points) {
                    if (first) { minT = maxT = pt.x(); minV = maxV = pt.y(); first = false; }
                    else {
                        if (pt.x() < minT) minT = pt.x();
                        if (pt.x() > maxT) maxT = pt.x();
                        if (pt.y() < minV) minV = pt.y();
                        if (pt.y() > maxV) maxV = pt.y();
                    }
                }
            }
            if (!first) {
                auto* axisX = new QValueAxis();
                axisX->setRange(minT, maxT);
                auto* axisY = new QValueAxis();
                double margin = (maxV - minV) * 0.05 + 1e-12;
                axisY->setRange(minV - margin, maxV + margin);
                m_spectrumChart->addAxis(axisX, Qt::AlignBottom);
                m_spectrumChart->addAxis(axisY, Qt::AlignLeft);
                for (auto* s : m_spectrumChart->series()) {
                    s->attachAxis(axisX);
                    s->attachAxis(axisY);
                }
            }
        }
    }

    // Restore simulation parameters
    setAnalysisConfig(state.analysisConfig);
    if (!state.commandText.isEmpty()) {
        updateSchematicDirectiveFromCommand(state.commandText);
    }
}

void SimulationPanel::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->windowBackground().name() : "#1e1e1e";
    QString panelBg = theme ? theme->panelBackground().name() : "#252526";
    QString textColor = theme ? theme->textColor().name() : "#cccccc";
    QString accent = theme ? theme->accentColor().name() : "#3b82f6";
    QString borderColor = theme ? theme->panelBorder().name() : "#3c3c3c";

    setObjectName("SimulationPanel");
    setStyleSheet(QString("#SimulationPanel { background-color: %1; }").arg(bg));

    // --- Toolbar ---
    QToolBar* toolbar = new QToolBar("Simulation Controls");
    toolbar->setIconSize(QSize(20, 20));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setStyleSheet(QString(
        "QToolBar { background-color: %1; border-bottom: 1px solid %2; padding: 4px; spacing: 8px; }"
        "QToolButton { background: %3; border: 1px solid %2; border-radius: 2px; padding: 4px 8px; font-weight: bold; color: %4; }"
        "QToolButton:hover { background: %5; }"
    ).arg(panelBg, borderColor, bg, textColor, accent));

    m_runButton = new QPushButton("Run Simulation");
    m_runButton->setStyleSheet("background-color: #065f46; color: white; font-weight: bold; padding: 4px 12px; border-radius: 4px;");
    connect(m_runButton, &QPushButton::clicked, this, &SimulationPanel::onRunSimulation);
    toolbar->addWidget(m_runButton);

    QPushButton* exportCsvResultsBtn = new QPushButton("Export Waves CSV");
    exportCsvResultsBtn->setStyleSheet("background-color: #1f2937; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px;");
    connect(exportCsvResultsBtn, &QPushButton::clicked, this, &SimulationPanel::onExportResultsCsv);
    toolbar->addWidget(exportCsvResultsBtn);

    QPushButton* exportJsonResultsBtn = new QPushButton("Export Results JSON");
    exportJsonResultsBtn->setStyleSheet("background-color: #1f2937; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px;");
    connect(exportJsonResultsBtn, &QPushButton::clicked, this, &SimulationPanel::onExportResultsJson);
    toolbar->addWidget(exportJsonResultsBtn);

    QPushButton* exportReportBtn = new QPushButton("Export Report");
    exportReportBtn->setStyleSheet("background-color: #0f766e; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px;");
    connect(exportReportBtn, &QPushButton::clicked, this, &SimulationPanel::onExportResultsReport);
    toolbar->addWidget(exportReportBtn);

    m_overlayPreviousRun = new QCheckBox("Overlay Previous");
    m_overlayPreviousRun->setStyleSheet("QCheckBox { color: #eee; font-weight: bold; }");
    connect(m_overlayPreviousRun, &QCheckBox::toggled, this, [this](bool) {
        if (m_hasLastResults) plotBuiltinResults(m_lastResults);
    });
    toolbar->addWidget(m_overlayPreviousRun);

    QPushButton* probeBtn = new QPushButton("Add Probe");
    probeBtn->setStyleSheet("background-color: #1e40af; color: white; font-weight: bold; padding: 4px 12px; border-radius: 4px;");
    connect(probeBtn, &QPushButton::clicked, this, &SimulationPanel::probeRequested);
    toolbar->addWidget(probeBtn);

    QPushButton* probePinBtn = new QPushButton("Probe On Canvas");
    probePinBtn->setStyleSheet("background-color: #1d4ed8; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px;");
    connect(probePinBtn, &QPushButton::clicked, this, [this]() { emit placementToolRequested("Probe"); });
    toolbar->addWidget(probePinBtn);

    QPushButton* placeScopeBtn = new QPushButton("Place Scope");
    placeScopeBtn->setStyleSheet("background-color: #0f766e; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px;");
    connect(placeScopeBtn, &QPushButton::clicked, this, [this]() { emit placementToolRequested("Oscilloscope Instrument"); });
    toolbar->addWidget(placeScopeBtn);

    QPushButton* placeMeterBtn = new QPushButton("Place Voltmeter");
    placeMeterBtn->setStyleSheet("background-color: #0f766e; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px;");
    connect(placeMeterBtn, &QPushButton::clicked, this, [this]() { emit placementToolRequested("Voltmeter (DC)"); });
    toolbar->addWidget(placeMeterBtn);

    QPushButton* removeProbeBtn = new QPushButton("Remove Probe");
    removeProbeBtn->setStyleSheet("background-color: #9a3412; color: white; font-weight: bold; padding: 4px 12px; border-radius: 4px;");
    connect(removeProbeBtn, &QPushButton::clicked, this, [this]() {
        QListWidgetItem* item = m_signalList ? m_signalList->currentItem() : nullptr;
        if (!item) {
            m_logOutput->append("No selected probe to remove.");
            return;
        }
        removeProbe(item->text());
    });
    toolbar->addWidget(removeProbeBtn);

    QPushButton* clearProbesBtn = new QPushButton("Clear Probes");
    clearProbesBtn->setStyleSheet("background-color: #4b5563; color: white; font-weight: bold; padding: 4px 12px; border-radius: 4px;");
    connect(clearProbesBtn, &QPushButton::clicked, this, &SimulationPanel::clearAllProbes);
    toolbar->addWidget(clearProbesBtn);

    toolbar->addSeparator();

    QCheckBox* showVoltageCheck = new QCheckBox("Voltages");
    showVoltageCheck->setChecked(true);
    showVoltageCheck->setStyleSheet("QCheckBox { color: #eee; font-weight: bold; }");
    toolbar->addWidget(showVoltageCheck);

    QCheckBox* showCurrentCheck = new QCheckBox("Currents");
    showCurrentCheck->setChecked(true);
    showCurrentCheck->setStyleSheet("QCheckBox { color: #eee; font-weight: bold; }");
    toolbar->addWidget(showCurrentCheck);

    auto updateOverlays = [this, showVoltageCheck, showCurrentCheck]() {
        emit overlayVisibilityChanged(showVoltageCheck->isChecked(), showCurrentCheck->isChecked());
    };
    connect(showVoltageCheck, &QCheckBox::toggled, this, updateOverlays);
    connect(showCurrentCheck, &QCheckBox::toggled, this, updateOverlays);

    QPushButton* clearOverlaysBtn = new QPushButton("Clear Overlays");
    clearOverlaysBtn->setStyleSheet("background-color: #1f2937; color: white; padding: 4px 8px; border-radius: 4px;");
    connect(clearOverlaysBtn, &QPushButton::clicked, this, &SimulationPanel::clearOverlaysRequested);
    toolbar->addWidget(clearOverlaysBtn);

    toolbar->addSeparator();

    QPushButton* netlistBtn = new QPushButton("View Netlist");
    connect(netlistBtn, &QPushButton::clicked, this, &SimulationPanel::onViewNetlist);
    toolbar->addWidget(netlistBtn);

    toolbar->addSeparator();

    QPushButton* listenBtn = new QPushButton("Listen");
    listenBtn->setToolTip("Play selected signal through speakers (Transient only)");
    listenBtn->setStyleSheet("background-color: #7c3aed; color: white; font-weight: bold; padding: 4px 12px; border-radius: 4px;");
    connect(listenBtn, &QPushButton::clicked, this, [this]() {
        if (!m_signalList || !m_signalList->currentItem()) return;
        QString name = m_signalList->currentItem()->text();
        for (const auto& w : m_lastResults.waveforms) {
            if (QString::fromStdString(w.name) == name) {
                SimAudioEngine::instance().playWaveform(w);
                break;
            }
        }
    });
    toolbar->addWidget(listenBtn);

    mainLayout->addWidget(toolbar);

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setHandleWidth(2);
    mainSplitter->setStyleSheet(QString("QSplitter::handle { background-color: %1; }").arg(borderColor));

    // --- Sidebar ---
    QWidget* sidebar = new QWidget();
    sidebar->setMinimumWidth(240);
    sidebar->setStyleSheet(QString("QWidget { background-color: %1; }").arg(panelBg));
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(8, 12, 8, 12);
    sidebarLayout->setSpacing(15);

    QLabel* settingsLabel = new QLabel("ANALYSIS SETUP");
    settingsLabel->setStyleSheet(QString("QLabel { font-weight: bold; font-size: 10px; color: %1; letter-spacing: 1px; border: none; }").arg(theme ? theme->textSecondary().name() : "#888"));
    sidebarLayout->addWidget(settingsLabel);

    QFrame* configFrame = new QFrame();
    configFrame->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 3px; }").arg(bg, borderColor));
    QFormLayout* configForm = new QFormLayout(configFrame);
    configForm->setLabelAlignment(Qt::AlignRight);

    m_analysisType = new QComboBox();
    m_analysisType->addItems({"Transient", "DC OP", "AC Sweep", "RF S-Parameter", "Monte Carlo", "Parametric Sweep", "Sensitivity", "Real-time Mode"});
    m_analysisType->setCurrentIndex(1);
    m_analysisType->setStyleSheet("QComboBox { background: #121214; color: #eee; }");
    configForm->addRow("Type:", m_analysisType);

    m_commandLine = new QLineEdit(".op");
    m_commandLine->setStyleSheet("QLineEdit { background: #1e3a5f; color: #3b82f6; border: 1px solid #3b82f6; font-family: 'Courier New'; font-weight: bold; }");
    m_commandLine->setPlaceholderText(".tran <tstep> <tstop>");
    QWidget* commandRow = new QWidget();
    auto* commandRowLayout = new QHBoxLayout(commandRow);
    commandRowLayout->setContentsMargins(0, 0, 0, 0);
    commandRowLayout->setSpacing(6);
    commandRowLayout->addWidget(m_commandLine, 1);
    QPushButton* stepBuilderBtn = new QPushButton(".step...");
    stepBuilderBtn->setToolTip("Open the LTspice .step sweep builder");
    stepBuilderBtn->setStyleSheet("QPushButton { background: #0f766e; color: white; font-weight: bold; padding: 4px 8px; border-radius: 4px; }");
    connect(stepBuilderBtn, &QPushButton::clicked, this, &SimulationPanel::onOpenStepBuilder);
    commandRowLayout->addWidget(stepBuilderBtn);
    configForm->addRow("Command:", commandRow);

    m_param1 = new QLineEdit("1u");
    m_param2 = new QLineEdit("10m");
    m_param3 = new QLineEdit("0");
    m_param4 = new QLineEdit();
    m_param5 = new QLineEdit();
    m_param6 = new QLineEdit("50");
    for(auto* l : {m_param1, m_param2, m_param3, m_param4, m_param5, m_param6}) l->setStyleSheet("QLineEdit { background: #121214; color: #fff; border: 1px solid #333; }");
    
    configForm->addRow("Step/Start:", m_param1);
    configForm->addRow("Stop:", m_param2);
    configForm->addRow("Start/Pts:", m_param3);
    configForm->addRow("P4/Src:", m_param4);
    configForm->addRow("P5/Node:", m_param5);
    configForm->addRow("Z0:", m_param6);
    sidebarLayout->addWidget(configFrame);

    QLabel* generatorsLabel = new QLabel("SOURCE GENERATORS");
    generatorsLabel->setStyleSheet(settingsLabel->styleSheet());
    sidebarLayout->addWidget(generatorsLabel);

    QFrame* generatorFrame = new QFrame();
    generatorFrame->setStyleSheet(QString("QFrame { background: %1; border: 1px solid %2; border-radius: 3px; }").arg(bg, borderColor));
    QFormLayout* generatorForm = new QFormLayout(generatorFrame);
    generatorForm->setLabelAlignment(Qt::AlignRight);

    m_generatorType = new QComboBox();
    m_generatorType->addItems({"DC", "SIN", "PULSE", "EXP", "SFFM", "PWL", "AM", "FM"});
    m_generatorType->setStyleSheet("QComboBox { background: #121214; color: #eee; }");
    generatorForm->addRow("Type:", m_generatorType);

    m_generatorPresetCombo = new QComboBox();
    m_generatorPresetCombo->setStyleSheet("QComboBox { background: #121214; color: #eee; }");
    generatorForm->addRow("Template:", m_generatorPresetCombo);

    m_genLabel1 = new QLabel("Value:");
    m_genLabel2 = new QLabel("P2:");
    m_genLabel3 = new QLabel("P3:");
    m_genLabel4 = new QLabel("P4:");
    m_genLabel5 = new QLabel("P5:");
    m_genLabel6 = new QLabel("P6:");

    m_genParam1 = new QLineEdit("5");
    m_genParam2 = new QLineEdit("1");
    m_genParam3 = new QLineEdit("1k");
    m_genParam4 = new QLineEdit("0");
    m_genParam5 = new QLineEdit("0");
    m_genParam6 = new QLineEdit("0");
    for (auto* l : {m_genParam1, m_genParam2, m_genParam3, m_genParam4, m_genParam5, m_genParam6}) {
        l->setStyleSheet("QLineEdit { background: #121214; color: #fff; border: 1px solid #333; }");
    }

    generatorForm->addRow(m_genLabel1, m_genParam1);
    generatorForm->addRow(m_genLabel2, m_genParam2);
    generatorForm->addRow(m_genLabel3, m_genParam3);
    generatorForm->addRow(m_genLabel4, m_genParam4);
    generatorForm->addRow(m_genLabel5, m_genParam5);
    generatorForm->addRow(m_genLabel6, m_genParam6);

    QPushButton* applyGeneratorBtn = new QPushButton("Apply to Selected Source");
    applyGeneratorBtn->setStyleSheet("QPushButton { background-color: #7c2d12; color: white; font-weight: bold; padding: 4px 10px; border-radius: 4px; }");
    generatorForm->addRow("", applyGeneratorBtn);

    QWidget* waveTools = new QWidget();
    QGridLayout* waveToolsLayout = new QGridLayout(waveTools);
    waveToolsLayout->setContentsMargins(0, 0, 0, 0);
    waveToolsLayout->setHorizontalSpacing(6);
    waveToolsLayout->setVerticalSpacing(6);

    QPushButton* pwlEditorBtn = new QPushButton("PWL Editor");
    QPushButton* importCsvBtn = new QPushButton("Import CSV");
    QPushButton* exportCsvBtn = new QPushButton("Export CSV");
    QPushButton* savePresetBtn = new QPushButton("Save Preset");
    QPushButton* deletePresetBtn = new QPushButton("Delete Preset");
    for (QPushButton* b : {pwlEditorBtn, importCsvBtn, exportCsvBtn, savePresetBtn, deletePresetBtn}) {
        b->setStyleSheet("QPushButton { background-color: #374151; color: white; padding: 4px 8px; border-radius: 4px; }");
    }
    pwlEditorBtn->setStyleSheet("QPushButton { background-color: #1e40af; color: white; font-weight: bold; padding: 4px 8px; border-radius: 4px; }");
    savePresetBtn->setStyleSheet("QPushButton { background-color: #065f46; color: white; font-weight: bold; padding: 4px 8px; border-radius: 4px; }");
    deletePresetBtn->setStyleSheet("QPushButton { background-color: #7f1d1d; color: white; padding: 4px 8px; border-radius: 4px; }");

    waveToolsLayout->addWidget(pwlEditorBtn, 0, 0);
    waveToolsLayout->addWidget(importCsvBtn, 0, 1);
    waveToolsLayout->addWidget(exportCsvBtn, 1, 0);
    waveToolsLayout->addWidget(savePresetBtn, 1, 1);
    waveToolsLayout->addWidget(deletePresetBtn, 2, 0, 1, 2);
    generatorForm->addRow("", waveTools);
    sidebarLayout->addWidget(generatorFrame);

    QLabel* signalsLabel = new QLabel("TRACE MONITOR");
    signalsLabel->setStyleSheet(settingsLabel->styleSheet());
    sidebarLayout->addWidget(signalsLabel);

    m_signalList = new QListWidget();
    m_signalList->setStyleSheet(QString(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 3px; color: #eee; }"
        "QListWidget::item { padding: 4px; border-bottom: 1px solid %2; }"
        "QListWidget::item:selected { background: %3; color: white; }"
    ).arg(bg, borderColor, accent));
    sidebarLayout->addWidget(m_signalList, 1);

    QLabel* measurementsLabel = new QLabel("MEASUREMENTS");
    measurementsLabel->setStyleSheet(settingsLabel->styleSheet());
    sidebarLayout->addWidget(measurementsLabel);

    QWidget* cursorWidget = new QWidget();
    QHBoxLayout* cursorLayout = new QHBoxLayout(cursorWidget);
    cursorLayout->setContentsMargins(0, 0, 0, 0);
    cursorLayout->setSpacing(6);
    cursorLayout->addWidget(new QLabel("A%"));
    QDoubleSpinBox* cursorASpin = new QDoubleSpinBox();
    cursorASpin->setRange(0.0, 100.0);
    cursorASpin->setDecimals(1);
    cursorASpin->setSingleStep(1.0);
    cursorASpin->setValue(m_cursorAFrac * 100.0);
    cursorASpin->setStyleSheet("QDoubleSpinBox { background: #121214; color: #fff; border: 1px solid #333; }");
    cursorLayout->addWidget(cursorASpin);
    cursorLayout->addWidget(new QLabel("B%"));
    QDoubleSpinBox* cursorBSpin = new QDoubleSpinBox();
    cursorBSpin->setRange(0.0, 100.0);
    cursorBSpin->setDecimals(1);
    cursorBSpin->setSingleStep(1.0);
    cursorBSpin->setValue(m_cursorBFrac * 100.0);
    cursorBSpin->setStyleSheet("QDoubleSpinBox { background: #121214; color: #fff; border: 1px solid #333; }");
    cursorLayout->addWidget(cursorBSpin);
    sidebarLayout->addWidget(cursorWidget);

    connect(cursorASpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        m_cursorAFrac = std::clamp(v / 100.0, 0.0, 1.0);
    });
    connect(cursorBSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        m_cursorBFrac = std::clamp(v / 100.0, 0.0, 1.0);
    });

    m_measurementsTable = new QTableWidget(0, 6);
    m_measurementsTable->setHorizontalHeaderLabels({"Name", "Primary/Result", "Avg", "RMS", "Freq/Step", "Delta(A-B)"});
    m_measurementsTable->horizontalHeader()->setStretchLastSection(true);
    m_measurementsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_measurementsTable->verticalHeader()->hide();
    m_measurementsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_measurementsTable->setStyleSheet(QString(
        "QTableWidget { background: %1; border: 1px solid %2; color: %3; font-size: 9px; }"
        "QHeaderView::section { background: %4; border: 1px solid %2; color: %3; padding: 2px; }"
    ).arg(bg, borderColor, textColor, panelBg));
    sidebarLayout->addWidget(m_measurementsTable, 1);

    m_logOutput = new QTextEdit();
    m_logOutput->setReadOnly(true);
    m_logOutput->setMaximumHeight(100);
    m_logOutput->setStyleSheet(QString("QTextEdit { background: %1; border: 1px solid %2; font-family: monospace; font-size: 9px; color: #eee; }").arg(bg, borderColor));
    sidebarLayout->addWidget(m_logOutput);

    QPushButton* showFullLogBtn = new QPushButton("Show Detailed Log");
    showFullLogBtn->setStyleSheet("QPushButton { background: #374151; color: white; border-radius: 3px; padding: 4px; font-size: 10px; }");
    connect(showFullLogBtn, &QPushButton::clicked, this, &SimulationPanel::showDetailedLog);
    auto* logShortcut = new QShortcut(QKeySequence("Ctrl+L"), this);
    connect(logShortcut, &QShortcut::activated, this, &SimulationPanel::showDetailedLog);
    sidebarLayout->addWidget(showFullLogBtn);

    QLabel* issuesLabel = new QLabel("SIM ISSUES (DOUBLE-CLICK TO NAVIGATE)");
    issuesLabel->setStyleSheet(settingsLabel->styleSheet());
    sidebarLayout->addWidget(issuesLabel);

    m_issueList = new QListWidget();
    m_issueList->setStyleSheet(QString(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 3px; color: #fbbf24; }"
        "QListWidget::item { padding: 4px; border-bottom: 1px solid %2; }"
    ).arg(bg, borderColor));
    connect(m_issueList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString type = item->data(Qt::UserRole + 1).toString();
        const QString id = item->data(Qt::UserRole + 2).toString();
        if (!type.isEmpty() && !id.isEmpty()) {
            emit simulationTargetRequested(type, id);
        }
    });
    sidebarLayout->addWidget(m_issueList, 1);

    QScrollArea* sidebarScroll = new QScrollArea();
    sidebarScroll->setWidgetResizable(true);
    sidebarScroll->setFrameShape(QFrame::NoFrame);
    sidebarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebarScroll->setMinimumWidth(240);
    sidebarScroll->setStyleSheet(QString("QScrollArea { background-color: %1; border-right: 1px solid %2; }").arg(panelBg, borderColor));
    sidebarScroll->setWidget(sidebar);
    mainSplitter->addWidget(sidebarScroll);

    // --- Main Plot Content ---
    QWidget* plotContainer = new QWidget();
    QVBoxLayout* plotLayout = new QVBoxLayout(plotContainer);
    plotLayout->setContentsMargins(10, 10, 10, 10);

    // ── Time-Travel Timeline ───────────────────────────────────────────
    QWidget* timelineWidget = new QWidget();
    QHBoxLayout* timelineLayout = new QHBoxLayout(timelineWidget);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    
    QLabel* ttLabel = new QLabel("TIME-TRAVEL:");
    ttLabel->setStyleSheet("font-weight: bold; font-size: 10px; color: #569cd6;");
    timelineLayout->addWidget(ttLabel);
    
    m_timelineSlider = new QSlider(Qt::Horizontal);
    m_timelineSlider->setRange(0, 1000);
    m_timelineSlider->setEnabled(false);
    m_timelineSlider->setStyleSheet("QSlider::handle:horizontal { background: #569cd6; border-radius: 4px; width: 12px; }");
    timelineLayout->addWidget(m_timelineSlider, 1);
    
    m_timelineLabel = new QLabel("--- s");
    m_timelineLabel->setMinimumWidth(80);
    m_timelineLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timelineLabel->setStyleSheet("font-family: monospace; color: #fff; font-size: 11px;");
    timelineLayout->addWidget(m_timelineLabel);
    
    plotLayout->addWidget(timelineWidget);
    connect(m_timelineSlider, &QSlider::valueChanged, this, &SimulationPanel::onTimelineValueChanged);

    m_chart = new QChart();
    m_chart->setTitle("Waveform Viewer");
    m_chart->setTitleBrush(QBrush(Qt::white));
    m_chart->setBackgroundBrush(QBrush(Qt::black));
    m_chart->setPlotAreaBackgroundBrush(QBrush(Qt::black));
    m_chart->setPlotAreaBackgroundVisible(true);
    m_chart->setMargins(QMargins(0, 0, 0, 0));
    m_chart->setBackgroundRoundness(0);
    
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignTop);
    m_chart->legend()->setMarkerShape(QLegend::MarkerShapeRectangle);
    m_chart->legend()->setBackgroundVisible(false);
    m_chart->legend()->setLabelColor(Qt::white);
    
    m_plotView = new QChartView(m_chart);
    m_plotView->setRenderHint(QPainter::Antialiasing);
    m_plotView->setStyleSheet(QString("background-color: black; border: 1px solid %1;").arg(borderColor));

    m_spectrumChart = new QChart();
    m_spectrumChart->setTitle("FFT Spectrum Analysis");
    m_spectrumChart->setTitleBrush(QBrush(Qt::white));
    m_spectrumChart->setBackgroundBrush(QBrush(Qt::black));
    m_spectrumChart->setPlotAreaBackgroundBrush(QBrush(Qt::black));
    m_spectrumChart->setPlotAreaBackgroundVisible(true);
    m_spectrumChart->setMargins(QMargins(0, 0, 0, 0));
    m_spectrumChart->setBackgroundRoundness(0);
    
    m_spectrumChart->legend()->setVisible(true);
    m_spectrumChart->legend()->setAlignment(Qt::AlignTop);
    m_spectrumChart->legend()->setBackgroundVisible(false);
    m_spectrumChart->legend()->setLabelColor(Qt::white);

    m_spectrumView = new QChartView(m_spectrumChart);
    m_spectrumView->setRenderHint(QPainter::Antialiasing);
    m_spectrumView->setStyleSheet(m_plotView->styleSheet());

    m_spectrumTab = new QWidget();
    auto* spectrumTabLayout = new QVBoxLayout(m_spectrumTab);
    spectrumTabLayout->setContentsMargins(0, 0, 0, 0);
    spectrumTabLayout->setSpacing(6);

    QWidget* steppedControls = new QWidget();
    auto* steppedControlsLayout = new QHBoxLayout(steppedControls);
    steppedControlsLayout->setContentsMargins(0, 0, 0, 0);
    steppedControlsLayout->setSpacing(6);
    steppedControlsLayout->addWidget(new QLabel("Measurement"));
    m_steppedMeasSeriesCombo = new QComboBox();
    m_steppedMeasSeriesCombo->setEnabled(false);
    steppedControlsLayout->addWidget(m_steppedMeasSeriesCombo, 1);
    steppedControlsLayout->addWidget(new QLabel("X Axis"));
    m_steppedMeasAxisCombo = new QComboBox();
    m_steppedMeasAxisCombo->setEnabled(false);
    steppedControlsLayout->addWidget(m_steppedMeasAxisCombo, 1);
    spectrumTabLayout->addWidget(steppedControls);
    spectrumTabLayout->addWidget(m_spectrumView, 1);

    m_viewTabs = new QTabWidget();
    m_viewTabs->setStyleSheet(QString("QTabWidget::pane { border: 1px solid %1; } QTabBar::tab { background: %2; color: %3; padding: 8px; } QTabBar::tab:selected { background: %4; }")
                            .arg(borderColor, panelBg, textColor, accent));

    m_rfTab = new QWidget();
    QVBoxLayout* rfLayout = new QVBoxLayout(m_rfTab);
    m_smithChart = new SmithChartWidget();
    rfLayout->addWidget(m_smithChart);
    m_viewTabs->addTab(m_rfTab, "RF Analysis");
    
    m_waveformViewer = new WaveformViewer();
    m_scopeContainer = m_waveformViewer;
    
    m_viewTabs->addTab(m_plotView, "Standard Waves");
    m_viewTabs->addTab(m_spectrumTab, "FFT Spectrum");
    // viewTabs->addTab(m_waveformViewer, "Oscilloscope"); // Handled via bottom dock
    
    m_logicAnalyzer = new LogicAnalyzerWidget();
    m_viewTabs->addTab(m_logicAnalyzer, "Logic Analyzer");

    m_voltmeter = new VoltmeterWidget();
    m_viewTabs->addTab(m_voltmeter, "Voltmeter");

    m_ammeter = new AmmeterWidget();
    m_viewTabs->addTab(m_ammeter, "Ammeter");

    m_wattmeter = new WattmeterWidget();
    m_viewTabs->addTab(m_wattmeter, "Wattmeter");

    m_freqCounter = new FrequencyCounterWidget();
    m_viewTabs->addTab(m_freqCounter, "Frequency Counter");

    m_logicProbe = new LogicProbeWidget();
    m_viewTabs->addTab(m_logicProbe, "Logic Probe");

    plotLayout->addWidget(m_viewTabs);

    mainSplitter->addWidget(plotContainer);
    mainSplitter->setSizes({260, 600});
    mainLayout->addWidget(mainSplitter);

    connect(m_analysisType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationPanel::onAnalysisChanged);
    connect(m_analysisType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationPanel::updateSchematicDirective);
    connect(m_analysisType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationPanel::updateCommandDisplay);
    connect(m_param1, &QLineEdit::textChanged, this, &SimulationPanel::updateSchematicDirective);
    connect(m_param2, &QLineEdit::textChanged, this, &SimulationPanel::updateSchematicDirective);
    connect(m_param3, &QLineEdit::textChanged, this, &SimulationPanel::updateSchematicDirective);
    connect(m_param1, &QLineEdit::textChanged, this, &SimulationPanel::updateCommandDisplay);
    connect(m_param2, &QLineEdit::textChanged, this, &SimulationPanel::updateCommandDisplay);
    connect(m_param3, &QLineEdit::textChanged, this, &SimulationPanel::updateCommandDisplay);
    connect(m_commandLine, &QLineEdit::editingFinished, this, [this]() {
        parseCommandText(m_commandLine->text());
    });
    connect(m_generatorType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationPanel::onGeneratorTypeChanged);
    connect(m_generatorPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SimulationPanel::onGeneratorPresetActivated);
    connect(applyGeneratorBtn, &QPushButton::clicked, this, &SimulationPanel::onApplyGeneratorToSelection);
    connect(pwlEditorBtn, &QPushButton::clicked, this, &SimulationPanel::onOpenPwlEditor);
    connect(importCsvBtn, &QPushButton::clicked, this, &SimulationPanel::onImportPwlCsv);
    connect(exportCsvBtn, &QPushButton::clicked, this, &SimulationPanel::onExportPwlCsv);
    connect(savePresetBtn, &QPushButton::clicked, this, &SimulationPanel::onSaveGeneratorPreset);
    connect(deletePresetBtn, &QPushButton::clicked, this, &SimulationPanel::onDeleteGeneratorPreset);
    connect(m_signalList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        QString seriesName = item->text();
        bool isVisible = (item->checkState() == Qt::Checked);
        if (m_chart) {
            for (auto* series : m_chart->series()) {
                if (series->name() == seriesName) {
                    series->setVisible(isVisible);
                    break;
                }
            }
        }
    });
    connect(m_steppedMeasSeriesCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        m_selectedSteppedMeasurement = text;
        if (m_hasLastResults) {
            refreshSteppedMeasurementControls(m_lastResults);
            rebuildSteppedMeasurementPlot(m_lastResults);
        }
    });
    connect(m_steppedMeasAxisCombo, &QComboBox::currentTextChanged, this, [this](const QString& text) {
        m_selectedSteppedAxis = text;
        if (m_hasLastResults) rebuildSteppedMeasurementPlot(m_lastResults);
    });

    onGeneratorTypeChanged(m_generatorType->currentIndex());
    loadGeneratorLibrary();
    updateCommandDisplay();
}

void SimulationPanel::onViewNetlist() {
    QDialog* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle("Generated SPICE Netlist");
    dlg->resize(600, 500);
    QVBoxLayout* lay = new QVBoxLayout(dlg);
    QTextEdit* edit = new QTextEdit;
    edit->setReadOnly(true);
    edit->setPlainText(generateSpiceNetlist());
    edit->setStyleSheet("background-color: #1e1e24; color: #dcdcdc; font-family: monospace;");
    lay->addWidget(edit);
    dlg->show();
}

void SimulationPanel::onAnalysisChanged(int index) {
    SimManager::instance().stopRealTime();

    QFormLayout* layout = qobject_cast<QFormLayout*>(m_param1->parentWidget()->layout());
    if (!layout) return;
    
    auto setLabel = [&](QLineEdit* field, const QString& text) {
        if (auto* lbl = qobject_cast<QLabel*>(layout->labelForField(field)))
            lbl->setText(text);
    };

    if (index == 0) { // Transient
        setLabel(m_param1, "Step:");
        setLabel(m_param2, "Stop Time:");
        setLabel(m_param3, "Start Time:");
        m_param1->setVisible(true); m_param2->setVisible(true); m_param3->setVisible(true);
        m_param4->setVisible(false); m_param5->setVisible(false); m_param6->setVisible(false);
        m_param1->setText("1u"); m_param2->setText("10m"); m_param3->setText("0");
    } else if (index == 1) { // DC OP
        m_param1->setVisible(false); m_param2->setVisible(false); m_param3->setVisible(false);
        m_param4->setVisible(false); m_param5->setVisible(false); m_param6->setVisible(false);
    } else if (index == 2) { // AC Sweep
        setLabel(m_param1, "Start Freq:");
        setLabel(m_param2, "Stop Freq:");
        setLabel(m_param3, "Points/Dec:");
        m_param1->setVisible(true); m_param2->setVisible(true); m_param3->setVisible(true);
        m_param4->setVisible(false); m_param5->setVisible(false); m_param6->setVisible(false);
        m_param1->setText("10"); m_param2->setText("1Meg"); m_param3->setText("10");
    } else if (index == 3) { // RF S-Parameter
        setLabel(m_param1, "Start Freq:");
        setLabel(m_param2, "Stop Freq:");
        setLabel(m_param3, "Points/Dec:");
        setLabel(m_param4, "Port 1 Src:");
        setLabel(m_param5, "Port 2 Node:");
        setLabel(m_param6, "Ref Z0:");
        m_param1->setVisible(true); m_param2->setVisible(true); m_param3->setVisible(true);
        m_param4->setVisible(true); m_param5->setVisible(true); m_param6->setVisible(true);
        m_param1->setText("10"); m_param2->setText("1Meg"); m_param3->setText("10");
        m_param4->setText("V1"); m_param5->setText("OUT"); m_param6->setText("50");
    } else if (index == 4) { // Monte Carlo
        setLabel(m_param1, "Runs:");
        m_param1->setVisible(true); m_param2->setVisible(false); m_param3->setVisible(false);
        m_param4->setVisible(false); m_param5->setVisible(false); m_param6->setVisible(false);
        m_param1->setText("10");
    } else if (index == 5) { // Parametric Sweep
        setLabel(m_param1, "Component:");
        setLabel(m_param2, "Param:");
        setLabel(m_param3, "Start:");
        setLabel(m_param4, "Stop:");
        setLabel(m_param5, "Steps:");
        m_param1->setVisible(true); m_param2->setVisible(true); m_param3->setVisible(true);
        m_param4->setVisible(true); m_param5->setVisible(true); m_param6->setVisible(false);
        m_param1->setText("R1"); m_param2->setText("resistance"); m_param3->setText("1k");
        m_param4->setText("10k"); m_param5->setText("10");
    } else if (index == 6) { // Sensitivity
        setLabel(m_param1, "Target Signal:");
        m_param1->setVisible(true); m_param2->setVisible(false); m_param3->setVisible(false);
        m_param4->setVisible(false); m_param5->setVisible(false); m_param6->setVisible(false);
        m_param1->setText("V(Out)");
    } else if (index == 7) { // Real-time
        setLabel(m_param1, "Update (ms):");
        m_param1->setVisible(true); m_param2->setVisible(false); m_param3->setVisible(false);
        m_param4->setVisible(false); m_param5->setVisible(false); m_param6->setVisible(false);
        m_param1->setText("100");
    }
}

void SimulationPanel::onGeneratorTypeChanged(int index) {
    Q_UNUSED(index)
    if (!m_generatorType) return;
    const QString type = m_generatorType->currentText();

    auto showParam = [](QLabel* lbl, QLineEdit* edit, const QString& title, const QString& value) {
        lbl->setVisible(true);
        edit->setVisible(true);
        lbl->setText(title);
        if (edit->text().isEmpty()) {
            edit->setText(value);
        }
    };
    auto hideParam = [](QLabel* lbl, QLineEdit* edit) {
        lbl->setVisible(false);
        edit->setVisible(false);
    };

    if (type == "DC") {
        showParam(m_genLabel1, m_genParam1, "Value:", "5");
        hideParam(m_genLabel2, m_genParam2); hideParam(m_genLabel3, m_genParam3);
        hideParam(m_genLabel4, m_genParam4); hideParam(m_genLabel5, m_genParam5);
        hideParam(m_genLabel6, m_genParam6);
    } else if (type == "SIN") {
        showParam(m_genLabel1, m_genParam1, "Offset:", "0");
        showParam(m_genLabel2, m_genParam2, "Amplitude (Peak):", "5");
        showParam(m_genLabel3, m_genParam3, "Freq:", "1k");
        showParam(m_genLabel4, m_genParam4, "Delay:", "0");
        showParam(m_genLabel5, m_genParam5, "Phase:", "0");
        hideParam(m_genLabel6, m_genParam6);
    } else if (type == "PULSE") {
        showParam(m_genLabel1, m_genParam1, "V1:", "0");
        showParam(m_genLabel2, m_genParam2, "V2:", "5");
        showParam(m_genLabel3, m_genParam3, "Delay:", "0");
        showParam(m_genLabel4, m_genParam4, "Rise:", "1u");
        showParam(m_genLabel5, m_genParam5, "Fall:", "1u");
        showParam(m_genLabel6, m_genParam6, "Width:", "500u");
    } else if (type == "PWL") {
        showParam(m_genLabel1, m_genParam1, "T1:", "0");
        showParam(m_genLabel2, m_genParam2, "V1:", "0");
        showParam(m_genLabel3, m_genParam3, "T2:", "1m");
        showParam(m_genLabel4, m_genParam4, "V2:", "5");
        showParam(m_genLabel5, m_genParam5, "T3:", "2m");
        showParam(m_genLabel6, m_genParam6, "V3:", "0");
    }
}

void SimulationPanel::onOpenPwlEditor() {
    seedDefaultPwlPointsIfNeeded();
    QDialog dlg(this);
    dlg.setWindowTitle("PWL Editor");
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    QTableWidget* table = new QTableWidget(static_cast<int>(m_pwlPoints.size()), 2, &dlg);
    table->setHorizontalHeaderLabels({"Time", "Value"});
    for (int i = 0; i < static_cast<int>(m_pwlPoints.size()); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(m_pwlPoints[i].first));
        table->setItem(i, 1, new QTableWidgetItem(m_pwlPoints[i].second));
    }
    layout->addWidget(table);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted) {
        m_pwlPoints.clear();
        for (int r = 0; r < table->rowCount(); ++r) {
            m_pwlPoints.push_back({table->item(r, 0)->text(), table->item(r, 1)->text()});
        }
    }
}

void SimulationPanel::onOpenStepBuilder() {
    QString currentStep;
    if (m_scene) {
        for (auto* gi : m_scene->items()) {
            auto* directive = dynamic_cast<SchematicSpiceDirectiveItem*>(gi);
            if (!directive) continue;
            if (directive->text().trimmed().startsWith(".step", Qt::CaseInsensitive)) {
                currentStep = directive->text().trimmed();
                break;
            }
        }
    }
    if (currentStep.isEmpty() && m_commandLine && m_commandLine->text().trimmed().startsWith(".step", Qt::CaseInsensitive)) {
        currentStep = m_commandLine->text().trimmed();
    }

    SpiceStepDialog dlg(currentStep, m_scene, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString stepCommand = dlg.commandText();
    if (m_commandLine) m_commandLine->setText(stepCommand);

    if (!m_scene) return;
    SchematicSpiceDirectiveItem* found = nullptr;
    for (auto* gi : m_scene->items()) {
        auto* directive = dynamic_cast<SchematicSpiceDirectiveItem*>(gi);
        if (!directive) continue;
        if (directive->text().trimmed().startsWith(".step", Qt::CaseInsensitive)) {
            found = directive;
            break;
        }
    }

    if (found) {
        found->setText(stepCommand);
        found->update();
        return;
    }

    QPointF cmdPos(100, 200);
    if (!m_scene->views().isEmpty()) {
        if (auto* view = m_scene->views().first()) {
            cmdPos = view->mapToScene(view->viewport()->rect().center() + QPoint(120, -60));
        }
    }
    auto* cmdItem = new SchematicSpiceDirectiveItem(stepCommand, cmdPos);
    m_scene->addItem(cmdItem);
}

void SimulationPanel::onImportPwlCsv() {
    QString path = QFileDialog::getOpenFileName(this, "Import PWL CSV", m_projectDir, "CSV Files (*.csv)");
    if (!path.isEmpty()) importPwlCsvFile(path);
}

void SimulationPanel::onExportPwlCsv() {
    QString path = QFileDialog::getSaveFileName(this, "Export PWL CSV", m_projectDir, "CSV Files (*.csv)");
    if (!path.isEmpty()) exportPwlCsvFile(path);
}

void SimulationPanel::onExportResultsCsv() {
    QString path = QFileDialog::getSaveFileName(this, "Export Results CSV", m_projectDir, "CSV Files (*.csv)");
    if (!path.isEmpty()) exportResultsCsvFile(path);
}

void SimulationPanel::onExportResultsJson() {
    QString path = QFileDialog::getSaveFileName(this, "Export Results JSON", m_projectDir, "JSON Files (*.json)");
    if (!path.isEmpty()) exportResultsJsonFile(path);
}

void SimulationPanel::onExportResultsReport() {
    QString path = QFileDialog::getSaveFileName(this, "Export Results Report", m_projectDir, "Markdown Files (*.md)");
    if (!path.isEmpty()) exportResultsReportFile(path);
}

void SimulationPanel::onSaveGeneratorPreset() {
    QString name = QInputDialog::getText(this, "Save Preset", "Preset Name:");
    if (!name.isEmpty()) {
        m_userGeneratorPresets[name] = collectGeneratorConfig();
        saveUserGeneratorPresets();
        refreshGeneratorPresetCombo();
    }
}

void SimulationPanel::onDeleteGeneratorPreset() {
    QString tag = m_generatorPresetCombo->currentData().toString();
    if (tag.startsWith("U:")) {
        m_userGeneratorPresets.remove(tag.mid(2));
        saveUserGeneratorPresets();
        refreshGeneratorPresetCombo();
    }
}

void SimulationPanel::setAnalysisConfig(const AnalysisConfig& cfg) {
    if (!m_analysisType) return;
    
    bool oldBuild = m_buildInProgress;
    m_buildInProgress = true;
    
    int idx = 0;
    switch (cfg.type) {
        case SimAnalysisType::Transient: idx = 0; break;
        case SimAnalysisType::OP:        idx = 1; break;
        case SimAnalysisType::AC:        idx = 2; break;
        case SimAnalysisType::SParameter: idx = 3; break;
        case SimAnalysisType::MonteCarlo: idx = 4; break;
        case SimAnalysisType::ParametricSweep: idx = 5; break;
        case SimAnalysisType::Sensitivity: idx = 6; break;
        case SimAnalysisType::RealTime: idx = 7; break;
    }
    
    m_analysisType->setCurrentIndex(idx);
    
    // Only set raw numeric fields if we don't have a command text to parse from.
    // This avoids losing precision/suffixes during the switch.
    if (cfg.commandText.isEmpty()) {
        if (idx == 0) {
            if (cfg.step > 0) m_param1->setText(QString::number(cfg.step, 'g', 10));
            if (cfg.stop > 0) m_param2->setText(QString::number(cfg.stop, 'g', 10));
        } else if (idx == 2) {
            const double fStart = (cfg.fStart > 0.0) ? cfg.fStart : 10.0;
            const double fStop = (cfg.fStop > 0.0) ? cfg.fStop : 1e6;
            const int pts = (cfg.pts > 0) ? cfg.pts : 10;
            m_param1->setText(QString::number(fStart, 'g', 12));
            m_param2->setText(QString::number(fStop, 'g', 12));
            m_param3->setText(QString::number(pts));
        } else if (idx == 3) { // RF S-Parameter
            m_param1->setText(QString::number(cfg.fStart, 'g', 12));
            m_param2->setText(QString::number(cfg.fStop, 'g', 12));
            m_param3->setText(QString::number(cfg.pts));
            m_param4->setText(cfg.rfPort1Source);
            m_param5->setText(cfg.rfPort2Node);
            m_param6->setText(QString::number(cfg.rfZ0, 'g', 10));
        }
    }

    if (!cfg.commandText.isEmpty()) {
        m_commandLine->setText(cfg.commandText);
        parseCommandText(cfg.commandText);
    } else {
        updateCommandDisplay();
    }
    m_buildInProgress = oldBuild;
}

SimulationPanel::AnalysisConfig SimulationPanel::getAnalysisConfig() const {
    AnalysisConfig cfg{};
    const int idx = m_analysisType ? m_analysisType->currentIndex() : 0;
    if (idx == 0) {
        cfg.type = SimAnalysisType::Transient;
        cfg.step = parseValue(m_param1 ? m_param1->text() : QString(), 1e-6);
        cfg.stop = parseValue(m_param2 ? m_param2->text() : QString(), 10e-3);
    } else if (idx == 1) {
        cfg.type = SimAnalysisType::OP;
    } else if (idx == 2) {
        cfg.type = SimAnalysisType::AC;
        cfg.fStart = parseValue(m_param1 ? m_param1->text() : QString(), 10);
        cfg.fStop = parseValue(m_param2 ? m_param2->text() : QString(), 1e6);
        cfg.pts = std::max(1, (m_param3 ? m_param3->text().trimmed().toInt() : 10));
    } else if (idx == 3) {
        cfg.type = SimAnalysisType::SParameter;
        cfg.fStart = parseValue(m_param1 ? m_param1->text() : QString(), 10);
        cfg.fStop = parseValue(m_param2 ? m_param2->text() : QString(), 1e6);
        cfg.pts = std::max(1, (m_param3 ? m_param3->text().trimmed().toInt() : 10));
        cfg.rfPort1Source = m_param4 ? m_param4->text().trimmed() : "V1";
        cfg.rfPort2Node = m_param5 ? m_param5->text().trimmed() : "OUT";
        cfg.rfZ0 = parseValue(m_param6 ? m_param6->text().trimmed() : QString(), 50.0);
    } else if (idx == 4) {
        cfg.type = SimAnalysisType::MonteCarlo;
    } else if (idx == 5) {
        cfg.type = SimAnalysisType::ParametricSweep;
    } else if (idx == 6) {
        cfg.type = SimAnalysisType::Sensitivity;
    } else if (idx == 7) {
        cfg.type = SimAnalysisType::RealTime;
    }
    
    cfg.commandText = m_commandLine ? m_commandLine->text() : QString();
    return cfg;
}

bool SimulationPanel::isRealTimeMode() const {
    return m_analysisType && m_analysisType->currentIndex() == 7;
}


void SimulationPanel::onRunSimulation() {
    m_logOutput->clear();
    if (m_issueList) m_issueList->clear();
    m_logBuffer.clear();
    if (m_logFlushTimer) m_logFlushTimer->stop();

    if (m_buildInProgress) {
        m_logOutput->append("Simulation build already in progress...");
        return;
    }

    if (m_waveformViewer && (!m_overlayPreviousRun || !m_overlayPreviousRun->isChecked())) {
        m_waveformViewer->beginBatchUpdate();
        m_waveformViewer->clear();
    }

    const int idx = m_analysisType->currentIndex();
    if (idx == 6) { // Real-time
        int interval = m_param1->text().toInt();
        if (interval < 10) interval = 10;
        SimManager::instance().runRealTime(m_scene, m_netManager, interval);
        return;
    }

    // Update simulation command directive on the schematic
    updateSchematicDirective();
    m_isSimInitiator = true; 

    m_buildInProgress = true;
    m_logOutput->append("Building netlist in background...");

    const double tStop = parseValue(m_param2->text(), 10e-3);
    const double tStep = parseValue(m_param1->text(), 1e-6);
    const QString fStartText = m_param1->text().trimmed();
    const QString fStopText = m_param2->text().trimmed();
    const QString ptsText = m_param3->text().trimmed();
    const QString rfPort1Text = m_param4->text().trimmed();
    const QString rfPort2Text = m_param5->text().trimmed();
    const QString rfZ0Text = m_param6->text().trimmed().isEmpty() ? "50" : m_param6->text().trimmed();
    const QString projectDir = m_projectDir;
    const QJsonObject snapshot = SchematicFileIO::serializeSceneToJson(m_scene, "A4");

    auto* watcher = new QFutureWatcher<SimBuildResult>(this);
    connect(watcher, &QFutureWatcher<SimBuildResult>::finished, this, [this, watcher, idx]() {
        const SimBuildResult result = watcher->result();
        watcher->deleteLater();
        m_buildInProgress = false;

        if (!result.ok) {
            const QString msg = "Simulation build failed: " + result.error;
            m_logOutput->append(msg);
            appendIssueItem(msg);
            return;
        }

        for (const QString& msg : result.diagnostics) {
            const QString line = QString("[Preflight] %1").arg(msg);
            m_logOutput->append(line);
            appendIssueItem(line);
        }

        m_currentNetlist = result.netlist;
        for (const auto& sig : m_currentNetlist.autoProbes()) {
            addProbe(QString::fromStdString(sig));
        }

        if (m_waveformViewer) {
            m_waveformViewer->endBatchUpdate();
            m_waveformViewer->updatePlot(true);
        }

        // Initialize real-time series tracking if needed
        m_realTimeSeries.clear();
        m_realTimePointCounter = 0;
        m_acceptRealTimeStream = (idx == 0);

        SimManager::instance().runNetlistText(result.netlistText);
    });

    watcher->setFuture(QtConcurrent::run([snapshot, projectDir, idx, tStop, tStep, fStartText, fStopText, ptsText, rfPort1Text, rfPort2Text, rfZ0Text]() {
        SimBuildResult result;
        QGraphicsScene tempScene;
        QString error;
        if (!SchematicFileIO::loadSchematicFromJson(&tempScene, snapshot, &error)) {
            result.error = error.isEmpty() ? "Failed to load schematic snapshot" : error;
            return result;
        }

        NetManager netMgr;
        netMgr.updateNets(&tempScene);

        SimNetlist preflight = SimSchematicBridge::buildNetlist(&tempScene, &netMgr);
        for (const auto& d : preflight.diagnostics()) {
            result.diagnostics.append(QString::fromStdString(d));
        }

        if (idx == 2) { // AC Sweep
            for (QGraphicsItem* gi : tempScene.items()) {
                auto* vsrc = dynamic_cast<VoltageSourceItem*>(gi);
                if (!vsrc) continue;
                if (vsrc->excludeFromSimulation()) continue;
                const QString ac = vsrc->acAmplitude().trimmed();
                bool missing = ac.isEmpty();
                if (!missing) {
                    double mag = 0.0;
                    if (SimValueParser::parseSpiceNumber(ac, mag) && qFuzzyIsNull(mag)) {
                        missing = true;
                    }
                }
                if (missing) {
                    const QString ref = vsrc->reference().isEmpty()
                        ? (vsrc->referencePrefix() + "?")
                        : vsrc->reference();
                    result.diagnostics.append(
                        QString("[error] AC analysis requires AC amplitude for %1").arg(ref)
                    );
                }
            }
        }

        SpiceNetlistGenerator::SimulationParams params;
        if (idx == 0) {
            params.type = SpiceNetlistGenerator::Transient;
            params.start = "0";
            params.stop = QString::number(tStop);
            params.step = QString::number(tStep);
        } else if (idx == 1) {
            params.type = SpiceNetlistGenerator::OP;
        } else if (idx == 2) {
            params.type = SpiceNetlistGenerator::AC;
            params.start = fStartText.isEmpty() ? "10" : fStartText;
            params.stop = fStopText.isEmpty() ? "1Meg" : fStopText;
            params.step = ptsText.isEmpty() ? "10" : ptsText;
        } else if (idx == 3) {
            params.type = SpiceNetlistGenerator::SParameter;
            params.start = fStartText.isEmpty() ? "10" : fStartText;
            params.stop = fStopText.isEmpty() ? "1Meg" : fStopText;
            params.step = ptsText.isEmpty() ? "10" : ptsText;
            params.rfPort1Source = rfPort1Text;
            params.rfPort2Node = rfPort2Text;
            params.rfZ0 = rfZ0Text;
        } else {
            params.type = SpiceNetlistGenerator::OP;
        }

        result.netlistText = SpiceNetlistGenerator::generate(&tempScene, projectDir, &netMgr, params);
        result.netlist = preflight;
        result.ok = true;
        return result;
    }));
}

void SimulationPanel::onLogReceived(const QString& msg) {
    if (msg.trimmed().isEmpty()) return;
    m_logBuffer.append(msg);
    if (m_logFlushTimer && !m_logFlushTimer->isActive()) {
        m_logFlushTimer->start();
    }
}

void SimulationPanel::appendIssueItem(const QString& msg) {
    if (!m_issueList) return;

    const auto target = SimSchematicBridge::extractDiagnosticTarget(msg);
    QString targetType;
    if (target.type == SimSchematicBridge::DiagnosticTarget::Type::Component) {
        targetType = "component";
    } else if (target.type == SimSchematicBridge::DiagnosticTarget::Type::Net) {
        targetType = "net";
    }

    const bool isIssue =
        msg.contains("Error", Qt::CaseInsensitive) ||
        msg.contains("Warn", Qt::CaseInsensitive) ||
        msg.contains("Fail", Qt::CaseInsensitive) ||
        msg.contains("[Diag]", Qt::CaseInsensitive) ||
        msg.contains("[Fix]", Qt::CaseInsensitive) ||
        msg.contains("[Preflight]", Qt::CaseInsensitive);

    if (!isIssue && targetType.isEmpty()) return;

    QListWidgetItem* item = new QListWidgetItem(msg);
    if (!targetType.isEmpty() && !target.id.trimmed().isEmpty()) {
        item->setData(Qt::UserRole + 1, targetType);
        item->setData(Qt::UserRole + 2, target.id.trimmed());
        item->setToolTip(QString("Navigate to %1: %2").arg(targetType, target.id));
    }

    if (msg.contains("Error", Qt::CaseInsensitive) || msg.contains("[error]", Qt::CaseInsensitive)) {
        item->setForeground(QColor("#ff3333"));
    } else if (msg.contains("Warn", Qt::CaseInsensitive) || msg.contains("[warn]", Qt::CaseInsensitive)) {
        item->setForeground(QColor("#ffcc00"));
    } else {
        item->setForeground(QColor("#cccccc"));
    }

    m_issueList->addItem(item);
}

void SimulationPanel::onSimulationFinished() {
    qDebug() << "SimulationPanel::onSimulationFinished() called";
    m_logOutput->append("\nSimulation finished (Ngspice).");
    
    if (!m_lastNetlistPath.isEmpty()) {
        QString rawPath = m_lastNetlistPath;
        rawPath.replace(".cir", ".raw");
        plotResultsFromRaw(rawPath);
    }
}

void SimulationPanel::plotResultsFromRaw(const QString& path) {
    qDebug() << "SimulationPanel: Plotting results from raw file:" << path;
    RawData rawData;
    QString error;
    if (!RawDataParser::loadRawAscii(path, &rawData, &error)) {
        m_logOutput->append("Error: " + error);
        return;
    }

    SimResults results = rawData.toSimResults();
    plotBuiltinResults(results);
    evaluateMeasStatements(results);
}

QString SimulationPanel::generateSpiceNetlist() {
    SpiceNetlistGenerator::SimulationParams params;
    int typeIndex = m_analysisType->currentIndex();
    
    if (typeIndex == 0) {
        params.type = SpiceNetlistGenerator::Transient;
        params.stop = m_param2->text();
        params.step = m_param1->text();
    } else if (typeIndex == 1) {
        params.type = SpiceNetlistGenerator::OP;
    } else if (typeIndex == 2) {
        params.type = SpiceNetlistGenerator::AC;
        params.start = m_param1->text();
        params.stop = m_param2->text();
        params.step = m_param3->text(); 
    } else {
        params.type = SpiceNetlistGenerator::Transient; 
        params.stop = "10m";
        params.step = "100u";
    }

    return SpiceNetlistGenerator::generate(m_scene, m_projectDir, m_netManager, params);
}

void SimulationPanel::onSimResultsReady(const SimResults& results) {
    m_acceptRealTimeStream = false; // Stop accepting real-time data immediately
    m_isSimInitiator = false;      // This panel was the initiator, but the simulation is now done.
    if (!results.isSchemaCompatible()) {
        m_logOutput->append(QString("Unsupported simulator results schema v%1 (expected v%2).")
                            .arg(results.schemaVersion)
                            .arg(SimResults::kSchemaVersion));
        return;
    }

    SimResults effectiveResults = results;
    appendDerivedPowerWaveforms(effectiveResults);

    if (m_hasLastResults) {
        m_previousResults = std::move(m_lastResults);
    }
    m_lastResults = effectiveResults;
    m_hasLastResults = true;

    // ── Timeline Initialization ──────────────────────────────────────
    bool isTransient = (effectiveResults.analysisType == SimAnalysisType::Transient);
    m_timelineSlider->setEnabled(isTransient);
    if (isTransient) {
        m_timelineSlider->blockSignals(true);
        m_timelineSlider->setValue(1000); // Start at end
        m_timelineSlider->blockSignals(false);
        
        if (!effectiveResults.waveforms.empty() && !effectiveResults.waveforms.front().xData.empty()) {
            m_timelineLabel->setText(QString::number(effectiveResults.waveforms.front().xData.back(), 'g', 4) + " s");
        }
    } else {
        m_timelineLabel->setText("--- s");
    }

    if (m_waveformViewer) {
        m_waveformViewer->beginBatchUpdate();
    }

    for (const auto& diag : effectiveResults.diagnostics) {
        const QString line = QString::fromStdString(diag);
        m_logOutput->append(line);
        appendIssueItem(line);
    }

    appendMeasurementLogBlock(m_logOutput, effectiveResults);

    plotBuiltinResults(effectiveResults);
    evaluateMeasStatements(effectiveResults);

    // --- RF / Smith Chart Update ---
    if (effectiveResults.analysisType == SimAnalysisType::SParameter && m_smithChart) {
        QVector<std::complex<double>> s11Points;
        for (const auto& w : effectiveResults.waveforms) {
            if (w.name == "S11" || w.name == "s11") {
                const size_t n = std::min(w.yData.size(), w.yPhase.size());
                s11Points.reserve(static_cast<int>(n));
                for (size_t i = 0; i < n; ++i) {
                    double mag = w.yData[i];
                    double phaseRad = w.yPhase[i] * M_PI / 180.0;
                    s11Points.append(std::polar(mag, phaseRad));
                }
                break;
            }
        }
        m_smithChart->setData(s11Points);
        if (m_viewTabs && m_rfTab) {
            m_viewTabs->setCurrentWidget(m_rfTab);
        }
    }

    if (m_waveformViewer) {
        m_waveformViewer->endBatchUpdate();
    }
}

void SimulationPanel::showDetailedLog() {
    if (!m_logOutput) return;
    SimulationLogDialog dlg(m_logOutput->toPlainText(), this);
    dlg.exec();
}

void SimulationPanel::evaluateMeasStatements(const SimResults& results) {
    if (!results.measurements.empty()) return;

    const auto& statements = m_currentNetlist.measStatements();
    if (statements.empty()) return;

    const std::string atype = measAnalysisToken(results.analysisType);
    if (atype.empty()) return;

    const std::vector<MeasResult> measResults = SimMeasEvaluator::evaluate(statements, results, atype);
    if (measResults.empty()) return;

    m_logOutput->append("\n--- Measurements (.meas/.mean) ---");
    for (const auto& mr : measResults) {
        const QString name = QString::fromStdString(mr.name);
        if (mr.valid) {
            m_logOutput->append(QString("%1 = %2").arg(name, formatMeasuredNumber(name, mr.value)));
        } else {
            m_logOutput->append(QString("%1 : ERROR (%2)")
                .arg(name, QString::fromStdString(mr.error)));
        }
    }
}

void SimulationPanel::refreshSteppedMeasurementControls(const SimResults& results) {
    if (!m_steppedMeasSeriesCombo || !m_steppedMeasAxisCombo) return;

    const auto grouped = groupSteppedMeasurementEntries(results.measurements);
    QStringList measurementNames;
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
        if (it.value().size() >= 2) measurementNames.append(it.key());
    }
    if (m_viewTabs && m_spectrumTab) {
        const int spectrumIndex = m_viewTabs->indexOf(m_spectrumTab);
        if (spectrumIndex >= 0) {
            m_viewTabs->setTabText(spectrumIndex, measurementNames.isEmpty() ? "FFT Spectrum" : "Stepped Measurements");
        }
    }

    m_steppedMeasSeriesCombo->blockSignals(true);
    m_steppedMeasSeriesCombo->clear();
    m_steppedMeasSeriesCombo->addItems(measurementNames);
    m_steppedMeasSeriesCombo->setEnabled(!measurementNames.isEmpty());
    if (!measurementNames.contains(m_selectedSteppedMeasurement)) {
        m_selectedSteppedMeasurement = measurementNames.isEmpty() ? QString() : measurementNames.first();
    }
    if (!m_selectedSteppedMeasurement.isEmpty()) {
        m_steppedMeasSeriesCombo->setCurrentText(m_selectedSteppedMeasurement);
    }
    m_steppedMeasSeriesCombo->blockSignals(false);

    QStringList axisNames;
    if (!m_selectedSteppedMeasurement.isEmpty() && grouped.contains(m_selectedSteppedMeasurement)) {
        axisNames = availableSweepAxes(grouped.value(m_selectedSteppedMeasurement));
    }
    if (!axisNames.contains("Sweep Point")) axisNames.prepend("Sweep Point");

    m_steppedMeasAxisCombo->blockSignals(true);
    m_steppedMeasAxisCombo->clear();
    m_steppedMeasAxisCombo->addItems(axisNames);
    m_steppedMeasAxisCombo->setEnabled(axisNames.size() > 1 || (axisNames.size() == 1 && axisNames.first() != "Sweep Point"));
    if (!axisNames.contains(m_selectedSteppedAxis)) {
        if (!m_selectedSteppedMeasurement.isEmpty() && grouped.contains(m_selectedSteppedMeasurement)) {
            const SweepAxisSelection selection = chooseSweepAxis(grouped.value(m_selectedSteppedMeasurement));
            m_selectedSteppedAxis = selection.valid ? selection.axisLabel : QString("Sweep Point");
        } else {
            m_selectedSteppedAxis = "Sweep Point";
        }
    }
    if (!m_selectedSteppedAxis.isEmpty()) {
        m_steppedMeasAxisCombo->setCurrentText(m_selectedSteppedAxis);
    }
    m_steppedMeasAxisCombo->blockSignals(false);
}

void SimulationPanel::rebuildSteppedMeasurementPlot(const SimResults& results) {
    if (!m_spectrumChart) return;

    const auto specSeriesList = m_spectrumChart->series();
    for (auto* series : specSeriesList) {
        m_spectrumChart->removeSeries(series);
        series->deleteLater();
    }
    const auto specAxesList = m_spectrumChart->axes();
    for (auto* axis : specAxesList) {
        m_spectrumChart->removeAxis(axis);
        axis->deleteLater();
    }

    const auto grouped = groupSteppedMeasurementEntries(results.measurements);
    const bool showSteppedMeasurementPlot = !m_selectedSteppedMeasurement.isEmpty() && grouped.contains(m_selectedSteppedMeasurement) && grouped.value(m_selectedSteppedMeasurement).size() >= 2;
    if (!showSteppedMeasurementPlot) {
        m_spectrumChart->setTitle("FFT Spectrum Analysis");
        return;
    }

    const QList<MeasurementDisplayEntry> entries = grouped.value(m_selectedSteppedMeasurement);
    m_spectrumChart->setTitle(QString("Stepped .meas Results - %1").arg(m_selectedSteppedMeasurement));

    struct MeasPoint { double x; double y; };
    QVector<MeasPoint> points;
    points.reserve(entries.size());
    const QMap<QString, double> chosenAxisValues = (m_selectedSteppedAxis == "Sweep Point")
        ? QMap<QString, double>()
        : sweepAxisValues(entries, m_selectedSteppedAxis);
    int pointIndex = 1;
    for (const MeasurementDisplayEntry& entry : entries) {
        double x = static_cast<double>(pointIndex++);
        if (chosenAxisValues.contains(entry.stepLabel)) x = chosenAxisValues.value(entry.stepLabel);
        points.append({x, entry.value});
    }
    std::sort(points.begin(), points.end(), [](const MeasPoint& a, const MeasPoint& b) { return a.x < b.x; });

    QLineSeries* series = new QLineSeries();
    series->setName(m_selectedSteppedMeasurement);
    series->setPen(QPen(Qt::red, 1.6));
    series->setPointsVisible(true);
    for (const MeasPoint& point : points) series->append(point.x, point.y);
    m_spectrumChart->addSeries(series);

    QValueAxis* axisX = new QValueAxis();
    axisX->setTitleText(m_selectedSteppedAxis.isEmpty() ? "Sweep Point" : m_selectedSteppedAxis);
    m_spectrumChart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText(measurementYAxisTitle(results, entries.first().fullName));
    m_spectrumChart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
}

void SimulationPanel::updateChartRealTime(const QString& name, double t, double value) {
    if (!m_chart) return;
    
    QLineSeries* series = m_realTimeSeries.value(name, nullptr);
    if (!series) return;
    
    series->append(t, value);
    
    // Throttle axis updates
    if ((++m_realTimePointCounter % 50) == 0) {
        auto axesX = m_chart->axes(Qt::Horizontal);
        auto axesY = m_chart->axes(Qt::Vertical);
        if (!axesX.isEmpty() && !axesY.isEmpty()) {
            auto* ax = qobject_cast<QValueAxis*>(axesX[0]);
            auto* ay = qobject_cast<QValueAxis*>(axesY[0]);
            
            if (t > ax->max()) ax->setMax(t * 1.1);
            if (value > ay->max()) ay->setMax(value > 0 ? value * 1.2 : value * 0.8);
            if (value < ay->min()) ay->setMin(value < 0 ? value * 1.2 : value * 0.8);
            
            // If it's a very small signal or just starting
            if (series->count() < 10 && ay->max() - ay->min() < 1e-9) {
                ay->setRange(value - 1.0, value + 1.0);
            }
        }
    }
}




void SimulationPanel::onRealTimeDataBatchReceived(const std::vector<double>& times, const std::vector<std::vector<double>>& values, const QStringList& names) {
    if (times.empty()) return;
    if (!m_isSimInitiator) return;
    qDebug() << "SimulationPanel: onRealTimeDataBatchReceived batchSize=" << times.size() << "firstT=" << times.front() << "lastT=" << times.back() << "numVars=" << names.size();
    if (!m_acceptRealTimeStream) return;
    if (!m_waveformViewer) return;
    if (times.empty() || values.empty() || names.empty()) return;

    qDebug() << "SimulationPanel: Received real-time batch. Times:" << times.size() << "Names:" << names.size() << "Last T:" << times.back();

    // 1. Update Probes / Schematic Labels (Live)
    // We only use the VERY LAST point for the schematic probes to save CPU.
    if (!times.empty()) {
        double lastT = times.back();
        const std::vector<double>& lastVals = values.back();
        QMap<QString, double> nodeVoltages;
        QMap<QString, double> currents;

        for (int i = 0; i < names.size(); ++i) {
            if (i >= static_cast<int>(lastVals.size())) break;
            QString name = names[i];
            double val = lastVals[i];
            
            if (name.startsWith("V(")) {
                QString node = name.mid(2, name.length() - 3);
                nodeVoltages[node] = val;
            } else if (name.toLower().endsWith("#branch") || name.startsWith("i(")) {
                currents[name] = val;
            }
        }
        emit timeSnapshotReady(lastT, nodeVoltages, currents);
    }

    // 2. Update Waveform Viewer and Scope Chart
    // Decimate for performance: if many points arrived, only pick a subset for the chart.
    // However, for the WaveformViewer memory buffer, we might want more.
    for (int i = 0; i < names.size(); ++i) {
        if (i >= static_cast<int>(values[0].size())) break;
        
        QString name = names[i];
        std::vector<double> signalValues;
        signalValues.reserve(times.size());
        for (const auto& row : values) {
            if (static_cast<size_t>(i) < row.size()) signalValues.push_back(row[i]);
            else signalValues.push_back(0.0);
        }

        m_waveformViewer->appendPoints(name, times, signalValues);

        // Update preview chart
        if (m_chart && !m_realTimeSeries.isEmpty()) {
            QLineSeries* series = m_realTimeSeries.value(name, nullptr);
            if (series) {
                // High-performance check: only append to the preview chart if it's currently visible
                // and has a reasonable number of points. In a future update, we should replace 
                // this with a more efficient custom-drawn widget for the preview as well.
                
                const int step = std::max(1, static_cast<int>(times.size()) / 50); 
                
                QList<QPointF> points;
                points.reserve(times.size() / step + 2);
                for(size_t k=0; k<times.size(); k += step) {
                    points.append(QPointF(times[k], signalValues[k]));
                }
                if (times.size() > 0 && (times.size() - 1) % step != 0) {
                     points.append(QPointF(times.back(), signalValues.back()));
                }

                // If the chart is being cleared or rebuilt (m_acceptRealTimeStream check above),
                // this series pointer might still be technically valid in this call stack, 
                // but we should be extremely careful.
                series->append(points);
                
                // Update axes
                double lastT = times.back();
                double maxVal = -1e9, minVal = 1e9;
                for(double v : signalValues) {
                    if (v > maxVal) maxVal = v;
                    if (v < minVal) minVal = v;
                }

                auto axesX = m_chart->axes(Qt::Horizontal);
                auto axesY = m_chart->axes(Qt::Vertical);
                if (!axesX.isEmpty() && !axesY.isEmpty()) {
                    auto* ax = qobject_cast<QValueAxis*>(axesX[0]);
                    auto* ay = qobject_cast<QValueAxis*>(axesY[0]);
                    if (lastT > ax->max()) ax->setMax(lastT * 1.1);
                    if (maxVal > ay->max()) ay->setMax(maxVal > 0 ? maxVal * 1.2 : maxVal * 0.8);
                    if (minVal < ay->min()) ay->setMin(minVal < 0 ? minVal * 1.2 : minVal * 0.8);
                }
            }
        }
    }
}

void SimulationPanel::onTimelineValueChanged(int value) {
    if (!m_hasLastResults || m_lastResults.waveforms.empty()) return;
    if (m_lastResults.analysisType != SimAnalysisType::Transient) return;
    
    // Find time range
    double tMin = 1e18, tMax = -1e18;
    for (const auto& w : m_lastResults.waveforms) {
        if (w.xData.empty()) continue;
        tMin = std::min(tMin, w.xData.front());
        tMax = std::max(tMax, w.xData.back());
    }
    
    if (tMin >= tMax) return;
    
    double t = tMin + (tMax - tMin) * (value / 1000.0);
    m_timelineLabel->setText(QString::number(t, 'g', 4) + " s");
    
    auto snap = m_lastResults.interpolateAt(t);
    
    QMap<QString, double> nodeVoltages;
    for (auto const& [name, val] : snap.nodeVoltages) nodeVoltages[name.c_str()] = val;
    
    QMap<QString, double> currents;
    for (auto const& [name, val] : snap.branchCurrents) currents[name.c_str()] = val;
    
    emit timeSnapshotReady(t, nodeVoltages, currents);
}

void SimulationPanel::plotBuiltinResults(const SimResults& results) {
    // Disable real-time data while clearing to avoid race conditions
    m_acceptRealTimeStream = false;
    if (m_signalList) {
        for (int i = 0; i < m_signalList->count(); ++i) {
            if (m_signalList->item(i)->checkState() == Qt::Checked) {
                m_persistentCheckedSignals.insert(m_signalList->item(i)->text());
            } else {
                m_persistentCheckedSignals.remove(m_signalList->item(i)->text());
            }
        }
    }
    bool hadChecks = !m_persistentCheckedSignals.isEmpty();
    qDebug() << "[CRASH_TRACE] plotBuiltinResults START - persistentChecks:" << m_persistentCheckedSignals.size();

    m_realTimeSeries.clear();

    if (m_waveformViewer) {
        m_waveformViewer->beginBatchUpdate();
        m_waveformViewer->clear();
    }
    if (m_logicAnalyzer) m_logicAnalyzer->clear();
    if (m_voltmeter) m_voltmeter->clear();
    if (m_ammeter) m_ammeter->clear();
    if (m_wattmeter) m_wattmeter->clear();
    if (m_freqCounter) m_freqCounter->clear();
    if (m_logicProbe) m_logicProbe->clear();

    int logicCh = 0;
    if (m_logicAnalyzer) m_logicAnalyzer->beginBatchUpdate();

    QSet<QString> currentWaveNames;
    
    // Safer clearing of series and axes
    const auto seriesList = m_chart->series();
    for (auto* series : seriesList) {
        m_chart->removeSeries(series);
        series->deleteLater();
    }
    
    const auto axesList = m_chart->axes();
    for (auto* axis : axesList) {
        m_chart->removeAxis(axis);
        axis->deleteLater();
    }
    
    const auto specSeriesList = m_spectrumChart->series();
    for (auto* series : specSeriesList) {
        m_spectrumChart->removeSeries(series);
        series->deleteLater();
    }
    
    const auto specAxesList = m_spectrumChart->axes();
    for (auto* axis : specAxesList) {
        m_spectrumChart->removeAxis(axis);
        axis->deleteLater();
    }

    m_signalList->blockSignals(true);
    m_signalList->clear();
    if (m_measurementsTable) m_measurementsTable->setRowCount(0);

    if (results.waveforms.empty()) {
        refreshSteppedMeasurementControls(results);
        rebuildSteppedMeasurementPlot(results);
        if (!results.sensitivities.empty()) {
            m_logOutput->append("\n--- Sensitivity Analysis ---");
            for (const auto& [name, val] : results.sensitivities) {
                m_logOutput->append(QString("dTarget/d(%1) = %2").arg(QString::fromStdString(name)).arg(val));
            }
        } else {
            m_logOutput->append("\n--- DC Operating Point ---");
            for (const auto& [name, val] : results.nodeVoltages) {
                m_logOutput->append(QString("V(%1) = %2 V").arg(QString::fromStdString(name)).arg(val));
            }
        }
        m_signalList->blockSignals(false);
        return;
    }

    m_chart->legend()->hide();

    int analysisIdx = m_analysisType->currentIndex();
    if (m_waveformViewer) {
        m_waveformViewer->setAcMode(analysisIdx == 2);
    }
    QAbstractAxis* axisX = nullptr;
    QAbstractAxis* axisYBase = nullptr;
    QValueAxis* axisYPhase = nullptr;

    auto formatValueSI = [](double val) {
        const double absVal = std::abs(val);
        if (absVal < 1e-18) return QString("0");
        static const struct { double mult; const char* sym; } suffixes[] = {
            {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"},
            {1.0, ""},
            {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}
        };
        for (const auto& s : suffixes) {
            if (absVal >= s.mult * 0.999) {
                QString num = QString::number(val / s.mult, 'f', 2).remove(QRegularExpression("\\.?0+$"));
                return num + s.sym;
            }
        }
        return QString::number(val, 'g', 4);
    };

    auto detectYUnit = [&]() {
        for (const auto& w : results.waveforms) {
            const QString n = QString::fromStdString(w.name).trimmed();
            if (n.startsWith("I(", Qt::CaseInsensitive)) return QString("A");
            if (n.startsWith("V(", Qt::CaseInsensitive)) return QString("V");
        }
        return QString("V");
    };

    auto buildSIAxis = [&](double minVal, double maxVal, const QString& title) -> QCategoryAxis* {
        auto* axis = new QCategoryAxis();
        axis->setTitleText(title);
        if (minVal == maxVal) {
            minVal -= 1.0;
            maxVal += 1.0;
        }
        axis->setRange(minVal, maxVal);

        const int ticks = 6;
        const double step = (maxVal - minVal) / (ticks - 1);
        for (int i = 0; i < ticks; ++i) {
            const double v = minVal + step * i;
            axis->append(formatValueSI(v), v);
        }
        axis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
        return axis;
    };
    
    if (analysisIdx == 2) { // AC Sweep / Bode Plot
        auto* logX = new QLogValueAxis();
        logX->setTitleText("Frequency (Hz)");
        logX->setBase(10.0);
        logX->setLabelFormat("%.0e");
        axisX = logX;

        axisYPhase = new QValueAxis();
        axisYPhase->setTitleText("Phase (Deg)");
        axisYPhase->setRange(-180, 180);
        axisYPhase->setLabelFormat("%d");
        axisYPhase->setGridLineVisible(false);
        m_chart->addAxis(axisYPhase, Qt::AlignRight);
    } else {
        if (analysisIdx == 3) {
            auto* valX = new QValueAxis();
            valX->setTitleText("Run Number");
            axisX = valX;
        } else {
            double minX = 0.0, maxX = 0.0;
            bool firstX = true;
            for (const auto& w : results.waveforms) {
                if (w.xData.empty()) continue;
                const double lo = *std::min_element(w.xData.begin(), w.xData.end());
                const double hi = *std::max_element(w.xData.begin(), w.xData.end());
                if (firstX) { minX = lo; maxX = hi; firstX = false; }
                else { minX = std::min(minX, lo); maxX = std::max(maxX, hi); }
            }
            axisX = buildSIAxis(minX, maxX, axisLabelFromSchema(results.xAxisName));
        }
    }
    
    axisX->setGridLinePen(QPen(QColor("#d0d0d0"), 1, Qt::DotLine));
    m_chart->addAxis(axisX, Qt::AlignBottom);

    if (analysisIdx == 2) {
        QValueAxis* axisY = new QValueAxis();
        axisY->setTitleText("Magnitude (dB)");
        axisY->setGridLinePen(QPen(QColor("#d0d0d0"), 1, Qt::DotLine));
        m_chart->addAxis(axisY, Qt::AlignLeft);
        axisYBase = axisY;
    } else {
        double minY = 0.0, maxY = 0.0;
        bool firstY = true;
        for (const auto& w : results.waveforms) {
            if (w.yData.empty()) continue;
            const double lo = *std::min_element(w.yData.begin(), w.yData.end());
            const double hi = *std::max_element(w.yData.begin(), w.yData.end());
            if (firstY) { minY = lo; maxY = hi; firstY = false; }
            else { minY = std::min(minY, lo); maxY = std::max(maxY, hi); }
        }
        const QString unit = detectYUnit();
        const QString title = (analysisIdx == 3) ? axisLabelFromSchema(results.yAxisName)
                                                 : axisLabelFromSchema(results.yAxisName) + " (" + unit + ")";
        auto* axisY = buildSIAxis(minY, maxY, title);
        axisY->setGridLinePen(QPen(QColor("#d0d0d0"), 1, Qt::DotLine));
        m_chart->addAxis(axisY, Qt::AlignLeft);
        axisYBase = axisY;
    }

    const bool showSteppedMeasurementPlot = hasPlottableSteppedMeasurements(results.measurements);

    const QList<QColor> colors = {Qt::red, Qt::blue, QColor("#00aa00"), Qt::magenta, Qt::darkCyan};
    int colorIdx = 0;
    
    for (const auto& wave : results.waveforms) {
        QLineSeries* series = new QLineSeries();
        series->setName(QString::fromStdString(wave.name));
        series->setPen(QPen(colors[colorIdx % colors.size()], 1.5));
        
        QLineSeries* phaseSeries = nullptr;
        if (analysisIdx == 2 && !wave.yPhase.empty()) {
            phaseSeries = new QLineSeries();
            phaseSeries->setName(series->name() + " (Phase)");
            QPen phasePen = series->pen();
            phasePen.setStyle(Qt::DashLine);
            phasePen.setWidthF(1.0);
            phaseSeries->setPen(phasePen);
        }

        const int targetPoints = 4000;
        
        if (analysisIdx == 2) {
            // AC Magnitude in dB
            std::vector<double> dbData;
            dbData.reserve(wave.yData.size());
            for (double v : wave.yData) dbData.push_back(20.0 * std::log10(std::max(v, 1e-15)));
            series->replace(decimateMinMaxBuckets(wave.xData, dbData, targetPoints));
            
            if (phaseSeries) {
                phaseSeries->replace(decimateMinMaxBuckets(wave.xData, wave.yPhase, targetPoints));
            }
        } else {
            series->replace(decimateMinMaxBuckets(wave.xData, wave.yData, targetPoints));
        }

        m_chart->addSeries(series);
        series->attachAxis(axisX);
        series->attachAxis(axisYBase);

        if (m_waveformViewer) {
            if (analysisIdx == 2 && !wave.yPhase.empty()) {
                m_waveformViewer->addSignal(QString::fromStdString(wave.name),
                                            QVector<double>(wave.xData.begin(), wave.xData.end()),
                                            QVector<double>(wave.yData.begin(), wave.yData.end()),
                                            QVector<double>(wave.yPhase.begin(), wave.yPhase.end()));
            } else {
                m_waveformViewer->addSignal(QString::fromStdString(wave.name),
                                            QVector<double>(wave.xData.begin(), wave.xData.end()),
                                            QVector<double>(wave.yData.begin(), wave.yData.end()));
            }
            
            // Restore checked state
            const QString waveName = QString::fromStdString(wave.name);
            bool shouldCheck = m_persistentCheckedSignals.contains(waveName);
            
            // Try normalized matching
            if (!shouldCheck) {
                QString norm = waveName.toUpper();
                for (const QString& s : m_persistentCheckedSignals) {
                    if (s.toUpper() == norm) { shouldCheck = true; break; }
                }
            }

            // Robust check: if Net2 is checked, V(Net2) should also be checked
            if (!shouldCheck && (waveName.startsWith("V(", Qt::CaseInsensitive) || waveName.startsWith("v(")) && waveName.endsWith(")")) {
                QString core = waveName.mid(2, waveName.length() - 3);
                for (const QString& s : m_persistentCheckedSignals) {
                    if (s.compare(core, Qt::CaseInsensitive) == 0) { shouldCheck = true; break; }
                }
            }

            // Optional: check "OUT" by default only if NEVER ANY signals were probed in this session
            if (!shouldCheck && !hadChecks && (waveName.compare("V(OUT)", Qt::CaseInsensitive) == 0 || waveName.compare("OUT", Qt::CaseInsensitive) == 0)) {
                shouldCheck = true;
            }
            
            if (shouldCheck) {
                series->show();
                m_waveformViewer->setSignalChecked(waveName, true);
            }

            // Update Signal List Widget
            QListWidgetItem* item = new QListWidgetItem(waveName);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
            item->setCheckState(shouldCheck ? Qt::Checked : Qt::Unchecked);
            item->setForeground(series->pen().color());
            m_signalList->addItem(item);
        }
        
        if (m_logicAnalyzer && logicCh < 8) {
            bool looksDigital = true;
            for(double v : wave.yData) {
                if (std::abs(v) > 0.5 && std::abs(v - 5.0) > 0.5 && std::abs(v - 3.3) > 0.5) {
                    looksDigital = false;
                    break;
                }
            }
            if (looksDigital) {
                const int logicStride = sampleStride(wave.xData.size(), 2500);
                QVector<QPointF> logicPoints;
                logicPoints.reserve(static_cast<int>((wave.xData.size() + static_cast<size_t>(logicStride) - 1) / static_cast<size_t>(logicStride)));
                for (size_t i = 0; i < wave.xData.size(); i += static_cast<size_t>(logicStride)) {
                    logicPoints.append(QPointF(wave.xData[i], wave.yData[i] > 2.0 ? 1.0 : 0.0));
                }
                m_logicAnalyzer->setChannelData(QString::fromStdString(wave.name), logicPoints);
                logicCh++;
            }
        }

        if (phaseSeries) {
            m_chart->addSeries(phaseSeries);
            phaseSeries->attachAxis(axisX);
            phaseSeries->attachAxis(axisYPhase);
        }

        colorIdx++;
        
        const QString waveName = QString::fromStdString(wave.name);
        currentWaveNames.insert(waveName);

        double minVal = 0.0, maxVal = 0.0, avgVal = 0.0;
        if (!wave.yData.empty()) {
            auto [minIt, maxIt] = std::minmax_element(wave.yData.begin(), wave.yData.end());
            minVal = *minIt;
            maxVal = *maxIt;
            double sum = 0.0;
            for (double v : wave.yData) sum += v;
            avgVal = sum / static_cast<double>(wave.yData.size());
        }

        if (!showSteppedMeasurementPlot && analysisIdx == 0 && wave.yData.size() >= 64) {
            int nfft = 1024;
            std::vector<double> resampled = SimMath::resample(wave.xData, wave.yData, nfft);
            std::vector<std::complex<double>> complexIn(nfft);
            for(int i=0; i<nfft; ++i) complexIn[i] = resampled[i];
            auto complexOut = SimMath::fft(complexIn);
            QLineSeries* specSeries = new QLineSeries();
            specSeries->setPen(series->pen());
            double sampleRate = 1.0 / ( (wave.xData.back() - wave.xData.front()) / (wave.xData.size()-1) );
            for (int i = 0; i < nfft / 2; ++i) {
                double freq = i * sampleRate / nfft;
                double mag = 2.0 * std::abs(complexOut[i]) / nfft;
                if (i == 0) mag /= 2.0;
                specSeries->append(freq, 20.0 * std::log10(std::max(mag, 1e-9)));
            }
            m_spectrumChart->addSeries(specSeries);
        }


        if (m_measurementsTable) {
            int row = m_measurementsTable->rowCount();
            m_measurementsTable->insertRow(row);
            m_measurementsTable->setItem(row, 0, new QTableWidgetItem(series->name()));
            m_measurementsTable->item(row, 0)->setForeground(series->pen().color());
            m_measurementsTable->setItem(row, 1, new QTableWidgetItem(QString::number(maxVal - minVal, 'f', 3)));
            m_measurementsTable->setItem(row, 2, new QTableWidgetItem(QString::number(avgVal, 'f', 3)));
            double sumSq = 0.0;
            for (double v : wave.yData) sumSq += v * v;
            const double rmsVal = wave.yData.empty() ? 0.0 : std::sqrt(sumSq / static_cast<double>(wave.yData.size()));
            m_measurementsTable->setItem(row, 3, new QTableWidgetItem(QString::number(rmsVal, 'f', 3)));
            const double freqHz = estimateFrequency(wave);
            QString freqStr = (freqHz > 0.0) ? QString("%1 Hz").arg(QString::number(freqHz, 'g', 4)) : "-";
            if (analysisIdx == 0) {
                const double fftPeak = estimateFftPeakHz(wave);
                if (fftPeak > 0.0) freqStr = QString("%1 (FFT %2)").arg(freqStr == "-" ? QString("~") : freqStr).arg(QString::number(fftPeak, 'g', 4));
            }
            m_measurementsTable->setItem(row, 4, new QTableWidgetItem(freqStr));
            QString deltaStr = "-";
            if (wave.xData.size() >= 2 && wave.yData.size() >= 2) {
                const double x0 = wave.xData.front();
                const double x1 = wave.xData.back();
                const double xa = x0 + (x1 - x0) * m_cursorAFrac;
                const double xb = x0 + (x1 - x0) * m_cursorBFrac;
                deltaStr = QString::number(sampleAtX(wave, xb) - sampleAtX(wave, xa), 'f', 3);
            }
            m_measurementsTable->setItem(row, 5, new QTableWidgetItem(deltaStr));
        }
    }

    // Finalize the waveform viewer AFTER all waves have been added
    if (m_waveformViewer) m_waveformViewer->endBatchUpdate();

    if (m_overlayPreviousRun && m_overlayPreviousRun->isChecked() && m_hasPreviousResults) {
        for (const auto& wave : m_previousResults.waveforms) {
            const QString prevName = QString::fromStdString(wave.name);
            if (currentWaveNames.contains(prevName)) {
                QLineSeries* prevSeries = new QLineSeries();
                prevSeries->setName("Prev: " + prevName);
                prevSeries->setPen(QPen(QColor("#94a3b8"), 1.1, Qt::DashLine));
                prevSeries->replace(decimateMinMaxBuckets(wave.xData, wave.yData, 3000));
                m_chart->addSeries(prevSeries);
                prevSeries->attachAxis(axisX);
                prevSeries->attachAxis(axisYBase);
            }
        }
    }

    refreshSteppedMeasurementControls(results);
    if (showSteppedMeasurementPlot) {
        rebuildSteppedMeasurementPlot(results);
    } else if (!m_spectrumChart->series().isEmpty()) {
        QValueAxis* axisFreq = new QValueAxis();
        axisFreq->setTitleText("Frequency (Hz)");
        m_spectrumChart->addAxis(axisFreq, Qt::AlignBottom);
        for(auto* s : m_spectrumChart->series()) s->attachAxis(axisFreq);
        QValueAxis* axisMag = new QValueAxis();
        axisMag->setTitleText("Magnitude (dB)");
        m_spectrumChart->addAxis(axisMag, Qt::AlignLeft);
        for(auto* s : m_spectrumChart->series()) s->attachAxis(axisMag);
    }

    if (m_measurementsTable && !results.measurements.empty()) {
        const QList<MeasurementDisplayEntry> entries = buildMeasurementDisplayEntries(results.measurements);
        const int separatorRow = m_measurementsTable->rowCount();
        m_measurementsTable->insertRow(separatorRow);
        for (int col = 0; col < m_measurementsTable->columnCount(); ++col) {
            auto* item = new QTableWidgetItem(col == 0 ? ".meas results" : QString());
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            item->setForeground(QColor("#fbbf24"));
            m_measurementsTable->setItem(separatorRow, col, item);
        }

        for (const MeasurementDisplayEntry& entry : entries) {
            const int row = m_measurementsTable->rowCount();
            m_measurementsTable->insertRow(row);
            m_measurementsTable->setItem(row, 0, new QTableWidgetItem(entry.baseName));
            m_measurementsTable->setItem(row, 1, new QTableWidgetItem(formatMeasuredNumber(results, entry.fullName, entry.baseName, entry.value)));
            m_measurementsTable->setItem(row, 2, new QTableWidgetItem("-"));
            m_measurementsTable->setItem(row, 3, new QTableWidgetItem("-"));
            m_measurementsTable->setItem(row, 4, new QTableWidgetItem(entry.stepLabel.isEmpty() ? "-" : entry.stepLabel));
            m_measurementsTable->setItem(row, 5, new QTableWidgetItem("-"));
        }
    }

    if (m_waveformViewer) {
        m_waveformViewer->zoomFit();
    }
    if (m_logicAnalyzer) m_logicAnalyzer->endBatchUpdate();

    updateVirtualMeters(results);
    emit resultsReady(results);

    m_signalList->blockSignals(false);
    m_isSimInitiator = false; // Just in case, though onSimResultsReady handles it
    m_realTimeSeries.clear();
}

double SimulationPanel::parseValue(const QString& text, double defaultVal) const {
    double parsed = 0.0;
    if (SimValueParser::parseSpiceNumber(text, parsed)) return parsed;
    return defaultVal;
}

double SimulationPanel::sampleAtX(const SimWaveform& wave, double x) const {
    if (wave.xData.empty() || wave.yData.empty()) return 0.0;
    const size_t n = std::min(wave.xData.size(), wave.yData.size());
    if (n == 1 || x <= wave.xData.front()) return wave.yData.front();
    if (x >= wave.xData[n - 1]) return wave.yData[n - 1];
    auto it = std::lower_bound(wave.xData.begin(), wave.xData.begin() + static_cast<long>(n), x);
    const size_t i1 = static_cast<size_t>(std::distance(wave.xData.begin(), it));
    const size_t i0 = i1 - 1;
    const double x0 = wave.xData[i0], x1 = wave.xData[i1], y0 = wave.yData[i0], y1 = wave.yData[i1];
    if (std::abs(x1 - x0) < 1e-18) return y0;
    return y0 + (x - x0) / (x1 - x0) * (y1 - y0);
}

double SimulationPanel::estimateFrequency(const SimWaveform& wave) const {
    const size_t n = std::min(wave.xData.size(), wave.yData.size());
    if (n < 4) return 0.0;
    int crossings = 0;
    for (size_t i = 1; i < n; ++i) {
        if ((wave.yData[i-1] <= 0.0 && wave.yData[i] > 0.0) || (wave.yData[i-1] >= 0.0 && wave.yData[i] < 0.0)) crossings++;
    }
    const double dt = wave.xData[n - 1] - wave.xData[0];
    if (dt <= 0.0 || crossings < 2) return 0.0;
    return (crossings * 0.5) / dt;
}

double SimulationPanel::estimateFftPeakHz(const SimWaveform& wave) const {
    const size_t n = std::min(wave.xData.size(), wave.yData.size());
    if (n < 64) return 0.0;
    const int nfft = 1024;
    std::vector<double> resampled = SimMath::resample(wave.xData, wave.yData, nfft);
    std::vector<std::complex<double>> complexIn(nfft);
    for (int i = 0; i < nfft; ++i) complexIn[i] = resampled[i];
    auto out = SimMath::fft(complexIn);
    const double totalT = wave.xData.back() - wave.xData.front();
    if (totalT <= 0.0) return 0.0;
    const double sampleRate = static_cast<double>(n) / totalT;
    int bestBin = -1; double bestMag = 0.0;
    for (int i = 1; i < nfft / 2; ++i) {
        if (std::abs(out[i]) > bestMag) { bestMag = std::abs(out[i]); bestBin = i; }
    }
    return bestBin <= 0 ? 0.0 : bestBin * sampleRate / nfft;
}

QString SimulationPanel::buildGeneratorExpression() const {
    const QString type = m_generatorType ? m_generatorType->currentText() : "DC";
    if (type == "DC") return QString("DC %1").arg(m_genParam1->text().trimmed());
    if (type == "SIN") return QString("SINE(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    if (type == "PULSE") return QString("PULSE(%1 %2 %3 %4 %5 %6 %7)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed(), m_genParam6->text().trimmed(), "1m");
    if (type == "EXP") return QString("EXP(%1 %2 %3 %4 %5 %6)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed(), m_genParam6->text().trimmed());
    if (type == "SFFM") return QString("SFFM(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    if (type == "PWL") {
        QStringList pairs;
        if (!m_pwlPoints.isEmpty()) { for (const auto& p : m_pwlPoints) pairs << p.first.trimmed() << p.second.trimmed(); }
        else pairs << m_genParam1->text().trimmed() << m_genParam2->text().trimmed() << m_genParam3->text().trimmed() << m_genParam4->text().trimmed() << m_genParam5->text().trimmed() << m_genParam6->text().trimmed();
        return QString("PWL(%1)").arg(pairs.join(' '));
    }
    if (type == "AM") return QString("AM(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    if (type == "FM") return QString("FM(%1 %2 %3 %4 %5)").arg(m_genParam1->text().trimmed(), m_genParam2->text().trimmed(), m_genParam3->text().trimmed(), m_genParam4->text().trimmed(), m_genParam5->text().trimmed());
    return QString("DC %1").arg(m_genParam1->text().trimmed());
}

QVariantMap SimulationPanel::collectGeneratorConfig() const {
    QVariantMap cfg;
    cfg["type"] = m_generatorType ? m_generatorType->currentText() : "DC";
    cfg["p1"] = m_genParam1->text().trimmed(); cfg["p2"] = m_genParam2->text().trimmed();
    cfg["p3"] = m_genParam3->text().trimmed(); cfg["p4"] = m_genParam4->text().trimmed();
    cfg["p5"] = m_genParam5->text().trimmed(); cfg["p6"] = m_genParam6->text().trimmed();
    cfg["expression"] = buildGeneratorExpression();
    QVariantList points;
    for (const auto& p : m_pwlPoints) { QVariantMap pt; pt["t"] = p.first; pt["v"] = p.second; points.push_back(pt); }
    cfg["pwl_points"] = points;
    return cfg;
}

void SimulationPanel::applyGeneratorConfig(const QVariantMap& cfg) {
    if (m_generatorType) { int idx = m_generatorType->findText(cfg.value("type", "DC").toString()); if (idx >= 0) m_generatorType->setCurrentIndex(idx); }
    m_genParam1->setText(cfg.value("p1").toString()); m_genParam2->setText(cfg.value("p2").toString());
    m_genParam3->setText(cfg.value("p3").toString()); m_genParam4->setText(cfg.value("p4").toString());
    m_genParam5->setText(cfg.value("p5").toString()); m_genParam6->setText(cfg.value("p6").toString());
    m_pwlPoints.clear();
    for (const QVariant& v : cfg.value("pwl_points").toList()) { QVariantMap m = v.toMap(); m_pwlPoints.push_back({m.value("t").toString(), m.value("v").toString()}); }
}

QString SimulationPanel::generatorPresetsPath() const {
    QString b = m_projectDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : m_projectDir;
    QDir(b).mkpath("."); return QDir(b).filePath("generator_presets.json");
}

void SimulationPanel::loadGeneratorLibrary() {
    m_generatorTemplates["Template: DC 5V"] = QVariantMap{{"type", "DC"}, {"p1", "5"}};
    m_generatorTemplates["Template: SIN 1kHz"] = QVariantMap{{"type", "SIN"}, {"p1", "0"}, {"p2", "5"}, {"p3", "1k"}, {"p4", "0"}, {"p5", "0"}};
    m_generatorTemplates["Template: Pulse 0-5V"] = QVariantMap{{"type", "PULSE"}, {"p1", "0"}, {"p2", "5"}, {"p3", "0"}, {"p4", "1u"}, {"p5", "1u"}, {"p6", "500u"}};
    QFile f(generatorPresetsPath());
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        for (const QJsonValue& v : root.value("presets").toArray()) {
            QJsonObject po = v.toObject(); m_userGeneratorPresets[po.value("name").toString()] = po.value("config").toObject().toVariantMap();
        }
    }
    refreshGeneratorPresetCombo();
}

void SimulationPanel::saveUserGeneratorPresets() const {
    QJsonArray arr;
    for (auto it = m_userGeneratorPresets.constBegin(); it != m_userGeneratorPresets.constEnd(); ++it) {
        QJsonObject p; p["name"] = it.key(); p["config"] = QJsonObject::fromVariantMap(it.value()); arr.append(p);
    }
    QJsonObject root; root["presets"] = arr;
    QFile f(generatorPresetsPath()); if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson());
}

void SimulationPanel::refreshGeneratorPresetCombo() {
    if (!m_generatorPresetCombo) return;
    m_generatorPresetCombo->blockSignals(true); m_generatorPresetCombo->clear();
    m_generatorPresetCombo->addItem("Select Template/Preset", "__none__");
    for (auto it = m_generatorTemplates.constBegin(); it != m_generatorTemplates.constEnd(); ++it) m_generatorPresetCombo->addItem(it.key(), "T:" + it.key());
    for (auto it = m_userGeneratorPresets.constBegin(); it != m_userGeneratorPresets.constEnd(); ++it) m_generatorPresetCombo->addItem("Preset: " + it.key(), "U:" + it.key());
    m_generatorPresetCombo->blockSignals(false);
}

void SimulationPanel::seedDefaultPwlPointsIfNeeded() {
    if (!m_pwlPoints.isEmpty()) return;
    m_pwlPoints.push_back({"0", "0"}); m_pwlPoints.push_back({"1m", "5"}); m_pwlPoints.push_back({"2m", "0"});
}

bool SimulationPanel::importPwlCsvFile(const QString& path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return false;
    m_pwlPoints.clear(); QTextStream in(&f);
    while(!in.atEnd()) {
        QStringList p = in.readLine().split(QRegularExpression("\\s*,\\s*|\\s+"), Qt::SkipEmptyParts);
        if (p.size() >= 2) m_pwlPoints.push_back({p[0], p[1]});
    }
    return m_pwlPoints.size() >= 2;
}

bool SimulationPanel::exportPwlCsvFile(const QString& path) const {
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    QTextStream out(&f); out << "time,value\n";
    for (const auto& p : m_pwlPoints) out << p.first << "," << p.second << "\n";
    return true;
}

bool SimulationPanel::exportResultsCsvFile(const QString& path) const {
    if (!m_hasLastResults) return false;
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    QTextStream out(&f); out << "index";
    for (const auto& w : m_lastResults.waveforms) out << "," << QString::fromStdString(w.name) << "_x," << QString::fromStdString(w.name) << "_y";
    out << "\n";
    size_t max = 0; for (const auto& w : m_lastResults.waveforms) max = std::max(max, w.xData.size());
    for (size_t i = 0; i < max; ++i) {
        out << static_cast<qulonglong>(i);
        for (const auto& w : m_lastResults.waveforms) {
            if (i < w.xData.size()) out << "," << w.xData[i] << "," << w.yData[i]; else out << ",,";
        }
        out << "\n";
    }
    return true;
}

bool SimulationPanel::exportResultsJsonFile(const QString& path) const {
    if (!m_hasLastResults) return false;
    QJsonObject root; root["schema"] = m_lastResults.schemaVersion;
    QJsonArray waves;
    for (const auto& w : m_lastResults.waveforms) {
        QJsonObject o; o["name"] = QString::fromStdString(w.name);
        QJsonArray x, y; for(double v : w.xData) x.append(v); for(double v : w.yData) y.append(v);
        o["x"] = x; o["y"] = y; waves.append(o);
    }
    root["waveforms"] = waves;
    QJsonObject measurements;
    for (const auto& [name, value] : m_lastResults.measurements) {
        measurements[QString::fromStdString(name)] = value;
    }
    root["measurements"] = measurements;
    QJsonObject measurementMetadata;
    for (const auto& [name, meta] : m_lastResults.measurementMetadata) {
        QJsonObject item;
        item["quantityLabel"] = QString::fromStdString(meta.quantityLabel);
        item["displayUnit"] = QString::fromStdString(meta.displayUnit);
        measurementMetadata[QString::fromStdString(name)] = item;
    }
    root["measurementMetadata"] = measurementMetadata;
    QJsonArray diagnostics;
    for (const auto& diag : m_lastResults.diagnostics) {
        diagnostics.append(QString::fromStdString(diag));
    }
    root["diagnostics"] = diagnostics;
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson()); return true;
}

bool SimulationPanel::exportResultsReportFile(const QString& path) const {
    if (!m_hasLastResults) return false;
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) return false;
    QTextStream out(&f); out << "# Simulation Report\n\n";
    for (const auto& w : m_lastResults.waveforms) out << "- " << QString::fromStdString(w.name) << ": " << w.xData.size() << " points\n";
    if (!m_lastResults.measurements.empty()) {
        out << "\n## Measurements\n\n";
        for (const MeasurementDisplayEntry& entry : buildMeasurementDisplayEntries(m_lastResults.measurements)) {
            out << "- " << entry.baseName;
            if (!entry.stepLabel.isEmpty()) out << " [" << entry.stepLabel << "]";
            out << ": " << formatMeasuredNumber(m_lastResults, entry.fullName, entry.baseName, entry.value) << "\n";
        }
    }
    if (!m_lastResults.diagnostics.empty()) {
        out << "\n## Diagnostics\n\n";
        for (const auto& diag : m_lastResults.diagnostics) {
            out << "- " << QString::fromStdString(diag) << "\n";
        }
    }
    return true;
}

void SimulationPanel::onGeneratorPresetActivated(int index) {
    if (index < 0 || !m_generatorPresetCombo) return;
    QString tag = m_generatorPresetCombo->itemData(index).toString();
    QVariantMap cfg = tag.startsWith("T:") ? m_generatorTemplates.value(tag.mid(2)) : m_userGeneratorPresets.value(tag.mid(2));
    if (!cfg.isEmpty()) applyGeneratorConfig(cfg);
}

void SimulationPanel::onApplyGeneratorToSelection() {
    if (!m_scene) return;
    QString expr = buildGeneratorExpression();
    int applied = 0;
    for (QGraphicsItem* gi : m_scene->selectedItems()) {
        if (auto* v = dynamic_cast<VoltageSourceItem*>(gi)) {
            v->setValue(expr); v->update(); applied++;
        }
    }
    m_logOutput->append(QString("Applied to %1 sources.").arg(applied));
}

void SimulationPanel::updateVirtualMeters(const SimResults& results) {
    if (m_voltmeter) {
        for (const auto& w : results.waveforms) {
            if (QString::fromStdString(w.name).startsWith("V(") && !w.yData.empty()) {
                m_voltmeter->setReading(QString::fromStdString(w.name), w.yData.back()); break;
            }
        }
    }
}

QWidget* SimulationPanel::getOscilloscopeContainer() const {
    return m_scopeContainer;
}
