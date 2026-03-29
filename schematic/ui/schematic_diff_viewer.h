#ifndef SCHEMATIC_DIFF_VIEWER_H
#define SCHEMATIC_DIFF_VIEWER_H

#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QJsonObject>
#include "../analysis/schematic_diff_engine.h"

/**
 * @brief Widget to visually compare two schematic versions.
 */
class SchematicDiffViewer : public QWidget {
    Q_OBJECT
public:
    explicit SchematicDiffViewer(QWidget* parent = nullptr);

    /**
     * @brief Load and compare two schematic JSON objects.
     */
    void setSchematics(const QJsonObject& jsonA, const QJsonObject& jsonB);

private:
    void setupUI();
    void populateScenes();
    void highlightDifferences();

    QJsonObject m_jsonA;
    QJsonObject m_jsonB;
    QList<SchematicDiffItem> m_diffs;

    QGraphicsScene* m_sceneA;
    QGraphicsScene* m_sceneB;
    QGraphicsView* m_viewA;
    QGraphicsView* m_viewB;
};

#endif // SCHEMATIC_DIFF_VIEWER_H
