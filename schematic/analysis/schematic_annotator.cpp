#include "schematic_annotator.h"
#include "schematic_item.h"
#include "schematic_file_io.h"
#include <algorithm>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

QMap<SchematicItem*, QString> SchematicAnnotator::annotate(QGraphicsScene* scene, bool resetExisting, Order order) {
    QMap<SchematicItem*, QString> changes;
    if (!scene) return changes;

    QList<SchematicItem*> items;
    for (QGraphicsItem* gi : scene->items()) {
        SchematicItem* si = dynamic_cast<SchematicItem*>(gi);
        if (!si) continue;

        // Skip non-component items
        SchematicItem::ItemType type = si->itemType();
        if (type == SchematicItem::WireType || 
            type == SchematicItem::BusType || 
            type == SchematicItem::JunctionType ||
            type == SchematicItem::LabelType ||
            type == (QGraphicsItem::UserType + 101)) { // BusEntry
            continue;
        }

        // Power items usually have fixed references like #GND
        if (si->referencePrefix().startsWith("#")) continue;

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

    QMap<QString, int> counters;
    
    // Optional: First pass to keep existing annotations if requested
    if (!resetExisting) {
        for (SchematicItem* si : items) {
            QString ref = si->reference();
            QString prefix = si->referencePrefix();
            if (!ref.contains("?") && ref.startsWith(prefix)) {
                bool ok;
                int num = ref.mid(prefix.length()).toInt(&ok);
                if (ok) {
                    counters[prefix] = std::max(counters[prefix], num);
                }
            }
        }
    }

    // Second pass: Assign new references
    for (SchematicItem* si : items) {
        QString prefix = si->referencePrefix();
        QString currentRef = si->reference();

        if (resetExisting || currentRef.contains("?") || !currentRef.startsWith(prefix)) {
            int nextIdx = ++counters[prefix];
            QString newRef = prefix + QString::number(nextIdx);
            if (newRef != currentRef) {
                changes[si] = newRef;
                si->setReference(newRef);
            }
        }
    }
    return changes;
}

QMap<SchematicItem*, QString> SchematicAnnotator::resetAnnotations(QGraphicsScene* scene) {
    QMap<SchematicItem*, QString> changes;
    if (!scene) return changes;

    for (QGraphicsItem* gi : scene->items()) {
        SchematicItem* si = dynamic_cast<SchematicItem*>(gi);
        if (!si) continue;

        // Power items usually have fixed references
        if (si->referencePrefix().startsWith("#")) continue;

        // Skip non-components as in annotate()
        SchematicItem::ItemType type = si->itemType();
        if (type == SchematicItem::WireType || type == SchematicItem::BusType || 
            type == SchematicItem::JunctionType || type == SchematicItem::LabelType) {
            continue;
        }

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
    };
    QList<GlobalComp> allComps;
    QMap<QString, QJsonObject> fileRoots;

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

            allComps.append({filePath, i, obj["y"].toDouble(), obj["x"].toDouble(), prefix, obj["reference"].toString()});
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

    // 4. Assign unique designators using project-wide counters
    QMap<QString, int> counters;
    for (int i = 0; i < allComps.size(); ++i) {
        QString newRef = allComps[i].prefix + QString::number(++counters[allComps[i].prefix]);
        
        // Update the cached JSON object
        QJsonObject& root = fileRoots[allComps[i].filePath];
        QJsonArray items = root["items"].toArray();
        QJsonObject itemObj = items[allComps[i].indexInFile].toObject();
        itemObj["reference"] = newRef;
        items[allComps[i].indexInFile] = itemObj;
        root["items"] = items;
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
