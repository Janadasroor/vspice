#ifndef PROJECT_EXPLORER_WIDGET_H
#define PROJECT_EXPLORER_WIDGET_H

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>

class ProjectExplorerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProjectExplorerWidget(QWidget *parent = nullptr);
    void setRootPath(const QString& path);

signals:
    void fileDoubleClicked(const QString& filePath);

private slots:
    void onDoubleClicked(const QModelIndex& index);
    void onFilterChanged(const QString& text);

private:
    void setupUi();
    void applyTheme();

    QTreeView* m_treeView;
    QFileSystemModel* m_model;
    class FileFilterProxyModel* m_proxyModel;
    QLineEdit* m_searchBox;
    QString m_rootPath;
};

#endif // PROJECT_EXPLORER_WIDGET_H
