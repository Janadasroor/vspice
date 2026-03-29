#include "python_manager.h"
#include "config_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

PythonManager& PythonManager::instance() {
    static PythonManager inst;
    return inst;
}

PythonManager::PythonManager(QObject* parent) : QObject(parent) {
}

void PythonManager::runScript(const QString& scriptName, const QStringList& args) {
    QString fullPath = QDir(getScriptsDir()).absoluteFilePath(scriptName);
    
    if (!QFile::exists(fullPath)) {
        emit scriptError(tr("Script not found: %1").arg(fullPath));
        return;
    }

    QProcess* process = new QProcess(this);
    process->setProcessEnvironment(getConfiguredEnvironment());
    
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        QByteArray data = process->readAllStandardOutput();
        if (!data.isEmpty()) {
            emit scriptOutput(QString::fromUtf8(data));
        }
    });

    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        QByteArray data = process->readAllStandardError();
        if (!data.isEmpty()) {
            emit scriptError(QString::fromUtf8(data));
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), 
            this, [this, process](int exitCode) {
        emit scriptFinished(exitCode);
        process->deleteLater();
    });

    QString pythonExec = getPythonExecutable();
    
    QStringList allArgs;
    allArgs << fullPath << args;
    
    qDebug() << "[PythonManager] Running script:" << scriptName << "with" << args.size() << "args";
    process->start(pythonExec, allArgs);
    if (!process->waitForStarted(1000)) {
        qDebug() << "[PythonManager] Process failed to start!" << process->errorString();
    } else {
        qDebug() << "[PythonManager] Process started successfully. PID:" << process->processId();
    }
}

QString PythonManager::getScriptsDir() {
    return QCoreApplication::applicationDirPath() + "/../python/scripts";
}

QString PythonManager::getPythonExecutable() {
    QString scriptsDir = getScriptsDir();
    QString venvPython = QDir(scriptsDir).absoluteFilePath("../venv/bin/python");
    if (QFile::exists(venvPython)) {
        return venvPython;
    }
    return "python3";
}

QProcessEnvironment PythonManager::getConfiguredEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString geminiKey = ConfigManager::instance().geminiApiKey();
    if (!geminiKey.isEmpty()) {
        env.insert("GEMINI_API_KEY", geminiKey);
        // Also insert GOOGLE_API_KEY as some newer libs expect that
        env.insert("GOOGLE_API_KEY", geminiKey);
    }
    QString octopartKey = ConfigManager::instance().octopartApiKey();
    if (!octopartKey.isEmpty()) {
        env.insert("OCTOPART_API_KEY", octopartKey);
    }
    return env;
}
