#ifndef SCHEMATIC_DIFF_ENGINE_H
#define SCHEMATIC_DIFF_ENGINE_H

#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QUuid>

/**
 * @brief Represents a single change between two schematic versions.
 */
struct SchematicDiffItem {
    enum ChangeType {
        Added,
        Removed,
        Modified
    };

    QUuid itemId;
    ChangeType type;
    QJsonObject stateA; // Null if Added
    QJsonObject stateB; // Null if Removed
    bool positionChanged = false;
    bool propertiesChanged = false;
};

/**
 * @brief Engine to calculate differences between two schematic JSON documents.
 */
class SchematicDiffEngine {
public:
    /**
     * @brief Compare two schematic JSON roots and return list of differences.
     */
    static QList<SchematicDiffItem> compare(const QJsonObject& rootA, const QJsonObject& rootB);

private:
    static QMap<QUuid, QJsonObject> indexItems(const QJsonArray& items);
};

#endif // SCHEMATIC_DIFF_ENGINE_H
