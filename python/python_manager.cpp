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
    QString fullPath = QDir(getScriptsPath()).absoluteFilePath(scriptName);
    
    if (!QFile::exists(fullPath)) {
        emit scriptError(tr("Script not found: %1").arg(fullPath));
        return;
    }

    QProcess* process = new QProcess(this);
    
    // Inject API Key from configuration
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString apiKey = ConfigManager::instance().geminiApiKey();
    if (!apiKey.isEmpty()) {
        env.insert("GEMINI_API_KEY", apiKey);
    }
    process->setProcessEnvironment(env);
    
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

    // Use venv python if available, otherwise fallback to system python3
    QString scriptsDir = getScriptsPath();
    // Path: Viora EDA/python/scripts -> ../venv/bin/python -> Viora EDA/python/venv/bin/python
    QString venvPython = QDir(scriptsDir).absoluteFilePath("../venv/bin/python");
    QString pythonExec = "python3";
    
    if (QFile::exists(venvPython)) {
        pythonExec = venvPython;
        qDebug() << "Using virtual environment python:" << pythonExec;
    } else {
        qDebug() << "Virtual environment not found at" << venvPython << ", using system python3";
    }
    
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

QString PythonManager::getScriptsPath() const {
    // In dev environment, look relative to project root
    // In production, this would be in app bundle/install dir
    return QCoreApplication::applicationDirPath() + "/../python/scripts";
}
