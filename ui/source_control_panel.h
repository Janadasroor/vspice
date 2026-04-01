#ifndef SOURCE_CONTROL_PANEL_H
#define SOURCE_CONTROL_PANEL_H

#include <QWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include "source_control_manager.h"
#include "git_backend.h"

class SourceControlPanel : public QWidget {
    Q_OBJECT

public:
    explicit SourceControlPanel(QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void onStatusUpdated();
    void onBranchChanged(const QString& branch);
    void onOperationStarted(const QString& op);
    void onOperationFinished(const QString& op, bool success, const QString& msg);
    void onRepoChanged(bool isRepo);
    void onCommitClicked();
    void onCommitPushClicked();
    void onPushClicked();
    void onPullClicked();
    void onFetchClicked();
    void onSyncClicked();
    void onBranchClicked();
    void onInitRepoClicked();
    void onCloneRepoClicked();
    void onFileDoubleClicked(QListWidgetItem* item);
    void onVisualDiffRequested(const QString& path);
    void onFileContextMenu(const QPoint& pos);
    void onFilterChanged(const QString& text);
    void onWorkspaceChanged(int index);
    void onTimelineClicked();
    void onCompareBranchesClicked();

private:
    void setupUi();
    void applyTheme();
    void updateFileList();
    void updateCommitList();
    void updateWorkspaceCombo();
    void showNotRepoView();
    void showRepoView();

    SourceControlManager& m_mgr;

    // Header
    QComboBox* m_workspaceCombo;
    QLabel* m_branchLabel;
    QPushButton* m_refreshBtn;
    QLabel* m_summaryLabel;

    // Not-a-repo view
    QWidget* m_notRepoView;
    QPushButton* m_initBtn;
    QPushButton* m_cloneBtn;

    // Repo view
    QWidget* m_repoView;

    // Branch & remote actions
    QPushButton* m_pushBtn;
    QPushButton* m_pullBtn;
    QPushButton* m_fetchBtn;
    QPushButton* m_syncBtn;

    // File list
    QListWidget* m_fileList;
    QPushButton* m_stageAllBtn;
    QPushButton* m_unstageAllBtn;
    QLabel* m_changesLabel;
    QLineEdit* m_filterEdit;
    QCheckBox* m_showStagedCheck;
    QCheckBox* m_showUntrackedCheck;

    // Commit
    QTextEdit* m_commitMsg;
    QPushButton* m_commitBtn;
    QPushButton* m_commitPushBtn;
    QCheckBox* m_amendCheck;

    // Recent commits
    QListWidget* m_commitList;

    // Status
    QLabel* m_statusLabel;

    // Version control actions
    QPushButton* m_timelineBtn;
    QPushButton* m_compareBranchesBtn;

    QString m_filterText;
};

#endif // SOURCE_CONTROL_PANEL_H
