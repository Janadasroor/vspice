#ifndef SCHEMATIC_HIERARCHY_PANEL_H
#define SCHEMATIC_HIERARCHY_PANEL_H

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>

class SchematicHierarchyPanel : public QWidget {
    Q_OBJECT

public:
    explicit SchematicHierarchyPanel(QWidget* parent = nullptr);

    void setProjectContext(const QString& rootFile);
    void refresh();

signals:
    void sheetSelected(const QString& fileName);

private slots:
    void onItemDoubleClicked(const QModelIndex& index);

private:
    void scanHierarchy(const QString& filePath, QStandardItem* parentItem, QSet<QString>& visited);

    QTreeView* m_treeView;
    QStandardItemModel* m_model;
    QString m_rootFile;
};

#endif // SCHEMATIC_HIERARCHY_PANEL_H
