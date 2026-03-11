#ifndef HELPWINDOW_H
#define HELPWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QTextBrowser>
#include <QSplitter>

class HelpWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit HelpWindow(QWidget *parent = nullptr);
    ~HelpWindow();

    void loadGuide(const QString& filename);

private slots:
    void onGuideSelected(QListWidgetItem* item);

private:
    void setupUi();
    void populateGuides();
    void applyTheme();
    QString markdownStyleSheet() const;

    QListWidget* m_guidesList;
    QTextBrowser* m_contentViewer;
    QSplitter* m_splitter;
};

#endif // HELPWINDOW_H
