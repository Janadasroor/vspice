#include "git_backend.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>

GitBackend::GitBackend(const QString& workingDir)
    : m_workingDir(workingDir)
{
}

void GitBackend::setWorkingDir(const QString& dir) {
    m_workingDir = dir;
}

GitBackend::RunResult GitBackend::run(const QStringList& args, int timeoutMs) const {
    RunResult result;
    QProcess proc;
    proc.setWorkingDirectory(m_workingDir);
    proc.start("git", args);
    proc.waitForFinished(timeoutMs);
    result.exitCode = proc.exitCode();
    result.stdoutData = QString::fromUtf8(proc.readAllStandardOutput());
    result.stderrData = QString::fromUtf8(proc.readAllStandardError());
    if (result.exitCode != 0) {
        m_lastError = result.stderrData.trimmed();
    }
    return result;
}

bool GitBackend::isGitRepo() const {
    if (m_workingDir.isEmpty()) return false;
    auto r = run({"rev-parse", "--git-dir"});
    return r.exitCode == 0;
}

QString GitBackend::repoRoot() const {
    auto r = run({"rev-parse", "--show-toplevel"});
    if (r.exitCode == 0) return r.stdoutData.trimmed();
    return QString();
}

bool GitBackend::initRepo() {
    auto r = run({"init"});
    return r.exitCode == 0;
}

bool GitBackend::cloneRepo(const QString& url, const QString& destDir) {
    auto r = run({"clone", url, destDir}, 120000);
    return r.exitCode == 0;
}

QVector<GitFileStatus> GitBackend::status() const {
    QVector<GitFileStatus> files;
    auto r = run({"status", "--porcelain=v1", "-uall"});
    if (r.exitCode != 0) return files;

    for (const QString& line : r.stdoutData.split('\n')) {
        if (line.length() < 4) continue;
        GitFileStatus fs;
        fs.indexStatus = line.left(1);
        fs.worktreeStatus = line.mid(1, 1);
        QString rest = line.mid(3);

        // Handle renames (R  old -> new)
        if (fs.indexStatus == "R" && rest.contains(" -> ")) {
            QStringList parts = rest.split(" -> ");
            fs.renameFrom = parts[0].trimmed();
            fs.path = parts[1].trimmed();
        } else {
            fs.path = rest.trimmed();
        }

        // Remove surrounding quotes if present
        if (fs.path.startsWith('"') && fs.path.endsWith('"')) {
            fs.path = fs.path.mid(1, fs.path.length() - 2);
        }

        fs.isUntracked = (fs.indexStatus == "?" && fs.worktreeStatus == "?");
        fs.staged = (fs.indexStatus != " " && fs.indexStatus != "?");
        files.append(fs);
    }
    return files;
}

bool GitBackend::stageFile(const QString& path) {
    auto r = run({"add", path});
    return r.exitCode == 0;
}

bool GitBackend::unstageFile(const QString& path) {
    auto r = run({"reset", "HEAD", "--", path});
    return r.exitCode == 0;
}

bool GitBackend::stageAll() {
    auto r = run({"add", "-A"});
    return r.exitCode == 0;
}

bool GitBackend::unstageAll() {
    auto r = run({"reset", "HEAD"});
    return r.exitCode == 0;
}

bool GitBackend::commit(const QString& message, bool amend, QString* output) {
    QStringList args = {"commit", "-m", message};
    if (amend) args << "--amend";
    auto r = run(args);
    if (output) *output = r.stdoutData + r.stderrData;
    return r.exitCode == 0;
}

bool GitBackend::push(const QString& remote, const QString& branch, QString* output) {
    QStringList args = {"push"};
    if (!remote.isEmpty()) args << remote;
    if (!branch.isEmpty()) args << branch;
    auto r = run(args, 60000);
    
    // Automatically set upstream if missing
    if (r.exitCode != 0 && (r.stderrData.contains("has no upstream branch") || r.stderrData.contains("--set-upstream"))) {
        QString current = currentBranch();
        QString targetRemote = remote.isEmpty() ? "origin" : remote;
        
        if (!current.isEmpty() && remotes().contains(targetRemote)) {
            r = run({"push", "--set-upstream", targetRemote, current}, 60000);
        }
    }
    
    if (output) *output = r.stderrData;
    return r.exitCode == 0;
}

bool GitBackend::pull(const QString& remote, const QString& branch, QString* output) {
    QStringList args = {"pull"};
    if (!remote.isEmpty()) args << remote;
    if (!branch.isEmpty()) args << branch;
    auto r = run(args, 60000);
    if (output) *output = r.stdoutData + r.stderrData;
    return r.exitCode == 0;
}

bool GitBackend::fetch(const QString& remote, QString* output) {
    QStringList args = {"fetch"};
    if (!remote.isEmpty()) args << remote;
    auto r = run(args, 60000);
    if (output) *output = r.stdoutData + r.stderrData;
    return r.exitCode == 0;
}

QVector<GitBranch> GitBackend::branches() const {
    QVector<GitBranch> list;
    auto r = run({"branch", "-a", "--format=%(HEAD)|%(refname:short)|%(upstream:short)|%(upstream:track)"});
    if (r.exitCode != 0) return list;

    for (const QString& line : r.stdoutData.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QStringList parts = line.split('|');
        if (parts.size() < 2) continue;

        GitBranch b;
        b.isCurrent = (parts[0].trimmed() == "*");
        b.name = parts[1].trimmed();
        b.isRemote = b.name.startsWith("origin/");
        b.tracking = parts.size() > 2 ? parts[2].trimmed() : QString();
        b.aheadBehind = parts.size() > 3 ? parts[3].trimmed() : QString();
        list.append(b);
    }
    return list;
}

QString GitBackend::currentBranch() const {
    auto r = run({"branch", "--show-current"});
    if (r.exitCode == 0) return r.stdoutData.trimmed();
    return QString();
}

bool GitBackend::createBranch(const QString& name) {
    auto r = run({"checkout", "-b", name});
    return r.exitCode == 0;
}

bool GitBackend::switchBranch(const QString& name) {
    auto r = run({"checkout", name});
    return r.exitCode == 0;
}

bool GitBackend::deleteBranch(const QString& name, bool force) {
    auto r = run({"branch", force ? "-D" : "-d", name});
    return r.exitCode == 0;
}

bool GitBackend::mergeBranch(const QString& name, QString* output) {
    auto r = run({"merge", name});
    if (output) *output = r.stdoutData + r.stderrData;
    return r.exitCode == 0;
}

QStringList GitBackend::remotes() const {
    auto r = run({"remote"});
    if (r.exitCode != 0) return QStringList();
    QStringList result;
    for (const QString& line : r.stdoutData.split('\n')) {
        QString t = line.trimmed();
        if (!t.isEmpty()) result << t;
    }
    return result;
}

bool GitBackend::addRemote(const QString& name, const QString& url) {
    auto r = run({"remote", "add", name, url});
    return r.exitCode == 0;
}

QString GitBackend::diffFile(const QString& path) const {
    auto r = run({"diff", "HEAD", "--", path});
    return r.stdoutData;
}

QString GitBackend::diffCached() const {
    auto r = run({"diff", "--cached"});
    return r.stdoutData;
}

QString GitBackend::diffAll() const {
    auto r = run({"diff"});
    return r.stdoutData;
}

QVector<GitCommit> GitBackend::log(int count) const {
    QVector<GitCommit> commits;
    auto r = run({"log", QString("--max-count=%1").arg(count),
                  "--format=%H|%h|%s|%an|%ai"});
    if (r.exitCode != 0) return commits;

    for (const QString& line : r.stdoutData.split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        QStringList parts = line.split('|');
        if (parts.size() < 5) continue;
        GitCommit c;
        c.sha = parts[0];
        c.shortSha = parts[1];
        c.subject = parts[2];
        c.author = parts[3];
        c.date = parts[4];
        commits.append(c);
    }
    return commits;
}

QString GitBackend::getFileContent(const QString& revision, const QString& path) const {
    auto r = run({"show", QString("%1:%2").arg(revision, path)});
    if (r.exitCode == 0) return r.stdoutData;
    return QString();
}

bool GitBackend::discardChanges(const QString& path) {
    auto r = run({"checkout", "--", path});
    return r.exitCode == 0;
}

QString GitBackend::gitVersion() {
    QProcess proc;
    proc.start("git", {"--version"});
    proc.waitForFinished(5000);
    if (proc.exitCode() == 0) {
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }
    return QString();
}

bool GitBackend::isGitAvailable() {
    return !gitVersion().isEmpty();
}
