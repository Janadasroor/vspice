#include "schematic_annotator.h"
#include "schematic_item.h"
#include "../items/generic_component_item.h"
#include "schematic_file_io.h"
#include "flux/symbols/symbol_library.h"
#include <algorithm>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace {

bool isSkippableForAnnotation(const SchematicItem* si) {
    if (!si) return true;
    const SchematicItem::ItemType type = si->itemType();
    if (type == SchematicItem::WireType ||
        type == SchematicItem::BusType ||
        type == SchematicItem::JunctionType ||
        type == SchematicItem::LabelType ||
        type == (QGraphicsItem::UserType + 101)) { // BusEntry
        return true;
    }
    if (si->referencePrefix().startsWith("#")) return true;
    return false;
}

QString baseReference(const QString& ref) {
    const QString trimmed = ref.trimmed();
    const int sep = trimmed.indexOf(':');
    return (sep >= 0 ? trimmed.left(sep) : trimmed).trimmed();
}

int referenceNumber(const QString& ref, const QString& prefix) {
    const QString base = baseReference(ref);
    if (!base.startsWith(prefix, Qt::CaseInsensitive)) return -1;
    const QString suffix = base.mid(prefix.size()).trimmed();
    bool ok = false;
    const int n = suffix.toInt(&ok);
    return ok ? n : -1;
}

QString symbolIdentityKey(const GenericComponentItem* gc) {
    if (!gc) return QString();
    const SymbolDefinition sym = gc->symbol();
    const QString sid = sym.symbolId().trimmed();
    if (!sid.isEmpty()) return sid.toLower();
    return sym.name().trimmed().toLower();
}

} // namespace

QMap<SchematicItem*, QString> SchematicAnnotator::annotate(QGraphicsScene* scene, bool resetExisting, Order order) {
    QMap<SchematicItem*, QString> changes;
    if (!scene) return changes;

    QList<SchematicItem*> items;
    for (QGraphicsItem* gi : scene->items()) {
        SchematicItem* si = dynamic_cast<SchematicItem*>(gi);
        if (!si || isSkippableForAnnotation(si)) continue;

        items.append(si);
    }

    if (items.isEmpty()) return changes;

    // Sorting logic
    std::sort(items.begin(), items.end(), [order](SchematicItem* a, SchematicItem* b) {
        QPointF posA = a->scenePos();
        QPointF posB = b->scenePos();

        if (order == TopToBottom) {
            // Sort by Y first, then X
            if (qAbs(posA.y() - posB.y()) > 1.0) {
                return posA.y() < posB.y();
            }
            return posA.x() < posB.x();
        } else {
            // Sort by X first, then Y
            if (qAbs(posA.x() - posB.x()) > 1.0) {
                return posA.x() < posB.x();
            }
            return posA.y() < posB.y();
        }
    });

    struct AnnotRec {
        SchematicItem* item = nullptr;
        QString prefix;
        QString currentRef;
        bool isMulti = false;
        int unitCount = 1;
        QString multiKey;
    };

    QList<AnnotRec> recs;
    recs.reserve(items.size());
    QMap<QString, int> counters; // per prefix

    for (SchematicItem* si : items) {
        AnnotRec r;
        r.item = si;
        r.prefix = si->referencePrefix();
        r.currentRef = si->reference().trimmed();

        if (auto* gc = dynamic_cast<GenericComponentItem*>(si)) {
            const int uc = qMax(1, gc->symbol().unitCount());
            if (uc > 1) {
                r.isMulti = true;
                r.unitCount = uc;
                r.multiKey = symbolIdentityKey(gc);
            }
        }
        recs.append(r);
        
        if (!resetExisting) {
            const int existing = referenceNumber(r.currentRef, r.prefix);
            if (existing > 0) {
                counters[r.prefix] = std::max(counters.value(r.prefix, 0), existing);
            }
        }
    }

    // Keep existing refs when requested.
    QMap<QString, QList<int>> pendingMulti; // group -> rec indices
    QList<int> pendingSingle;

    for (int i = 0; i < recs.size(); ++i) {
        const AnnotRec& r = recs[i];
        const bool hasValidExisting = (referenceNumber(r.currentRef, r.prefix) > 0);
        if (!resetExisting && hasValidExisting) continue;

        if (r.isMulti && !r.multiKey.isEmpty()) {
            const QString key = r.prefix + "|" + r.multiKey;
            pendingMulti[key].append(i);
        } else {
            pendingSingle.append(i);
        }
    }

    // Singles: annotate one-by-one.
    for (int idx : pendingSingle) {
        const AnnotRec& r = recs[idx];
        const QString newRef = r.prefix + QString::number(++counters[r.prefix]);
        if (newRef != r.item->reference()) {
            changes[r.item] = newRef;
            r.item->setReference(newRef);
        }
    }

    // Multi-unit symbols: assign package refs in chunks of unitCount.
    for (auto it = pendingMulti.begin(); it != pendingMulti.end(); ++it) {
        const QList<int> idxs = it.value();
        if (idxs.isEmpty()) continue;

        const AnnotRec& first = recs[idxs.first()];
        const int chunk = qMax(1, first.unitCount);
        int start = 0;
        while (start < idxs.size()) {
            const QString newRef = first.prefix + QString::number(++counters[first.prefix]);
            const int end = qMin(start + chunk, idxs.size());
            for (int j = start; j < end; ++j) {
                SchematicItem* si = recs[idxs[j]].item;
                if (newRef != si->reference()) {
                    changes[si] = newRef;
                    si->setReference(newRef);
                }
            }
            start = end;
        }
    }

    return changes;
}

QMap<SchematicItem*, QString> SchematicAnnotator::resetAnnotations(QGraphicsScene* scene) {
    QMap<SchematicItem*, QString> changes;
    if (!scene) return changes;

    for (QGraphicsItem* gi : scene->items()) {
        SchematicItem* si = dynamic_cast<SchematicItem*>(gi);
        if (!si || isSkippableForAnnotation(si)) continue;

        QString newRef = si->referencePrefix() + "?";
        if (si->reference() != newRef) {
            changes[si] = newRef;
            si->setReference(newRef);
        }
    }
    return changes;
}

void SchematicAnnotator::annotateProject(const QString& rootFilePath, const QString& projectDir, bool resetExisting, Order order) {
    if (rootFilePath.isEmpty()) return;

    // 1. Recursive collection of all project files linked via "Sheet" items
    QStringList allFiles;
    QList<QString> queue;
    queue.append(rootFilePath);
    allFiles.append(rootFilePath);

    int head = 0;
    while(head < queue.size()) {
        QString currentPath = queue[head++];
        QFile file(currentPath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) continue;
        
        QJsonObject root = doc.object();
        QJsonArray items = root["items"].toArray();
        for (const QJsonValue& val : items) {
            QJsonObject item = val.toObject();
            if (item["type"].toString() == "Sheet") {
                QString subFile = item["fileName"].toString();
                if (!subFile.isEmpty()) {
                    QString fullSubPath = subFile;
                    if (QFileInfo(subFile).isRelative()) {
                        fullSubPath = projectDir + "/" + subFile;
                    }
                    if (!allFiles.contains(fullSubPath) && QFile::exists(fullSubPath)) {
                        allFiles.append(fullSubPath);
                        queue.append(fullSubPath);
                    }
                }
            }
        }
    }

    // 2. Load all component metadata into a sortable global list
    struct GlobalComp { 
        QString filePath; 
        int indexInFile; 
        qreal y; 
        qreal x; 
        QString prefix; 
        QString originalRef;
        QString typeName;
        int unitCount = 1;
    };
    QList<GlobalComp> allComps;
    QMap<QString, QJsonObject> fileRoots;
    QMap<QString, int> knownUnitCountByType;
    QMap<QString, int> counters;

    auto unitCountForType = [&](const QString& typeName) -> int {
        const QString key = typeName.trimmed();
        if (key.isEmpty()) return 1;
        if (knownUnitCountByType.contains(key)) return knownUnitCountByType.value(key);
        int uc = 1;
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol(key)) {
            uc = qMax(1, def->unitCount());
        }
        knownUnitCountByType[key] = uc;
        return uc;
    };

    for (const QString& filePath : allFiles) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        fileRoots[filePath] = root;
        file.close();

        QJsonArray items = root["items"].toArray();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject obj = items[i].toObject();
            QString type = obj["type"].toString();
            
            // Filter non-components
            if (type == "Wire" || type == "Bus" || type == "Junction" || type == "Text" || type == "Net Label") continue;
            if (type == "HierarchicalPort" || type == "Sheet") continue;
            if (type == "GND" || type == "VCC" || type == "VDD") continue;

            // Determine prefix
            QString prefix = "U";
            if (type.contains("Resistor")) prefix = "R";
            else if (type.contains("Capacitor")) prefix = "C";
            else if (type.contains("Inductor")) prefix = "L";
            else if (type.contains("Diode")) prefix = "D";
            else if (type.contains("Transistor")) prefix = "Q";
            else if (obj.contains("reference")) {
                QString ref = obj["reference"].toString();
                QString foundPrefix;
                for(QChar c : ref) if (c.isLetter()) foundPrefix += c; else break;
                if (!foundPrefix.isEmpty()) prefix = foundPrefix;
            }

            const QString ref = obj["reference"].toString();
            if (!resetExisting) {
                const int existing = referenceNumber(ref, prefix);
                if (existing > 0) counters[prefix] = std::max(counters.value(prefix, 0), existing);
            }
            allComps.append({filePath, i, obj["y"].toDouble(), obj["x"].toDouble(), prefix, ref, type, unitCountForType(type)});
        }
    }

    // 3. Sort globally across all sheets to maintain a logical numbering flow
    std::sort(allComps.begin(), allComps.end(), [order](const GlobalComp& a, const GlobalComp& b) {
        if (order == TopToBottom) {
            if (qAbs(a.y - b.y) > 5.0) return a.y < b.y;
            return a.x < b.x;
        } else {
            if (qAbs(a.x - b.x) > 5.0) return a.x < b.x;
            return a.y < b.y;
        }
    });

    // 4. Assign unique designators using project-wide counters (multi-unit aware)
    QMap<QString, QList<int>> pendingMulti;
    QList<int> pendingSingle;

    for (int i = 0; i < allComps.size(); ++i) {
        const GlobalComp& c = allComps[i];
        const bool hasValidExisting = (referenceNumber(c.originalRef, c.prefix) > 0);
        if (!resetExisting && hasValidExisting) continue;

        if (c.unitCount > 1) {
            const QString key = c.prefix + "|" + c.typeName.toLower();
            pendingMulti[key].append(i);
        } else {
            pendingSingle.append(i);
        }
    }

    auto writeRef = [&](int compIndex, const QString& newRef) {
        const GlobalComp& c = allComps[compIndex];
        QJsonObject& root = fileRoots[c.filePath];
        QJsonArray items = root["items"].toArray();
        QJsonObject itemObj = items[c.indexInFile].toObject();
        itemObj["reference"] = newRef;
        items[c.indexInFile] = itemObj;
        root["items"] = items;
    };

    for (int idx : pendingSingle) {
        const GlobalComp& c = allComps[idx];
        const QString newRef = c.prefix + QString::number(++counters[c.prefix]);
        writeRef(idx, newRef);
    }

    for (auto it = pendingMulti.begin(); it != pendingMulti.end(); ++it) {
        const QList<int> idxs = it.value();
        if (idxs.isEmpty()) continue;
        const GlobalComp& first = allComps[idxs.first()];
        const int chunk = qMax(1, first.unitCount);
        int start = 0;
        while (start < idxs.size()) {
            const QString newRef = first.prefix + QString::number(++counters[first.prefix]);
            const int end = qMin(start + chunk, idxs.size());
            for (int j = start; j < end; ++j) {
                writeRef(idxs[j], newRef);
            }
            start = end;
        }
    }

    // 5. Commit changes to disk
    for (auto it = fileRoots.begin(); it != fileRoots.end(); ++it) {
        QFile file(it.key());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(it.value()).toJson());
            file.close();
        }
    }
}
