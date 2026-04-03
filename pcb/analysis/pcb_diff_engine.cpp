#include "pcb_diff_engine.h"
#include "../models/board_model.h"
#include "../models/component_model.h"
#include "../models/trace_model.h"
#include "../models/via_model.h"
#include "../models/copper_pour_model.h"
#include "../models/pad_model.h"
#include <QtMath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QFileInfo>

using namespace Flux::Model;

// ============================================================================
// Comparison helpers
// ============================================================================

static bool pointsEqual(QPointF a, QPointF b, double tolerance) {
    return qSqrt((a.x() - b.x()) * (a.x() - b.x()) + (a.y() - b.y()) * (a.y() - b.y())) < tolerance;
}

static bool rotationsEqual(double a, double b, double tolerance) {
    double diff = qAbs(a - b);
    if (diff > 180.0) diff = 360.0 - diff;
    return diff < tolerance;
}

static bool sizesEqual(QSizeF a, QSizeF b, double tolerance) {
    return qAbs(a.width() - b.width()) < tolerance && qAbs(a.height() - b.height()) < tolerance;
}

// ============================================================================
// Main compare
// ============================================================================

DiffReport PCBDiffEngine::compare(const BoardModel* boardA, const BoardModel* boardB,
                                   const CompareOptions& options) {
    DiffReport report;
    report.boardAName = "Board A";
    report.boardBName = "Board B";

    if (!boardA || !boardB) return report;

    compareComponents(boardA, boardB, report, options);
    compareTraces(boardA, boardB, report, options);
    compareVias(boardA, boardB, report, options);
    compareCopperPours(boardA, boardB, report, options);
    compareNets(boardA, boardB, report);

    report.identical = report.stats.isEmpty();
    return report;
}

DiffReport PCBDiffEngine::compareFiles(const QString& filePathA, const QString& filePathB,
                                        const CompareOptions& options) {
    DiffReport report;
    report.boardAName = QFileInfo(filePathA).fileName();
    report.boardBName = QFileInfo(filePathB).fileName();

    BoardModel boardA, boardB;

    QFile fileA(filePathA);
    if (fileA.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(fileA.readAll(), &err);
        if (err.error == QJsonParseError::NoError) {
            boardA.fromJson(doc.object());
        }
    }

    QFile fileB(filePathB);
    if (fileB.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(fileB.readAll(), &err);
        if (err.error == QJsonParseError::NoError) {
            boardB.fromJson(doc.object());
        }
    }

    report = compare(&boardA, &boardB, options);
    report.boardAName = QFileInfo(filePathA).fileName();
    report.boardBName = QFileInfo(filePathB).fileName();
    return report;
}

// ============================================================================
// Component comparison
// ============================================================================

void PCBDiffEngine::compareComponents(const BoardModel* a, const BoardModel* b,
                                       DiffReport& report, const CompareOptions& opts) {
    QMap<QString, ComponentModel*> compsA, compsB;

    for (auto* c : a->components()) compsA[c->name()] = c;
    for (auto* c : b->components()) compsB[c->name()] = c;

    QSet<QString> allRefs;
    for (auto it = compsA.begin(); it != compsA.end(); ++it) allRefs.insert(it.key());
    for (auto it = compsB.begin(); it != compsB.end(); ++it) allRefs.insert(it.key());

    for (const QString& ref : allRefs) {
        bool inA = compsA.contains(ref);
        bool inB = compsB.contains(ref);

        if (!inA && inB) {
            // Added in B
            DiffEntry entry;
            entry.type = DiffType::Added;
            entry.category = "Component";
            entry.identifier = ref;
            entry.description = QString("Component '%1' added (%2, value: %3)")
                .arg(ref, compsB[ref]->componentType(), compsB[ref]->value());
            entry.location = compsB[ref]->pos();
            entry.layer = compsB[ref]->layer();
            report.entries.append(entry);
            report.stats.componentsAdded++;
        } else if (inA && !inB) {
            // Removed in B
            DiffEntry entry;
            entry.type = DiffType::Removed;
            entry.category = "Component";
            entry.identifier = ref;
            entry.description = QString("Component '%1' removed (was %2, value: %3)")
                .arg(ref, compsA[ref]->componentType(), compsA[ref]->value());
            entry.location = compsA[ref]->pos();
            entry.layer = compsA[ref]->layer();
            report.entries.append(entry);
            report.stats.componentsRemoved++;
        } else if (inA && inB) {
            // Both exist — check for modifications
            auto* ca = compsA[ref];
            auto* cb = compsB[ref];

            // Footprint change
            if (opts.compareFootprints && ca->componentType() != cb->componentType()) {
                DiffEntry entry;
                entry.type = DiffType::FootprintChanged;
                entry.category = "Component";
                entry.identifier = ref;
                entry.description = QString("Component '%1' footprint: %2 → %3")
                    .arg(ref, ca->componentType(), cb->componentType());
                entry.location = cb->pos();
                entry.layer = cb->layer();
                report.entries.append(entry);
                report.stats.componentsModified++;
            }

            // Value change
            if (opts.compareValues && ca->value() != cb->value()) {
                DiffEntry entry;
                entry.type = DiffType::ValueChanged;
                entry.category = "Component";
                entry.identifier = ref;
                entry.description = QString("Component '%1' value: %2 → %3")
                    .arg(ref, ca->value(), cb->value());
                entry.location = cb->pos();
                entry.layer = cb->layer();
                report.entries.append(entry);
                report.stats.componentsModified++;
            }

            // Position change
            if (!pointsEqual(ca->pos(), cb->pos(), opts.positionTolerance)) {
                DiffEntry entry;
                entry.type = DiffType::Moved;
                entry.category = "Component";
                entry.identifier = ref;
                entry.description = QString("Component '%1' moved: (%2,%3) → (%4,%5)")
                    .arg(ref)
                    .arg(ca->pos().x(), 0, 'f', 2).arg(ca->pos().y(), 0, 'f', 2)
                    .arg(cb->pos().x(), 0, 'f', 2).arg(cb->pos().y(), 0, 'f', 2);
                entry.location = cb->pos();
                entry.layer = cb->layer();
                report.entries.append(entry);
                report.stats.componentsModified++;
            }

            // Rotation change
            if (!rotationsEqual(ca->rotation(), cb->rotation(), opts.rotationTolerance)) {
                DiffEntry entry;
                entry.type = DiffType::Rotated;
                entry.category = "Component";
                entry.identifier = ref;
                entry.description = QString("Component '%1' rotation: %2° → %3°")
                    .arg(ref).arg(ca->rotation(), 0, 'f', 1).arg(cb->rotation(), 0, 'f', 1);
                entry.location = cb->pos();
                entry.layer = cb->layer();
                report.entries.append(entry);
                report.stats.componentsModified++;
            }

            // Layer change
            if (opts.compareLayers && ca->layer() != cb->layer()) {
                DiffEntry entry;
                entry.type = DiffType::LayerChanged;
                entry.category = "Component";
                entry.identifier = ref;
                entry.description = QString("Component '%1' layer: %2 → %3")
                    .arg(ref).arg(ca->layer()).arg(cb->layer());
                entry.location = cb->pos();
                entry.layer = cb->layer();
                report.entries.append(entry);
                report.stats.componentsModified++;
            }
        }
    }
}

// ============================================================================
// Trace comparison
// ============================================================================

void PCBDiffEngine::compareTraces(const BoardModel* a, const BoardModel* b,
                                   DiffReport& report, const CompareOptions& opts) {
    // Traces don't have unique IDs — compare by position + net
    // We'll use a simple approach: match traces by net + approximate position
    QList<TraceModel*> tracesA = a->traces();
    QList<TraceModel*> tracesB = b->traces();

    QSet<int> matchedB;

    for (int i = 0; i < tracesA.size(); ++i) {
        auto* ta = tracesA[i];
        bool found = false;

        for (int j = 0; j < tracesB.size(); ++j) {
            if (matchedB.contains(j)) continue;
            auto* tb = tracesB[j];

            // Match by net + layer + approximate endpoints
            bool sameNet = !opts.compareNetNames || ta->netName() == tb->netName();
            bool sameLayer = !opts.compareLayers || ta->layer() == tb->layer();
            bool sameEndpoints = pointsEqual(ta->start(), tb->start(), opts.positionTolerance) &&
                                  pointsEqual(ta->end(), tb->end(), opts.positionTolerance);

            if (sameNet && sameLayer && sameEndpoints) {
                // Check width
                if (qAbs(ta->width() - tb->width()) > opts.sizeTolerance) {
                    DiffEntry entry;
                    entry.type = DiffType::Resized;
                    entry.category = "Trace";
                    entry.identifier = QString("%1 [%2→%3]").arg(ta->netName()).arg(ta->width(), 0, 'f', 2).arg(tb->width(), 0, 'f', 2);
                    entry.description = QString("Trace width changed on net '%1': %2 → %3 mm")
                        .arg(ta->netName()).arg(ta->width(), 0, 'f', 2).arg(tb->width(), 0, 'f', 2);
                    entry.location = tb->start();
                    entry.layer = tb->layer();
                    report.entries.append(entry);
                    report.stats.tracesModified++;
                }
                matchedB.insert(j);
                found = true;
                break;
            }
        }

        if (!found) {
            // Removed
            DiffEntry entry;
            entry.type = DiffType::Removed;
            entry.category = "Trace";
            entry.identifier = QString("%1 [%2,%3 → %4,%5]").arg(ta->netName())
                .arg(ta->start().x(), 0, 'f', 1).arg(ta->start().y(), 0, 'f', 1)
                .arg(ta->end().x(), 0, 'f', 1).arg(ta->end().y(), 0, 'f', 1);
            entry.description = QString("Trace removed on net '%1'")
                .arg(ta->netName());
            entry.location = ta->start();
            entry.layer = ta->layer();
            report.entries.append(entry);
            report.stats.tracesRemoved++;
        }
    }

    // Traces in B but not in A (added)
    for (int j = 0; j < tracesB.size(); ++j) {
        if (matchedB.contains(j)) continue;
        auto* tb = tracesB[j];
        DiffEntry entry;
        entry.type = DiffType::Added;
        entry.category = "Trace";
        entry.identifier = QString("%1 [%2,%3 → %4,%5]").arg(tb->netName())
            .arg(tb->start().x(), 0, 'f', 1).arg(tb->start().y(), 0, 'f', 1)
            .arg(tb->end().x(), 0, 'f', 1).arg(tb->end().y(), 0, 'f', 1);
        entry.description = QString("Trace added on net '%1'")
            .arg(tb->netName());
        entry.location = tb->start();
        entry.layer = tb->layer();
        report.entries.append(entry);
        report.stats.tracesAdded++;
    }
}

// ============================================================================
// Via comparison
// ============================================================================

void PCBDiffEngine::compareVias(const BoardModel* a, const BoardModel* b,
                                 DiffReport& report, const CompareOptions& opts) {
    QList<ViaModel*> viasA = a->vias();
    QList<ViaModel*> viasB = b->vias();

    QSet<int> matchedB;

    for (int i = 0; i < viasA.size(); ++i) {
        auto* va = viasA[i];
        bool found = false;

        for (int j = 0; j < viasB.size(); ++j) {
            if (matchedB.contains(j)) continue;
            auto* vb = viasB[j];

            bool samePos = pointsEqual(va->pos(), vb->pos(), opts.positionTolerance);
            bool sameLayers = !opts.compareLayers || (va->startLayer() == vb->startLayer() && va->endLayer() == vb->endLayer());
            bool sameNet = !opts.compareNetNames || va->netName() == vb->netName();

            if (samePos && sameLayers && sameNet) {
                // Check size
                bool sizeChanged = qAbs(va->diameter() - vb->diameter()) > opts.sizeTolerance ||
                                   qAbs(va->drillSize() - vb->drillSize()) > opts.sizeTolerance;
                if (sizeChanged) {
                    DiffEntry entry;
                    entry.type = DiffType::Resized;
                    entry.category = "Via";
                    entry.identifier = QString("Via @ (%1,%2) net=%3").arg(vb->pos().x(), 0, 'f', 2).arg(vb->pos().y(), 0, 'f', 2).arg(vb->netName());
                    entry.description = QString("Via size changed at (%1,%2): ⌀%3→⌀%4")
                        .arg(vb->pos().x(), 0, 'f', 2).arg(vb->pos().y(), 0, 'f', 2)
                        .arg(va->diameter(), 0, 'f', 2).arg(vb->diameter(), 0, 'f', 2);
                    entry.location = vb->pos();
                    entry.layer = vb->startLayer();
                    report.entries.append(entry);
                    report.stats.viasModified++;
                }
                matchedB.insert(j);
                found = true;
                break;
            }
        }

        if (!found) {
            DiffEntry entry;
            entry.type = DiffType::Removed;
            entry.category = "Via";
            entry.identifier = QString("Via @ (%1,%2)").arg(va->pos().x(), 0, 'f', 2).arg(va->pos().y(), 0, 'f', 2);
            entry.description = QString("Via removed at (%1,%2)")
                .arg(va->pos().x(), 0, 'f', 2).arg(va->pos().y(), 0, 'f', 2);
            entry.location = va->pos();
            entry.layer = va->startLayer();
            report.entries.append(entry);
            report.stats.viasRemoved++;
        }
    }

    for (int j = 0; j < viasB.size(); ++j) {
        if (matchedB.contains(j)) continue;
        auto* vb = viasB[j];
        DiffEntry entry;
        entry.type = DiffType::Added;
        entry.category = "Via";
        entry.identifier = QString("Via @ (%1,%2)").arg(vb->pos().x(), 0, 'f', 2).arg(vb->pos().y(), 0, 'f', 2);
        entry.description = QString("Via added at (%1,%2)")
            .arg(vb->pos().x(), 0, 'f', 2).arg(vb->pos().y(), 0, 'f', 2);
        entry.location = vb->pos();
        entry.layer = vb->startLayer();
        report.entries.append(entry);
        report.stats.viasAdded++;
    }
}

// ============================================================================
// Copper pour comparison
// ============================================================================

void PCBDiffEngine::compareCopperPours(const BoardModel* a, const BoardModel* b,
                                        DiffReport& report, const CompareOptions& opts) {
    QList<CopperPourModel*> poursA = a->copperPours();
    QList<CopperPourModel*> poursB = b->copperPours();

    QSet<int> matchedB;

    for (int i = 0; i < poursA.size(); ++i) {
        auto* pa = poursA[i];
        bool found = false;

        for (int j = 0; j < poursB.size(); ++j) {
            if (matchedB.contains(j)) continue;
            auto* pb = poursB[j];

            bool sameNet = !opts.compareNetNames || pa->netName() == pb->netName();
            bool sameLayer = !opts.compareLayers || pa->layer() == pb->layer();
            bool sameSize = sizesEqual(pa->polygon().boundingRect().size(), pb->polygon().boundingRect().size(), opts.sizeTolerance);
            bool samePos = pointsEqual(pa->polygon().boundingRect().center(), pb->polygon().boundingRect().center(), opts.positionTolerance);

            if (sameNet && sameLayer && samePos && sameSize) {
                matchedB.insert(j);
                found = true;
                break;
            }
        }

        if (!found) {
            DiffEntry entry;
            entry.type = DiffType::Removed;
            entry.category = "CopperPour";
            entry.identifier = QString("Pour net=%1 layer=%2").arg(pa->netName()).arg(pa->layer());
            entry.description = QString("Copper pour removed (net: %1, layer: %2)")
                .arg(pa->netName()).arg(pa->layer());
            entry.location = pa->polygon().boundingRect().center();
            entry.layer = pa->layer();
            report.entries.append(entry);
            report.stats.copperPoursRemoved++;
        }
    }

    for (int j = 0; j < poursB.size(); ++j) {
        if (matchedB.contains(j)) continue;
        auto* pb = poursB[j];
        DiffEntry entry;
        entry.type = DiffType::Added;
        entry.category = "CopperPour";
        entry.identifier = QString("Pour net=%1 layer=%2").arg(pb->netName()).arg(pb->layer());
        entry.description = QString("Copper pour added (net: %1, layer: %2)")
            .arg(pb->netName()).arg(pb->layer());
        entry.location = pb->polygon().boundingRect().center();
        entry.layer = pb->layer();
        report.entries.append(entry);
        report.stats.copperPoursAdded++;
    }
}

// ============================================================================
// Net comparison (implicit from pad assignments)
// ============================================================================

void PCBDiffEngine::compareNets(const BoardModel* a, const BoardModel* b, DiffReport& report) {
    QSet<QString> netsA, netsB;

    for (auto* comp : a->components()) {
        for (auto* pad : comp->pads()) {
            if (!pad->netName().isEmpty()) netsA.insert(pad->netName());
        }
    }
    for (auto* comp : b->components()) {
        for (auto* pad : comp->pads()) {
            if (!pad->netName().isEmpty()) netsB.insert(pad->netName());
        }
    }

    for (const QString& net : netsB) {
        if (!netsA.contains(net)) {
            DiffEntry entry;
            entry.type = DiffType::Added;
            entry.category = "Net";
            entry.identifier = net;
            entry.description = QString("Net '%1' added").arg(net);
            report.entries.append(entry);
            report.stats.netsAdded++;
        }
    }

    for (const QString& net : netsA) {
        if (!netsB.contains(net)) {
            DiffEntry entry;
            entry.type = DiffType::Removed;
            entry.category = "Net";
            entry.identifier = net;
            entry.description = QString("Net '%1' removed").arg(net);
            report.entries.append(entry);
            report.stats.netsRemoved++;
        }
    }
}

// ============================================================================
// JSON serialization
// ============================================================================

QString DiffReport::toJson() const {
    QJsonObject root;
    root["boardA"] = boardAName;
    root["boardB"] = boardBName;
    root["identical"] = identical;

    QJsonArray entriesArray;
    for (const auto& e : entries) {
        QJsonObject obj;
        obj["type"] = static_cast<int>(e.type);
        obj["category"] = e.category;
        obj["identifier"] = e.identifier;
        obj["description"] = e.description;
        obj["x"] = e.location.x();
        obj["y"] = e.location.y();
        obj["layer"] = e.layer;
        entriesArray.append(obj);
    }
    root["entries"] = entriesArray;

    QJsonObject statsObj;
    statsObj["componentsAdded"] = stats.componentsAdded;
    statsObj["componentsRemoved"] = stats.componentsRemoved;
    statsObj["componentsModified"] = stats.componentsModified;
    statsObj["tracesAdded"] = stats.tracesAdded;
    statsObj["tracesRemoved"] = stats.tracesRemoved;
    statsObj["tracesModified"] = stats.tracesModified;
    statsObj["viasAdded"] = stats.viasAdded;
    statsObj["viasRemoved"] = stats.viasRemoved;
    statsObj["viasModified"] = stats.viasModified;
    root["stats"] = statsObj;

    QJsonDocument doc(root);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

DiffReport DiffReport::fromJson(const QString& json) {
    DiffReport report;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return report;

    QJsonObject root = doc.object();
    report.boardAName = root["boardA"].toString();
    report.boardBName = root["boardB"].toString();
    report.identical = root["identical"].toBool();

    QJsonArray entriesArray = root["entries"].toArray();
    for (const QJsonValue& val : entriesArray) {
        QJsonObject obj = val.toObject();
        DiffEntry entry;
        entry.type = static_cast<DiffType>(obj["type"].toInt());
        entry.category = obj["category"].toString();
        entry.identifier = obj["identifier"].toString();
        entry.description = obj["description"].toString();
        entry.location = QPointF(obj["x"].toDouble(), obj["y"].toDouble());
        entry.layer = obj["layer"].toInt(-1);
        report.entries.append(entry);
    }

    QJsonObject statsObj = root["stats"].toObject();
    report.stats.componentsAdded = statsObj["componentsAdded"].toInt();
    report.stats.componentsRemoved = statsObj["componentsRemoved"].toInt();
    report.stats.componentsModified = statsObj["componentsModified"].toInt();
    report.stats.tracesAdded = statsObj["tracesAdded"].toInt();
    report.stats.tracesRemoved = statsObj["tracesRemoved"].toInt();
    report.stats.tracesModified = statsObj["tracesModified"].toInt();
    report.stats.viasAdded = statsObj["viasAdded"].toInt();
    report.stats.viasRemoved = statsObj["viasRemoved"].toInt();
    report.stats.viasModified = statsObj["viasModified"].toInt();

    return report;
}
