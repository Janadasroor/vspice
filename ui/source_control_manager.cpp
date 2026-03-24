#include "source_control_manager.h"
#include <QtConcurrent>
#include <QFuture>
#include <QDebug>

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
    m_projectDir = dir;
    m_backend.setWorkingDir(dir);
    m_isRepo = m_backend.isGitRepo();
    emit repoChanged(m_isRepo);
    if (m_isRepo) {
        refresh();
    }
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
    bool wasRepo = m_isRepo;
    m_isRepo = m_backend.isGitRepo();
    
    if (wasRepo != m_isRepo) {
        emit repoChanged(m_isRepo);
    }

    if (!m_isRepo) return;

    m_fileStatuses = m_backend.status();
    m_currentBranch = m_backend.currentBranch();
    m_recentCommits = m_backend.log(15);
    m_remoteNames = m_backend.remotes();

    emit statusUpdated();
    emit branchChanged(m_currentBranch);
}

void SourceControlManager::scheduleRefresh() {
    if (m_isRepo) {
        m_refreshTimer->start();
    }
}

void SourceControlManager::runAsync(const QString& opName, std::function<bool()> task) {
    emit operationStarted(opName);

    QFuture<bool> future = QtConcurrent::run([task]() {
        return task();
    });

    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, opName]() {
        bool ok = watcher->result();
        watcher->deleteLater();

        QString msg = ok ? (opName + " completed successfully") : (opName + " failed: " + m_backend.lastError());
        emit operationFinished(opName, ok, msg);

        // Refresh after any mutating operation
        refresh();
    });
    
    watcher->setFuture(future);
}

void SourceControlManager::stageFile(const QString& path) {
    if (m_backend.stageFile(path)) refresh();
}

void SourceControlManager::unstageFile(const QString& path) {
    if (m_backend.unstageFile(path)) refresh();
}

void SourceControlManager::stageAll() {
    if (m_backend.stageAll()) refresh();
}

void SourceControlManager::unstageAll() {
    if (m_backend.unstageAll()) refresh();
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
