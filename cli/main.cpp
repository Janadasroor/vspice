#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QGraphicsScene>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QCryptographicHash>
#include <QPainterPath>
#include <QDirIterator>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QLoggingCategory>
#include <QProcess>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <optional>
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#endif

#include "core/plugins/plugin_manager.h"
// Symbols
#include "symbols/models/symbol_definition.h"
#include "symbols/symbol_library.h"
#include "symbols/kicad_symbol_importer.h"
#include "symbols/ltspice_symbol_importer.h"
// PCB Includes (optional)
#if __has_include("pcb/drc/pcb_drc.h")
#define VIOSPICE_HAS_PCB 1
#include "vioraeda/drc/pcb_drc.h"
#include "vioraeda/factories/pcb_item_registry.h"
#include "vioraeda/io/pcb_file_io.h"
#else
#define VIOSPICE_HAS_PCB 0
#endif

// Schematic Includes
#include "flux/schematic/analysis/schematic_annotator.h"
#include "flux/schematic/analysis/schematic_erc.h"
#include "schematic/analysis/spice_netlist_generator.h"
#include "schematic/items/schematic_item.h"
#include "flux/schematic/factories/schematic_item_registry.h"
#include "flux/schematic/io/schematic_file_io.h"
#include "schematic/io/netlist_generator.h"
#include "flux/schematic/items/wire_item.h"
#include "flux/schematic/editor/schematic_api.h"
#if VIOSPICE_HAS_PCB
#include "vioraeda/editor/pcb_api.h"
#include "vioraeda/io/pcb_file_io.h"
#endif
#include "flux/core/flux_python.h"
#include "schematic/analysis/spice_netlist_generator.h"
#include "core/simulation_manager.h"
#include "simulator/bridge/sim_manager.h"
#include "simulator/core/sim_value_parser.h"

namespace {
std::optional<int> parseTimeoutMs(const QString& value, QString* error) {
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = "Timeout is empty.";
        return std::nullopt;
    }

    const QRegularExpression re(R"(^\s*([0-9]*\.?[0-9]+)\s*(ms|s|m)?\s*$)");
    const QRegularExpressionMatch match = re.match(trimmed);
    if (!match.hasMatch()) {
        if (error) *error = "Invalid timeout format. Use values like 10s or 5000ms.";
        return std::nullopt;
    }

    bool ok = false;
    const double number = match.captured(1).toDouble(&ok);
    if (!ok || number < 0) {
        if (error) *error = "Timeout must be a non-negative number.";
        return std::nullopt;
    }

    const QString unit = match.captured(2);
    double ms = number;
    if (unit == "ms" || unit.isEmpty()) {
        ms = number;
    } else if (unit == "s") {
        ms = number * 1000.0;
    } else if (unit == "m") {
        ms = number * 60000.0;
    }

    if (ms > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error) *error = "Timeout is too large.";
        return std::nullopt;
    }

    return static_cast<int>(ms);
}

static bool parseRangeOption(const QString& value, double* outStart, double* outEnd, QString* error) {
    if (outStart) *outStart = std::numeric_limits<double>::quiet_NaN();
    if (outEnd) *outEnd = std::numeric_limits<double>::quiet_NaN();
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) return true;
    const QStringList parts = trimmed.split(':');
    if (parts.size() != 2) {
        if (error) *error = "Invalid range format. Use t0:t1 (e.g. 1ms:5ms).";
        return false;
    }
    double t0 = 0.0;
    double t1 = 0.0;
    if (!SimValueParser::parseSpiceNumber(parts[0].trimmed(), t0) ||
        !SimValueParser::parseSpiceNumber(parts[1].trimmed(), t1)) {
        if (error) *error = "Invalid range values. Use spice numbers like 1ms:5ms.";
        return false;
    }
    if (outStart) *outStart = t0;
    if (outEnd) *outEnd = t1;
    return true;
}

static QJsonValue sortJsonValue(const QJsonValue& value) {
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        QStringList keys = obj.keys();
        keys.sort(Qt::CaseInsensitive);
        QJsonObject sorted;
        for (const auto& key : keys) {
            sorted.insert(key, sortJsonValue(obj.value(key)));
        }
        return sorted;
    }
    if (value.isArray()) {
        QJsonArray arr;
        const QJsonArray in = value.toArray();
        for (const auto& v : in) arr.append(sortJsonValue(v));
        return arr;
    }
    return value;
}

static void printJsonValue(const QJsonValue& value) {
    const QJsonValue sorted = sortJsonValue(value);
    QJsonDocument doc = sorted.isArray() ? QJsonDocument(sorted.toArray()) : QJsonDocument(sorted.toObject());
    std::cout << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

static void printJsonValueTo(const QJsonValue& value, std::ostream& out) {
    const QJsonValue sorted = sortJsonValue(value);
    QJsonDocument doc = sorted.isArray() ? QJsonDocument(sorted.toArray()) : QJsonDocument(sorted.toObject());
    out << doc.toJson(QJsonDocument::Compact).toStdString() << std::endl;
}

static bool isWarningLine(const QString& msg) {
    const QString trimmed = msg.trimmed();
    if (trimmed.isEmpty()) return false;
    const QString lower = trimmed.toLower();
    return lower.startsWith("warning") || lower.contains(" warning") || lower.contains("warning:");
}

#ifdef Q_OS_UNIX
class ScopedFdSilence {
public:
    explicit ScopedFdSilence(bool enable, bool restoreOnDestroy = true)
        : m_restore(restoreOnDestroy) { if (enable) start(); }
    ~ScopedFdSilence() { if (m_restore) stop(); }
    ScopedFdSilence(const ScopedFdSilence&) = delete;
    ScopedFdSilence& operator=(const ScopedFdSilence&) = delete;
    void release() { m_restore = false; }
private:
    int m_stdoutFd = -1;
    int m_stderrFd = -1;
    int m_nullFd = -1;
    bool m_active = false;
    bool m_restore = true;

    void start() {
        m_nullFd = ::open("/dev/null", O_WRONLY);
        if (m_nullFd < 0) return;
        m_stdoutFd = ::dup(STDOUT_FILENO);
        m_stderrFd = ::dup(STDERR_FILENO);
        if (m_stdoutFd < 0 || m_stderrFd < 0) {
            if (m_stdoutFd >= 0) ::close(m_stdoutFd);
            if (m_stderrFd >= 0) ::close(m_stderrFd);
            ::close(m_nullFd);
            m_stdoutFd = m_stderrFd = m_nullFd = -1;
            return;
        }
        ::dup2(m_nullFd, STDOUT_FILENO);
        ::dup2(m_nullFd, STDERR_FILENO);
        m_active = true;
    }

    void stop() {
        if (!m_active) return;
        std::fflush(stdout);
        std::fflush(stderr);
        ::dup2(m_stdoutFd, STDOUT_FILENO);
        ::dup2(m_stderrFd, STDERR_FILENO);
        ::close(m_stdoutFd);
        ::close(m_stderrFd);
        ::close(m_nullFd);
        m_stdoutFd = m_stderrFd = m_nullFd = -1;
        m_active = false;
    }
};
#else
class ScopedFdSilence {
public:
    explicit ScopedFdSilence(bool, bool = true) {}
    void release() {}
};
#endif

struct RawData {
    QStringList varNames;
    int numVariables = 0;
    int numPoints = 0;
    QVector<double> x;
    QVector<QVector<double>> y;
};

static bool resolveBaseSignalIndex(const RawData& data, const QString& name, int* outIndex, QString* error) {
    if (outIndex) *outIndex = -1;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return true;
    int idx = -1;
    for (int i = 0; i < data.varNames.size(); ++i) {
        if (data.varNames[i].compare(trimmed, Qt::CaseInsensitive) == 0) { idx = i; break; }
    }
    if (idx < 1) {
        if (error) *error = QString("Base signal not found (or invalid): %1").arg(trimmed);
        return false;
    }
    if (outIndex) *outIndex = idx - 1;
    return true;
}

static QVector<int> decimatedIndices(const RawData& data, int baseSignalIndex, int maxPoints, double tStart, double tEnd) {
    const int total = data.x.size();
    QVector<int> out;
    if (total <= 0) return out;
    const bool useRange = !(std::isnan(tStart) || std::isnan(tEnd));
    const double rangeStart = useRange ? qMin(tStart, tEnd) : 0.0;
    const double rangeEnd = useRange ? qMax(tStart, tEnd) : 0.0;

    QVector<int> candidates;
    candidates.reserve(total);
    for (int i = 0; i < total; ++i) {
        if (!useRange || (data.x[i] >= rangeStart && data.x[i] <= rangeEnd)) candidates.push_back(i);
    }
    if (candidates.isEmpty()) return out;

    if (maxPoints <= 0 || candidates.size() <= maxPoints) {
        return candidates;
    }

    const int buckets = qMax(1, maxPoints / 2);
    const int bucketSize = qMax(1, (candidates.size() + buckets - 1) / buckets);
    out.reserve(qMin(maxPoints, total));
    const auto& base = data.y[baseSignalIndex];

    for (int start = 0; start < candidates.size(); start += bucketSize) {
        const int end = qMin(candidates.size(), start + bucketSize);
        int minIdx = candidates[start];
        int maxIdx = candidates[start];
        double minVal = base[minIdx];
        double maxVal = base[maxIdx];
        for (int i = start + 1; i < end; ++i) {
            const int idx = candidates[i];
            const double v = base[idx];
            if (v < minVal) { minVal = v; minIdx = idx; }
            if (v > maxVal) { maxVal = v; maxIdx = idx; }
        }
        if (minVal == maxVal) {
            if (out.isEmpty() || out.back() != minIdx) out.push_back(minIdx);
        } else if (minIdx < maxIdx) {
            if (out.isEmpty() || out.back() != minIdx) out.push_back(minIdx);
            if (out.back() != maxIdx) out.push_back(maxIdx);
        } else {
            if (out.isEmpty() || out.back() != maxIdx) out.push_back(maxIdx);
            if (out.back() != minIdx) out.push_back(minIdx);
        }
        if (out.size() >= maxPoints) break;
    }

    while (out.size() > maxPoints) out.pop_back();
    return out;
}

static QVector<int> filteredIndices(const RawData& data, double tStart, double tEnd) {
    QVector<int> out;
    const bool useRange = !(std::isnan(tStart) || std::isnan(tEnd));
    const double rangeStart = useRange ? qMin(tStart, tEnd) : 0.0;
    const double rangeEnd = useRange ? qMax(tStart, tEnd) : 0.0;
    out.reserve(data.x.size());
    for (int i = 0; i < data.x.size(); ++i) {
        if (!useRange || (data.x[i] >= rangeStart && data.x[i] <= rangeEnd)) out.push_back(i);
    }
    return out;
}

static QJsonObject rawToJson(const RawData& data, const QStringList& signalNames, const QVector<int>& indices, int maxPoints, double tStart, double tEnd, int baseSignalIndex = -1) {
    QJsonObject out;
    QJsonArray xArr;
    int baseSignal = baseSignalIndex;
    if (baseSignal < 0 || baseSignal >= data.y.size()) {
        baseSignal = indices.isEmpty() ? 0 : indices[0];
    }
    const QVector<int> idx = decimatedIndices(data, baseSignal, maxPoints, tStart, tEnd);
    for (int i : idx) xArr.append(data.x[i]);
    out["x"] = xArr;
    QJsonArray sigArr;
    for (int i = 0; i < signalNames.size(); ++i) {
        QJsonObject s;
        s["name"] = signalNames[i];
        QJsonArray vals;
            const auto& vec = data.y[indices[i]];
            for (int k : idx) vals.append(vec[k]);
            s["values"] = vals;
            sigArr.append(s);
    }
    out["signals"] = sigArr;
    return out;
}

static QString rawToCsv(const RawData& data, const QStringList& signalNames, const QVector<int>& indices, int maxPoints, double tStart, double tEnd, int baseSignalIndex = -1) {
    QString out;
    QTextStream stream(&out);
    stream << data.varNames[0];
    for (const auto& sig : signalNames) stream << "," << sig;
    stream << "\n";
    int baseSignal = baseSignalIndex;
    if (baseSignal < 0 || baseSignal >= data.y.size()) {
        baseSignal = indices.isEmpty() ? 0 : indices[0];
    }
    const QVector<int> idx = decimatedIndices(data, baseSignal, maxPoints, tStart, tEnd);
    for (int i : idx) {
        stream << data.x[i];
        for (int j = 0; j < indices.size(); ++j) {
            const auto& vec = data.y[indices[j]];
            if (i < vec.size()) stream << "," << vec[i];
            else stream << ",";
        }
        stream << "\n";
    }
    return out;
}

struct SignalStats {
    QString name;
    double min = 0.0;
    double max = 0.0;
    double avg = 0.0;
    double rms = 0.0;
};

static QVector<SignalStats> computeSignalStats(const RawData& data, const QStringList& signalNames, const QVector<int>& indices, const QVector<int>& sampleIndices) {
    QVector<SignalStats> stats;
    stats.reserve(indices.size());
    for (int i = 0; i < indices.size(); ++i) {
        const auto& vec = data.y[indices[i]];
        if (vec.isEmpty()) continue;
        SignalStats s;
        s.name = signalNames[i];
        double sum = 0.0;
        double sumSq = 0.0;
        bool seeded = false;
        double minVal = 0.0;
        double maxVal = 0.0;
        if (sampleIndices.isEmpty()) {
            for (double v : vec) {
                if (!seeded) { minVal = v; maxVal = v; seeded = true; }
                sum += v;
                sumSq += v * v;
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }
            const int n = vec.size();
            s.min = minVal;
            s.max = maxVal;
            s.avg = sum / n;
            s.rms = std::sqrt(sumSq / n);
        } else {
            for (int idx : sampleIndices) {
                if (idx < 0 || idx >= vec.size()) continue;
                const double v = vec[idx];
                if (!seeded) { minVal = v; maxVal = v; seeded = true; }
                sum += v;
                sumSq += v * v;
                if (v < minVal) minVal = v;
                if (v > maxVal) maxVal = v;
            }
            const int n = sampleIndices.size();
            if (n > 0 && seeded) {
                s.min = minVal;
                s.max = maxVal;
                s.avg = sum / n;
                s.rms = std::sqrt(sumSq / n);
            } else {
                continue;
            }
        }
        stats.push_back(s);
    }
    return stats;
}

enum class MeasureType { Min, Max, Avg, Rms, Pp, At };

struct MeasureRequest {
    QString expr;
    QString signalName;
    MeasureType type;
    double atTime = 0.0;
};

static int findVarIndex(const QStringList& vars, const QString& name) {
    for (int i = 0; i < vars.size(); ++i) {
        if (vars[i].compare(name, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

static bool parseMeasure(const QString& expr, MeasureRequest* out, QString* error) {
    if (!out) return false;
    QString s = expr.trimmed();
    if (s.isEmpty()) {
        if (error) *error = "Empty measure expression.";
        return false;
    }
    MeasureRequest req;
    req.expr = s;

    int atPos = s.indexOf("@t=", Qt::CaseInsensitive);
    if (atPos >= 0) {
        const QString namePart = s.left(atPos).trimmed();
        const QString timePart = s.mid(atPos + 3).trimmed();
        double t = 0.0;
        if (!SimValueParser::parseSpiceNumber(timePart, t)) {
            if (error) *error = "Invalid time in measure: " + expr;
            return false;
        }
        req.type = MeasureType::At;
        req.atTime = t;
        s = namePart;
    } else if (s.endsWith("_min", Qt::CaseInsensitive)) {
        req.type = MeasureType::Min;
        s.chop(4);
    } else if (s.endsWith("_max", Qt::CaseInsensitive)) {
        req.type = MeasureType::Max;
        s.chop(4);
    } else if (s.endsWith("_avg", Qt::CaseInsensitive)) {
        req.type = MeasureType::Avg;
        s.chop(4);
    } else if (s.endsWith("_rms", Qt::CaseInsensitive)) {
        req.type = MeasureType::Rms;
        s.chop(4);
    } else if (s.endsWith("_pp", Qt::CaseInsensitive)) {
        req.type = MeasureType::Pp;
        s.chop(3);
    } else {
        req.type = MeasureType::Avg;
    }

    s = s.trimmed();
    if (s.startsWith("V(", Qt::CaseInsensitive) && s.endsWith(")")) {
        req.signalName = s.mid(2, s.size() - 3);
    } else if (s.startsWith("I(", Qt::CaseInsensitive) && s.endsWith(")")) {
        req.signalName = "i(" + s.mid(2, s.size() - 3) + ")";
    } else {
        req.signalName = s;
    }
    if (req.signalName.isEmpty()) {
        if (error) *error = "Invalid measure signal: " + expr;
        return false;
    }
    *out = req;
    return true;
}

static int nearestIndex(const QVector<double>& xs, double t) {
    if (xs.isEmpty()) return -1;
    int lo = 0;
    int hi = xs.size() - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (xs[mid] < t) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return 0;
    if (lo >= xs.size()) return xs.size() - 1;
    const double d1 = std::abs(xs[lo] - t);
    const double d0 = std::abs(xs[lo - 1] - t);
    return (d0 <= d1) ? (lo - 1) : lo;
}

static bool loadRawAscii(const QString& path, RawData* out, QString* error) {
    if (!out) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "Could not open raw file: " + path;
        return false;
    }

    QByteArray allData = file.readAll();
    file.close();

    if (allData.isEmpty()) {
        if (error) *error = "Raw file is empty: " + path;
        return false;
    }

    const char* dataPtr = allData.constData();
    const char* endPtr = dataPtr + allData.size();

    auto readLine = [&]() -> QByteArray {
        const char* start = dataPtr;
        while (dataPtr < endPtr && *dataPtr != '\n' && *dataPtr != '\r') {
            dataPtr++;
        }
        QByteArray line(start, dataPtr - start);
        if (dataPtr < endPtr && *dataPtr == '\r') dataPtr++;
        if (dataPtr < endPtr && *dataPtr == '\n') dataPtr++;
        return line.trimmed();
    };

    bool collectingData = false;
    bool isBinary = false;
    int numVariables = 0;
    int numPoints = 0;
    QStringList varNames;

    while (dataPtr < endPtr) {
        QByteArray line = readLine();
        if (line.isEmpty()) continue;

        if (line.startsWith("No. Variables:")) {
            numVariables = line.mid(14).trimmed().toInt();
        } else if (line.startsWith("No. Points:")) {
            numPoints = line.mid(11).trimmed().toInt();
        } else if (line.startsWith("Variables:")) {
            for (int i = 0; i < numVariables; ++i) {
                QByteArray vLine = readLine();
                QStringList parts = QString::fromLatin1(vLine).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) varNames << parts[1];
            }
        } else if (line.startsWith("Values:")) {
            collectingData = true;
            isBinary = false;
            break;
        } else if (line.startsWith("Binary:")) {
            collectingData = true;
            isBinary = true;
            break;
        }
    }

    if (!collectingData || varNames.isEmpty() || numVariables <= 0) {
        if (error) *error = "Raw file missing Variables/Values/Binary sections: " + path;
        return false;
    }

    out->numVariables = numVariables;
    out->numPoints = numPoints;
    out->varNames = varNames;
    
    if (numPoints > 0) {
        out->x.reserve(numPoints);
        out->y.resize(numVariables - 1);
        for (int i = 0; i < out->y.size(); ++i) out->y[i].reserve(numPoints);

        if (isBinary) {
            std::cout << "RawDataParser (CLI): Using binary parsing mode" << std::endl;
            qint64 totalDoubles = (qint64)numPoints * numVariables;
            qint64 remainingBytes = endPtr - dataPtr;
            if (remainingBytes >= totalDoubles * (qint64)sizeof(double)) {
                for (int p = 0; p < numPoints; ++p) {
                    double val;
                    memcpy(&val, dataPtr, sizeof(double));
                    dataPtr += sizeof(double);
                    out->x.push_back(val);
                    for (int v = 1; v < numVariables; ++v) {
                        memcpy(&val, dataPtr, sizeof(double));
                        dataPtr += sizeof(double);
                        out->y[v - 1].push_back(val);
                    }
                }
            } else {
                if (error) *error = QString("Raw file binary payload is incomplete: %1 (Expected %2 bytes, got %3)").arg(path).arg(totalDoubles * sizeof(double)).arg(remainingBytes);
                return false;
            }
        } else {
            auto getNextValue = [&]() -> double {
                while (dataPtr < endPtr) {
                    const char* start = dataPtr;
                    while (dataPtr < endPtr && !isspace(*dataPtr)) dataPtr++;
                    QByteArray word(start, dataPtr - start);
                    while (dataPtr < endPtr && isspace(*dataPtr)) dataPtr++;
                    if (!word.isEmpty()) return word.toDouble();
                }
                return 0.0;
            };

            for (int p = 0; p < numPoints; ++p) {
                getNextValue(); // skip index
                out->x.push_back(getNextValue());
                for (int v = 1; v < numVariables; ++v) {
                    out->y[v - 1].push_back(getNextValue());
                }
            }
        }
    }

    return true;
}
using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;
bool g_quiet = false;
bool g_noColor = false;
bool g_exitOnWarning = false;

QString sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QRectF standardSymbolBounds() {
    SymbolLibraryManager& libMgr = SymbolLibraryManager::instance();
    const SymbolDefinition* res = libMgr.findSymbol("Resistor");
    const SymbolDefinition* cap = libMgr.findSymbol("Capacitor");
    if (!res && !cap) return QRectF();
    if (res && cap) {
        const QRectF r = res->boundingRect();
        const QRectF c = cap->boundingRect();
        const qreal w = qMax(r.width(), c.width());
        const qreal h = qMax(r.height(), c.height());
        const QPointF center = QPointF(0.0, 0.0);
        return QRectF(center.x() - w * 0.5, center.y() - h * 0.5, w, h);
    }
    return res ? res->boundingRect() : cap->boundingRect();
}

QPointF scalePoint(const QPointF& p, const QPointF& fromCenter, const QPointF& toCenter, double scale) {
    return QPointF((p.x() - fromCenter.x()) * scale + toCenter.x(),
                   (p.y() - fromCenter.y()) * scale + toCenter.y());
}

void normalizeSymbolToStandardSize(SymbolDefinition& symbol) {
    const QRectF target = standardSymbolBounds();
    if (!target.isValid()) return;
    QRectF current = symbol.boundingRect();
    if (!current.isValid() || current.width() <= 0.0 || current.height() <= 0.0) return;

    const double scale = qMin(target.width() / current.width(), target.height() / current.height());
    if (!std::isfinite(scale) || scale <= 0.0) return;

    const QPointF fromCenter = current.center();
    const QPointF toCenter = target.center();

    for (SymbolPrimitive& prim : symbol.primitives()) {
        switch (prim.type) {
        case SymbolPrimitive::Line: {
            QPointF p1(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
            QPointF p2(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
            p1 = scalePoint(p1, fromCenter, toCenter, scale);
            p2 = scalePoint(p2, fromCenter, toCenter, scale);
            prim.data["x1"] = p1.x();
            prim.data["y1"] = p1.y();
            prim.data["x2"] = p2.x();
            prim.data["y2"] = p2.y();
            break;
        }
        case SymbolPrimitive::Rect:
        case SymbolPrimitive::Arc:
        case SymbolPrimitive::Image: {
            QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                     prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble(),
                     prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble());
            QPointF tl = scalePoint(r.topLeft(), fromCenter, toCenter, scale);
            QPointF br = scalePoint(r.bottomRight(), fromCenter, toCenter, scale);
            QRectF nr(tl, br);
            prim.data["x"] = nr.x();
            prim.data["y"] = nr.y();
            if (prim.data.contains("width")) prim.data["width"] = nr.width();
            if (prim.data.contains("height")) prim.data["height"] = nr.height();
            if (prim.data.contains("w")) prim.data["w"] = nr.width();
            if (prim.data.contains("h")) prim.data["h"] = nr.height();
            break;
        }
        case SymbolPrimitive::Circle: {
            QPointF c(prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble(),
                      prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble());
            c = scalePoint(c, fromCenter, toCenter, scale);
            const double r = (prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble()) * scale;
            if (prim.data.contains("centerX")) prim.data["centerX"] = c.x();
            if (prim.data.contains("centerY")) prim.data["centerY"] = c.y();
            if (prim.data.contains("cx")) prim.data["cx"] = c.x();
            if (prim.data.contains("cy")) prim.data["cy"] = c.y();
            if (prim.data.contains("radius")) prim.data["radius"] = r;
            if (prim.data.contains("r")) prim.data["r"] = r;
            break;
        }
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = prim.data["points"].toArray();
            QJsonArray outPts;
            for (const auto& v : pts) {
                QJsonObject pt = v.toObject();
                QPointF p(pt["x"].toDouble(), pt["y"].toDouble());
                p = scalePoint(p, fromCenter, toCenter, scale);
                QJsonObject o;
                o["x"] = p.x();
                o["y"] = p.y();
                outPts.append(o);
            }
            prim.data["points"] = outPts;
            break;
        }
        case SymbolPrimitive::Text: {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            p = scalePoint(p, fromCenter, toCenter, scale);
            prim.data["x"] = p.x();
            prim.data["y"] = p.y();
            if (prim.data.contains("fontSize")) {
                prim.data["fontSize"] = prim.data["fontSize"].toDouble() * scale;
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            QPointF p(prim.data["x"].toDouble(), prim.data["y"].toDouble());
            p = scalePoint(p, fromCenter, toCenter, scale);
            prim.data["x"] = p.x();
            prim.data["y"] = p.y();
            if (prim.data.contains("length")) prim.data["length"] = prim.data["length"].toDouble() * scale;
            if (prim.data.contains("nameSize")) prim.data["nameSize"] = prim.data["nameSize"].toDouble() * scale;
            if (prim.data.contains("numSize")) prim.data["numSize"] = prim.data["numSize"].toDouble() * scale;
            break;
        }
        case SymbolPrimitive::Bezier: {
            for (int i = 1; i <= 4; ++i) {
                QPointF p(prim.data[QString("x%1").arg(i)].toDouble(),
                          prim.data[QString("y%1").arg(i)].toDouble());
                p = scalePoint(p, fromCenter, toCenter, scale);
                prim.data[QString("x%1").arg(i)] = p.x();
                prim.data[QString("y%1").arg(i)] = p.y();
            }
            break;
        }
        default:
            break;
        }
    }

    symbol.setReferencePos(scalePoint(symbol.referencePos(), fromCenter, toCenter, scale));
    symbol.setNamePos(scalePoint(symbol.namePos(), fromCenter, toCenter, scale));
}

QString normalizeNetlistLine(const QString& line) {
    QString s = line;
    const int commentIdx = s.indexOf(';');
    if (commentIdx >= 0) s = s.left(commentIdx);
    s = s.trimmed();
    if (s.isEmpty()) return QString();
    s.replace(QRegularExpression("\\s+"), " ");
    return s.toUpper();
}

QStringList normalizeNetlistText(const QString& text) {
    QStringList lines = text.split('\n');
    QStringList out;
    QString current;
    for (QString line : lines) {
        line.replace('\r', "");
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;
        if (trimmed.startsWith('*') || trimmed.startsWith(';')) continue;
        if (trimmed.startsWith('+')) {
            const QString cont = normalizeNetlistLine(trimmed.mid(1));
            if (!cont.isEmpty() && !current.isEmpty()) {
                current += " " + cont;
            }
            continue;
        }
        if (!current.isEmpty()) {
            out.append(current);
            current.clear();
        }
        current = normalizeNetlistLine(trimmed);
    }
    if (!current.isEmpty()) out.append(current);
    return out;
}

QString stripAnsiCodes(const QString& text) {
    if (!g_noColor) return text;
    static const QRegularExpression ansiRe("\x1B\\[[0-9;?]*[ -/]*[@-~]");
    QString out = text;
    out.remove(ansiRe);
    return out;
}

void printInfo(const QString& msg) {
    if (!g_quiet) std::cout << stripAnsiCodes(msg).toStdString() << std::endl;
}
void printInfoStd(const std::string& msg) {
    if (!g_quiet) std::cout << stripAnsiCodes(QString::fromStdString(msg)).toStdString() << std::endl;
}

Qt::PenStyle parseLineStyle(const QString& style) {
    const QString s = style.trimmed().toLower();
    if (s == "dash") return Qt::DashLine;
    if (s == "dot") return Qt::DotLine;
    if (s == "dashdot") return Qt::DashDotLine;
    return Qt::SolidLine;
}

QColor parseColorOrDefault(const QJsonObject& data, const QString& key, const QColor& fallback) {
    if (data.contains(key)) {
        QColor c(data.value(key).toString());
        if (c.isValid()) return c;
    }
    return fallback;
}

bool renderSymbolToPng(const SymbolDefinition& symbol, const QString& outPath, bool transparent = false, qreal scale = 4.0) {
    QRectF rect = symbol.boundingRect();
    if (rect.isNull() || rect.width() <= 0 || rect.height() <= 0) {
        rect = QRectF(-20, -20, 40, 40);
    }

    const qreal margin = 10.0;
    QSize imageSize = QSize(qCeil((rect.width() + margin * 2.0) * scale),
                            qCeil((rect.height() + margin * 2.0) * scale));

    QImage image(imageSize, QImage::Format_ARGB32);
    image.fill(transparent ? Qt::transparent : QColor(30, 30, 30));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((-rect.left() + margin) * scale, (-rect.top() + margin) * scale);
    painter.scale(scale, scale);

    const QColor lineColor(220, 220, 220);
    const QColor textColor(230, 230, 230);

    for (const SymbolPrimitive& prim : symbol.primitives()) {
        const QJsonObject& d = prim.data;
        switch (prim.type) {
        case SymbolPrimitive::Line: {
            qreal w = d.value("lineWidth").toDouble(1.5);
            QPen pen(lineColor, w, parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(d.value("x1").toDouble(), d.value("y1").toDouble()),
                             QPointF(d.value("x2").toDouble(), d.value("y2").toDouble()));
            break;
        }
        case SymbolPrimitive::Rect: {
            qreal x = d.value("x").toDouble();
            qreal y = d.value("y").toDouble();
            qreal w = d.contains("width") ? d.value("width").toDouble() : d.value("w").toDouble();
            qreal h = d.contains("height") ? d.value("height").toDouble() : d.value("h").toDouble();
            bool filled = d.value("filled").toBool(false);
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(filled ? parseColorOrDefault(d, "fillColor", QColor(255, 255, 255, 30)) : Qt::NoBrush);
            painter.drawRect(QRectF(x, y, w, h));
            break;
        }
        case SymbolPrimitive::Circle: {
            qreal cx = d.value("cx").toDouble();
            qreal cy = d.value("cy").toDouble();
            qreal r = d.value("r").toDouble();
            bool filled = d.value("filled").toBool(false);
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(filled ? parseColorOrDefault(d, "fillColor", QColor(255, 255, 255, 30)) : Qt::NoBrush);
            painter.drawEllipse(QPointF(cx, cy), r, r);
            break;
        }
        case SymbolPrimitive::Arc: {
            qreal x = d.value("x").toDouble();
            qreal y = d.value("y").toDouble();
            qreal w = d.contains("width") ? d.value("width").toDouble() : d.value("w").toDouble();
            qreal h = d.contains("height") ? d.value("height").toDouble() : d.value("h").toDouble();
            int sa = d.contains("startAngle") ? d.value("startAngle").toInt() : d.value("start").toInt();
            int sp = d.contains("spanAngle") ? d.value("spanAngle").toInt() : d.value("span").toInt();
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(QRectF(x, y, w, h), sa, sp);
            break;
        }
        case SymbolPrimitive::Polygon: {
            QJsonArray pts = d.value("points").toArray();
            QPolygonF poly;
            for (const auto& v : pts) {
                QJsonObject pt = v.toObject();
                poly << QPointF(pt.value("x").toDouble(), pt.value("y").toDouble());
            }
            bool filled = d.value("filled").toBool(false);
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(filled ? parseColorOrDefault(d, "fillColor", QColor(255, 255, 255, 30)) : Qt::NoBrush);
            painter.drawPolygon(poly);
            break;
        }
        case SymbolPrimitive::Bezier: {
            QPainterPath path;
            path.moveTo(d.value("x1").toDouble(), d.value("y1").toDouble());
            path.cubicTo(QPointF(d.value("x2").toDouble(), d.value("y2").toDouble()),
                         QPointF(d.value("x3").toDouble(), d.value("y3").toDouble()),
                         QPointF(d.value("x4").toDouble(), d.value("y4").toDouble()));
            QPen pen(lineColor, d.value("lineWidth").toDouble(1.5), parseLineStyle(d.value("lineStyle").toString()));
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
            break;
        }
        case SymbolPrimitive::Text: {
            const QString text = d.value("text").toString();
            const qreal x = d.value("x").toDouble();
            const qreal y = d.value("y").toDouble();
            int fs = d.value("fontSize").toInt(10);
            QFont font("SansSerif", fs);
            painter.setFont(font);
            painter.setPen(parseColorOrDefault(d, "color", textColor));
            const qreal rot = d.value("rotation").toDouble(0.0);
            if (std::abs(rot) > 1e-6) {
                painter.save();
                painter.translate(x, y);
                painter.rotate(-rot);
                painter.drawText(QPointF(0, 0), text);
                painter.restore();
            } else {
                painter.drawText(QPointF(x, y), text);
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            const qreal px = d.value("x").toDouble();
            const qreal py = d.value("y").toDouble();
            qreal len = d.value("length").toDouble(15.0);
            if (len <= 0) len = 15.0;
            const QString orient = d.value("orientation").toString("Right");
            QPointF endPt(px + len, py);
            if (orient == "Left") endPt = QPointF(px - len, py);
            else if (orient == "Up") endPt = QPointF(px, py - len);
            else if (orient == "Down") endPt = QPointF(px, py + len);
            QPen pen(lineColor, 2.0);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(px, py), endPt);
            painter.setBrush(lineColor);
            painter.drawEllipse(QPointF(px, py), 2.5, 2.5);

            const bool hideNum = d.value("hideNum").toBool(false);
            const bool hideName = d.value("hideName").toBool(false);
            const QString num = d.contains("number") ? QString::number(d.value("number").toInt()) : d.value("num").toString();
            const QString name = d.value("name").toString();
            int nsz = d.value("numSize").toInt(7);
            int asz = d.value("nameSize").toInt(7);
            if (!hideNum && !num.isEmpty()) {
                painter.setFont(QFont("Monospace", nsz > 0 ? nsz : 7));
                painter.setPen(textColor);
                painter.drawText(QPointF(px + 2, py - 2), num);
            }
            if (!hideName && !name.isEmpty()) {
                painter.setFont(QFont("SansSerif", asz > 0 ? asz : 7));
                painter.setPen(textColor);
                painter.drawText(QPointF(endPt.x() + 2, endPt.y() - 2), name);
            }
            break;
        }
        case SymbolPrimitive::Image: {
            QString base64 = d.value("image").toString();
            if (!base64.isEmpty()) {
                QByteArray bytes = QByteArray::fromBase64(base64.toLatin1());
                QImage img;
                img.loadFromData(bytes);
                if (!img.isNull()) {
                    qreal x = d.value("x").toDouble();
                    qreal y = d.value("y").toDouble();
                    qreal w = d.contains("width") ? d.value("width").toDouble() : d.value("w").toDouble();
                    qreal h = d.contains("height") ? d.value("height").toDouble() : d.value("h").toDouble();
                    painter.drawImage(QRectF(x, y, w, h), img);
                }
            }
            break;
        }
        }
    }

    painter.end();
    return image.save(outPath);
}

bool runSymbolRender(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: vio-cmd symbol-render <file.viosym> <out.png>" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    const QString outPath = args.at(2);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    QJsonObject obj = doc.object();
    if (obj.contains("library")) {
        std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
        return false;
    }

    SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
    if (symbol.name().trimmed().isEmpty()) {
        symbol.setName(QFileInfo(filePath).completeBaseName());
    }

    const bool transparent = parser.isSet("transparent");
    const qreal scale = qMax(0.1, parser.value("scale").toDouble());
    if (!renderSymbolToPng(symbol, outPath, transparent, scale)) {
        std::cerr << "Error: Failed to render symbol to " << outPath.toStdString() << std::endl;
        return false;
    }
    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["output"] = outPath;
        out["transparent"] = transparent;
        out["scale"] = scale;
        printJsonValue(out);
    } else {
    printInfoStd("Rendered symbol to " + outPath.toStdString());
    }
    return true;
}

bool runSymbolList(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: vio-cmd symbol-list <folder|library.sclib>" << std::endl;
        return false;
    }
    const QString path = args.at(1);
    QFileInfo info(path);
    if (!info.exists()) {
        std::cerr << "Error: Path not found: " << path.toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["path"] = path;
    QJsonArray symbols;

    auto appendSymbol = [&](const QString& name, const QString& source) {
        QJsonObject s;
        s["name"] = name;
        s["source"] = source;
        symbols.append(s);
    };

    if (info.isFile() && path.endsWith(".sclib", Qt::CaseInsensitive)) {
        SymbolLibrary lib;
        if (!lib.load(path)) {
            std::cerr << "Error: Failed to load library: " << path.toStdString() << std::endl;
            return false;
        }
        for (const QString& name : lib.symbolNames()) {
            appendSymbol(name, QFileInfo(path).fileName());
        }
    } else if (info.isDir()) {
        QDir dir(path);
        QStringList viosyms = dir.entryList(QStringList() << "*.viosym", QDir::Files);
        for (const QString& file : viosyms) {
            QString filePath = dir.filePath(file);
            QFile f(filePath);
            if (f.open(QIODevice::ReadOnly)) {
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
                f.close();
                if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    QString name = obj.value("name").toString();
                    if (name.trimmed().isEmpty()) name = QFileInfo(filePath).completeBaseName();
                    appendSymbol(name, file);
                    continue;
                }
            }
            appendSymbol(QFileInfo(filePath).completeBaseName(), file);
        }

        QStringList libs = dir.entryList(QStringList() << "*.sclib", QDir::Files);
        for (const QString& libFile : libs) {
            SymbolLibrary lib;
            if (!lib.load(dir.filePath(libFile))) continue;
            for (const QString& name : lib.symbolNames()) {
                appendSymbol(name, libFile);
            }
        }
    } else {
        std::cerr << "Error: Unsupported path. Use a folder or .sclib file." << std::endl;
        return false;
    }

    out["symbols"] = symbols;
    printJsonValue(out);
    return true;
}

bool runSymbolExport(const QStringList& args) {
    if (args.size() < 4) {
        std::cerr << "Usage: vio-cmd symbol-export <symbolName> <library.sclib> <out.viosym>" << std::endl;
        return false;
    }
    const QString symName = args.at(1);
    const QString libPath = args.at(2);
    const QString outPath = args.at(3);

    if (!QFileInfo::exists(libPath)) {
        std::cerr << "Error: Library not found: " << libPath.toStdString() << std::endl;
        return false;
    }

    SymbolLibrary lib;
    if (!lib.load(libPath)) {
        std::cerr << "Error: Failed to load library: " << libPath.toStdString() << std::endl;
        return false;
    }

    SymbolDefinition* sym = lib.findSymbol(symName);
    if (!sym) {
        std::cerr << "Error: Symbol not found: " << symName.toStdString() << std::endl;
        return false;
    }

    QString finalOut = outPath;
    if (!finalOut.endsWith(".viosym", Qt::CaseInsensitive)) finalOut += ".viosym";

    QJsonDocument doc(sortJsonValue(sym->toJson()).toObject());
    QFile file(finalOut);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Error: Cannot write " << finalOut.toStdString() << std::endl;
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    printInfoStd("Exported " + symName.toStdString() + " to " + finalOut.toStdString());
    return true;
}

bool runSymbolImport(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: vio-cmd symbol-import <input.asy|input.kicad_sym> <out.viosym|out.sclib> [--name SYMBOL]" << std::endl;
        return false;
    }

    const QString inPath = args.at(1);
    const QString outPath = args.at(2);
    const QFileInfo inInfo(inPath);
    if (!inInfo.exists()) {
        std::cerr << "Error: Input not found: " << inPath.toStdString() << std::endl;
        return false;
    }

    const QString lowerIn = inPath.toLower();
    SymbolDefinition symbol;
    QString detectedFootprint;

    if (lowerIn.endsWith(".asy")) {
        auto result = LtspiceSymbolImporter::importSymbolDetailed(inPath);
        if (!result.success || !result.symbol.isValid()) {
            const QString msg = result.errorMessage.isEmpty()
                                    ? "Failed to import LTspice symbol."
                                    : result.errorMessage;
            std::cerr << "Error: " << msg.toStdString() << std::endl;
            return false;
        }
        symbol = result.symbol;
    } else if (lowerIn.endsWith(".kicad_sym")) {
        const QString symName = parser.value("name").trimmed();
        if (symName.isEmpty()) {
            QStringList names = KicadSymbolImporter::getSymbolNames(inPath);
            names.sort(Qt::CaseInsensitive);

            QJsonObject out;
            out["file"] = inPath;
            QJsonArray list;
            for (const QString& n : names) list.append(n);
            out["symbols"] = list;
            printJsonValue(out);
            return true;
        }
        auto result = KicadSymbolImporter::importSymbolDetailed(inPath, symName);
        symbol = result.symbol;
        detectedFootprint = result.detectedFootprint;
        if (!symbol.isValid()) {
            std::cerr << "Error: Failed to import KiCad symbol: " << symName.toStdString() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Unsupported input. Use .asy or .kicad_sym" << std::endl;
        return false;
    }

    normalizeSymbolToStandardSize(symbol);

    QString finalOut = outPath;
    const QString lowerOut = outPath.toLower();
    if (lowerOut.endsWith(".viosym")) {
        QJsonDocument doc(sortJsonValue(symbol.toJson()).toObject());
        QFile file(finalOut);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            std::cerr << "Error: Cannot write " << finalOut.toStdString() << std::endl;
            return false;
        }
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    } else if (lowerOut.endsWith(".sclib")) {
        SymbolLibrary lib;
        if (QFileInfo::exists(finalOut)) {
            if (!lib.load(finalOut)) {
                std::cerr << "Error: Failed to load library: " << finalOut.toStdString() << std::endl;
                return false;
            }
        } else {
            lib = SymbolLibrary(QFileInfo(finalOut).completeBaseName(), false);
            lib.setPath(finalOut);
        }
        lib.addSymbol(symbol);
        if (!lib.save(finalOut)) {
            std::cerr << "Error: Failed to save library: " << finalOut.toStdString() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Output must be .viosym or .sclib" << std::endl;
        return false;
    }

    QJsonObject out;
    out["input"] = inPath;
    out["output"] = finalOut;
    out["name"] = symbol.name();
    if (!detectedFootprint.isEmpty()) out["footprint"] = detectedFootprint;
    printJsonValue(out);
    return true;
}

bool runLibraryIndex(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 2) {
        std::cerr << "Usage: vio-cmd library-index <folder>" << std::endl;
        return false;
    }
    const QString root = args.at(1);
    QDir dir(root);
    if (!dir.exists()) {
        std::cerr << "Error: Folder not found: " << root.toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["root"] = root;

    // Symbols (.viosym and .sclib)
    QJsonArray symbols;
    {
        QDirIterator it(root, QStringList() << "*.viosym", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            QFile f(filePath);
            QString name = QFileInfo(filePath).completeBaseName();
            if (f.open(QIODevice::ReadOnly)) {
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
                f.close();
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    if (obj.contains("name")) {
                        const QString n = obj.value("name").toString().trimmed();
                        if (!n.isEmpty()) name = n;
                    }
                }
            }
            QJsonObject s;
            s["name"] = name;
            s["path"] = filePath;
            s["type"] = "viosym";
            symbols.append(s);
        }

        QDirIterator libIt(root, QStringList() << "*.sclib", QDir::Files, QDirIterator::Subdirectories);
        while (libIt.hasNext()) {
            QString filePath = libIt.next();
            SymbolLibrary lib;
            if (!lib.load(filePath)) continue;
            for (const QString& symName : lib.symbolNames()) {
                QJsonObject s;
                s["name"] = symName;
                s["path"] = filePath;
                s["type"] = "sclib";
                symbols.append(s);
            }
        }
    }
    out["symbols"] = symbols;

    const bool includeComments = parser.isSet("include-comments");

    // Models (.lib, .sub, .cmp, .cir, .sp, .txt) + parsed names
    QJsonArray models;
    QMap<QString, QSet<QString>> modelIndex;
    QMap<QString, QSet<QString>> subcktIndex;
    {
        QDirIterator it(root, QStringList() << "*.lib" << "*.sub" << "*.cmp" << "*.cir" << "*.sp" << "*.txt",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            QJsonObject m;
            m["path"] = filePath;
            m["type"] = QFileInfo(filePath).suffix().toLower();

            QSet<QString> subcktSet;
            QSet<QString> modelSet;
            QFile f(filePath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&f);
                QRegularExpression subcktRe("^\\s*\\.subckt\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                QRegularExpression modelRe("^\\s*\\.model\\s+([^\\s]+)", QRegularExpression::CaseInsensitiveOption);
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    QString candidate = line;
                    if (includeComments) {
                        QString trimmed = line.trimmed();
                        if (trimmed.startsWith('*') || trimmed.startsWith(';')) {
                            trimmed.remove(0, 1);
                            candidate = trimmed.trimmed();
                        }
                    } else {
                        QString trimmed = line.trimmed();
                        if (trimmed.startsWith('*') || trimmed.startsWith(';')) continue;
                    }
                    auto sm = subcktRe.match(candidate);
                    if (sm.hasMatch()) {
                        const QString name = sm.captured(1).trimmed();
                        if (!name.isEmpty()) subcktSet.insert(name);
                    }
                    auto mm = modelRe.match(candidate);
                    if (mm.hasMatch()) {
                        const QString name = mm.captured(1).trimmed();
                        if (!name.isEmpty()) modelSet.insert(name);
                    }
                }
                f.close();
            }
            QJsonArray subckts;
            for (const auto& name : subcktSet) subckts.append(name);
            QJsonArray modelNames;
            for (const auto& name : modelSet) modelNames.append(name);
            m["subckts"] = subckts;
            m["models"] = modelNames;
            models.append(m);

            for (const auto& name : subcktSet) subcktIndex[name].insert(filePath);
            for (const auto& name : modelSet) modelIndex[name].insert(filePath);
        }
    }
    out["models"] = models;

    QJsonObject indexObj;
    QJsonObject modelMap;
    for (auto it = modelIndex.begin(); it != modelIndex.end(); ++it) {
        QJsonArray paths;
        for (const auto& p : it.value()) paths.append(p);
        modelMap[it.key()] = paths;
    }
    QJsonObject subcktMap;
    for (auto it = subcktIndex.begin(); it != subcktIndex.end(); ++it) {
        QJsonArray paths;
        for (const auto& p : it.value()) paths.append(p);
        subcktMap[it.key()] = paths;
    }
    indexObj["models"] = modelMap;
    indexObj["subckts"] = subcktMap;
    out["modelIndex"] = indexObj;

    printJsonValue(out);
    return true;
}

bool runSchematicRender(const QString& filePath, const QString& outPath, const QCommandLineParser& parser) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    QRectF rect = scene.itemsBoundingRect();
    if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
    rect.adjust(-10, -10, 10, 10);

    const qreal scale = qMax(0.1, parser.value("scale").toDouble());
    QImage image(rect.size().toSize() * scale, QImage::Format_ARGB32);
    const bool transparent = parser.isSet("transparent");
    image.fill(transparent ? Qt::transparent : QColor(20, 20, 25));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene.render(&painter, QRectF(), rect);
    painter.end();

    if (!image.save(outPath)) {
        std::cerr << "Failed to save image to " << outPath.toStdString() << std::endl;
        return false;
    }

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["output"] = outPath;
        out["width"] = image.width();
        out["height"] = image.height();
        out["scale"] = scale;
        out["transparent"] = transparent;
        QJsonObject bounds;
        bounds["x"] = rect.x();
        bounds["y"] = rect.y();
        bounds["w"] = rect.width();
        bounds["h"] = rect.height();
        out["bounds"] = bounds;
        printJsonValue(out);
    } else {
        printInfoStd("Successfully rendered schematic to " + outPath.toStdString());
    }
    return true;
}

bool runSymbolQuery(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: vio-cmd symbol-query <file.viosym>" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("library")) {
        std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
        return false;
    }

    SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
    if (symbol.name().trimmed().isEmpty()) {
        symbol.setName(QFileInfo(filePath).completeBaseName());
    }

    QJsonObject out;
    out["file"] = filePath;
    out["name"] = symbol.name();
    out["description"] = symbol.description();
    out["category"] = symbol.category();
    out["referencePrefix"] = symbol.referencePrefix();
    out["defaultValue"] = symbol.defaultValue();
    out["modelSource"] = symbol.modelSource();
    out["modelPath"] = symbol.modelPath();
    out["modelName"] = symbol.modelName();
    int pinCount = 0;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) pinCount++;
    }
    out["pinCount"] = pinCount;

    QJsonArray pins;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type != SymbolPrimitive::Pin) continue;
        QJsonObject p;
        p["number"] = prim.data.value("number").toInt();
        p["name"] = prim.data.value("name").toString();
        p["x"] = prim.data.value("x").toDouble();
        p["y"] = prim.data.value("y").toDouble();
        p["orientation"] = prim.data.value("orientation").toString();
        p["length"] = prim.data.value("length").toDouble(15.0);
        p["visible"] = prim.data.value("visible").toBool(true);
        pins.append(p);
    }
    out["pins"] = pins;

    QJsonObject bounds;
    const QRectF rect = symbol.boundingRect();
    bounds["x"] = rect.x();
    bounds["y"] = rect.y();
    bounds["w"] = rect.width();
    bounds["h"] = rect.height();
    out["boundingRect"] = bounds;

    printJsonValue(out);
    return true;
}

bool runSymbolValidate(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: vio-cmd symbol-validate <file.viosym>" << std::endl;
        return false;
    }
    const QString filePath = args.at(1);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read symbol file: " << filePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid symbol JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("library")) {
        std::cerr << "Error: This looks like a library file (.sclib), not a .viosym." << std::endl;
        return false;
    }

    SymbolDefinition symbol = SymbolDefinition::fromJson(obj);
    if (symbol.name().trimmed().isEmpty()) {
        symbol.setName(QFileInfo(filePath).completeBaseName());
    }

    QJsonArray issues;
    auto addIssue = [&](const QString& severity, const QString& message) {
        QJsonObject i;
        i["severity"] = severity;
        i["message"] = message;
        issues.append(i);
    };

    if (symbol.name().trimmed().isEmpty()) {
        addIssue("Error", "Symbol name is empty.");
    }
    if (symbol.referencePrefix().trimmed().isEmpty()) {
        addIssue("Warning", "Reference prefix is empty.");
    }

    int pinCount = 0;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type != SymbolPrimitive::Pin) continue;
        pinCount++;
        const QString pinName = prim.data.value("name").toString().trimmed();
        const int number = prim.data.value("number").toInt();
        const QString orient = prim.data.value("orientation").toString("Right");
        const qreal len = prim.data.value("length").toDouble(15.0);
        if (number <= 0) {
            addIssue("Warning", QString("Pin has invalid number (%1).").arg(number));
        }
        if (pinName.isEmpty()) {
            addIssue("Warning", QString("Pin %1 has empty name.").arg(number > 0 ? QString::number(number) : "?"));
        }
        if (orient != "Left" && orient != "Right" && orient != "Up" && orient != "Down") {
            addIssue("Warning", QString("Pin %1 has invalid orientation '%2'.").arg(number > 0 ? QString::number(number) : "?", orient));
        }
        if (len <= 0.0) {
            addIssue("Warning", QString("Pin %1 has non-positive length.").arg(number > 0 ? QString::number(number) : "?"));
        }
    }
    if (pinCount == 0) {
        addIssue("Error", "Symbol has no pins.");
    }

    const QString modelName = symbol.modelName().trimmed();
    const QString modelPath = symbol.modelPath().trimmed();
    const QString modelSource = symbol.modelSource().trimmed().toLower();
    const QSet<QString> validSources = {"", "none", "library", "project", "absolute"};
    if (!modelSource.isEmpty() && !validSources.contains(modelSource)) {
        addIssue("Warning", QString("Model source '%1' is not recognized.").arg(symbol.modelSource()));
    }
    if (!modelName.isEmpty() && modelPath.isEmpty()) {
        addIssue("Warning", "Model name is set but model file path is empty.");
    }
    if (modelName.isEmpty() && !modelPath.isEmpty()) {
        addIssue("Warning", "Model file path is set but model name is empty.");
    }

    QJsonObject out;
    out["file"] = filePath;
    out["name"] = symbol.name();
    out["pinCount"] = pinCount;
    out["issues"] = issues;
    QJsonObject summary;
    summary["count"] = issues.size();
    out["summary"] = summary;

    printJsonValue(out);
    return true;
}

double parseNumericValue(const QString& val) {
    if (val.isEmpty()) return 0.0;
    QString s = val.trimmed().toLower();
    double multiplier = 1.0;
    if (s.endsWith("k")) { multiplier = 1e3; s.chop(1); }
    else if (s.endsWith("m") && !s.endsWith("meg")) { multiplier = 1e-3; s.chop(1); }
    else if (s.endsWith("u")) { multiplier = 1e-6; s.chop(1); }
    else if (s.endsWith("n")) { multiplier = 1e-9; s.chop(1); }
    else if (s.endsWith("p")) { multiplier = 1e-12; s.chop(1); }
    else if (s.endsWith("meg")) { multiplier = 1e6; s.chop(3); }
    else if (s.endsWith("g")) { multiplier = 1e9; s.chop(1); }
    bool ok;
    double d = s.toDouble(&ok);
    return ok ? d * multiplier : 0.0;
}

bool runSchematicQuery(const QString& filePath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["file"] = filePath;
    out["pageSize"] = pageSize;

    ECOPackage pkg = NetlistGenerator::generateECOPackage(&scene, QFileInfo(filePath).absolutePath(), nullptr);
    QList<NetlistNet> nets = NetlistGenerator::buildConnectivity(&scene, QFileInfo(filePath).absolutePath(), nullptr);

    QJsonArray comps;
    for (const auto& comp : pkg.components) {
        QJsonObject c;
        c["reference"] = comp.reference;
        c["typeName"] = comp.typeName;
        c["type"] = comp.type;
        c["value"] = comp.value;
        c["spiceModel"] = comp.spiceModel;
        c["footprint"] = comp.footprint;
        c["symbolPinCount"] = comp.symbolPinCount;
        c["excludeFromSim"] = comp.excludeFromSim;
        c["excludeFromPcb"] = comp.excludeFromPcb;
        comps.append(c);
    }
    out["components"] = comps;

    QJsonArray netsArr;
    for (const auto& net : nets) {
        QJsonObject n;
        n["name"] = net.name;
        QJsonArray pins;
        for (const auto& pin : net.pins) {
            QJsonObject p;
            p["ref"] = pin.componentRef;
            p["pin"] = pin.pinName;
            pins.append(p);
        }
        n["pins"] = pins;
        netsArr.append(n);
    }
    out["nets"] = netsArr;

    printJsonValue(out);
    return true;
}

bool runSchematicNetlist(const QString& filePath, const QCommandLineParser& parser) {
    const QString format = parser.value("format").trimmed().toLower();
    const QString outPath = parser.value("out").trimmed();
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    if (format == "json") {
        const QString net = NetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), NetlistGenerator::FluxJSON, nullptr);
        if (!outPath.isEmpty()) {
            QFile outFile(outPath);
            if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                std::cerr << "Error: Unable to write netlist to " << outPath.toStdString() << std::endl;
                return false;
            }
            outFile.write(net.toUtf8());
            outFile.close();
        } else {
            std::cout << net.toStdString() << std::endl;
        }
        return true;
    }

    SpiceNetlistGenerator::SimulationParams params;
    QString analysisType = parser.value("analysis").toLower();
    if (analysisType == "tran") {
        params.type = SpiceNetlistGenerator::Transient;
        params.step = parser.value("step").isEmpty() ? "1e-6" : parser.value("step");
        params.stop = parser.value("stop").isEmpty() ? "1e-2" : parser.value("stop");
    } else if (analysisType == "ac") {
        params.type = SpiceNetlistGenerator::AC;
        params.start = "10";
        params.stop = "1e6";
    } else {
        params.type = SpiceNetlistGenerator::OP;
    }

    QString netlist = SpiceNetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), nullptr, params);
    if (!outPath.isEmpty()) {
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::cerr << "Error: Unable to write netlist to " << outPath.toStdString() << std::endl;
            return false;
        }
        outFile.write(netlist.toUtf8());
        outFile.close();
    } else {
        std::cout << netlist.toStdString() << std::endl;
    }
    return true;
}

QJsonObject componentToJson(const ECOComponent& comp) {
    QJsonObject c;
    c["reference"] = comp.reference;
    c["typeName"] = comp.typeName;
    c["type"] = comp.type;
    c["value"] = comp.value;
    c["spiceModel"] = comp.spiceModel;
    c["footprint"] = comp.footprint;
    c["symbolPinCount"] = comp.symbolPinCount;
    c["excludeFromSim"] = comp.excludeFromSim;
    c["excludeFromPcb"] = comp.excludeFromPcb;
    return c;
}

QMap<QString, QPointF> collectComponentPositions(QGraphicsScene* scene) {
    QMap<QString, QPointF> out;
    if (!scene) return out;
    for (auto* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            const QString ref = si->reference();
            if (!ref.trimmed().isEmpty() && !out.contains(ref)) {
                out[ref] = si->pos();
            }
        }
    }
    return out;
}

bool runSchematicValidate(const QString& filePath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    QJsonObject out;
    out["file"] = filePath;
    out["pageSize"] = pageSize;

    // ERC
    QJsonArray ercIssues;
    auto violations = SchematicERC::run(&scene, QFileInfo(filePath).absolutePath());
    for (const auto& v : violations) {
        QJsonObject issue;
        issue["severity"] = (v.severity == ERCViolation::Error) ? "Error" :
                            (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
        issue["message"] = v.message;
        issue["x"] = v.position.x();
        issue["y"] = v.position.y();
        ercIssues.append(issue);
    }
    out["erc"] = ercIssues;

    // Simulator preflight (ground, model resolution, pin mismatch)
    SimNetlist preflightNetlist;
    QStringList preflight = SimManager::instance().preflightCheck(&scene, nullptr, preflightNetlist);
    QJsonArray preflightArr;
    for (const QString& msg : preflight) preflightArr.append(msg);
    out["preflight"] = preflightArr;

    // Summary
    QJsonObject summary;
    summary["ercCount"] = ercIssues.size();
    summary["preflightCount"] = preflightArr.size();
    out["summary"] = summary;

    printJsonValue(out);
    return true;
}

bool runSchematicBom(const QString& filePath) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    ECOPackage pkg = NetlistGenerator::generateECOPackage(&scene, QFileInfo(filePath).absolutePath(), nullptr);
    QJsonObject out;
    out["file"] = filePath;

    // Flat component list (sorted by reference)
    std::sort(pkg.components.begin(), pkg.components.end(), [](const ECOComponent& a, const ECOComponent& b) {
        return a.reference.toLower() < b.reference.toLower();
    });
    QJsonArray comps;
    for (const auto& comp : pkg.components) {
        comps.append(componentToJson(comp));
    }
    out["components"] = comps;

    // Grouped BOM
    struct BomKey {
        QString typeName;
        QString value;
        QString footprint;
        bool operator<(const BomKey& other) const {
            if (typeName != other.typeName) return typeName < other.typeName;
            if (value != other.value) return value < other.value;
            return footprint < other.footprint;
        }
    };
    QMap<BomKey, QStringList> groups;
    for (const auto& comp : pkg.components) {
        BomKey key{comp.typeName, comp.value, comp.footprint};
        groups[key].append(comp.reference);
    }
    QJsonArray grouped;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        QJsonObject g;
        g["typeName"] = it.key().typeName;
        g["value"] = it.key().value;
        g["footprint"] = it.key().footprint;
        g["qty"] = it.value().size();
        QStringList refs = it.value();
        std::sort(refs.begin(), refs.end(), [](const QString& a, const QString& b) { return a.toLower() < b.toLower(); });
        QJsonArray refArr;
        for (const QString& r : refs) refArr.append(r);
        g["references"] = refArr;
        grouped.append(g);
    }
    out["groups"] = grouped;

    printJsonValue(out);
    return true;
}

bool runSchematicDiff(const QStringList& args) {
    if (args.size() < 3) {
        std::cerr << "Usage: vio-cmd schematic-diff <a.flxsch> <b.flxsch>" << std::endl;
        return false;
    }
    const QString aPath = args.at(1);
    const QString bPath = args.at(2);

    QGraphicsScene sceneA;
    QString pageA;
    TitleBlockData tbA;
    if (!SchematicFileIO::loadSchematic(&sceneA, aPath, pageA, tbA)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }
    QGraphicsScene sceneB;
    QString pageB;
    TitleBlockData tbB;
    if (!SchematicFileIO::loadSchematic(&sceneB, bPath, pageB, tbB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    ECOPackage pkgA = NetlistGenerator::generateECOPackage(&sceneA, QFileInfo(aPath).absolutePath(), nullptr);
    ECOPackage pkgB = NetlistGenerator::generateECOPackage(&sceneB, QFileInfo(bPath).absolutePath(), nullptr);

    QMap<QString, ECOComponent> mapA;
    QMap<QString, ECOComponent> mapB;
    for (const auto& c : pkgA.components) mapA[c.reference] = c;
    for (const auto& c : pkgB.components) mapB[c.reference] = c;

    QMap<QString, QPointF> posA = collectComponentPositions(&sceneA);
    QMap<QString, QPointF> posB = collectComponentPositions(&sceneB);

    QJsonObject out;
    out["a"] = aPath;
    out["b"] = bPath;

    QJsonArray added;
    QJsonArray removed;
    QJsonArray changed;

    for (auto it = mapA.begin(); it != mapA.end(); ++it) {
        const QString ref = it.key();
        if (!mapB.contains(ref)) {
            removed.append(ref);
            continue;
        }
        const ECOComponent& ca = it.value();
        const ECOComponent& cb = mapB[ref];
        QJsonObject diff;
        bool hasDiff = false;
        if (ca.typeName != cb.typeName) { diff["typeName"] = QJsonArray{ca.typeName, cb.typeName}; hasDiff = true; }
        if (ca.value != cb.value) { diff["value"] = QJsonArray{ca.value, cb.value}; hasDiff = true; }
        if (ca.footprint != cb.footprint) { diff["footprint"] = QJsonArray{ca.footprint, cb.footprint}; hasDiff = true; }
        if (ca.spiceModel != cb.spiceModel) { diff["spiceModel"] = QJsonArray{ca.spiceModel, cb.spiceModel}; hasDiff = true; }
        if (posA.contains(ref) && posB.contains(ref)) {
            QPointF pa = posA[ref];
            QPointF pb = posB[ref];
            if (pa != pb) {
                QJsonObject pos;
                pos["from"] = QJsonArray{pa.x(), pa.y()};
                pos["to"] = QJsonArray{pb.x(), pb.y()};
                diff["position"] = pos;
                hasDiff = true;
            }
        }
        if (hasDiff) {
            QJsonObject entry;
            entry["reference"] = ref;
            entry["changes"] = diff;
            changed.append(entry);
        }
    }
    for (auto it = mapB.begin(); it != mapB.end(); ++it) {
        const QString ref = it.key();
        if (!mapA.contains(ref)) added.append(ref);
    }

    out["components"] = QJsonObject{
        {"added", added},
        {"removed", removed},
        {"changed", changed}
    };

    printJsonValue(out);
    return true;
}

bool runSchematicTransform(const QString& filePath, const QCommandLineParser& parser) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    const QStringList renames = parser.values("rename-net");
    const QStringList valueRules = parser.values("normalize-value");
    const QString prefixRule = parser.value("prefix-ref");

    QMap<QString, QString> netRename;
    for (const QString& rule : renames) {
        const int eq = rule.indexOf('=');
        if (eq > 0) {
            const QString from = rule.left(eq).trimmed();
            const QString to = rule.mid(eq + 1).trimmed();
            if (!from.isEmpty() && !to.isEmpty()) netRename[from] = to;
        }
    }

    QMap<QString, QString> valueMap;
    for (const QString& rule : valueRules) {
        const int eq = rule.indexOf('=');
        if (eq > 0) {
            const QString from = rule.left(eq).trimmed();
            const QString to = rule.mid(eq + 1).trimmed();
            if (!from.isEmpty() && !to.isEmpty()) valueMap[from] = to;
        }
    }

    QString prefixFrom;
    QString prefixTo;
    if (!prefixRule.isEmpty()) {
        const int eq = prefixRule.indexOf('=');
        if (eq > 0) {
            prefixFrom = prefixRule.left(eq).trimmed();
            prefixTo = prefixRule.mid(eq + 1).trimmed();
        }
    }

    int renamedNets = 0;
    int normalizedValues = 0;
    int updatedRefs = 0;

    for (auto* item : scene.items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            // Net label rename
            if (si->itemType() == SchematicItem::NetLabelType) {
                const QString cur = si->value();
                if (netRename.contains(cur)) {
                    si->setValue(netRename[cur]);
                    renamedNets++;
                }
            }

            // Normalize values
            const QString val = si->value();
            if (valueMap.contains(val)) {
                si->setValue(valueMap[val]);
                normalizedValues++;
            }

            // Prefix rename
            const QString ref = si->reference();
            if (!prefixFrom.isEmpty() && ref.startsWith(prefixFrom)) {
                si->setReference(prefixTo + ref.mid(prefixFrom.size()));
                updatedRefs++;
            }
        }
    }

    if (!SchematicFileIO::saveSchematic(&scene, filePath, pageSize)) {
        std::cerr << "Error saving schematic." << std::endl;
        return false;
    }

    QJsonObject out;
    out["file"] = filePath;
    out["renamedNets"] = renamedNets;
    out["normalizedValues"] = normalizedValues;
    out["updatedRefs"] = updatedRefs;
    printJsonValue(out);
    return true;
}

bool runSchematicProbe(const QString& filePath, const QCommandLineParser& parser) {
    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    SimNetlist netlist = SimSchematicBridge::buildNetlist(&scene, nullptr);

    const bool listSignals = parser.isSet("list");
    const QStringList addSignals = parser.values("add");
    const bool autoProbe = parser.isSet("auto");
    const bool outputJson = parser.isSet("json");

    if (listSignals || (addSignals.isEmpty() && !autoProbe)) {
        QStringList voltageSignals;
        QStringList currentSignals;

        for (int i = 0; i < netlist.nodeCount(); ++i) {
            const QString name = QString::fromStdString(netlist.nodeName(i)).trimmed();
            if (name.isEmpty()) continue;
            voltageSignals.append("V(" + name + ")");
        }

        for (const auto& comp : netlist.components()) {
            const QString name = QString::fromStdString(comp.name).trimmed();
            if (name.isEmpty()) continue;
            
            // Simplified branch current detection for CLI signal listing
            bool hasBranch = (comp.type == SimComponentType::VoltageSource || 
                              comp.type == SimComponentType::Inductor ||
                              comp.type == SimComponentType::B_VoltageSource ||
                              comp.type == SimComponentType::OpAmpMacro);
            
            if (!hasBranch) continue;
            currentSignals.append("I(" + name + ")");
        }

        voltageSignals.sort(Qt::CaseInsensitive);
        currentSignals.sort(Qt::CaseInsensitive);

        QJsonArray voltageJson;
        for (const QString& v : voltageSignals) voltageJson.append(v);

        QJsonArray currentJson;
        for (const QString& c : currentSignals) currentJson.append(c);

        QJsonArray allSignals;
        for (const QString& v : voltageSignals) allSignals.append(v);
        for (const QString& c : currentSignals) allSignals.append(c);

        QJsonObject out;
        out["file"] = filePath;
        out["signals"] = allSignals;
        out["voltages"] = voltageJson;
        out["currents"] = currentJson;
        printJsonValue(out);
        return true;
    }

    // Append probes into netlist (for future simulation selection)
    if (autoProbe) {
        for (int i = 0; i < netlist.nodeCount(); ++i) {
            const QString name = QString::fromStdString(netlist.nodeName(i)).trimmed();
            if (name.isEmpty()) continue;
            netlist.addAutoProbe(("V(" + name + ")").toStdString());
        }
    } else {
        for (const QString& sig : addSignals) {
            netlist.addAutoProbe(sig.toStdString());
        }
    }

    QJsonObject out;
    out["file"] = filePath;
    QJsonArray probes;
    for (const auto& p : netlist.autoProbes()) {
        probes.append(QString::fromStdString(p));
    }
    out["probes"] = probes;
    printJsonValue(out);
    Q_UNUSED(outputJson);
    return true;
}

bool runNetlistCompare(const QStringList& args, const QCommandLineParser& parser) {
    if (args.size() < 3) {
        std::cerr << "Usage: vio-cmd netlist-compare <file.flxsch> <external.net>" << std::endl;
        return false;
    }
    const QString schematicPath = args.at(1);
    const QString externalPath = args.at(2);

    if (!QFileInfo::exists(schematicPath)) {
        std::cerr << "Error: Schematic not found: " << schematicPath.toStdString() << std::endl;
        return false;
    }
    if (!QFileInfo::exists(externalPath)) {
        std::cerr << "Error: Netlist not found: " << externalPath.toStdString() << std::endl;
        return false;
    }

    QGraphicsScene scene;
    QString pageSize;
    TitleBlockData dummyTB;
    if (!SchematicFileIO::loadSchematic(&scene, schematicPath, pageSize, dummyTB)) {
        std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
        return false;
    }

    SpiceNetlistGenerator::SimulationParams params;
    QString analysisType = parser.value("analysis").toLower();
    if (analysisType == "tran") {
        params.type = SpiceNetlistGenerator::Transient;
        params.step = parser.value("step").isEmpty() ? "1e-6" : parser.value("step");
        params.stop = parser.value("stop").isEmpty() ? "1e-2" : parser.value("stop");
    } else if (analysisType == "ac") {
        params.type = SpiceNetlistGenerator::AC;
        params.start = "10";
        params.stop = "1e6";
    } else {
        params.type = SpiceNetlistGenerator::OP;
    }

    const QString schematicNetlist = SpiceNetlistGenerator::generate(&scene, QFileInfo(schematicPath).absolutePath(), nullptr, params);

    QFile extFile(externalPath);
    if (!extFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Error: Cannot read netlist file: " << externalPath.toStdString() << std::endl;
        return false;
    }
    const QString externalNetlist = QString::fromUtf8(extFile.readAll());
    extFile.close();

    const QStringList schLines = normalizeNetlistText(schematicNetlist);
    const QStringList extLines = normalizeNetlistText(externalNetlist);

    QMap<QString, int> schCounts;
    for (const QString& l : schLines) schCounts[l]++;
    QMap<QString, int> extCounts;
    for (const QString& l : extLines) extCounts[l]++;

    QSet<QString> allKeys = QSet<QString>(schCounts.keyBegin(), schCounts.keyEnd());
    allKeys.unite(QSet<QString>(extCounts.keyBegin(), extCounts.keyEnd()));

    QJsonArray diffs;
    int onlySch = 0;
    int onlyExt = 0;
    int common = 0;

    for (const QString& key : allKeys) {
        const int a = schCounts.value(key);
        const int b = extCounts.value(key);
        if (a == b && a > 0) {
            common += a;
            continue;
        }
        if (a > b) onlySch += (a - b);
        if (b > a) onlyExt += (b - a);
        QJsonObject d;
        d["line"] = key;
        d["schematicCount"] = a;
        d["externalCount"] = b;
        diffs.append(d);
    }

    QJsonObject out;
    out["schematic"] = schematicPath;
    out["external"] = externalPath;
    out["schematicLineCount"] = schLines.size();
    out["externalLineCount"] = extLines.size();
    out["differences"] = diffs;
    QJsonObject summary;
    summary["common"] = common;
    summary["onlyInSchematic"] = onlySch;
    summary["onlyInExternal"] = onlyExt;
    summary["differentLineCount"] = diffs.size();
    out["summary"] = summary;
    printJsonValue(out);
    return true;
}

bool runNetlistRun(const QString& filePath, const QCommandLineParser& parser) {
    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: Netlist not found: " << filePath.toStdString() << std::endl;
        return false;
    }

    auto& sim = SimulationManager::instance();
    if (!sim.isAvailable()) {
        std::cerr << "Error: Ngspice not available in this build." << std::endl;
        return false;
    }

    QString timeoutError;
    const auto timeoutMsOpt = parseTimeoutMs(parser.value("timeout"), &timeoutError);
    if (!timeoutMsOpt.has_value()) {
        std::cerr << "Error: " << timeoutError.toStdString() << std::endl;
        return false;
    }
    const int timeoutMs = timeoutMsOpt.value();

    QString runPath = filePath;
    std::unique_ptr<QTemporaryFile> tempNetlist;

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "flxsch") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return false;
        }

        SpiceNetlistGenerator::SimulationParams params;
        QString analysisType = parser.value("analysis").toLower();
        if (analysisType == "tran") {
            params.type = SpiceNetlistGenerator::Transient;
            params.step = parser.value("step").isEmpty() ? "1e-6" : parser.value("step");
            params.stop = parser.value("stop").isEmpty() ? "1e-2" : parser.value("stop");
        } else if (analysisType == "ac") {
            params.type = SpiceNetlistGenerator::AC;
            params.start = "10";
            params.stop = "1e6";
        } else {
            params.type = SpiceNetlistGenerator::OP;
        }

        const QString netlist = SpiceNetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), nullptr, params);
        const QString baseName = QFileInfo(filePath).completeBaseName();
        const QString tempPattern = QDir::tempPath() + "/viospice_netlist_" + baseName + "_XXXXXX.cir";
        tempNetlist = std::make_unique<QTemporaryFile>(tempPattern);
        if (!tempNetlist->open()) {
            std::cerr << "Error: Failed to create temporary netlist." << std::endl;
            return false;
        }
        tempNetlist->write(netlist.toUtf8());
        tempNetlist->flush();
        runPath = tempNetlist->fileName();
    }

    const bool exportRaw = parser.isSet("export-raw");
    const bool exportStats = parser.isSet("stats");
    const QStringList measureExprs = parser.values("measure");
    const bool exportMeasures = !measureExprs.isEmpty();
    const bool exportRequested = exportRaw || exportStats || exportMeasures;
    const QString measureFormat = parser.value("measure-format").trimmed().toLower();
    const bool measureFormatJson = (measureFormat == "json");
    if (!measureFormat.isEmpty() && measureFormat != "text" && measureFormat != "json") {
        std::cerr << "Error: Invalid --measure-format. Use text or json." << std::endl;
        return false;
    }

    QStringList outputs;
    QStringList warnings;
    QString errorMsg;
    bool finished = false;

    QObject::connect(&sim, &SimulationManager::outputReceived, &sim, [&](const QString& msg) {
        const QString trimmed = msg.trimmed();
        outputs << trimmed;
        if (isWarningLine(trimmed)) warnings << trimmed;
    });
    QObject::connect(&sim, &SimulationManager::errorOccurred, &sim, [&](const QString& msg) {
        errorMsg = msg;
    });
    QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &sim, [&]() {
        finished = true;
    });

    {
        const bool jsonOut = parser.isSet("json");
        const bool restoreFd = jsonOut || exportRequested;
        const bool silenceFd = g_quiet || jsonOut;
        ScopedFdSilence silence(silenceFd, restoreFd);
        sim.runSimulation(runPath);

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&sim, qOverload<>(&SimulationManager::simulationFinished), &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        if (timeoutMs > 0) {
            timer.start(timeoutMs);
        }
        loop.exec();
    }

    const bool timedOut = !finished;

    const QFileInfo info(runPath);
    const QString rawPath = info.absolutePath() + "/" + info.completeBaseName() + ".raw";
    QString exportRawFormat = parser.value("export-raw").trimmed().toLower();
    if (exportRaw && exportRawFormat.isEmpty()) exportRawFormat = "json";
    bool maxOk = false;
    const int maxPoints = parser.value("max-points").toInt(&maxOk);
    const int maxPointsValue = maxOk ? maxPoints : 0;
    double tStart = std::numeric_limits<double>::quiet_NaN();
    double tEnd = std::numeric_limits<double>::quiet_NaN();
    QString rangeError;
    if (!parseRangeOption(parser.value("range"), &tStart, &tEnd, &rangeError)) {
        std::cerr << "Error: " << rangeError.toStdString() << std::endl;
        return false;
    }

    const bool hasWarnings = !warnings.isEmpty();
    const bool okResult = finished && errorMsg.isEmpty() && !timedOut;
    const bool okForExit = okResult && !(g_exitOnWarning && hasWarnings);

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = runPath;
        out["ok"] = okForExit;
        out["timeout"] = timedOut;
        if (!errorMsg.isEmpty()) out["error"] = errorMsg;
        QJsonArray log;
        for (const QString& line : outputs) log.append(line);
        out["log"] = log;
        out["rawPath"] = rawPath;
        if (hasWarnings) {
            QJsonArray warnArr;
            for (const QString& w : warnings) warnArr.append(w);
            out["warnings"] = warnArr;
        }
        out["hasWarnings"] = hasWarnings;

        if (exportRequested && okResult) {
            RawData data;
            QString err;
            if (loadRawAscii(rawPath, &data, &err)) {
                QStringList signalNames = parser.values("signal");
                if (signalNames.isEmpty()) {
                    for (int i = 1; i < data.varNames.size(); ++i) signalNames << data.varNames[i];
                }
                QVector<int> indices;
                bool ok = true;
                for (const auto& sig : signalNames) {
                    const int idx = data.varNames.indexOf(sig);
                    if (idx < 1) { ok = false; break; }
                    indices << (idx - 1);
                }
                const QVector<int> rangeIndices = filteredIndices(data, tStart, tEnd);
                int baseSignalIndex = -1;
                QString baseError;
                if (!resolveBaseSignalIndex(data, parser.value("base-signal"), &baseSignalIndex, &baseError)) {
                    out["error"] = baseError;
                    printJsonValue(out);
                    _Exit(1);
                }
                if (ok && exportRaw) out["raw"] = rawToJson(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
                if (ok && exportStats) {
                    const auto stats = computeSignalStats(data, signalNames, indices, rangeIndices);
                    QJsonArray statArr;
                    for (const auto& s : stats) {
                        QJsonObject st;
                        st["name"] = s.name;
                        st["min"] = s.min;
                        st["max"] = s.max;
                        st["avg"] = s.avg;
                        st["rms"] = s.rms;
                        statArr.append(st);
                    }
                    out["stats"] = statArr;
                }
                if (exportMeasures) {
                    QJsonArray measArr;
                    for (const auto& expr : measureExprs) {
                        MeasureRequest req;
                        QString perr;
                        QJsonObject m;
                        m["expr"] = expr;
                        if (!parseMeasure(expr, &req, &perr)) {
                            m["error"] = perr;
                            measArr.append(m);
                            continue;
                        }
                        const int varIndex = findVarIndex(data.varNames, req.signalName);
                        if (varIndex < 0) {
                            m["error"] = "Signal not found";
                            measArr.append(m);
                            continue;
                        }
                        if (req.type == MeasureType::At) {
                            const int idx = nearestIndex(data.x, req.atTime);
                            if (idx < 0) {
                                m["error"] = "No samples";
                            } else if (varIndex == 0) {
                                m["value"] = data.x[idx];
                            } else {
                                m["value"] = data.y[varIndex - 1][idx];
                            }
                        } else {
                            const QVector<int>& samples = rangeIndices;
                            if (samples.isEmpty()) {
                                m["error"] = "No samples in range";
                            } else {
                                const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                                double minVal = vec[samples[0]];
                                double maxVal = vec[samples[0]];
                                double sum = 0.0;
                                double sumSq = 0.0;
                                for (int idx : samples) {
                                    const double v = vec[idx];
                                    if (v < minVal) minVal = v;
                                    if (v > maxVal) maxVal = v;
                                    sum += v;
                                    sumSq += v * v;
                                }
                                if (req.type == MeasureType::Min) m["value"] = minVal;
                                else if (req.type == MeasureType::Max) m["value"] = maxVal;
                                else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                                else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / samples.size());
                                else m["value"] = (sum / samples.size());
                            }
                        }
                        measArr.append(m);
                    }
                    out["measures"] = measArr;
                }
            }
        }
        printJsonValue(out);
        _Exit(okForExit ? 0 : 1);
    }

    if (!g_quiet) {
        for (const QString& line : outputs) {
            if (!line.isEmpty()) std::cout << line.toStdString() << std::endl;
        }
    }
    if (timedOut) {
        std::cerr << "Error: Simulation timed out." << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }
    if (!errorMsg.isEmpty()) {
        std::cerr << "Error: " << errorMsg.toStdString() << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }

    if (exportRequested) {
        RawData data;
        QString err;
        if (!loadRawAscii(rawPath, &data, &err)) {
            std::cerr << "Error: " << err.toStdString() << std::endl;
            return false;
        }
        QStringList signalNames = parser.values("signal");
        if (signalNames.isEmpty()) {
            for (int i = 1; i < data.varNames.size(); ++i) signalNames << data.varNames[i];
        }
        QVector<int> indices;
        for (const auto& sig : signalNames) {
            const int idx = data.varNames.indexOf(sig);
            if (idx < 1) {
                std::cerr << "Error: Signal not found: " << sig.toStdString() << std::endl;
                return false;
            }
            indices << (idx - 1);
        }
        const QVector<int> rangeIndices = filteredIndices(data, tStart, tEnd);
        int baseSignalIndex = -1;
        QString baseError;
        if (!resolveBaseSignalIndex(data, parser.value("base-signal"), &baseSignalIndex, &baseError)) {
            std::cerr << "Error: " << baseError.toStdString() << std::endl;
            return false;
        }

        if (exportMeasures && !measureFormatJson) {
            for (const auto& expr : measureExprs) {
                MeasureRequest req;
                QString perr;
                if (!parseMeasure(expr, &req, &perr)) {
                    std::cerr << expr.toStdString() << " error=" << perr.toStdString() << std::endl;
                    continue;
                }
                const int varIndex = findVarIndex(data.varNames, req.signalName);
                if (varIndex < 0) {
                    std::cerr << expr.toStdString() << " error=Signal not found" << std::endl;
                    continue;
                }
                if (req.type == MeasureType::At) {
                    const int idx = nearestIndex(data.x, req.atTime);
                    if (idx < 0) {
                        std::cerr << expr.toStdString() << " error=No samples" << std::endl;
                    } else if (varIndex == 0) {
                        std::cout << expr.toStdString() << " = " << data.x[idx] << std::endl;
                    } else {
                        std::cout << expr.toStdString() << " = " << data.y[varIndex - 1][idx] << std::endl;
                    }
                } else {
                    if (rangeIndices.isEmpty()) {
                        std::cerr << expr.toStdString() << " error=No samples in range" << std::endl;
                    } else {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minVal = vec[rangeIndices[0]];
                        double maxVal = vec[rangeIndices[0]];
                        double sum = 0.0;
                        double sumSq = 0.0;
                        for (int idx : rangeIndices) {
                            const double v = vec[idx];
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                            sum += v;
                            sumSq += v * v;
                        }
                        double value = 0.0;
                        if (req.type == MeasureType::Min) value = minVal;
                        else if (req.type == MeasureType::Max) value = maxVal;
                        else if (req.type == MeasureType::Pp) value = (maxVal - minVal);
                        else if (req.type == MeasureType::Rms) value = std::sqrt(sumSq / rangeIndices.size());
                        else value = (sum / rangeIndices.size());
                        std::cout << expr.toStdString() << " = " << value << std::endl;
                    }
                }
            }
        }
        if (exportMeasures && measureFormatJson && exportRaw && exportRawFormat == "csv") {
            QJsonArray measArr;
            for (const auto& expr : measureExprs) {
                MeasureRequest req;
                QString perr;
                QJsonObject m;
                m["expr"] = expr;
                if (!parseMeasure(expr, &req, &perr)) {
                    m["error"] = perr;
                    measArr.append(m);
                    continue;
                }
                const int varIndex = findVarIndex(data.varNames, req.signalName);
                if (varIndex < 0) {
                    m["error"] = "Signal not found";
                    measArr.append(m);
                    continue;
                }
                if (req.type == MeasureType::At) {
                    const int idx = nearestIndex(data.x, req.atTime);
                    if (idx < 0) m["error"] = "No samples";
                    else if (varIndex == 0) m["value"] = data.x[idx];
                    else m["value"] = data.y[varIndex - 1][idx];
                } else {
                    if (rangeIndices.isEmpty()) {
                        m["error"] = "No samples in range";
                    } else {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minVal = vec[rangeIndices[0]];
                        double maxVal = vec[rangeIndices[0]];
                        double sum = 0.0;
                        double sumSq = 0.0;
                        for (int idx : rangeIndices) {
                            const double v = vec[idx];
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                            sum += v;
                            sumSq += v * v;
                        }
                        if (req.type == MeasureType::Min) m["value"] = minVal;
                        else if (req.type == MeasureType::Max) m["value"] = maxVal;
                        else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                        else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / rangeIndices.size());
                        else m["value"] = (sum / rangeIndices.size());
                    }
                }
                measArr.append(m);
            }
            QJsonObject out;
            out["measures"] = measArr;
            printJsonValueTo(out, std::cerr);
        }
        if (exportMeasures && measureFormatJson && !exportRaw) {
            QJsonArray measArr;
            for (const auto& expr : measureExprs) {
                MeasureRequest req;
                QString perr;
                QJsonObject m;
                m["expr"] = expr;
                if (!parseMeasure(expr, &req, &perr)) {
                    m["error"] = perr;
                    measArr.append(m);
                    continue;
                }
                const int varIndex = findVarIndex(data.varNames, req.signalName);
                if (varIndex < 0) {
                    m["error"] = "Signal not found";
                    measArr.append(m);
                    continue;
                }
                if (req.type == MeasureType::At) {
                    const int idx = nearestIndex(data.x, req.atTime);
                    if (idx < 0) m["error"] = "No samples";
                    else if (varIndex == 0) m["value"] = data.x[idx];
                    else m["value"] = data.y[varIndex - 1][idx];
                } else {
                    if (rangeIndices.isEmpty()) {
                        m["error"] = "No samples in range";
                    } else {
                        const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                        double minVal = vec[rangeIndices[0]];
                        double maxVal = vec[rangeIndices[0]];
                        double sum = 0.0;
                        double sumSq = 0.0;
                        for (int idx : rangeIndices) {
                            const double v = vec[idx];
                            if (v < minVal) minVal = v;
                            if (v > maxVal) maxVal = v;
                            sum += v;
                            sumSq += v * v;
                        }
                        if (req.type == MeasureType::Min) m["value"] = minVal;
                        else if (req.type == MeasureType::Max) m["value"] = maxVal;
                        else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                        else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / rangeIndices.size());
                        else m["value"] = (sum / rangeIndices.size());
                    }
                }
                measArr.append(m);
            }
            QJsonObject out;
            out["measures"] = measArr;
            printJsonValue(out);
        }
        if (exportStats && !exportRaw) {
            const auto stats = computeSignalStats(data, signalNames, indices, rangeIndices);
            for (const auto& s : stats) {
                std::cout << s.name.toStdString()
                          << " min=" << s.min
                          << " max=" << s.max
                          << " avg=" << s.avg
                          << " rms=" << s.rms
                          << std::endl;
            }
        } else if (exportRaw) {
            if (exportRawFormat == "json") {
                QJsonObject out = rawToJson(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
                out["file"] = rawPath;
                if (exportStats) {
                    const auto stats = computeSignalStats(data, signalNames, indices, rangeIndices);
                    QJsonArray statArr;
                    for (const auto& s : stats) {
                        QJsonObject st;
                        st["name"] = s.name;
                        st["min"] = s.min;
                        st["max"] = s.max;
                        st["avg"] = s.avg;
                        st["rms"] = s.rms;
                        statArr.append(st);
                    }
                    out["stats"] = statArr;
                }
                if (exportMeasures) {
                    QJsonArray measArr;
                    for (const auto& expr : measureExprs) {
                        MeasureRequest req;
                        QString perr;
                        QJsonObject m;
                        m["expr"] = expr;
                        if (!parseMeasure(expr, &req, &perr)) {
                            m["error"] = perr;
                            measArr.append(m);
                            continue;
                        }
                        const int varIndex = findVarIndex(data.varNames, req.signalName);
                        if (varIndex < 0) {
                            m["error"] = "Signal not found";
                            measArr.append(m);
                            continue;
                        }
                        if (req.type == MeasureType::At) {
                            const int idx = nearestIndex(data.x, req.atTime);
                            if (idx < 0) m["error"] = "No samples";
                            else if (varIndex == 0) m["value"] = data.x[idx];
                            else m["value"] = data.y[varIndex - 1][idx];
                        } else {
                            if (rangeIndices.isEmpty()) {
                                m["error"] = "No samples in range";
                            } else {
                                const auto& vec = (varIndex == 0) ? data.x : data.y[varIndex - 1];
                                double minVal = vec[rangeIndices[0]];
                                double maxVal = vec[rangeIndices[0]];
                                double sum = 0.0;
                                double sumSq = 0.0;
                                for (int idx : rangeIndices) {
                                    const double v = vec[idx];
                                    if (v < minVal) minVal = v;
                                    if (v > maxVal) maxVal = v;
                                    sum += v;
                                    sumSq += v * v;
                                }
                                if (req.type == MeasureType::Min) m["value"] = minVal;
                                else if (req.type == MeasureType::Max) m["value"] = maxVal;
                                else if (req.type == MeasureType::Pp) m["value"] = (maxVal - minVal);
                                else if (req.type == MeasureType::Rms) m["value"] = std::sqrt(sumSq / rangeIndices.size());
                                else m["value"] = (sum / rangeIndices.size());
                            }
                        }
                        measArr.append(m);
                    }
                    out["measures"] = measArr;
                }
                printJsonValue(out);
            } else {
                std::cout << rawToCsv(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex).toStdString();
            }
        }
        _Exit(okForExit ? 0 : 1);
    }
    if (g_exitOnWarning && hasWarnings) {
        if (!g_quiet) {
            std::cerr << "Warning: ngspice reported warnings during simulation." << std::endl;
        }
        if (g_quiet && !parser.isSet("json") && !exportRequested) _Exit(1);
        return false;
    }
    if (g_quiet && !parser.isSet("json") && !exportRequested) _Exit(okResult ? 0 : 1);
    return okResult;
}

bool runNetlistValidate(const QString& filePath, const QCommandLineParser& parser) {
    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: Netlist not found: " << filePath.toStdString() << std::endl;
        return false;
    }

    auto& sim = SimulationManager::instance();
    if (!sim.isAvailable()) {
        std::cerr << "Error: Ngspice not available in this build." << std::endl;
        return false;
    }

    QStringList outputs;
    QStringList warnings;
    QString errorMsg;
    QObject::connect(&sim, &SimulationManager::outputReceived, &sim, [&](const QString& msg) {
        const QString trimmed = msg.trimmed();
        outputs << trimmed;
        if (isWarningLine(trimmed)) warnings << trimmed;
    });
    QObject::connect(&sim, &SimulationManager::errorOccurred, &sim, [&](const QString& msg) {
        errorMsg = msg;
    });

    const bool ok = [&]() {
        const bool jsonOut = parser.isSet("json");
        const bool restoreFd = jsonOut;
        const bool silenceFd = g_quiet || jsonOut;
        ScopedFdSilence silence(silenceFd, restoreFd);
        return sim.validateNetlist(filePath, &errorMsg);
    }();
    const bool hasWarnings = !warnings.isEmpty();
    const bool okForExit = ok && errorMsg.isEmpty() && !(g_exitOnWarning && hasWarnings);

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["ok"] = okForExit;
        if (!errorMsg.isEmpty()) out["error"] = errorMsg;
        QJsonArray log;
        for (const QString& line : outputs) log.append(line);
        out["log"] = log;
        if (hasWarnings) {
            QJsonArray warnArr;
            for (const QString& w : warnings) warnArr.append(w);
            out["warnings"] = warnArr;
        }
        out["hasWarnings"] = hasWarnings;
        printJsonValue(out);
        return okForExit;
    }

    if (!g_quiet) {
        for (const QString& line : outputs) {
            if (!line.isEmpty()) std::cout << line.toStdString() << std::endl;
        }
    }
    if (!errorMsg.isEmpty()) {
        std::cerr << "Error: " << errorMsg.toStdString() << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }
    if (g_exitOnWarning && hasWarnings) {
        if (!g_quiet) std::cerr << "Warning: ngspice reported warnings during validation." << std::endl;
        if (g_quiet && !parser.isSet("json")) _Exit(1);
        return false;
    }
    if (ok) std::cout << "Netlist OK" << std::endl;
    if (g_quiet && !parser.isSet("json")) _Exit(okForExit ? 0 : 1);
    return okForExit;
}

bool runRawInfo(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    QString error;
    if (!loadRawAscii(filePath, &data, &error)) {
        std::cerr << "Error: " << error.toStdString() << std::endl;
        return false;
    }

    if (parser.isSet("summary")) {
        int voltageCount = 0;
        int currentCount = 0;
        for (const auto& name : data.varNames) {
            if (name.compare("time", Qt::CaseInsensitive) == 0) continue;
            if (name.startsWith("i(", Qt::CaseInsensitive)) currentCount++;
            else voltageCount++;
        }
        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = filePath;
            out["variables"] = data.numVariables;
            out["points"] = data.numPoints;
            out["voltages"] = voltageCount;
            out["currents"] = currentCount;
            printJsonValue(out);
            return true;
        }
        std::cout << "File: " << filePath.toStdString() << std::endl;
        std::cout << "Variables: " << data.numVariables << std::endl;
        std::cout << "Points: " << data.numPoints << std::endl;
        std::cout << "Voltages: " << voltageCount << std::endl;
        std::cout << "Currents: " << currentCount << std::endl;
        return true;
    }

    int baseSignalIndex = -1;
    const QString baseSignalName = parser.value("base-signal").trimmed();
    if (!baseSignalName.isEmpty()) {
        int idx = -1;
        for (int i = 0; i < data.varNames.size(); ++i) {
            if (data.varNames[i].compare(baseSignalName, Qt::CaseInsensitive) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 1) {
            std::cerr << "Error: Base signal not found (or invalid): " << baseSignalName.toStdString() << std::endl;
            return false;
        }
        baseSignalIndex = idx - 1;
    }

    if (parser.isSet("json")) {
        QJsonObject out;
        out["file"] = filePath;
        out["variables"] = data.numVariables;
        out["points"] = data.numPoints;
        QJsonArray names;
        for (const auto& name : data.varNames) names.append(name);
        out["varNames"] = names;
        printJsonValue(out);
        return true;
    }

    std::cout << "File: " << filePath.toStdString() << std::endl;
    std::cout << "Variables: " << data.numVariables << std::endl;
    std::cout << "Points: " << data.numPoints << std::endl;
    for (const auto& name : data.varNames) {
        std::cout << "  " << name.toStdString() << std::endl;
    }
    return true;
}

bool runRawExport(const QString& filePath, const QCommandLineParser& parser) {
    RawData data;
    QString error;
    if (!loadRawAscii(filePath, &data, &error)) {
        std::cerr << "Error: " << error.toStdString() << std::endl;
        return false;
    }

    double tStart = std::numeric_limits<double>::quiet_NaN();
    double tEnd = std::numeric_limits<double>::quiet_NaN();
    QString rangeError;
    if (!parseRangeOption(parser.value("range"), &tStart, &tEnd, &rangeError)) {
        std::cerr << "Error: " << rangeError.toStdString() << std::endl;
        return false;
    }

    QStringList signalNames = parser.values("signal");
    const QString signalRegex = parser.value("signal-regex").trimmed();
    if (signalNames.isEmpty()) {
        for (int i = 1; i < data.varNames.size(); ++i) signalNames << data.varNames[i];
    }
    if (!signalRegex.isEmpty()) {
        QRegularExpression re(signalRegex);
        if (!re.isValid()) {
            std::cerr << "Error: Invalid signal-regex: " << signalRegex.toStdString() << std::endl;
            return false;
        }
        QStringList filtered;
        for (const auto& name : signalNames) {
            if (re.match(name).hasMatch()) filtered << name;
        }
        signalNames = filtered;
    }
    if (signalNames.isEmpty()) {
        std::cerr << "Error: No signals matched." << std::endl;
        return false;
    }

    QVector<int> indices;
    for (const auto& sig : signalNames) {
        const int idx = data.varNames.indexOf(sig);
        if (idx < 1) {
            std::cerr << "Error: Signal not found: " << sig.toStdString() << std::endl;
            return false;
        }
        indices << (idx - 1);
    }
    int baseSignalIndex = -1;
    QString baseError;
    if (!resolveBaseSignalIndex(data, parser.value("base-signal"), &baseSignalIndex, &baseError)) {
        std::cerr << "Error: " << baseError.toStdString() << std::endl;
        return false;
    }

    const QString format = parser.value("format").trimmed().toLower();
    bool ok = false;
    const int maxPoints = parser.value("max-points").toInt(&ok);
    const int maxPointsValue = ok ? maxPoints : 0;
    if (format == "parquet") {
        const QString outPath = parser.value("out").trimmed();
        if (outPath.isEmpty()) {
            std::cerr << "Error: --out is required for parquet export." << std::endl;
            return false;
        }
        const QString csvData = rawToCsv(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
        QTemporaryFile temp(QDir::tempPath() + "/viospice_raw_XXXXXX.csv");
        if (!temp.open()) {
            std::cerr << "Error: Failed to create temp CSV for parquet export." << std::endl;
            return false;
        }
        temp.write(csvData.toUtf8());
        temp.flush();

        QProcess proc;
        QStringList args;
        const QString script = QStringLiteral(
            "import sys\n"
            "try:\n"
            "    import pyarrow.csv as csv\n"
            "    import pyarrow.parquet as pq\n"
            "except Exception as e:\n"
            "    sys.stderr.write('pyarrow import failed: %s\\n' % e)\n"
            "    sys.exit(2)\n"
            "table = csv.read_csv(sys.argv[1])\n"
            "pq.write_table(table, sys.argv[2])\n"
        );
        args << "-c" << script << temp.fileName() << outPath;
        proc.start(QStringLiteral("python3"), args);
        if (!proc.waitForFinished(60000)) {
            std::cerr << "Error: parquet export timed out." << std::endl;
            return false;
        }
        if (proc.exitCode() != 0) {
            const QByteArray err = proc.readAllStandardError();
            std::cerr << "Error: parquet export failed. " << err.toStdString()
                      << "Hint: install pyarrow in a venv: python3 -m venv .venv && . .venv/bin/activate && pip install pyarrow"
                      << std::endl;
            return false;
        }
        if (parser.isSet("json")) {
            QJsonObject out;
            out["file"] = outPath;
            out["format"] = "parquet";
            printJsonValue(out);
        } else {
            std::cout << outPath.toStdString() << std::endl;
        }
        return true;
    }
    if (format == "json") {
        QJsonObject out = rawToJson(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex);
        out["file"] = filePath;
        printJsonValue(out);
        return true;
    }

    // Default CSV
    std::cout << rawToCsv(data, signalNames, indices, maxPointsValue, tStart, tEnd, baseSignalIndex).toStdString();
    return true;
}

void printSchema(const QString& command) {
    QJsonObject root;
    root["command"] = command;

    auto setSchema = [&](const QJsonObject& input, const QJsonObject& output) {
        root["input"] = input;
        root["output"] = output;
    };

    if (command == "schematic-query") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}},
            QJsonObject{
                {"file", "string"},
                {"pageSize", "string"},
                {"components", "array[component]"},
                {"nets", "array[net]"}
            }
        );
    } else if (command == "schematic-netlist") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}, {"options", QJsonObject{{"format", "spice|json"}, {"analysis", "op|tran|ac"}, {"step", "string"}, {"stop", "string"}, {"out", "file"}}}},
            QJsonObject{{"netlist", "string (spice or json)"}}
        );
    } else if (command == "schematic-render") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}}}},
            QJsonObject{{"file", "string"}, {"output", "string"}, {"width", "int"}, {"height", "int"}, {"scale", "number"}, {"transparent", "bool"}, {"bounds", "rect"}}
        );
    } else if (command == "schematic-bom") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}},
            QJsonObject{{"file", "string"}, {"components", "array[component]"}, {"groups", "array[group]"}}
        );
    } else if (command == "schematic-validate") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}},
            QJsonObject{{"file", "string"}, {"erc", "array[erc_issue]"}, {"preflight", "array[string]"}, {"summary", "object"}}
        );
    } else if (command == "schematic-diff") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"a.flxsch", "b.flxsch"}}},
            QJsonObject{{"a", "string"}, {"b", "string"}, {"components", "object{added,removed,changed}"}}
        );
    } else if (command == "schematic-transform") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}, {"options", QJsonObject{{"rename-net", "old=new (repeatable)"}, {"normalize-value", "old=new (repeatable)"}, {"prefix-ref", "old=new"}}}},
            QJsonObject{{"file", "string"}, {"renamedNets", "int"}, {"normalizedValues", "int"}, {"updatedRefs", "int"}}
        );
    } else if (command == "schematic-probe") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch"}}, {"options", QJsonObject{{"list", "bool"}, {"add", "signal (repeatable)"}, {"auto", "bool"}}}},
            QJsonObject{{"file", "string"}, {"signals", "array[string]"}, {"voltages", "array[string]"}, {"currents", "array[string]"}, {"probes", "array[string]"}}
        );
    } else if (command == "netlist-compare") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.flxsch", "external.net"}}, {"options", QJsonObject{{"analysis", "op|tran|ac"}, {"step", "string"}, {"stop", "string"}}}},
            QJsonObject{{"schematic", "string"}, {"external", "string"}, {"schematicLineCount", "int"}, {"externalLineCount", "int"}, {"differences", "array[{line,schematicCount,externalCount}]"}, {"summary", "object"}}
        );
    } else if (command == "netlist-validate") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.cir"}}, {"options", QJsonObject{{"json", "bool"}}}},
            QJsonObject{{"file", "string"}, {"ok", "bool"}, {"error", "string"}, {"log", "array[string]"}}
        );
    } else if (command == "netlist-run") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.cir|file.flxsch"}}, {"options", QJsonObject{{"json", "bool"}, {"timeout", "string"}, {"analysis", "op|tran|ac"}, {"step", "string"}, {"stop", "string"}, {"export-raw", "csv|json"}, {"signal", "name (repeatable)"}, {"max-points", "int"}, {"base-signal", "name"}, {"stats", "bool"}, {"range", "t0:t1"}, {"measure", "expr (repeatable)"}}}},
            QJsonObject{{"file", "string"}, {"ok", "bool"}, {"timeout", "bool"}, {"error", "string"}, {"log", "array[string]"}, {"rawPath", "string"}}
        );
    } else if (command == "raw-info") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.raw"}}, {"options", QJsonObject{{"json", "bool"}, {"summary", "bool"}}}},
            QJsonObject{{"file", "string"}, {"variables", "int"}, {"points", "int"}, {"varNames", "array[string]"}, {"voltages", "int"}, {"currents", "int"}}
        );
    } else if (command == "raw-export") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.raw"}}, {"options", QJsonObject{{"signal", "name (repeatable)"}, {"signal-regex", "pattern"}, {"format", "csv|json|parquet"}, {"max-points", "int"}, {"base-signal", "name"}, {"range", "t0:t1"}, {"out", "file (parquet)"}}}},
            QJsonObject{{"file", "string"}, {"x", "array[number]"}, {"signals", "array[{name,values}]"}}
        );
    } else if (command == "symbol-query") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.viosym"}}},
            QJsonObject{{"file", "string"}, {"name", "string"}, {"modelName", "string"}, {"pins", "array[pin]"}, {"boundingRect", "rect"}}
        );
    } else if (command == "symbol-validate") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.viosym"}}},
            QJsonObject{{"file", "string"}, {"name", "string"}, {"pinCount", "int"}, {"issues", "array[issue]"}, {"summary", "object"}}
        );
    } else if (command == "symbol-render") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"file.viosym", "out.png"}}, {"options", QJsonObject{{"transparent", "bool"}, {"json", "bool"}, {"scale", "number"}}}},
            QJsonObject{{"file", "string"}, {"output", "string"}, {"transparent", "bool"}, {"scale", "number"}}
        );
    } else if (command == "symbol-list") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"folder|library.sclib"}}},
            QJsonObject{{"path", "string"}, {"symbols", "array[{name,source}]"}}
        );
    } else if (command == "symbol-export") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"symbolName", "library.sclib", "out.viosym"}}},
            QJsonObject{{"ok", "bool"}, {"output", "string"}}
        );
    } else if (command == "symbol-import") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"input.asy|input.kicad_sym", "out.viosym|out.sclib"}}, {"options", QJsonObject{{"name", "symbolName (for KiCad)"}}}},
            QJsonObject{{"input", "string"}, {"output", "string"}, {"name", "string"}, {"footprint", "string"}}
        );
    } else if (command == "library-index") {
        setSchema(
            QJsonObject{{"args", QJsonArray{"folder"}}, {"options", QJsonObject{{"include-comments", "bool"}}}},
            QJsonObject{{"root", "string"}, {"symbols", "array[{name,path,type}]"}, {"models", "array[{path,type,subckts,models}]"}, {"modelIndex", "object"}}
        );
    } else {
        root["error"] = "No schema available for this command.";
    }

    printJsonValue(root);
}

bool runPluginPack(const QStringList& args) {
    if (args.size() < 4) {
        std::cerr << "Usage: vio-cmd plugin-pack <manifest.json> <artifact-file> <output.fluxplugin>" << std::endl;
        return false;
    }

    const QString manifestPath = args.at(1);
    const QString artifactPath = args.at(2);
    const QString outputPath = args.at(3);

    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read manifest: " << manifestPath.toStdString() << std::endl;
        return false;
    }
    const QByteArray manifestBytes = manifestFile.readAll();
    manifestFile.close();

    QJsonParseError parseError;
    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
        std::cerr << "Error: Invalid manifest JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }
    const QJsonObject manifest = manifestDoc.object();
    const QString pluginId = manifest.value("id").toString().trimmed();
    const QString version = manifest.value("version").toString().trimmed();
    if (pluginId.isEmpty() || version.isEmpty()) {
        std::cerr << "Error: manifest must contain non-empty id and version fields." << std::endl;
        return false;
    }

    QFile artifactFile(artifactPath);
    if (!artifactFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read artifact: " << artifactPath.toStdString() << std::endl;
        return false;
    }
    const QByteArray artifactBytes = artifactFile.readAll();
    artifactFile.close();

    const QString artifactSha = sha256Hex(artifactBytes);
    const QString artifactName = QFileInfo(artifactPath).fileName();

    QJsonObject payload;
    payload["format"] = "fluxplugin-v1";
    payload["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    payload["manifest"] = manifest;

    QJsonObject artifact;
    artifact["name"] = artifactName;
    artifact["sizeBytes"] = static_cast<qint64>(artifactBytes.size());
    artifact["sha256"] = artifactSha;
    artifact["contentBase64"] = QString::fromLatin1(artifactBytes.toBase64());
    payload["artifact"] = artifact;

    const QJsonDocument outDoc(payload);
    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Error: Cannot write output package: " << outputPath.toStdString() << std::endl;
        return false;
    }
    outFile.write(outDoc.toJson(QJsonDocument::Indented));
    outFile.close();

    if (!g_quiet) {
        std::cout << "Packed plugin:" << std::endl;
        std::cout << "  ID: " << pluginId.toStdString() << std::endl;
        std::cout << "  Version: " << version.toStdString() << std::endl;
        std::cout << "  Artifact: " << artifactName.toStdString() << " (" << artifactBytes.size() << " bytes)" << std::endl;
        std::cout << "  SHA-256: " << artifactSha.toStdString() << std::endl;
        std::cout << "  Output: " << outputPath.toStdString() << std::endl;
    }
    return true;
}

bool runPluginInspect(const QStringList& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: vio-cmd plugin-inspect <package.fluxplugin>" << std::endl;
        return false;
    }
    const QString packagePath = args.at(1);

    QFile packageFile(packagePath);
    if (!packageFile.open(QIODevice::ReadOnly)) {
        std::cerr << "Error: Cannot read package: " << packagePath.toStdString() << std::endl;
        return false;
    }
    const QByteArray bytes = packageFile.readAll();
    packageFile.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        std::cerr << "Error: Invalid package JSON: " << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    const QJsonObject root = doc.object();
    const QString format = root.value("format").toString();
    const QJsonObject manifest = root.value("manifest").toObject();
    const QJsonObject artifact = root.value("artifact").toObject();

    const QString pluginId = manifest.value("id").toString();
    const QString version = manifest.value("version").toString();
    const QString artifactName = artifact.value("name").toString();
    const qint64 sizeBytes = static_cast<qint64>(artifact.value("sizeBytes").toDouble(0.0));
    const QString expectedSha = artifact.value("sha256").toString().toLower();
    const QByteArray content = QByteArray::fromBase64(artifact.value("contentBase64").toString().toLatin1());
    const QString actualSha = sha256Hex(content);

    if (!g_quiet) {
        std::cout << "Package: " << packagePath.toStdString() << std::endl;
        std::cout << "  Format: " << format.toStdString() << std::endl;
        std::cout << "  Plugin ID: " << pluginId.toStdString() << std::endl;
        std::cout << "  Version: " << version.toStdString() << std::endl;
        std::cout << "  Artifact: " << artifactName.toStdString() << std::endl;
        std::cout << "  Declared Size: " << sizeBytes << std::endl;
        std::cout << "  Extracted Size: " << content.size() << std::endl;
        std::cout << "  Declared SHA-256: " << expectedSha.toStdString() << std::endl;
        std::cout << "  Actual SHA-256:   " << actualSha.toStdString() << std::endl;
    }

    const bool sizeOk = (sizeBytes == content.size());
    const bool shaOk = (!expectedSha.isEmpty() && expectedSha == actualSha);
    const bool manifestOk = (!pluginId.trimmed().isEmpty() && !version.trimmed().isEmpty());

    if (!g_quiet) {
        std::cout << "Validation:" << std::endl;
        std::cout << "  Manifest fields: " << (manifestOk ? "OK" : "FAIL") << std::endl;
        std::cout << "  Artifact size:   " << (sizeOk ? "OK" : "FAIL") << std::endl;
        std::cout << "  Artifact sha256: " << (shaOk ? "OK" : "FAIL") << std::endl;
    }

    return manifestOk && sizeOk && shaOk;
}

QString analysisTypeToString(SimAnalysisType type) {
    switch (type) {
        case SimAnalysisType::OP: return "op";
        case SimAnalysisType::Transient: return "transient";
        case SimAnalysisType::AC: return "ac";
        case SimAnalysisType::MonteCarlo: return "monte_carlo";
        case SimAnalysisType::Sensitivity: return "sensitivity";
        case SimAnalysisType::ParametricSweep: return "parametric_sweep";
        case SimAnalysisType::Noise: return "noise";
        case SimAnalysisType::Distortion: return "distortion";
        case SimAnalysisType::Optimization: return "optimization";
        case SimAnalysisType::FFT: return "fft";
        case SimAnalysisType::RealTime: return "real_time";
    }
    return "unknown";
}

QJsonObject resultsToJson(const SimResults& results) {
    QJsonObject root;
    root["analysis"] = analysisTypeToString(results.analysisType);
    root["xAxis"] = QString::fromStdString(results.xAxisName);
    root["yAxis"] = QString::fromStdString(results.yAxisName);

    QJsonArray waves;
    for (const auto& wave : results.waveforms) {
        QJsonObject w;
        w["name"] = QString::fromStdString(wave.name);
        
        QJsonArray xData;
        for (double val : wave.xData) xData.append(val);
        w["x"] = xData;

        QJsonArray yData;
        for (double val : wave.yData) yData.append(val);
        w["y"] = yData;

        if (!wave.yPhase.empty()) {
            QJsonArray yPhase;
            for (double val : wave.yPhase) yPhase.append(val);
            w["phase"] = yPhase;
        }
        waves.append(w);
    }
    root["waveforms"] = waves;

    QJsonObject nodes;
    for (auto it = results.nodeVoltages.begin(); it != results.nodeVoltages.end(); ++it) {
        nodes[QString::fromStdString(it->first)] = it->second;
    }
    root["nodeVoltages"] = nodes;

    QJsonObject branches;
    for (auto it = results.branchCurrents.begin(); it != results.branchCurrents.end(); ++it) {
        branches[QString::fromStdString(it->first)] = it->second;
    }
    root["branchCurrents"] = branches;

    QJsonObject measurements;
    for (auto it = results.measurements.begin(); it != results.measurements.end(); ++it) {
        measurements[QString::fromStdString(it->first)] = it->second;
    }
    root["measurements"] = measurements;

    QJsonArray diags;
    for (const auto& d : results.diagnostics) diags.append(QString::fromStdString(d));
    root["diagnostics"] = diags;

    return root;
}
} // namespace

static void printGeneralHelp() {
    std::cout << "Usage: vio-cmd <command> [file] [options]\n\n";
    std::cout << "Common commands:\n";
    std::cout << "  schematic-query <file.flxsch>\n";
    std::cout << "  schematic-netlist <file.flxsch> [--analysis tran|ac|op] [--step <s>] [--stop <s>]\n";
    std::cout << "  netlist-run <file.cir|file.flxsch> [--analysis tran|ac|op] [--export-raw csv|json]\n";
    std::cout << "  raw-info <file.raw> [--summary --json]\n";
    std::cout << "  raw-export <file.raw> [--format csv|json|parquet] [--out <file>]\n";
    std::cout << "  symbol-render <file.viosym> <out.png>\n";
    std::cout << "  symbol-validate <file.viosym>\n";
    std::cout << "\nTips:\n";
    std::cout << "  Use \"vio-cmd help <command>\" for command-specific help.\n";
    std::cout << "  Use --json for machine-readable output.\n";
}

static void printCommandHelp(const QString& command) {
    if (command == "netlist-run") {
        std::cout << "netlist-run <file.cir|file.flxsch>\n";
        std::cout << "  --analysis tran|ac|op  --step <s>  --stop <s>\n";
        std::cout << "  --export-raw csv|json  --signal <name> (repeatable)\n";
        std::cout << "  --max-points <n>  --base-signal <name>  --range t0:t1\n";
        std::cout << "  --stats  --measure <expr> (repeatable)  --measure-format text|json\n";
        std::cout << "  --quiet  --json  --timeout <10s>\n";
        return;
    }
    if (command == "raw-export") {
        std::cout << "raw-export <file.raw>\n";
        std::cout << "  --format csv|json|parquet  --out <file> (parquet)\n";
        std::cout << "  --signal <name> (repeatable)  --signal-regex <pattern>\n";
        std::cout << "  --max-points <n>  --base-signal <name>  --range t0:t1\n";
        return;
    }
    if (command == "raw-info") {
        std::cout << "raw-info <file.raw>\n";
        std::cout << "  --summary  --json\n";
        return;
    }
    if (command == "schematic-netlist") {
        std::cout << "schematic-netlist <file.flxsch>\n";
        std::cout << "  --analysis tran|ac|op  --step <s>  --stop <s>\n";
        std::cout << "  --format spice|json  --out <file>\n";
        return;
    }
    if (command == "schematic-query") {
        std::cout << "schematic-query <file.flxsch>\n";
        std::cout << "  --json\n";
        return;
    }
    if (command == "symbol-render") {
        std::cout << "symbol-render <file.viosym> <out.png>\n";
        std::cout << "  --scale <n>  --transparent  --json\n";
        return;
    }
    if (command == "symbol-validate") {
        std::cout << "symbol-validate <file.viosym>\n";
        std::cout << "  --json\n";
        return;
    }
    printGeneralHelp();
}

int main(int argc, char *argv[]) {
    // Some GUI classes like QGraphicsScene and QColor require QApplication
    // We run with offscreen platform to keep it CLI-friendly
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setApplicationName("vio-cmd");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Viora EDA Command Line Interface");
    parser.addVersionOption();

    QCommandLineOption analysisOption(QStringList() << "a" << "analysis", "Analysis type (op, tran, ac)", "type", "op");
    parser.addOption(analysisOption);

    QCommandLineOption stopOption(QStringList() << "t" << "stop", "Stop time for transient", "time", "10m");
    parser.addOption(stopOption);

    QCommandLineOption stepOption(QStringList() << "s" << "step", "Step size for transient", "time", "100u");
    parser.addOption(stepOption);

    QCommandLineOption jsonOption("json", "Output results in JSON format");
    QCommandLineOption transparentOption("transparent", "Render PNG with transparent background (schematic-render, symbol-render)");
    QCommandLineOption includeCommentsOption("include-comments", "Parse commented .model/.subckt lines (library-index)");
    QCommandLineOption schemaOption("schema", "Print JSON schema for the command and exit");
    QCommandLineOption scaleOption("scale", "Render scale (default 4.0)", "scale", "4");
    QCommandLineOption quietOption("quiet", "Silence non-JSON output");
    QCommandLineOption exitWarnOption("exit-on-warning", "Exit with non-zero code if warnings appear (netlist-run/netlist-validate)");
    QCommandLineOption helpOption(QStringList() << "h" << "help", "Show help for a command");
    QCommandLineOption noColorOption("no-color", "Disable colored output");
    QCommandLineOption renameNetOption("rename-net", "Rename net label (repeatable): old=new", "pair");
    QCommandLineOption normalizeValueOption("normalize-value", "Normalize value (repeatable): old=new", "pair");
    QCommandLineOption prefixRefOption("prefix-ref", "Rename reference prefix: old=new", "pair");
    QCommandLineOption probeListOption("list", "List available signals (schematic-probe)");
    QCommandLineOption probeAddOption("add", "Add probe (repeatable): V(net) or I(device)", "signal");
    QCommandLineOption probeAutoOption("auto", "Auto-probe all nets (schematic-probe)");
    QCommandLineOption symbolNameOption("name", "Symbol name (for KiCad .kicad_sym import)", "name");
    QCommandLineOption timeoutOption("timeout", "Netlist run timeout (e.g. 10s, 5000ms)", "time", "10s");
    QCommandLineOption formatOption(QStringList() << "f" << "format", "Output format (netlist: spice|json, raw-export: csv|json|parquet)", "format", "spice");
    QCommandLineOption signalOption("signal", "Signal to export (repeatable, raw-export)", "name");
    QCommandLineOption exportRawOption("export-raw", "Export raw data after netlist-run (csv|json)", "format");
    QCommandLineOption maxPointsOption("max-points", "Limit exported samples (raw-export, netlist-run --export-raw)", "count");
    QCommandLineOption baseSignalOption("base-signal", "Signal to drive decimation (raw-export, netlist-run --export-raw)", "name");
    QCommandLineOption statsOption("stats", "Export signal statistics after netlist-run (min/max/avg/rms)");
    QCommandLineOption rangeOption("range", "Limit exported samples to time window (t0:t1)", "t0:t1");
    QCommandLineOption measureOption("measure", "Compute measurement (repeatable). Examples: V(net1)_max, I(V1)_rms, V(net1)@t=1ms", "expr");
    QCommandLineOption measureFormatOption("measure-format", "Measure output format (text|json)", "format", "text");
    QCommandLineOption summaryOption("summary", "Show concise summary (raw-info)");
    QCommandLineOption signalRegexOption("signal-regex", "Filter signals by regex (raw-export)", "pattern");
    QCommandLineOption outOption("out", "Write output to file (schematic-netlist)", "file");
    parser.addOption(jsonOption);
    parser.addOption(transparentOption);
    parser.addOption(includeCommentsOption);
    parser.addOption(schemaOption);
    parser.addOption(scaleOption);
    parser.addOption(quietOption);
    parser.addOption(exitWarnOption);
    parser.addOption(helpOption);
    parser.addOption(noColorOption);
    parser.addOption(renameNetOption);
    parser.addOption(normalizeValueOption);
    parser.addOption(prefixRefOption);
    parser.addOption(probeListOption);
    parser.addOption(probeAddOption);
    parser.addOption(probeAutoOption);
    parser.addOption(symbolNameOption);
    parser.addOption(timeoutOption);
    parser.addOption(formatOption);
    parser.addOption(signalOption);
    parser.addOption(exportRawOption);
    parser.addOption(maxPointsOption);
    parser.addOption(baseSignalOption);
    parser.addOption(statsOption);
    parser.addOption(rangeOption);
    parser.addOption(measureOption);
    parser.addOption(measureFormatOption);
    parser.addOption(summaryOption);
    parser.addOption(signalRegexOption);
    parser.addOption(outOption);

    // Positional arguments
    parser.addPositionalArgument("command", "Command to run: drc, erc, simulate, netlist-run, netlist-validate, raw-info, raw-export, render, schematic-render, symbol-render, symbol-query, symbol-validate, symbol-list, symbol-export, symbol-import, library-index, schematic-query, schematic-netlist, schematic-bom, schematic-validate, schematic-diff, schematic-transform, schematic-probe, netlist-compare, audit, autofix, process, python, plugins-smoke, plugin-pack, plugin-inspect");
    parser.addPositionalArgument("file", "File to process (.pcb or .sch), except for plugins-smoke");
    parser.addPositionalArgument("script", "JSON script file for 'process' command", "");

    parser.process(app);
    g_quiet = parser.isSet("quiet");
    g_exitOnWarning = parser.isSet("exit-on-warning");
    g_noColor = parser.isSet("no-color");
    if (g_noColor) {
        qputenv("NO_COLOR", "1");
    }
    const bool jsonRequested = parser.isSet(jsonOption);
    if (g_quiet || jsonRequested) {
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n"));
    }

    const QStringList args = parser.positionalArguments();
    if (parser.isSet("help")) {
        if (args.size() > 0) {
            printCommandHelp(args.at(0));
        } else {
            printGeneralHelp();
        }
        return 0;
    }

    if (args.size() < 1) {
        printGeneralHelp();
        return 1;
    }

    QString command = args.at(0);
    if (command == "help") {
        if (args.size() > 1) {
            printCommandHelp(args.at(1));
        } else {
            printGeneralHelp();
        }
        return 0;
    }

    if (command == "plugin-pack") {
        return runPluginPack(args) ? 0 : 1;
    }

    if (command == "plugin-inspect") {
        return runPluginInspect(args) ? 0 : 1;
    }

    if (parser.isSet("schema")) {
        printSchema(command);
        return 0;
    }

    if (command == "plugins-smoke") {
        PluginManager::instance().unloadPlugins();
        PluginManager::instance().loadPlugins();

        const auto plugins = PluginManager::instance().activePlugins();
        if (!g_quiet) std::cout << "Loaded plugins: " << plugins.size() << std::endl;
        for (FluxPlugin* plugin : plugins) {
            if (!g_quiet) std::cout << "  - " << plugin->name().toStdString()
                      << " v" << plugin->version().toStdString()
                      << " (sdk " << plugin->sdkVersionMajor()
                      << "." << plugin->sdkVersionMinor() << ")" << std::endl;
        }

        PluginManager::instance().unloadPlugins();
        if (!g_quiet) std::cout << "Plugin lifecycle smoke test completed." << std::endl;
        return 0;
    }

    if (args.size() < 2) {
        parser.showHelp();
        return 1;
    }

    QString filePath = args.at(1);

    if (!QFileInfo::exists(filePath)) {
        std::cerr << "Error: File not found: " << filePath.toStdString() << std::endl;
        return 1;
    }

    // Register items for correct deserialization
    #if VIOSPICE_HAS_PCB
    PCBItemRegistry::registerBuiltInItems();
    #endif
    SchematicItemRegistry::registerBuiltInItems();

    if (command == "drc") {
        #if !VIOSPICE_HAS_PCB
        std::cerr << "PCB features are not available in this build." << std::endl;
        return 1;
        #else
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Running DRC on " << filePath.toStdString() << "..." << std::endl;
        PCBDRC drc;
        drc.runFullCheck(&scene);

        if (drc.violations().isEmpty()) {
            if (!g_quiet) std::cout << "DRC Passed! No violations found." << std::endl;
        } else {
            if (!g_quiet) std::cout << "DRC Failed! Found " << drc.violations().size() << " violations:" << std::endl;
            for (const auto& v : drc.violations()) {
                if (!g_quiet) std::cout << "  [" << v.severityString().toStdString() << "] " 
                          << v.typeString().toStdString() << ": "
                          << v.message().toStdString() << " at (" 
                          << v.location().x() << ", " << v.location().y() << ")" << std::endl;
            }
            return drc.errorCount() > 0 ? 1 : 0;
        }
        #endif
    } else if (command == "erc") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Running ERC on " << filePath.toStdString() << "..." << std::endl;
        auto violations = SchematicERC::run(&scene, QFileInfo(filePath).absolutePath());

        if (violations.isEmpty()) {
            if (!g_quiet) std::cout << "ERC Passed! No issues found." << std::endl;
        } else {
            if (!g_quiet) std::cout << "ERC found " << violations.size() << " issues:" << std::endl;
            for (const auto& v : violations) {
                QString sev = (v.severity == ERCViolation::Error) ? "Error" : "Warning";
                if (!g_quiet) std::cout << "  [" << sev.toStdString() << "] " 
                          << v.message.toStdString() << " at (" 
                          << v.position.x() << ", " << v.position.y() << ")" << std::endl;
            }
        }
    } else if (command == "simulate") {
        QGraphicsScene scene;
        QString pageSize;
        TitleBlockData dummyTB;
        if (!SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
            std::cerr << "Error loading schematic: " << SchematicFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Simulating circuit " << filePath.toStdString() << " (Ngspice backend)..." << std::endl;
        
        QString analysisType = parser.value("analysis").toLower();
        SpiceNetlistGenerator::SimulationParams spiceParams;
        SimAnalysisType t = SimAnalysisType::OP;

        if (analysisType == "tran") {
            t = SimAnalysisType::Transient;
            spiceParams.type = SpiceNetlistGenerator::Transient;
            spiceParams.stop = parser.value("stop");
            spiceParams.step = parser.value("step");
            if (spiceParams.stop.isEmpty()) spiceParams.stop = "10m";
            if (spiceParams.step.isEmpty()) spiceParams.step = "100u";
            if (!g_quiet) std::cout << "  - Type: Transient (Stop=" << spiceParams.stop.toStdString() << ", Step=" << spiceParams.step.toStdString() << ")" << std::endl;
        } else if (analysisType == "ac") {
            t = SimAnalysisType::AC;
            spiceParams.type = SpiceNetlistGenerator::AC;
            spiceParams.start = "10";
            spiceParams.stop = "1meg";
            spiceParams.step = "100";
            if (!g_quiet) std::cout << "  - Type: AC Sweep (10Hz to 1MHz)" << std::endl;
        } else {
            t = SimAnalysisType::OP;
            spiceParams.type = SpiceNetlistGenerator::OP;
            if (!g_quiet) std::cout << "  - Type: DC Operating Point" << std::endl;
        }

        QString netlistText = SpiceNetlistGenerator::generate(&scene, QFileInfo(filePath).absolutePath(), nullptr, spiceParams);
        
        QTemporaryFile tempNetlist(QDir::tempPath() + "/viospice_cli_XXXXXX.cir");
        tempNetlist.setAutoRemove(false);
        if (!tempNetlist.open()) {
            std::cerr << "Error creating temporary netlist file." << std::endl;
            return 1;
        }
        {
            QTextStream out(&tempNetlist);
            out << netlistText;
        }
        tempNetlist.close();

        QEventLoop loop;
        SimResults results;
        bool success = false;
        QString lastError;

        auto& sm = SimulationManager::instance();
        QObject::connect(&sm, &SimulationManager::simulationFinished, &loop, &QEventLoop::quit);
        QObject::connect(&sm, &SimulationManager::errorOccurred, [&](const QString& err) {
            lastError = err;
            loop.quit();
        });
        
        QObject::connect(&sm, &SimulationManager::rawResultsReady, [&](const QString& path) {
            RawData rd;
            if (loadRawAscii(path, &rd, &lastError)) {
                // Simplified SimResults conversion for CLI
                results.analysisType = t;
                for (int i = 0; i < rd.varNames.size(); ++i) {
                    if (i == 0) continue; // skip time/frequency
                    SimWaveform wave;
                    wave.name = rd.varNames[i].toStdString();
                    wave.xData = std::vector<double>(rd.x.begin(), rd.x.end());
                    wave.yData = std::vector<double>(rd.y[i-1].begin(), rd.y[i-1].end());
                    results.waveforms.push_back(wave);
                    
                    if (t == SimAnalysisType::OP && !rd.y[i-1].isEmpty()) {
                        QString qName = QString::fromStdString(wave.name);
                        if (qName.startsWith("V(", Qt::CaseInsensitive)) {
                             results.nodeVoltages[rd.varNames[i].mid(2, rd.varNames[i].size() - 3).toStdString()] = rd.y[i-1][0];
                        } else if (qName.startsWith("I(", Qt::CaseInsensitive)) {
                             results.branchCurrents[rd.varNames[i].mid(2, rd.varNames[i].size() - 3).toStdString()] = rd.y[i-1][0];
                        }
                    }
                }
                success = true;
            }
        });

        sm.runSimulation(tempNetlist.fileName(), nullptr);
        loop.exec();
        
        QFile::remove(tempNetlist.fileName());
        QFile::remove(tempNetlist.fileName() + ".raw");

        if (!success) {
            std::cerr << "Simulation failed: " << lastError.toStdString() << std::endl;
            SimulationManager::instance().shutdown();
            return 1;
        }

        if (parser.isSet(jsonOption)) {
            printJsonValue(resultsToJson(results));
            std::cout.flush();
            std::cerr.flush();
            std::_Exit(0);
        }

        if (results.waveforms.empty() && results.nodeVoltages.empty()) {
            std::cerr << "Simulation failed to produce results." << std::endl;
            SimulationManager::instance().shutdown();
            return 1;
        }

        if (t == SimAnalysisType::OP) {
            if (!g_quiet) std::cout << "\n--- DC Operating Point Results ---" << std::endl;
            for (const auto& [node, v] : results.nodeVoltages) {
                if (!g_quiet) std::cout << "V(" << node << ") = " << v << " V" << std::endl;
            }
            for (const auto& [branch, i] : results.branchCurrents) {
                if (!g_quiet) std::cout << "I(" << branch << ") = " << (i * 1000.0) << " mA" << std::endl;
            }
        } else {
            if (!g_quiet) std::cout << "\nGenerated " << results.waveforms.size() << " waveforms." << std::endl;
            for (const auto& wave : results.waveforms) {
                if (!g_quiet) std::cout << "  - " << wave.name << " (" << wave.yData.size() << " points)" << std::endl;
                if (!wave.yData.empty()) {
                    if (!g_quiet) std::cout << "    Range: [" << *std::min_element(wave.yData.begin(), wave.yData.end()) 
                              << " V, " << *std::max_element(wave.yData.begin(), wave.yData.end()) << " V]" << std::endl;
                }
            }
        }

        if (!g_quiet) std::cout << "\nSimulation successful." << std::endl;
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(0);
    } else if (command == "render") {
        #if !VIOSPICE_HAS_PCB
        std::cerr << "PCB features are not available in this build." << std::endl;
        return 1;
        #else
        if (args.size() < 3) {
            std::cerr << "Usage: vio-cmd render <file.pcb> <output.png>" << std::endl;
            return 1;
        }
        QString output = args.at(2);
        QGraphicsScene scene;
        if (!PCBFileIO::loadPCB(&scene, filePath)) {
            std::cerr << "Error loading PCB: " << PCBFileIO::lastError().toStdString() << std::endl;
            return 1;
        }

        if (!g_quiet) std::cout << "Rendering " << filePath.toStdString() << " to " << output.toStdString() << "..." << std::endl;
        
        QRectF rect = scene.itemsBoundingRect();
        if (rect.isEmpty()) rect = QRectF(-50, -50, 100, 100);
        rect.adjust(-10, -10, 10, 10); // Adding margin

        QImage image(rect.size().toSize() * 4, QImage::Format_ARGB32); // 4x scale for high res
        image.fill(QColor(20, 20, 25)); // Dark board color
        
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene.render(&painter, QRectF(), rect);
        painter.end();

        if (image.save(output)) {
            if (!g_quiet) std::cout << "Successfully rendered scene to " << output.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save image to " << output.toStdString() << std::endl;
            return 1;
        }
        #endif
    } else if (command == "schematic-render") {
        if (args.size() < 3) {
            std::cerr << "Usage: vio-cmd schematic-render <file.flxsch> <out.png>" << std::endl;
            return 1;
        }
        return runSchematicRender(filePath, args.at(2), parser) ? 0 : 1;
    } else if (command == "symbol-render") {
        return runSymbolRender(args, parser) ? 0 : 1;
    } else if (command == "symbol-query") {
        return runSymbolQuery(args) ? 0 : 1;
    } else if (command == "symbol-validate") {
        return runSymbolValidate(args) ? 0 : 1;
    } else if (command == "symbol-list") {
        return runSymbolList(args) ? 0 : 1;
    } else if (command == "symbol-export") {
        return runSymbolExport(args) ? 0 : 1;
    } else if (command == "symbol-import") {
        return runSymbolImport(args, parser) ? 0 : 1;
    } else if (command == "library-index") {
        return runLibraryIndex(args, parser) ? 0 : 1;
    } else if (command == "schematic-query") {
        return runSchematicQuery(filePath) ? 0 : 1;
    } else if (command == "schematic-bom") {
        return runSchematicBom(filePath) ? 0 : 1;
    } else if (command == "schematic-validate") {
        return runSchematicValidate(filePath) ? 0 : 1;
    } else if (command == "schematic-diff") {
        return runSchematicDiff(args) ? 0 : 1;
    } else if (command == "schematic-transform") {
        return runSchematicTransform(filePath, parser) ? 0 : 1;
    } else if (command == "schematic-probe") {
        return runSchematicProbe(filePath, parser) ? 0 : 1;
    } else if (command == "netlist-compare") {
        return runNetlistCompare(args, parser) ? 0 : 1;
    } else if (command == "netlist-run") {
        return runNetlistRun(filePath, parser) ? 0 : 1;
    } else if (command == "netlist-validate") {
        return runNetlistValidate(filePath, parser) ? 0 : 1;
    } else if (command == "raw-info") {
        return runRawInfo(filePath, parser) ? 0 : 1;
    } else if (command == "raw-export") {
        return runRawExport(filePath, parser) ? 0 : 1;
    } else if (command == "schematic-netlist") {
        return runSchematicNetlist(filePath, parser) ? 0 : 1;
    } else if (command == "audit") {
        if (!g_quiet) std::cout << "Project Doctor (Audit) starting on: " << filePath.toStdString() << "..." << std::endl;
        
        QJsonObject report;
        report["project_path"] = filePath;
        report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        
        QJsonArray pcbAudits;
        QJsonArray schAudits;

        QFileInfo info(filePath);
        QStringList pcbFiles;
        QStringList schFiles;

        if (info.isDir()) {
            QDir dir(filePath);
            for (const QString& f : dir.entryList({"*.pcb"}, QDir::Files)) pcbFiles << dir.filePath(f);
            for (const QString& f : dir.entryList({"*.sch"}, QDir::Files)) schFiles << dir.filePath(f);
        } else {
            if (filePath.endsWith(".pcb")) pcbFiles << filePath;
            else if (filePath.endsWith(".sch")) schFiles << filePath;
        }

        // Run DRC on PCBs
        #if VIOSPICE_HAS_PCB
        for (const QString& pcbFile : pcbFiles) {
            QJsonObject pcbReport;
            pcbReport["file"] = QFileInfo(pcbFile).fileName();
            
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, pcbFile)) {
                PCBDRC drc;
                drc.runFullCheck(&scene);
                
                QJsonArray violations;
                for (const auto& v : drc.violations()) {
                    QJsonObject vio;
                    vio["type"] = v.typeString();
                    vio["severity"] = v.severityString();
                    vio["message"] = v.message();
                    vio["x"] = v.location().x();
                    vio["y"] = v.location().y();
                    violations.append(vio);
                }
                pcbReport["violations"] = violations;
                pcbReport["status"] = drc.errorCount() == 0 ? "Healthy" : "Needs Attention";
            } else {
                pcbReport["status"] = "Error Loading";
            }
            pcbAudits.append(pcbReport);
        }
        #endif

        // Run ERC on Schematics
        for (const QString& schFile : schFiles) {
            QJsonObject schReport;
            schReport["file"] = QFileInfo(schFile).fileName();
            
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, schFile, pageSize, dummyTB)) {
                auto violations = SchematicERC::run(&scene, QFileInfo(schFile).absolutePath());
                
                QJsonArray issues;
                for (const auto& v : violations) {
                    QJsonObject issue;
                    issue["severity"] = (v.severity == ERCViolation::Error) ? "Error" : 
                                       (v.severity == ERCViolation::Critical) ? "Critical" : "Warning";
                    issue["message"] = v.message;
                    issue["x"] = v.position.x();
                    issue["y"] = v.position.y();
                    issues.append(issue);
                }
                schReport["issues"] = issues;
                schReport["status"] = issues.isEmpty() ? "Healthy" : "Needs Attention";
            } else {
                schReport["status"] = "Error Loading";
            }
            schAudits.append(schReport);
        }

        report["pcb_reports"] = pcbAudits;
        report["schematic_reports"] = schAudits;
        report["overall_status"] = (pcbFiles.size() + schFiles.size() > 0) ? "Audit Complete" : "No files found";

        QJsonDocument doc(sortJsonValue(report).toObject());
        QDir().mkpath("docs/reports");
        QString reportPath = "docs/reports/project_health_report.json";
        QFile file(reportPath);
        if (file.open(QFile::WriteOnly)) {
            file.write(doc.toJson());
            file.close();
            if (!g_quiet) std::cout << "Project health report generated: " << reportPath.toStdString() << std::endl;
        } else {
            std::cerr << "Failed to save health report." << std::endl;
        }

    } else if (command == "autofix") {
        if (!g_quiet) std::cout << "Project Autofix starting on: " << filePath.toStdString() << "..." << std::endl;
        
        if (filePath.endsWith(".sch")) {
            QGraphicsScene scene;
            QString pageSize;
            TitleBlockData dummyTB;
            if (SchematicFileIO::loadSchematic(&scene, filePath, pageSize, dummyTB)) {
                int fixedCount = 0;
                
                // 1. Run Annotation
                SchematicAnnotator::annotate(&scene, false); // Only fix '?'
                if (!g_quiet) std::cout << "  - Reference annotation completed." << std::endl;

                // 2. Remove duplicate/redundant wires
                QList<WireItem*> wires;
                for (auto* item : scene.items()) {
                    if (auto* w = dynamic_cast<WireItem*>(item)) wires.append(w);
                }

                for (int i = 0; i < wires.size(); ++i) {
                    for (int j = i + 1; j < wires.size(); ++j) {
                        if (wires[i]->startPoint() == wires[j]->startPoint() && 
                            wires[i]->endPoint() == wires[j]->endPoint()) {
                            scene.removeItem(wires[j]);
                            delete wires[j];
                            wires.removeAt(j);
                            j--;
                            fixedCount++;
                        }
                    }
                }
                
                if (!g_quiet && fixedCount > 0) std::cout << "  - Removed " << fixedCount << " duplicate wires." << std::endl;
                
                if (SchematicFileIO::saveSchematic(&scene, filePath, pageSize)) {
                    if (!g_quiet) std::cout << "  - Schematic fixed and saved successfully." << std::endl;
                }
            }
        } else if (filePath.endsWith(".pcb")) {
            #if !VIOSPICE_HAS_PCB
            std::cerr << "PCB features are not available in this build." << std::endl;
            return 1;
            #else
            QGraphicsScene scene;
            if (PCBFileIO::loadPCB(&scene, filePath)) {
                int fixedTraces = 0;
                int snappedPoints = 0;
                int snappedComponents = 0;
                PCBDRC drc;
                double minWidth = drc.rules().minTraceWidth();
                double gridSize = 0.1; // 0.1mm grid for snapping

                // Helper to snap a point to grid
                auto snap = [&](QPointF p) {
                    double x = std::round(p.x() / gridSize) * gridSize;
                    double y = std::round(p.y() / gridSize) * gridSize;
                    return QPointF(x, y);
                };

                // 1. Grid Snapping (Shared points)
                QMap<QString, QPointF> pointMap; 
                auto ptKey = [](QPointF p) { return QString("%1,%2").arg(p.x(), 0, 'f', 4).arg(p.y(), 0, 'f', 4); };

                QList<TraceItem*> traces;
                for (auto* item : scene.items()) {
                    if (auto* trace = dynamic_cast<TraceItem*>(item)) {
                        traces.append(trace);
                        if (trace->width() < minWidth) {
                            trace->setWidth(minWidth);
                            fixedTraces++;
                        }
                        pointMap[ptKey(trace->startPoint())] = snap(trace->startPoint());
                        pointMap[ptKey(trace->endPoint())] = snap(trace->endPoint());
                    } else if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
                        QPointF oldPos = comp->pos();
                        QPointF newPos = snap(oldPos);
                        if (oldPos != newPos) {
                            comp->setPos(newPos);
                            snappedComponents++;
                        }
                    }
                }

                // Apply trace grid snaps
                for (auto* trace : traces) {
                    QPointF oldS = trace->startPoint();
                    QPointF oldE = trace->endPoint();
                    QPointF newS = pointMap[ptKey(oldS)];
                    QPointF newE = pointMap[ptKey(oldE)];

                    if (newS != oldS || newE != oldE) {
                        trace->setStartPoint(newS);
                        trace->setEndPoint(newE);
                        snappedPoints++;
                    }
                }

                if (!g_quiet && fixedTraces > 0) std::cout << "  - Adjusted " << fixedTraces << " traces to minimum width (" << minWidth << "mm)." << std::endl;
                if (!g_quiet && snappedPoints > 0) std::cout << "  - Snapped " << snappedPoints << " trace points to " << gridSize << "mm grid." << std::endl;
                if (!g_quiet && snappedComponents > 0) std::cout << "  - Realigned " << snappedComponents << " components to " << gridSize << "mm grid." << std::endl;
                
                if (PCBFileIO::savePCB(&scene, filePath)) {
                    if (!g_quiet) std::cout << "  - PCB fixed and saved successfully." << std::endl;
                }
            }
            #endif
        } else {
            std::cerr << "Error: Autofix only supports .sch and .pcb files." << std::endl;
            return 1;
        }

    } else if (command == "process") {
        if (args.size() < 3) {
            std::cerr << "Usage: vio-cmd process <file.sch|.pcb> <script.json> [output.file]" << std::endl;
            return 1;
        }
        
        QString scriptPath = args.at(2);
        QString outputPath = (args.size() >= 4) ? args.at(3) : filePath;
        
        QGraphicsScene scene;
        QFile scriptFile(scriptPath);
        if (!scriptFile.open(QIODevice::ReadOnly)) {
            std::cerr << "Error reading script: " << scriptPath.toStdString() << std::endl;
            return 1;
        }
        
        QJsonDocument scriptDoc = QJsonDocument::fromJson(scriptFile.readAll());
        if (!scriptDoc.isArray()) {
            std::cerr << "Error: Script must be a JSON array of commands." << std::endl;
            return 1;
        }

        if (filePath.endsWith(".sch")) {
            SchematicAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading schematic: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            if (!g_quiet) std::cout << "Executed " << count << " schematic commands." << std::endl;
            if (api.save(outputPath)) {
                if (!g_quiet) std::cout << "Saved processed schematic to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed schematic." << std::endl;
                return 1;
            }
        } else if (filePath.endsWith(".pcb")) {
            #if !VIOSPICE_HAS_PCB
            std::cerr << "PCB features are not available in this build." << std::endl;
            return 1;
            #else
            PCBAPI api(&scene);
            if (!api.load(filePath)) {
                std::cerr << "Error loading PCB: " << filePath.toStdString() << std::endl;
                return 1;
            }
            int count = api.executeBatch(scriptDoc.array());
            if (!g_quiet) std::cout << "Executed " << count << " PCB commands." << std::endl;
            if (api.save(outputPath)) {
                if (!g_quiet) std::cout << "Saved processed PCB to: " << outputPath.toStdString() << std::endl;
            } else {
                std::cerr << "Error saving processed PCB." << std::endl;
                return 1;
            }
            #endif
        } else {
            std::cerr << "Error: Unsupported file extension for process." << std::endl;
            return 1;
        }
    } else if (command == "python") {
        if (args.size() < 2) {
            std::cerr << "Usage: vio-cmd python <script.py> [args...]" << std::endl;
            return 1;
        }
        
        QString scriptPath = args.at(1);
        QFile file(scriptPath);
        if (!file.open(QIODevice::ReadOnly)) {
            std::cerr << "Error: Could not open python script: " << scriptPath.toStdString() << std::endl;
            return 1;
        }
        
        QString code = QString::fromUtf8(file.readAll());
        
        FluxPython& py = FluxPython::instance();
        py.initialize();
        
        extern void flux_python_init_bindings();
        flux_python_init_bindings();
        
        QString error;
        if (!py.executeString(code, &error)) {
            std::cerr << "Python Error: " << error.toStdString() << std::endl;
            return 1;
        }
        
        py.finalize();
    } else {
        std::cerr << "Unknown command: " << command.toStdString() << std::endl;
        return 1;
    }

    return 0;
}
