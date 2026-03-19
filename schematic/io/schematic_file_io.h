#ifndef SCHEMATIC_FILE_IO_H
#define SCHEMATIC_FILE_IO_H

#include <QString>
#include <QJsonObject>
#include <QGraphicsScene>
#include <QMap>
#include <QStringList>
#include <QSet>
#include "../items/schematic_page_item.h"

class SchematicItem;

/**
 * @brief Handles save/load operations for schematic files
 * 
 * File format: JSON-based .sch files containing:
 * - File metadata (version, creation date, etc.)
 * - Page settings (size, orientation)
 * - All schematic items serialized via their toJson() methods
 */
class SchematicFileIO {
public:
    // File format version
    static constexpr int FILE_FORMAT_VERSION = 1;
    
    static bool saveSchematic(QGraphicsScene* scene, const QString& filePath, 
                              const QString& pageSize = "A4", const QString& script = "",
                              const TitleBlockData& titleBlock = TitleBlockData(),
                              const QMap<QString, QList<QString>>& busAliases = QMap<QString, QList<QString>>(),
                              const QSet<QString>& ercExclusions = QSet<QString>(),
                              const QJsonObject* simulationSetup = nullptr);
    static bool saveSchematicAI(QGraphicsScene* scene, const QString& filePath,
                                const QString& pageSize, const QString& script,
                                const TitleBlockData& titleBlock,
                                const QMap<QString, QList<QString>>& busAliases,
                                const QSet<QString>& ercExclusions,
                                const QJsonObject* simulationSetup);
    
    /**
     * @brief Load a schematic from a file
     * @param scene The graphics scene to populate
     * @param filePath Path to the schematic file
     * @param pageSize Output parameter for loaded page size
     * @param titleBlock Output parameter for title block data
     * @param script Optional output parameter to extract embedded FluxScript
     * @return True if load was successful
     */
    static bool loadSchematic(QGraphicsScene* scene, const QString& filePath,
                              QString& pageSize, TitleBlockData& titleBlock, QString* script = nullptr,
                              QMap<QString, QList<QString>>* busAliases = nullptr,
                              QSet<QString>* ercExclusions = nullptr,
                              QJsonObject* simulationSetup = nullptr);
    static bool loadSchematicFromJson(QGraphicsScene* scene, const QJsonObject& root, QString* errorOut = nullptr);
    
    /**
     * @brief Get the last error message
     * @return Description of the last error
     */
    static QString lastError();

    /**
     * @brief Create a schematic item from JSON object
     */
    static SchematicItem* createItemFromJson(const QJsonObject& json);

    /**
     * @brief Serialize the entire scene to a JSON object
     */
    static QJsonObject serializeSceneToJson(QGraphicsScene* scene, const QString& pageSize = "A4");

    /**
     * @brief Generate FluxScript source code from the scene
     */
    static QString convertToFluxScript(QGraphicsScene* scene, class NetManager* netManager = nullptr);

private:
    static QString s_lastError;
    
    /**
     * @brief Serialize all items in the scene to JSON
     */
    static QJsonArray serializeItems(QGraphicsScene* scene);
    
    /**
     * @brief Deserialize items from JSON and add to scene
     */
    static bool deserializeItems(QGraphicsScene* scene, const QJsonArray& itemsArray);
};

#endif // SCHEMATIC_FILE_IO_H
