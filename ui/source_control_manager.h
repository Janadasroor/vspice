#ifndef SOURCE_CONTROL_MANAGER_H
#define SOURCE_CONTROL_MANAGER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QTimer>
#include "git_backend.h"

class SourceControlManager : public QObject {
    Q_OBJECT

public:
    static SourceControlManager& instance();

    void setProjectDir(const QString& dir);
    QString projectDir() const { return m_projectDir; }

    bool isGitRepo() const { return m_isRepo; }
    QString currentBranch() const { return m_currentBranch; }
    QVector<GitFileStatus> fileStatuses() const { return m_fileStatuses; }
    QVector<GitCommit> recentCommits() const { return m_recentCommits; }
    QStringList remoteNames() const { return m_remoteNames; }

    int stagedCount() const;
    int unstagedCount() const;
    int untrackedCount() const;

public slots:
    void refresh();
    void scheduleRefresh(); // Debounced refresh
    void stageFile(const QString& path);
    void unstageFile(const QString& path);
    void stageAll();
    void unstageAll();
    void commit(const QString& message, bool amend = false);
    void commitAndPush(const QString& message, bool amend = false);
    void sync();
    void push();
    void pull();
    void fetch();
    void createBranch(const QString& name);
    void switchBranch(const QString& name);
    void deleteBranch(const QString& name);
    void mergeBranch(const QString& name);
    void discardChanges(const QString& path);
    void addRemote(const QString& name, const QString& url);

signals:
    void repoChanged(bool isRepo);
    void statusUpdated();
    void branchChanged(const QString& currentBranch);
    void operationStarted(const QString& operation);
    void operationFinished(const QString& operation, bool success, const QString& message);

private:
    explicit SourceControlManager(QObject* parent = nullptr);
    ~SourceControlManager() = default;
    SourceControlManager(const SourceControlManager&) = delete;
    SourceControlManager& operator=(const SourceControlManager&) = delete;

    void runAsync(const QString& opName, std::function<bool()> task);

    GitBackend m_backend;
    QString m_projectDir;
    bool m_isRepo = false;
    QString m_currentBranch;
    QVector<GitFileStatus> m_fileStatuses;
    QVector<GitCommit> m_recentCommits;
    QStringList m_remoteNames;
    QTimer* m_refreshTimer;
};

#endif // SOURCE_CONTROL_MANAGER_H
