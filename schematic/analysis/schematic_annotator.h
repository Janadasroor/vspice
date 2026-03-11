#ifndef SCHEMATIC_ANNOTATOR_H
#define SCHEMATIC_ANNOTATOR_H

#include <QList>
#include <QGraphicsScene>
#include <QString>
#include <QMap>

class SchematicItem;

class SchematicAnnotator {
public:
    enum Order {
        TopToBottom,
        LeftToRight
    };

    /**
     * @brief Annotates all items in the scene.
     * @return Map of items that were changed and their new reference strings.
     */
    static QMap<SchematicItem*, QString> annotate(QGraphicsScene* scene, bool resetExisting = true, Order order = TopToBottom);
    
    /**
     * @brief Clears all annotations (Reset to R?, C?, etc.)
     * @return Map of items that were changed and their reset reference strings (e.g. "R?")
     */
    static QMap<SchematicItem*, QString> resetAnnotations(QGraphicsScene* scene);

    /**
     * @brief Annotates all items in the entire project hierarchy.
     * @param rootFilePath Path to the main schematic file.
     * @param projectDir Directory containing project files.
     * @param resetExisting If true, resets all items.
     */
    static void annotateProject(const QString& rootFilePath, const QString& projectDir, bool resetExisting = true, Order order = TopToBottom);
};

#endif // SCHEMATIC_ANNOTATOR_H
