#include "plugin_package_installer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

QString PluginPackageInstaller::userPluginDirectory() {
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appData.isEmpty()) {
        return QDir::home().absoluteFilePath(".vioraeda/plugins");
    }
    return QDir(appData).absoluteFilePath("plugins");
}

bool PluginPackageInstaller::installPackage(const QString& packagePath,
                                            QString* outInstalledPath,
                                            QString* outError) {
    if (outInstalledPath) {
        outInstalledPath->clear();
    }
    if (outError) {
        outError->clear();
    }

    const QFileInfo srcInfo(packagePath);
    if (!srcInfo.exists() || !srcInfo.isFile()) {
        if (outError) {
            *outError = QString("Package file does not exist: %1").arg(packagePath);
        }
        return false;
    }

    const QString targetDirPath = userPluginDirectory();
    QDir targetDir(targetDirPath);
    if (!targetDir.mkpath(".")) {
        if (outError) {
            *outError = QString("Cannot create plugin directory: %1").arg(targetDirPath);
        }
        return false;
    }

    const QString fileName = srcInfo.fileName();
    const QString finalPath = targetDir.absoluteFilePath(fileName);

    // Stage inside target filesystem so final rename is atomic.
    const QString stageDirName = QString(".staging-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString stageDirPath = targetDir.absoluteFilePath(stageDirName);
    QDir stageDir(stageDirPath);
    if (!stageDir.mkpath(".")) {
        if (outError) {
            *outError = QString("Cannot create staging directory: %1").arg(stageDirPath);
        }
        return false;
    }

    const QString stagedPath = stageDir.absoluteFilePath(fileName);
    if (!QFile::copy(packagePath, stagedPath)) {
        stageDir.removeRecursively();
        if (outError) {
            *outError = QString("Failed to stage package: %1").arg(packagePath);
        }
        return false;
    }

    QString backupPath;
    const bool targetExists = QFileInfo::exists(finalPath);
    if (targetExists) {
        backupPath = QString("%1.bak.%2")
            .arg(finalPath)
            .arg(QDateTime::currentMSecsSinceEpoch());
        if (!QFile::rename(finalPath, backupPath)) {
            QFile::remove(stagedPath);
            stageDir.removeRecursively();
            if (outError) {
                *outError = QString("Failed to move existing plugin to backup: %1").arg(finalPath);
            }
            return false;
        }
    }

    if (!QFile::rename(stagedPath, finalPath)) {
        // Roll back previous file if present.
        if (!backupPath.isEmpty()) {
            QFile::rename(backupPath, finalPath);
        }
        QFile::remove(stagedPath);
        stageDir.removeRecursively();
        if (outError) {
            *outError = QString("Failed to move staged plugin into place: %1").arg(finalPath);
        }
        return false;
    }

    if (!backupPath.isEmpty()) {
        QFile::remove(backupPath);
    }
    stageDir.removeRecursively();

    if (outInstalledPath) {
        *outInstalledPath = finalPath;
    }
    return true;
}
