#include "schematic_diff_engine.h"
#include <QMap>

QList<SchematicDiffItem> SchematicDiffEngine::compare(const QJsonObject& rootA, const QJsonObject& rootB) {
    QList<SchematicDiffItem> diffs;

    QMap<QUuid, QJsonObject> itemsA = indexItems(rootA["items"].toArray());
    QMap<QUuid, QJsonObject> itemsB = indexItems(rootB["items"].toArray());

    // 1. Find Added or Modified items
    for (auto it = itemsB.begin(); it != itemsB.end(); ++it) {
        QUuid id = it.key();
        const QJsonObject& objB = it.value();

        if (!itemsA.contains(id)) {
            // Added
            SchematicDiffItem diff;
            diff.itemId = id;
            diff.type = SchematicDiffItem::Added;
            diff.stateB = objB;
            diffs.append(diff);
        } else {
            // Potential modification
            const QJsonObject& objA = itemsA[id];
            
            bool posChanged = (objA["x"].toDouble() != objB["x"].toDouble() || 
                               objA["y"].toDouble() != objB["y"].toDouble());
            
            // Check properties (ignoring ID/X/Y)
            bool propsChanged = false;
            QStringList keys = objB.keys();
            for (const QString& key : keys) {
                if (key == "x" || key == "y" || key == "id") continue;
                if (objA[key] != objB[key]) {
                    propsChanged = true;
                    break;
                }
            }

            if (posChanged || propsChanged) {
                SchematicDiffItem diff;
                diff.itemId = id;
                diff.type = SchematicDiffItem::Modified;
                diff.stateA = objA;
                diff.stateB = objB;
                diff.positionChanged = posChanged;
                diff.propertiesChanged = propsChanged;
                diffs.append(diff);
            }
        }
    }

    // 2. Find Removed items
    for (auto it = itemsA.begin(); it != itemsA.end(); ++it) {
        if (!itemsB.contains(it.key())) {
            SchematicDiffItem diff;
            diff.itemId = it.key();
            diff.type = SchematicDiffItem::Removed;
            diff.stateA = it.value();
            diffs.append(diff);
        }
    }

    return diffs;
}

QMap<QUuid, QJsonObject> SchematicDiffEngine::indexItems(const QJsonArray& items) {
    QMap<QUuid, QJsonObject> map;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject obj = items[i].toObject();
        QUuid id = QUuid::fromString(obj["id"].toString());
        if (!id.isNull()) map[id] = obj;
    }
    return map;
}
