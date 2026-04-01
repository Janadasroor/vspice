#ifndef SCHEMATIC_TIMELINE_VIEW_H
#define SCHEMATIC_TIMELINE_VIEW_H

#include <QWidget>
#include <QGraphicsScene>
#include <QJsonObject>
#include <QPixmap>
#include "../../ui/git_backend.h"

class QListWidget;
class QListWidgetItem;
class QSplitter;
class QLabel;
class QPushButton;
class QScrollArea;
class QFrame;
class QVBoxLayout;

class SchematicTimelineView : public QWidget {
    Q_OBJECT

public:
    explicit SchematicTimelineView(QWidget* parent = nullptr);
    ~SchematicTimelineView();

    void setWorkingDir(const QString& dir);
    void loadHistory(const QString& schematicPath);

signals:
    void commitSelected(const QString& sha, const QJsonObject& schematicJson);
    void compareCommitsRequested(const QString& shaA, const QString& shaB);
    void checkoutRequested(const QString& sha);
    void createBranchRequested(const QString& sha, const QString& branchName);

private slots:
    void onCommitSelected(QListWidgetItem* item);
    void onCompareClicked();
    void onCheckoutClicked();
    void onCreateBranchClicked();
    void onRefreshClicked();

private:
    void setupUi();
    void loadCommits();
    void loadThumbnail(const QString& sha);
    QString getSchematicContent(const QString& sha, const QString& path) const;
    QPixmap renderSchematicThumbnail(const QString& jsonContent);
    void updateSelectedCommitInfo();

    QString m_workingDir;
    QString m_schematicPath;
    QString m_currentSha;
    QString m_compareSha;

    QListWidget* m_commitList;
    QScrollArea* m_thumbnailScroll;
    QLabel* m_thumbnailLabel;
    QLabel* m_commitInfoLabel;
    QLabel* m_diffSummaryLabel;
    QPushButton* m_compareBtn;
    QPushButton* m_checkoutBtn;
    QPushButton* m_createBranchBtn;
    QPushButton* m_refreshBtn;
    QFrame* m_infoPanel;

    QMap<QString, QPixmap> m_thumbnails;
    QVector<GitCommit> m_commits;
};

#endif // SCHEMATIC_TIMELINE_VIEW_H