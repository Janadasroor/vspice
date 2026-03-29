#ifndef GIT_BACKEND_H
#define GIT_BACKEND_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QProcess>

struct GitFileStatus {
    QString path;
    QString indexStatus;  // staged: M, A, D, R, C, ?
    QString worktreeStatus; // unstaged: M, D, ?
    bool staged = false;
    bool isUntracked = false;
    QString renameFrom;   // for renames
};

struct GitBranch {
    QString name;
    bool isCurrent = false;
    bool isRemote = false;
    QString tracking;     // upstream tracking info
    QString aheadBehind;  // "ahead 2, behind 1"
};

struct GitCommit {
    QString sha;
    QString shortSha;
    QString subject;
    QString author;
    QString date;
};

class GitBackend {
public:
    explicit GitBackend(const QString& workingDir = QString());

    void setWorkingDir(const QString& dir);
    QString workingDir() const { return m_workingDir; }

    // --- Repo detection ---
    bool isGitRepo() const;
    QString repoRoot() const;

    // --- Init / Clone ---
    bool initRepo();
    bool cloneRepo(const QString& url, const QString& destDir);

    // --- Status ---
    QVector<GitFileStatus> status() const;

    // --- Staging ---
    bool stageFile(const QString& path);
    bool unstageFile(const QString& path);
    bool stageAll();
    bool unstageAll();

    // --- Commit ---
    bool commit(const QString& message, bool amend = false, QString* output = nullptr);

    // --- Push / Pull / Fetch ---
    bool push(const QString& remote = "origin", const QString& branch = QString(), QString* output = nullptr);
    bool pull(const QString& remote = "origin", const QString& branch = QString(), QString* output = nullptr);
    bool fetch(const QString& remote = QString(), QString* output = nullptr);

    // --- Branches ---
    QVector<GitBranch> branches() const;
    QString currentBranch() const;
    bool createBranch(const QString& name);
    bool switchBranch(const QString& name);
    bool deleteBranch(const QString& name, bool force = false);
    bool mergeBranch(const QString& name, QString* output = nullptr);

    // --- Remotes ---
    QStringList remotes() const;
    bool addRemote(const QString& name, const QString& url);

    // --- Diff ---
    QString diffFile(const QString& path) const;
    QString diffCached() const;
    QString diffAll() const;

    // --- Log ---
    QVector<GitCommit> log(int count = 20) const;

    // --- Content ---
    QString getFileContent(const QString& revision, const QString& path) const;

    // --- Discard ---
    bool discardChanges(const QString& path);

    // --- Global ---
    static QString gitVersion();
    static bool isGitAvailable();

    QString lastError() const { return m_lastError; }

private:
    struct RunResult {
        int exitCode;
        QString stdoutData;
        QString stderrData;
    };

    RunResult run(const QStringList& args, int timeoutMs = 30000) const;

    QString m_workingDir;
    mutable QString m_lastError;
};

#endif // GIT_BACKEND_H
