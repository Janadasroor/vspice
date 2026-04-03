#include "length_measurement_engine.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../items/pad_item.h"
#include "../items/pcb_item.h"
#include <QGraphicsScene>
#include <QtMath>

// Speed of light = 299.79 mm/ns, in FR4 (Er~4) it's ~150 mm/ns = 150 mm/1000ps
// So delay = length_mm / 150 * 1000 = length_mm * 6.67 ps/mm
static const double SPEED_OF_LIGHT_MM_PER_PS = 0.299792458; // mm/ps in vacuum
static const double FR4_EFFECTIVE_ER = 4.0;

// ============================================================================
// Measure all nets
// ============================================================================

QMap<QString, NetLengthData> LengthMeasurementEngine::measureAllNets(QGraphicsScene* scene) {
    QMap<QString, NetLengthData> result;

    if (!scene) return result;

    // Collect all unique net names
    QStringList netNames = getNetNames(scene);

    for (const QString& netName : netNames) {
        result[netName] = measureNet(scene, netName);
    }

    return result;
}

NetLengthData LengthMeasurementEngine::measureNet(QGraphicsScene* scene, const QString& netName) {
    NetLengthData data;
    data.netName = netName;

    if (!scene || netName.isEmpty()) return data;

    // Measure trace segments
    auto traces = findTracesForNet(scene, netName);
    data.segmentCount = traces.size();

    for (auto* trace : traces) {
        double segLen = QLineF(trace->startPoint(), trace->endPoint()).length();
        data.traceLength += segLen;
    }

    // Measure vias
    auto vias = findViasForNet(scene, netName);
    data.viaCount = vias.size();

    for (auto* via : vias) {
        double viaLen = computeViaLength(1.6, via->startLayer(), via->endLayer());
        data.viaLength += viaLen;
    }

    // Total length
    data.totalLength = data.traceLength + data.viaLength;

    // Propagation delay
    data.propagationDelayPs = computePropagationDelay(data.totalLength);

    // Pad positions
    auto pads = findPadsForNet(scene, netName);
    for (auto* pad : pads) {
        data.padPositions.append(pad->scenePos());
    }

    // Detect differential pair membership
    auto diffPairs = detectDiffPairs(scene);
    for (auto it = diffPairs.begin(); it != diffPairs.end(); ++it) {
        if (it.value().first == netName) {
            data.isDiffPairP = true;
            data.diffPairName = it.key();
            break;
        }
        if (it.value().second == netName) {
            data.isDiffPairN = true;
            data.diffPairName = it.key();
            break;
        }
    }

    return data;
}

QStringList LengthMeasurementEngine::getNetNames(QGraphicsScene* scene) {
    QSet<QString> nets;

    if (!scene) return nets.values();

    for (auto* item : scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            if (!trace->netName().isEmpty()) {
                nets.insert(trace->netName());
            }
        }
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            if (!via->netName().isEmpty()) {
                nets.insert(via->netName());
            }
        }
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (!pad->netName().isEmpty()) {
                nets.insert(pad->netName());
            }
        }
    }

    return nets.values();
}

// ============================================================================
// Differential Pair Detection
// ============================================================================

QMap<QString, QPair<QString, QString>> LengthMeasurementEngine::detectDiffPairs(QGraphicsScene* scene) {
    QMap<QString, QPair<QString, QString>> pairs;
    auto netNames = getNetNames(scene);

    // Pattern 1: NAME_P and NAME_N
    QRegularExpression pattern1("^(.+)[_\\-](P|N|\\+|\\-)$");

    // Pattern 2: NAME_Px and NAME_Nx (with index)
    QRegularExpression pattern2("^(.+)[_\\-](P|N)(\\d+)$");

    // Group nets by base name
    QMap<QString, QStringList> baseGroups;
    for (const QString& net : netNames) {
        QRegularExpressionMatch match2 = pattern2.match(net);
        QRegularExpressionMatch match1 = pattern1.match(net);

        if (match2.hasMatch()) {
            QString base = match2.captured(1) + match2.captured(2); // e.g. "USB_DP", "USB_DN"
            QString idx = match2.captured(3);
            QString fullBase = match2.captured(1) + "_" + idx;
            baseGroups[fullBase].append(net);
        } else if (match1.hasMatch()) {
            QString base = match1.captured(1);
            QString polarity = match1.captured(2);
            QString cleanPolarity = (polarity == "+") ? "P" : (polarity == "-") ? "N" : polarity;
            baseGroups[base].append(net);
        }
    }

    // Find P/N pairs
    for (auto it = baseGroups.begin(); it != baseGroups.end(); ++it) {
        const QString& base = it.key();
        const QStringList& members = it.value();

        QString pNet, nNet;
        for (const QString& net : members) {
            if (net.contains("_P", Qt::CaseInsensitive) || net.contains("+")) {
                pNet = net;
            } else if (net.contains("_N", Qt::CaseInsensitive) || net.contains("-")) {
                nNet = net;
            }
        }

        if (!pNet.isEmpty() && !nNet.isEmpty()) {
            pairs[base] = qMakePair(pNet, nNet);
        }
    }

    return pairs;
}

// ============================================================================
// Propagation Delay
// ============================================================================

double LengthMeasurementEngine::computePropagationDelay(double lengthMm, double effectiveEr) {
    // v = c / sqrt(Er), delay = length / v
    // delay_ps = length_mm / (c_mm_ps / sqrt(Er))
    // c = 0.299792458 mm/ps
    double velocity = SPEED_OF_LIGHT_MM_PER_PS / qSqrt(effectiveEr);
    return lengthMm / velocity;
}

double LengthMeasurementEngine::computeViaLength(double boardThicknessMm, int startLayer, int endLayer) {
    // Simplified: assume through-hole via spans entire board
    // For blind/buried vias, compute fraction based on layer indices
    if (startLayer == endLayer) return 0.0;

    // Standard 2-layer: via spans full thickness
    // Multi-layer: compute fraction based on layer order
    int layerSpan = qAbs(endLayer - startLayer);
    // Assume layers are evenly spaced
    double fraction = static_cast<double>(layerSpan) / 1000.0; // Normalize (arbitrary layer spacing)
    return boardThicknessMm * qMin(fraction, 1.0);
}

double LengthMeasurementEngine::computeSkew(double len1, double len2) {
    return qAbs(len1 - len2);
}

// ============================================================================
// Helper: Find items by net
// ============================================================================

QMultiMap<QString, TraceItem*> LengthMeasurementEngine::findTracesForNet(QGraphicsScene* scene, const QString& netName) {
    QMultiMap<QString, TraceItem*> result;
    if (!scene) return result;

    for (auto* item : scene->items()) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            if (trace->netName() == netName) {
                result.insert(netName, trace);
            }
        }
    }
    return result;
}

QList<ViaItem*> LengthMeasurementEngine::findViasForNet(QGraphicsScene* scene, const QString& netName) {
    QList<ViaItem*> result;
    if (!scene) return result;

    for (auto* item : scene->items()) {
        if (auto* via = dynamic_cast<ViaItem*>(item)) {
            if (via->netName() == netName) {
                result.append(via);
            }
        }
    }
    return result;
}

QList<PadItem*> LengthMeasurementEngine::findPadsForNet(QGraphicsScene* scene, const QString& netName) {
    QList<PadItem*> result;
    if (!scene) return result;

    for (auto* item : scene->items()) {
        if (auto* pad = dynamic_cast<PadItem*>(item)) {
            if (pad->netName() == netName) {
                result.append(pad);
            }
        }
    }
    return result;
}
