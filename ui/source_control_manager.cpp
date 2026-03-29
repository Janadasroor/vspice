#include "source_control_manager.h"
#include <QtConcurrent>
#include <QFuture>
#include <QDebug>
#include <QMutexLocker>

SourceControlManager::SourceControlManager(QObject* parent)
    : QObject(parent)
{
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(1500); // 1.5s debounce
    connect(m_refreshTimer, &QTimer::timeout, this, &SourceControlManager::refresh);
}

SourceControlManager& SourceControlManager::instance() {
    static SourceControlManager inst;
    return inst;
}

void SourceControlManager::setProjectDir(const QString& dir) {
    QMutexLocker locker(&m_backendMutex);
    m_projectDir = dir;
    m_backend.setWorkingDir(dir);
    m_isRepo = m_backend.isGitRepo();
    const bool isRepo = m_isRepo;
    locker.unlock();

    emit repoChanged(isRepo);
    if (isRepo) refresh();
}

int SourceControlManager::stagedCount() const {
    int count = 0;
    for (const auto& f : m_fileStatuses) {
        if (f.staged) count++;
    }
    return count;
}

int SourceControlManager::unstagedCount() const {
    int count = 0;
    for (const auto& f : m_fileStatuses) {
        if (!f.staged && !f.isUntracked) count++;
    }
    return count;
}

int SourceControlManager::untrackedCount() const {
    int count = 0;
    for (const auto& f : m_fileStatuses) {
        if (f.isUntracked) count++;
    }
    return count;
}

void SourceControlManager::refresh() {
    QString currentBranch;
    QVector<GitFileStatus> fileStatuses;
    QVector<GitCommit> recentCommits;
    QStringList remoteNames;
    bool isRepo = false;
    bool wasRepo = false;

    {
        QMutexLocker locker(&m_backendMutex);
        wasRepo = m_isRepo;
        m_isRepo = m_backend.isGitRepo();
        isRepo = m_isRepo;
        if (!isRepo) return;

        fileStatuses = m_backend.status();
        currentBranch = m_backend.currentBranch();
        recentCommits = m_backend.log(15);
        remoteNames = m_backend.remotes();

        m_fileStatuses = fileStatuses;
        m_currentBranch = currentBranch;
        m_recentCommits = recentCommits;
        m_remoteNames = remoteNames;
    }

    if (wasRepo != isRepo) {
        emit repoChanged(isRepo);
    }
    emit statusUpdated();
    emit branchChanged(currentBranch);
}

void SourceControlManager::scheduleRefresh() {
    if (m_isRepo) {
        m_refreshTimer->start();
    }
}

void SourceControlManager::runAsync(const QString& opName, std::function<bool()> task) {
    emit operationStarted(opName);

    QFuture<bool> future = QtConcurrent::run([this, task]() {
        QMutexLocker locker(&m_backendMutex);
        return task();
    });

    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, opName]() {
        bool ok = watcher->result();
        watcher->deleteLater();

        QString backendError;
        {
            QMutexLocker locker(&m_backendMutex);
            backendError = m_backend.lastError();
        }

        QString msg = ok ? (opName + " completed successfully") : (opName + " failed: " + backendError);
        emit operationFinished(opName, ok, msg);

        // Refresh after any mutating operation
        refresh();
    });
    
    watcher->setFuture(future);
}

void SourceControlManager::stageFile(const QString& path) {
    bool ok = false;
    {
        QMutexLocker locker(&m_backendMutex);
        ok = m_backend.stageFile(path);
    }
    if (ok) refresh();
}

void SourceControlManager::unstageFile(const QString& path) {
    bool ok = false;
    {
        QMutexLocker locker(&m_backendMutex);
        ok = m_backend.unstageFile(path);
    }
    if (ok) refresh();
}

void SourceControlManager::stageAll() {
    bool ok = false;
    {
        QMutexLocker locker(&m_backendMutex);
        ok = m_backend.stageAll();
    }
    if (ok) refresh();
}

void SourceControlManager::unstageAll() {
    bool ok = false;
    {
        QMutexLocker locker(&m_backendMutex);
        ok = m_backend.unstageAll();
    }
    if (ok) refresh();
}

void SourceControlManager::commit(const QString& message, bool amend) {
    runAsync("Commit", [this, message, amend]() {
        return m_backend.commit(message, amend);
    });
}

void SourceControlManager::commitAndPush(const QString& message, bool amend) {
    runAsync("Commit & Push", [this, message, amend]() {
        if (!m_backend.commit(message, amend)) return false;
        return m_backend.push();
    });
}

void SourceControlManager::sync() {
    runAsync("Sync", [this]() {
        if (!m_backend.pull()) return false;
        return m_backend.push();
    });
}

void SourceControlManager::push() {
    runAsync("Push", [this]() {
        return m_backend.push();
    });
}

void SourceControlManager::pull() {
    runAsync("Pull", [this]() {
        return m_backend.pull();
    });
}

void SourceControlManager::fetch() {
    runAsync("Fetch", [this]() {
        return m_backend.fetch();
    });
}

void SourceControlManager::createBranch(const QString& name) {
    runAsync("Create Branch", [this, name]() {
        return m_backend.createBranch(name);
    });
}

void SourceControlManager::switchBranch(const QString& name) {
    runAsync("Switch Branch", [this, name]() {
        return m_backend.switchBranch(name);
    });
}

void SourceControlManager::deleteBranch(const QString& name) {
    runAsync("Delete Branch", [this, name]() {
        return m_backend.deleteBranch(name);
    });
}

void SourceControlManager::mergeBranch(const QString& name) {
    runAsync("Merge Branch", [this, name]() {
        return m_backend.mergeBranch(name);
    });
}

void SourceControlManager::discardChanges(const QString& path) {
    runAsync("Discard", [this, path]() {
        return m_backend.discardChanges(path);
    });
}

void SourceControlManager::addRemote(const QString& name, const QString& url) {
    runAsync("Add Remote", [this, name, url]() {
        return m_backend.addRemote(name, url);
    });
}

QString SourceControlManager::getFileContent(const QString& revision, const QString& path) {
    QMutexLocker locker(&m_backendMutex);
    return m_backend.getFileContent(revision, path);
}
