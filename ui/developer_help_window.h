#ifndef DEVELOPERHELPWINDOW_H
#define DEVELOPERHELPWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QTextBrowser>
#include <QSplitter>
#include <QTreeWidgetItem>
#include <QTreeWidget>

class DeveloperHelpWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DeveloperHelpWindow(QWidget *parent = nullptr);
    ~DeveloperHelpWindow();

private slots:
    void onDocSelected(QTreeWidgetItem* item, int column);

private:
    void setupUi();
    void populateTechnicalDocs();
    void scanDirectory(const QString& path, QTreeWidgetItem* parent);
    void applyDeveloperTheme();
    void loadMarkdown(const QString& path);

    QTreeWidget* m_docTree;
    QTextBrowser* m_contentViewer;
    QSplitter* m_splitter;
};

#endif // DEVELOPERHELPWINDOW_H
