#ifndef SCHEMATIC_DIFF_DIALOG_H
#define SCHEMATIC_DIFF_DIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QString>

class SchematicDiffViewer;
class QLabel;

/**
 * @brief Dialog to visually compare two versions of a schematic.
 */
class SchematicDiffDialog : public QDialog {
    Q_OBJECT
public:
    explicit SchematicDiffDialog(QWidget* parent = nullptr);

    /**
     * @brief Set schematics to compare.
     * @param jsonA Base version (e.g. HEAD)
     * @param jsonB New version (e.g. Worktree)
     * @param labelA Description for version A
     * @param labelB Description for version B
     */
    void compare(const QJsonObject& jsonA, const QJsonObject& jsonB, 
                 const QString& labelA = "VERSION A", const QString& labelB = "VERSION B");

private:
    void setupUI();

    SchematicDiffViewer* m_viewer;
    QLabel* m_statusLabel;
    QLabel* m_labelA;
    QLabel* m_labelB;
};

#endif // SCHEMATIC_DIFF_DIALOG_H
