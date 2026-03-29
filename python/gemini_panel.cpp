#include "gemini_panel.h"
#include "gemini_instructions_dialog.h"
#include "python_manager.h"
#include "config_manager.h"
#include "theme_manager.h"
#include "gemini_bridge.h"
#include "../schematic/analysis/net_manager.h"

#include <QtQuickWidgets/QQuickWidget>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QDateTime>
#include <QGraphicsView>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QClipboard>
#include <QApplication>
#include <QBuffer>
#include <QPainter>
#include <QDialog>
#include <QListWidget>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QFileInfo>

namespace {
QString compactErrorSummary(const QString& raw, int maxLen = 180) {
    QString text = raw;
    if (text.contains("RESOURCE_EXHAUSTED", Qt::CaseInsensitive) || text.contains("429", Qt::CaseInsensitive)) {
        return "GEMINI QUOTA EXCEEDED: You have exceeded your free tier rate limit. Please wait about 30 seconds and try again.";
    }
    if (text.contains("SAFETY", Qt::CaseInsensitive)) {
        return "SAFETY FILTER BLOCKED: The model refused to answer because of safety constraints.";
    }
    if (text.contains("API_KEY_INVALID", Qt::CaseInsensitive) || text.contains("invalid api key", Qt::CaseInsensitive)) {
        return "INVALID API KEY: Please check your Gemini settings and ensure your API key is correct.";
    }

    text.replace("\r\n", "\n");
    text = text.trimmed();
    if (text.isEmpty()) return QString("Unknown error");
    const int nl = text.indexOf('\n');
    if (nl >= 0) text = text.left(nl).trimmed();
    if (text.size() > maxLen) text = text.left(maxLen) + "...";
    return text;
}

QString nowTimeChip() {
    return QDateTime::currentDateTime().toString("HH:mm");
}

QString sanitizeAgentTextChunk(QString text) {
    text.remove(QRegularExpression(R"(</?(THOUGHT|ACTION|SUGGESTION|HIGHLIGHT)>)", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression(R"((^|\s)(THOUGHT|ACTION|SUGGESTION|HIGHLIGHT)>)", QRegularExpression::CaseInsensitiveOption));
    return text;
}
} // namespace

GeminiPanel::GeminiPanel(QGraphicsScene* scene, QWidget* parent) 
    : QWidget(parent), m_scene(scene) 
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Initialize Bridge
    m_bridge = new GeminiBridge(this);

    // Initialize QQuickWidget
    m_quickWidget = new QQuickWidget(this);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->engine()->rootContext()->setContextProperty("geminiBridge", m_bridge);
    
    // Set source (adjust path if necessary)
    m_quickWidget->setSource(QUrl::fromLocalFile(QDir::current().absoluteFilePath("python/qml/GeminiRoot.qml")));
    
    mainLayout->addWidget(m_quickWidget);

    // Setup bridge connections
    connect(m_bridge, &GeminiBridge::sendMessageRequested, this, &GeminiPanel::onBridgeSendMessage);
    connect(m_bridge, &GeminiBridge::stopRequested, this, &GeminiPanel::onBridgeStopRequest);
    connect(m_bridge, &GeminiBridge::refreshModelsRequested, this, &GeminiPanel::onBridgeRefreshModelsRequest);
    connect(m_bridge, &GeminiBridge::clearHistoryRequested, this, &GeminiPanel::clearHistory);
    connect(m_bridge, &GeminiBridge::closeRequested, this, &GeminiPanel::onBridgeCloseRequest);

    m_bridge->updateTitle("VIORA AI");

    m_thinkingPulseTimer = new QTimer(this);
    
    // Initial data fetch
    refreshModelList();
    loadHistory();
}

void GeminiPanel::setMode(const QString& mode) {
    m_mode = mode;
    if (m_bridge) m_bridge->updateStatus("Mode: " + mode);
}

void GeminiPanel::setProjectFilePath(const QString& path) {
    m_projectFilePath = path;
}

void GeminiPanel::setUndoStack(QUndoStack* stack) {
    m_undoStack = stack;
}

void GeminiPanel::onBridgeSendMessage(const QString& text) {
    // Update title if this is likely the start of a conversation
    if (m_bridge && m_bridge->conversationTitle() == "VIORA AI") {
        QString title = text;
        if (title.length() > 30) title = title.left(27) + "...";
        m_bridge->updateTitle(title.toUpper());
    }
    askPrompt(text, true);
}

void GeminiPanel::onBridgeStopRequest() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
    m_isWorking = false;
    if (m_bridge) m_bridge->setWorking(false);
}

void GeminiPanel::onBridgeRefreshModelsRequest() {
    refreshModelList();
}

void GeminiPanel::onBridgeCloseRequest() {
    // Try to find the parent dock widget to hide
    QWidget* p = parentWidget();
    while (p) {
        if (p->inherits("QDockWidget")) {
            p->hide();
            return;
        }
        p = p->parentWidget();
    }
    hide();
}

void GeminiPanel::clearHistory() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
    m_history.clear();
    if (m_bridge) m_bridge->clearHistory();
    m_isWorking = false;
    if (m_bridge) m_bridge->setWorking(false);
    
    appendSystemNote("[SYSTEM] New conversation started.");
}

void GeminiPanel::askPrompt(const QString& text, bool includeContext) {
    if (m_isWorking) return;
    
    QString key = ConfigManager::instance().geminiApiKey().trimmed();
    if (key.isEmpty()) {
        reportError("API Key Missing", "Please set your Gemini API key in settings.", true);
        return;
    }

    QVariantMap entry;
    entry["role"] = "user";
    entry["content"] = text;
    entry["timestamp"] = nowTimeChip();
    m_history.append(entry);
    syncHistoryToBridge();

    m_isWorking = true;
    if (m_bridge) m_bridge->setWorking(true);
    
    // Prepare arguments for gemini_query.py
    QStringList args;
    args << text;
    
    const QString selectedModel = m_bridge ? m_bridge->currentModel() : "gemini-2.0-flash";
    if (!selectedModel.isEmpty()) args << "--model" << selectedModel;

    QString instructions = gatherInstructions();
    if (!instructions.isEmpty()) args << "--instructions" << instructions;

    if (m_process) {
        m_process->deleteLater();
    }
    m_process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GEMINI_API_KEY", key);
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &GeminiPanel::onProcessReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &GeminiPanel::onProcessFinished);

    QString sDir = QCoreApplication::applicationDirPath() + "/../python/scripts";
    QString sPath = QDir(sDir).absoluteFilePath("gemini_query.py");
    QString vPy = QDir(sDir).absoluteFilePath("../venv/bin/python");
    QString py = QFile::exists(vPy) ? vPy : "python3";

    m_responseBuffer.clear();
    m_leftover.clear();
    
    m_process->start(py, QStringList() << sPath << args);
}

void GeminiPanel::onProcessReadyRead() {
    if (!m_process) return;
    QString chunk = m_leftover + QString::fromUtf8(m_process->readAllStandardOutput());
    processAgentStdoutChunk(chunk);
}

void GeminiPanel::processAgentStdoutChunk(const QString& chunk) {
    m_responseBuffer += chunk;
    
    // In a simple implementation, we just update the last model message
    if (m_history.isEmpty() || m_history.last()["role"].toString() != "model") {
        QVariantMap entry;
        entry["role"] = "model";
        entry["content"] = sanitizeAgentTextChunk(m_responseBuffer);
        entry["timestamp"] = nowTimeChip();
        m_history.append(entry);
    } else {
        m_history.last()["content"] = sanitizeAgentTextChunk(m_responseBuffer);
    }
    syncHistoryToBridge();
    
    // Check for thinking/action tags in real-time
    if (chunk.contains("<THOUGHT>")) {
        if (m_bridge) m_bridge->updateStatus("Thinking...");
    }
    if (chunk.contains("<ACTION>")) {
        // Extract action for UI feedback
        QRegularExpression re("<ACTION>(.*?)</ACTION>");
        auto match = re.match(chunk);
        if (match.hasMatch()) {
            if (m_bridge) m_bridge->updateStatus("Executing: " + match.captured(1));
        }
    }
}

void GeminiPanel::onProcessFinished(int exitCode) {
    m_isWorking = false;
    if (m_bridge) {
        m_bridge->setWorking(false);
        m_bridge->updateStatus("Ready");
    }
    
    if (exitCode != 0) {
        reportError("Process Error", "AI process terminated unexpectedly.", false);
    }
    
    saveHistory();
}

void GeminiPanel::syncHistoryToBridge() {
    if (m_bridge) {
        QVariantList qmlHistory;
        for (const auto& entry : m_history) {
            qmlHistory.append(entry);
        }
        m_bridge->updateMessages(qmlHistory);
    }
}

void GeminiPanel::refreshModelList() {
    QString key = ConfigManager::instance().geminiApiKey().trimmed();
    if (key.isEmpty()) return;

    if (m_modelFetchProcess && m_modelFetchProcess->state() != QProcess::NotRunning) return;

    m_modelFetchProcess = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("GEMINI_API_KEY", key);
    m_modelFetchProcess->setProcessEnvironment(env);

    connect(m_modelFetchProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &GeminiPanel::onModelFetchFinished);

    QString sDir = QCoreApplication::applicationDirPath() + "/../python/scripts";
    QString sPath = QDir(sDir).absoluteFilePath("gemini_query.py");
    m_modelFetchProcess->start("python3", QStringList() << sPath << "--list-models");
}

void GeminiPanel::onModelFetchFinished(int exitCode, QProcess::ExitStatus) {
    if (exitCode != 0 || !m_modelFetchProcess) return;
    
    QString output = QString::fromUtf8(m_modelFetchProcess->readAllStandardOutput());
    QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
    if (doc.isArray()) {
        QStringList models;
        for (auto v : doc.array()) {
            if (v.isString()) models << v.toString();
            else if (v.isObject()) models << v.toObject()["name"].toString();
        }
        if (m_bridge) m_bridge->updateAvailableModels(models);
    }
}

void GeminiPanel::reportError(const QString& title, const QString& details, bool openDialog) {
    appendSystemNote(QString("<b>%1</b>: %2").arg(title, details));
    if (openDialog) {
        // Implementation for dialog
    }
}

void GeminiPanel::appendSystemNote(const QString& text) {
    QVariantMap entry;
    entry["role"] = "action";
    entry["content"] = text;
    entry["timestamp"] = nowTimeChip();
    m_history.append(entry);
    syncHistoryToBridge();
}

void GeminiPanel::appendSystemAction(const QString& title, const QString& details, const QString& icon) {
    appendSystemNote(QString("%1 <b>%2</b>: %3").arg(icon, title, details));
}

QString GeminiPanel::gatherInstructions() const {
    QString combined;
    QString global = ConfigManager::instance().geminiGlobalInstructions().trimmed();
    if (!global.isEmpty()) combined = "GLOBAL:\n" + global;

    if (!m_projectFilePath.isEmpty()) {
        QFileInfo info(m_projectFilePath);
        QString pFile = info.absolutePath() + "/.gemini/custom_instructions.txt";
        QFile file(pFile);
        if (file.open(QIODevice::ReadOnly)) {
            QString project = QString::fromUtf8(file.readAll()).trimmed();
            if (!project.isEmpty()) {
                if (!combined.isEmpty()) combined += "\n\n";
                combined += "PROJECT:\n" + project;
            }
        }
    }
    return combined;
}

void GeminiPanel::saveHistory() {}
void GeminiPanel::loadHistory() {}
void GeminiPanel::loadHistoryFromFile(const QString& path) { Q_UNUSED(path); }
void GeminiPanel::onRefreshModelsClicked() { refreshModelList(); }
void GeminiPanel::onCustomInstructionsClicked() {}
void GeminiPanel::onCopyPromptClicked() {}
void GeminiPanel::handleActionTag(const QString& act) { Q_UNUSED(act); }
void GeminiPanel::handleSuggestionTag(const QString& sug) { Q_UNUSED(sug); }
void GeminiPanel::parseAndExecuteCommandModeInput(const QString& in) { Q_UNUSED(in); }

void GeminiPanel::ensureErrorDialog() {}
void GeminiPanel::populateErrorDialogHistory() {}
void GeminiPanel::selectErrorHistoryRow(int r) { Q_UNUSED(r); }
void GeminiPanel::appendErrorHistory(const QString& t, const QString& d) { Q_UNUSED(t); Q_UNUSED(d); }
void GeminiPanel::showErrorDialog(const QString& t, const QString& d) { Q_UNUSED(t); Q_UNUSED(d); }
void GeminiPanel::showErrorBanner(const QString& s, const QString& d) { Q_UNUSED(s); Q_UNUSED(d); }
void GeminiPanel::hideErrorBanner() {}
