#include "gemini_panel.h"
#include "../dialogs/gemini_instructions_dialog.h"
#include "../core/flux_script_manager.h"
#include "config_manager.h"
#include "theme_manager.h"
#include "gemini_bridge.h"
#include "../schematic/analysis/net_manager.h"

#include <QDebug>

#include <QtQuickWidgets/QQuickWidget>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "../schematic/analysis/spice_netlist_generator.h"
#include <QTimer>
#include <QDateTime>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
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
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QTextStream>
#include <QTemporaryFile>
#include <QDirIterator>
#include <QUndoStack>

namespace {
QString compactErrorSummary(const QString& raw, int maxLen = 180) {
    QString text = raw;
    if (text.contains("RESOURCE_EXHAUSTED", Qt::CaseInsensitive) || text.contains("429", Qt::CaseInsensitive)) {
        return "GEMINI QUOTA EXCEEDED: You have exceeded your free tier rate limit. Please wait about 30 seconds and try again.";
    }
    if (text.contains("UNAVAILABLE", Qt::CaseInsensitive) || text.contains("503") || text.contains("high demand", Qt::CaseInsensitive)) {
        return "GEMINI UNAVAILABLE: The model is currently experiencing high demand. Please try again in a few seconds.";
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
    // Hide content of all metadata tags during streaming so they don't leak into the chat bubble
    static const QStringList metadataTags = {"THOUGHT", "USAGE", "SNIPPET", "SUGGESTION", "ACTION", "TOOL_CALL", "TOOL_RESULT"};
    for (const auto& tag : metadataTags) {
        text.remove(QRegularExpression(QString("<%1>.*?(</%1>|$)").arg(tag),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption));
    }

    // For HIGHLIGHT (component markers), keep the content but strip the structural tags
    text.remove(QRegularExpression(R"(</?HIGHLIGHT>)", QRegularExpression::CaseInsensitiveOption));

    return text.trimmed();
}

QVariantList parseMessageParts(const QString& text) {
    QVariantList parts;

    // Combined regex to find Code Blocks, Tool Calls, Tool Results, Actions, and Snippets
    const static QRegularExpression masterRe("(```(\\w*)\\n?(.*?)(?:\\n?```|$)|<TOOL_CALL>(.*?)</TOOL_CALL>|<TOOL_RESULT>(.*?)</TOOL_RESULT>|<ACTION>(.*?)</ACTION>|<SNIPPET>(.*?)</SNIPPET>)", 
                                           QRegularExpression::DotMatchesEverythingOption);
    const static QRegularExpression atRe(R"(@(\w+[\w\.]*))");

    int lastPos = 0;
    auto iter = masterRe.globalMatch(text);
    while (iter.hasNext()) {
        auto match = iter.next();

        // Text part before the match
        QString preText = text.mid(lastPos, match.capturedStart() - lastPos);
        if (!preText.isEmpty()) {
            QString highlighted = preText;
            highlighted.replace(atRe, R"(<font color="#10b981">@\1</font>)");

            QVariantMap part;
            part["type"] = "text";
            part["content"] = highlighted;
            parts.append(part);
        }

        QString fullMatch = match.captured(0);
        if (match.captured(1).startsWith("```")) {
            // Code block
            QVariantMap codePart;
            codePart["type"] = "code";
            codePart["language"] = match.captured(2).isEmpty() ? "text" : match.captured(2);
            codePart["content"] = match.captured(3);
            parts.append(codePart);
        } else if (fullMatch.startsWith("<TOOL_CALL>")) {
            QJsonDocument doc = QJsonDocument::fromJson(match.captured(4).toUtf8());
            if (doc.isObject()) {
                QVariantMap toolPart;
                toolPart["type"] = "tool_call";
                toolPart["name"] = doc.object()["name"].toString();
                toolPart["args"] = doc.object()["args"].toVariant();
                parts.append(toolPart);
            }
        } else if (fullMatch.startsWith("<TOOL_RESULT>")) {
            QJsonDocument doc = QJsonDocument::fromJson(match.captured(5).toUtf8());
            if (doc.isObject()) {
                QVariantMap resPart;
                resPart["type"] = "tool_result";
                resPart["name"] = doc.object()["name"].toString();
                resPart["result"] = doc.object()["result"].toVariant();
                parts.append(resPart);
            }
        } else if (fullMatch.startsWith("<ACTION>")) {
            QVariantMap actPart;
            actPart["type"] = "action_note";
            actPart["content"] = match.captured(6);
            parts.append(actPart);
        } else if (fullMatch.startsWith("<SNIPPET>")) {
            QVariantMap snipPart;
            snipPart["type"] = "command_snippet";
            snipPart["content"] = match.captured(7);
            parts.append(snipPart);
        }

        lastPos = match.capturedEnd();
    }

    // Remaining text
    QString postText = text.mid(lastPos);
    if (!postText.isEmpty() || (parts.isEmpty() && !text.isEmpty())) {
        QString highlighted = postText;
        highlighted.replace(atRe, R"(<font color="#10b981">@\1</font>)");

        QVariantMap part;
        part["type"] = "text";
        part["content"] = highlighted;
        parts.append(part);
    }

    return parts;
}


} // namespace

GeminiPanel::~GeminiPanel() {
    m_isDestroying = true;

    // Stop timers before any teardown can trigger queued UI updates.
    if (m_thinkingPulseTimer) m_thinkingPulseTimer->stop();
    if (m_syncTimer) m_syncTimer->stop();

    // Detach the bridge from QML. Clearing the source here is unnecessary and can
    // provoke crashes while the widget and engine are already being destroyed.
    if (m_quickWidget) {
        if (auto engine = m_quickWidget->engine()) {
            if (auto context = engine->rootContext()) {
                context->setContextProperty("geminiBridge", nullptr);
            }
        }
    }

    auto stopProcess = [](QProcess* proc) {
        if (proc) {
            proc->disconnect();
            if (proc->state() != QProcess::NotRunning) {
                proc->terminate();
                if (!proc->waitForFinished(300)) {
                    proc->kill();
                }
            }
        }
    };

    stopProcess(m_process);
    stopProcess(m_probeProcess);
    stopProcess(m_modelFetchProcess);

    // Child QObject destruction handles the remaining cleanup.
}

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
    
    mainLayout->addWidget(m_quickWidget);

    // Setup bridge connections
    connect(m_bridge, &GeminiBridge::sendMessageRequested, this, [this](const QString& text){
        onBridgeSendMessage(text);
    });
    connect(m_bridge, &GeminiBridge::sendMessageWithImageRequested, this, [this](const QString& text, const QString& image){
        onBridgeSendMessage(text, image);
    });
    connect(m_bridge, &GeminiBridge::stopRequested, this, &GeminiPanel::onBridgeStopRequest);
    connect(m_bridge, &GeminiBridge::refreshModelsRequested, this, &GeminiPanel::onBridgeRefreshModelsRequest);
    connect(m_bridge, &GeminiBridge::clearHistoryRequested, this, &GeminiPanel::clearHistory);
    connect(m_bridge, &GeminiBridge::closeRequested, this, &GeminiPanel::onBridgeCloseRequest);
    connect(m_bridge, &GeminiBridge::showHistoryRequested, this, &GeminiPanel::onBridgeShowHistoryRequest);
    connect(m_bridge, &GeminiBridge::undoToPointRequested, this, &GeminiPanel::onUndoToPoint);
    connect(m_bridge, &GeminiBridge::startNewChatRequested, this, &GeminiPanel::clearHistory);
    connect(m_bridge, &GeminiBridge::showInstructionsRequested, this, &GeminiPanel::onCustomInstructionsClicked);
    connect(m_bridge, &GeminiBridge::exportRequested, this, &GeminiPanel::onExportRequested);

    m_bridge->updateTitle("VIORA AI");

    m_thinkingPulseTimer = new QTimer(this);
    m_thinkingPulseTimer->setInterval(200);
    connect(m_thinkingPulseTimer, &QTimer::timeout, this, [this]() {
        if (!m_bridge) return;
        m_pulseStep = (m_pulseStep + 1) % 4;
        QString dots = QString(".").repeated(m_pulseStep);
        m_bridge->updateStatus("Thinking" + dots);
    });

    m_syncTimer = new QTimer(this);
    m_syncTimer->setSingleShot(true);
    m_syncTimer->setInterval(50); // 20 FPS (Smooth typing)
    connect(m_syncTimer, &QTimer::timeout, this, &GeminiPanel::syncHistoryToBridge);

    connect(&ConfigManager::instance(), &ConfigManager::requestModelRefresh, this, &GeminiPanel::refreshModelList);

    // Defer QML loading and startup work until the surrounding editor UI has settled.
    QTimer::singleShot(0, this, [this]() {
        if (m_isDestroying || !m_quickWidget) {
            return;
        }

        m_quickWidget->setSource(QUrl::fromLocalFile(QDir::current().absoluteFilePath("python/qml/GeminiRoot.qml")));
        refreshModelList();
        loadHistory();
    });
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

void GeminiPanel::onBridgeSendMessage(const QString& text, const QString& imageBase64) {
    qDebug() << "[GeminiPanel] onBridgeSendMessage:" << text << (imageBase64.isEmpty() ? "" : "(image)");

    // Update title if this is likely the start of a conversation
    if (m_bridge && m_bridge->conversationTitle() == "VIORA AI") {
        QString title = text;
        if (title.length() > 30) title = title.left(27) + "...";
        m_bridge->updateTitle(title.toUpper());
    }

    if (text.trimmed().toLower() == "/rewind") {
        appendSystemAction("Rewind", "Rewinding schematic state...", "");
        Q_EMIT rewindRequested();
        return;
    }

    if (m_bridge && m_bridge->currentMode().toLower() == "cmd" && imageBase64.isEmpty()) {
        parseAndExecuteCommandModeInput(text);
    } else {
        askPrompt(text, true, imageBase64);
    }
}
void GeminiPanel::onBridgeStopRequest() {
    qDebug() << "[GeminiPanel] onBridgeStopRequest";
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
    m_isWorking = false;
    if (m_bridge) m_bridge->setWorking(false);
}

void GeminiPanel::onBridgeRefreshModelsRequest() {
    qDebug() << "[GeminiPanel] onBridgeRefreshModelsRequest";
    refreshModelList();
}


void GeminiPanel::onBridgeCloseRequest() {
    qDebug() << "[GeminiPanel] onBridgeCloseRequest";
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

void GeminiPanel::onUndoToPoint(int messageIndex) {
    if (messageIndex < 0 || messageIndex >= m_history.size()) return;
    
    qDebug() << "[GeminiPanel] Request to undo to message index:" << messageIndex;
    
    const QVariantMap& entry = m_history.at(messageIndex);
    if (entry["role"].toString() != "user") {
        qDebug() << "[GeminiPanel] Can only undo to a 'user' message point.";
        return;
    }

    int targetUndoIndex = entry["undoIndex"].toInt();
    qDebug() << "[GeminiPanel] Target undo stack index:" << targetUndoIndex;

    // 1. Revert the editor undo stack
    if (m_undoStack && targetUndoIndex >= 0) {
        m_undoStack->setIndex(targetUndoIndex);
    }

    // 2. Truncate the chat history
    // We remove the selected message and everything that follows it.
    while (m_history.size() > messageIndex) {
        m_history.removeLast();
    }

    syncHistoryToBridge();
    saveHistory();
    
    appendSystemNote("<b>Chat Rewound:</b> Reverted to state before this message.");
}

void GeminiPanel::onExportRequested() {
    QString defaultName = m_bridge->conversationTitle().trimmed();
    if (defaultName.isEmpty() || defaultName == "VIORA AI") defaultName = "Conversation";
    defaultName.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");

    QString fileName = QFileDialog::getSaveFileName(this, tr("Export Chat"), 
                                                    QDir::homePath() + "/" + defaultName + ".md",
                                                    tr("Markdown Files (*.md)"));
    if (fileName.isEmpty()) return;

    QString md;
    md += QString("# Chat: %1\n").arg(m_bridge->conversationTitle());
    md += QString("*Exported on: %1*\n\n").arg(QDateTime::currentDateTime().toString());
    md += "---\n\n";

    for (const auto& entry : m_history) {
        QString role = entry.value("role").toString();
        QString content = entry.value("content").toString().trimmed();
        QString thought = entry.value("thought").toString().trimmed();
        QString timestamp = entry.value("timestamp").toString();

        if (role == "user") {
            md += QString("### You (%1)\n\n").arg(timestamp);
            md += content + "\n\n";
        } else if (role == "model") {
            md += QString("### Viora AI (%1)\n\n").arg(timestamp);
            if (!thought.isEmpty()) {
                md += "> **Thinking**:\n> " + thought.replace("\n", "\n> ") + "\n\n";
            }
            md += content + "\n\n";
        } else if (role == "action") {
             md += QString("*Action: %1*\n\n").arg(content);
        }
        
        md += "---\n\n";
    }

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << md;
        file.close();
        appendSystemNote("Chat exported successfully to **" + QFileInfo(fileName).fileName() + "**");
    } else {
        reportError("Export Failed", "Could not open file for writing: " + file.errorString(), true);
    }
}

void GeminiPanel::clearHistory() {
    qDebug() << "[GeminiPanel] clearHistory";
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
    }
    m_history.clear();
    m_isWorking = false;
    if (m_bridge) {
        m_bridge->setWorking(false);
        m_bridge->clearToolCalls();
    }
    
    appendSystemNote("[SYSTEM] New conversation started.");
}

void GeminiPanel::askPrompt(const QString& text, bool includeContext, const QString& imageBase64) {
    qDebug() << "[GeminiPanel] askPrompt:" << text << (imageBase64.isEmpty() ? "" : "(image)");
    if (m_isWorking) {
        qDebug() << "[GeminiPanel] askPrompt: Busy (m_isWorking is true)";
        return;
    }
    
    QString key = ConfigManager::instance().geminiApiKey().trimmed();
    if (key.isEmpty()) {
        reportError("API Key Missing", "Please set your Gemini API key in settings.", true);
        return;
    }

    QVariantMap entry;
    entry["role"] = "user";
    entry["content"] = text;
    entry["parts"] = parseMessageParts(text);
    if (!imageBase64.isEmpty()) {
        entry["image"] = imageBase64;
    }
    entry["timestamp"] = nowTimeChip();
    entry["undoIndex"] = m_undoStack ? m_undoStack->index() : -1;
    m_history.append(entry);
    syncHistoryToBridge();

    m_isWorking = true;
    if (m_bridge) m_bridge->setWorking(true);
    
    // Prepare arguments for gemini_query.py
    QStringList args;
    args << text;
    
    if (!imageBase64.isEmpty()) {
        args << "--image" << imageBase64;
    }

    const QString selectedModel = m_bridge ? m_bridge->currentModel() : "gemini-2.0-flash";
    if (!selectedModel.isEmpty()) args << "--model" << selectedModel;

    QString instructions = gatherInstructions();
    QString fileContext = gatherFileMentionsContext(text);
    if (!fileContext.isEmpty()) {
        if (!instructions.isEmpty()) instructions += "\n\n";
        instructions += fileContext;
    }
    
    if (!instructions.isEmpty()) args << "--instructions" << instructions;

    if (!m_history.isEmpty()) {
        // Implement history sliding window (limit to last 50 messages to keep context manageable)
        const int maxHistory = 50;
        int startIndex = qMax(0, m_history.size() - maxHistory);
        
        QJsonArray histArray;
        for (int i = startIndex; i < m_history.size(); ++i) {
            const auto& m = m_history[i];
            QJsonObject obj;
            obj["role"] = m["role"].toString();
            obj["text"] = m["content"].toString();
            histArray.append(obj);
        }

        // Use a temporary file to pass history to avoid OS command-line length limits
        QTemporaryFile* histFile = new QTemporaryFile(this);
        if (histFile->open()) {
            histFile->write(QJsonDocument(histArray).toJson(QJsonDocument::Compact));
            histFile->close();
            // We pass the path to the script. The script should read it.
            args << "--history-file" << histFile->fileName();
            // histFile will be deleted when this GeminiPanel is destroyed, 
            // but we want it to persist while the process is running.
            // Since we're using a single m_process, we can manage it there.
        }
    }

    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
        delete m_process;
        m_process = nullptr;
    }
    m_process = new QProcess(this);
    m_process->setProcessEnvironment(FluxScriptManager::getConfiguredEnvironment());

    m_responseBuffer.clear();
    m_errorBuffer.clear();
    m_thinkingBuffer.clear();
    m_leftover.clear();
    m_lastProcessedTagPos = 0;

    connect(m_process, &QProcess::readyReadStandardOutput, this, &GeminiPanel::onProcessReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (m_isDestroying || !m_process) return;
        QByteArray err = m_process->readAllStandardError();
        if (!err.isEmpty()) {
            m_errorBuffer += QString::fromUtf8(err);
            qDebug() << "[GeminiPanel] stderr:" << err.trimmed();
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &GeminiPanel::onProcessFinished);

    QString sPath = QDir(FluxScriptManager::getScriptsDir()).absoluteFilePath("gemini_query.py");
    QString py = FluxScriptManager::getPythonExecutable();
    
    qDebug() << "[GeminiPanel] Executing:" << py << sPath << args.join(" ");
    m_process->start(py, QStringList() << sPath << args);
}

void GeminiPanel::askSmartProbe(const QString& prompt,
                               std::function<void(const QString& chunk)> onChunk,
                               std::function<void()> onDone) {
    if (m_probeProcess && m_probeProcess->state() != QProcess::NotRunning) {
        m_probeProcess->kill();
        m_probeProcess->waitForFinished(200);
    }

    if (!m_probeProcess) {
        m_probeProcess = new QProcess(this);
        m_probeProcess->setProcessEnvironment(FluxScriptManager::getConfiguredEnvironment());
    } else {
        m_probeProcess->disconnect(this);
    }

    QString key = ConfigManager::instance().geminiApiKey().trimmed();
    if (key.isEmpty()) return;

    QString sPath = QDir(FluxScriptManager::getScriptsDir()).absoluteFilePath("gemini_query.py");
    QString py = FluxScriptManager::getPythonExecutable();

    QStringList args;
    QString overlayModel = ConfigManager::instance().geminiOverlayModel();
    if (overlayModel.isEmpty()) overlayModel = (m_bridge ? m_bridge->currentModel() : "gemini-2.0-flash-lite");
    
    args << prompt << "--mode" << "ask" << "--model" << overlayModel;
    
    struct ProbeState {
        QString fullResponse;
        std::function<void(const QString&)> onChunk;
        std::function<void()> onDone;
    };
    auto state = std::make_shared<ProbeState>();
    state->onChunk = onChunk;
    state->onDone = onDone;

    connect(m_probeProcess, &QProcess::readyReadStandardOutput, this, [this, state]() {
        QString chunk = QString::fromUtf8(m_probeProcess->readAllStandardOutput());
        // Clean tags like <THOUGHT>, <ACTION>, etc.
        chunk.remove(QRegularExpression("<THOUGHT>.*?</THOUGHT>", QRegularExpression::DotMatchesEverythingOption));
        chunk.remove(QRegularExpression("<ACTION>.*?</ACTION>"));
        chunk.remove(QRegularExpression("<USAGE>.*?</USAGE>"));
        
        state->fullResponse += chunk;
        if (state->onChunk) state->onChunk(state->fullResponse.trimmed());
    });

    connect(m_probeProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [state](int code) {
        if (code == 0 && state->onDone) state->onDone();
    });

    m_probeProcess->start(py, args);
}

void GeminiPanel::onProcessReadyRead() {
    if (m_isDestroying || !m_process) return;
    QString chunk = m_leftover + QString::fromUtf8(m_process->readAllStandardOutput());
    processAgentStdoutChunk(chunk);
}

void GeminiPanel::processAgentStdoutChunk(const QString& chunk) {
    m_responseBuffer += chunk;
    
    // Find the current active model entry to update.
    // It MUST be after the last 'user' entry to belong to the current turn.
    int lastUserIndex = -1;
    int modelIndex = -1;
    
    for (int i = m_history.size() - 1; i >= 0; --i) {
        QString role = m_history[i]["role"].toString();
        if (role == "user" && lastUserIndex == -1) {
            lastUserIndex = i;
        } else if (role == "model" && modelIndex == -1) {
            modelIndex = i;
        }
        if (lastUserIndex != -1 && modelIndex != -1) break;
    }

    // If the model entry we found is actually from a PREVIOUS turn (before the current user message),
    // then we must ignore it and create a new one for this turn.
    if (modelIndex != -1 && modelIndex < lastUserIndex) {
        modelIndex = -1;
    }

    if (modelIndex == -1) {
        QVariantMap entry;
        entry["role"] = "model";
        entry["content"] = "";
        entry["thought"] = "";
        entry["timestamp"] = nowTimeChip();
        m_history.append(entry);
        modelIndex = m_history.size() - 1;
    }

    // Extract and update thought process if present
    static QRegularExpression thoughtRe("<THOUGHT>(.*?)(</THOUGHT>|$)", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    auto thoughtMatch = thoughtRe.match(m_responseBuffer);
    if (thoughtMatch.hasMatch()) {
        m_history[modelIndex]["thought"] = thoughtMatch.captured(1).trimmed();
        if (m_bridge) m_bridge->updateStatus("Reasoning...");
    }

    // ... (Check for ACTION tags)
    if (chunk.contains("<ACTION>")) {
        QRegularExpression re("<ACTION>(.*?)</ACTION>");
        auto match = re.match(chunk);
        if (match.hasMatch()) {
            if (m_bridge) m_bridge->updateStatus("Executing: " + match.captured(1));
        }
    }

    // Handle Tool and Metadata tags with m_lastProcessedTagPos
    if (chunk.contains("<TOOL_CALL>")) {
        static QRegularExpression tcallRe("<TOOL_CALL>(.*?)</TOOL_CALL>", QRegularExpression::DotMatchesEverythingOption);
        auto iter = tcallRe.globalMatch(m_responseBuffer, m_lastProcessedTagPos);
        while (iter.hasNext()) {
            auto match = iter.next();
            QJsonDocument doc = QJsonDocument::fromJson(match.captured(1).toUtf8());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QVariantMap call;
                call["name"] = obj["name"].toString();
                call["args"] = obj["args"].toVariant();
                call["status"] = "running";
                call["timestamp"] = QDateTime::currentDateTime().toString("HH:mm:ss");
                if (m_bridge) m_bridge->addToolCall(call);
                m_lastProcessedTagPos = match.capturedEnd(0);
            }
        }
    }

    if (chunk.contains("<TOOL_RESULT>")) {
        static QRegularExpression tresRe("<TOOL_RESULT>(.*?)</TOOL_RESULT>", QRegularExpression::DotMatchesEverythingOption);
        auto iter = tresRe.globalMatch(m_responseBuffer, m_lastProcessedTagPos);
        while (iter.hasNext()) {
            auto match = iter.next();
            QJsonDocument doc = QJsonDocument::fromJson(match.captured(1).toUtf8());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                QString name = obj["name"].toString();
                QVariantMap result;
                result["result"] = obj["result"].toVariant();
                if (m_bridge) m_bridge->updateToolResult(name, result);
                m_lastProcessedTagPos = match.capturedEnd(0);

                // Auto-open created netlist files
                if (name == "create_netlist_file") {
                    QVariantMap resMap = result["result"].toMap();
                    if (resMap["status"].toString() == "success") {
                        QString path = resMap["saved_to"].toString();
                        if (!path.isEmpty()) {
                            qDebug() << "[GeminiPanel] Auto-opening created file:" << path;
                            Q_EMIT fileOpenRequested(path);
                        }
                    }
                }
            }
        }
    }

    if (chunk.contains("<USAGE>")) {
        QRegularExpression re("<USAGE>(.*?)</USAGE>");
        auto match = re.match(chunk);
        if (match.hasMatch()) {
            QJsonDocument doc = QJsonDocument::fromJson(match.captured(1).toUtf8());
            if (doc.isObject()) {
                int total = doc.object()["total_tokens"].toInt();
                if (m_bridge) {
                    m_bridge->setTokenCount(total);
                    double pct = (double)total / 1000000.0;
                    m_bridge->setUsagePercentage(pct);
                }
            }
            m_lastProcessedTagPos = match.capturedEnd(0);
        }
    }

    // Handle SNIPPET tags (Command execution)
    static QRegularExpression snippetRe("<SNIPPET>(.*?)</SNIPPET>", QRegularExpression::DotMatchesEverythingOption);
    auto snipIter = snippetRe.globalMatch(m_responseBuffer, m_lastProcessedTagPos);
    while (snipIter.hasNext()) {
        auto match = snipIter.next();
        QString snippetContent = match.captured(1).trimmed();
        QJsonDocument doc = QJsonDocument::fromJson(snippetContent.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("commands") && obj["commands"].isArray()) {
                Q_EMIT checkpointRequested();
                QJsonArray cmds = obj["commands"].toArray();
                for (const auto& cmdVal : cmds) {
                    parseAndExecuteCommandModeInput(cmdVal.toString());
                }
            }
        }
        m_lastProcessedTagPos = match.capturedEnd(0);
    }

    // Handle SUGGESTION tags (Buttons)
    if (chunk.contains("<SUGGESTION>")) {
        static QRegularExpression sugRe("<SUGGESTION>(.*?)</SUGGESTION>");
        auto sugIter = sugRe.globalMatch(m_responseBuffer, m_lastProcessedTagPos);
        while (sugIter.hasNext()) {
            auto match = sugIter.next();
            QString content = match.captured(1);
            QStringList parts = content.split('|');
            if (parts.size() >= 1) {
                QVariantMap suggestion;
                suggestion["label"] = parts[0];
                suggestion["command"] = (parts.size() > 1) ? parts[1] : parts[0];
                
                QVariantList currentSuggestions = m_history[modelIndex]["suggestions"].toList();
                currentSuggestions.append(suggestion);
                m_history[modelIndex]["suggestions"] = currentSuggestions;
            }
            m_lastProcessedTagPos = match.capturedEnd(0);
        }
    }

    // Fallback for raw command JSON (if AI forgets tags)
    if (chunk.contains("\"commands\":") && !m_responseBuffer.contains("<SNIPPET>")) {
        static QRegularExpression rawCmdRe(R"(\{\s*"commands"\s*:\s*\[.*?\]\s*\})", QRegularExpression::DotMatchesEverythingOption);
        auto match = rawCmdRe.match(m_responseBuffer, m_lastProcessedTagPos);
        if (match.hasMatch()) {
            QJsonDocument doc = QJsonDocument::fromJson(match.captured(0).toUtf8());
            if (doc.isObject()) {
                Q_EMIT checkpointRequested();
                QJsonArray cmds = doc.object()["commands"].toArray();
                for (const auto& cmdVal : cmds) {
                    parseAndExecuteCommandModeInput(cmdVal.toString());
                }
            }
            m_lastProcessedTagPos = match.capturedEnd(0);
        }
    }

    // Final Update: Main text content of the MODEL entry
    m_history[modelIndex]["content"] = m_responseBuffer;
    m_history[modelIndex]["parts"] = parseMessageParts(m_responseBuffer);

    // Synchronize to the bridge UI
    if (m_syncTimer && !m_syncTimer->isActive()) {
        m_syncTimer->start();
    }
}

void GeminiPanel::onProcessFinished(int exitCode) {
    if (m_isDestroying) return;
    m_isWorking = false;
    if (m_bridge) {
        m_bridge->setWorking(false);
        m_bridge->updateStatus("Ready");
    }
    
    if (exitCode != 0 || m_responseBuffer.trimmed().isEmpty()) {
        qDebug() << "[GeminiPanel] Process issue detected. Exit:" << exitCode << "Err:" << m_errorBuffer;
        
        QString title = "AI Error";
        QString detail = "The AI process encountered an unexpected issue.";
        bool handled = false;

        if (m_errorBuffer.contains("429") || m_errorBuffer.contains("RESOURCE_EXHAUSTED", Qt::CaseInsensitive)) {
            title = "Quota Exceeded";
            detail = "Gemini free tier limit reached. Please wait 1 minute and try again.";
            handled = true;
        } else if (m_errorBuffer.contains("503") || m_errorBuffer.contains("UNAVAILABLE", Qt::CaseInsensitive) || m_errorBuffer.contains("high demand", Qt::CaseInsensitive)) {
            title = "Model Busy";
            detail = "Gemini is currently experiencing high demand. Please wait a few seconds and try again.";
            handled = true;
        } else if (m_errorBuffer.contains("API_KEY_INVALID", Qt::CaseInsensitive) || m_errorBuffer.contains("INVALID_ARGUMENT", Qt::CaseInsensitive)) {
            title = "API Key Error";
            detail = "Ensure a valid API key is set in your Viora AI settings.";
            handled = true;
        } else if (m_errorBuffer.contains("SAFETY", Qt::CaseInsensitive)) {
            title = "Response Blocked";
            detail = "Gemini safety filters prevented a response for this query.";
            handled = true;
        } else if (m_errorBuffer.contains("Connection") || m_errorBuffer.contains("host", Qt::CaseInsensitive)) {
            title = "Network Error";
            detail = "Could not reach the Gemini service. Check your internet connection.";
            handled = true;
        }

        if (handled) {
            reportError(title, detail, false);
        } else if (exitCode != 0) {
            reportError("Process Error", "AI process terminated unexpectedly (code " + QString::number(exitCode) + ").", false);
        } else {
            reportError("Empty Response", "The AI returned an empty response. Try rephrasing or switching models.", false);
        }
    } else {
        qDebug() << "[GeminiPanel] Process finished successfully.";
        // Final sync and save
        syncHistoryToBridge();
        saveHistory();
    }
}

void GeminiPanel::syncHistoryToBridge() {
    if (m_bridge && !m_isDestroying) {
        QVariantList qmlHistory;
        for (const QVariantMap& entry : m_history) {
            QVariantMap msg = entry;
            QString content = msg.value("content").toString();
            if (content.isEmpty()) content = msg.value("text").toString();
            
            // Carry over thought
            if (entry.contains("thought")) {
                msg["thought"] = entry["thought"];
            }

            if (!msg.contains("parts") || msg["parts"].toList().isEmpty()) {
                msg["parts"] = parseMessageParts(content);
            }
            qmlHistory.append(msg);
        }
        m_bridge->updateMessages(qmlHistory);
    }
}

void GeminiPanel::saveHistory() {
    if (m_isDestroying || m_history.isEmpty()) return;
    static bool isSaving = false;
    if (isSaving) return;
    isSaving = true;
    
    QString historyDir = QDir::homePath() + "/.viospice/gemini/history";
    if (!QDir().mkpath(historyDir)) {
        isSaving = false;
        return;
    }
    
    QString title = m_bridge ? m_bridge->conversationTitle() : "VIORA AI";
    if (title == "VIORA AI") title = "Chat_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    
    // Sanitize title for filename
    QString safeTitle = title;
    safeTitle.replace(QRegularExpression("[^a-zA-Z0-9_]"), "_");
    QString filePath = historyDir + "/" + safeTitle + ".json";
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonArray arr;
        for (const auto& m : m_history) {
            QJsonObject obj;
            obj["role"] = m["role"].toString();
            obj["content"] = m["content"].toString();
            obj["timestamp"] = m["timestamp"].toString();
            if (m.contains("thought")) {
                obj["thought"] = m["thought"].toString();
            }
            arr.append(obj);
        }
        
        QJsonObject root;
        root["title"] = title;
        root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["messages"] = arr;
        
        file.write(QJsonDocument(root).toJson());
        file.close();
        qDebug() << "[GeminiPanel] History saved to" << filePath;
    }
    isSaving = false;
}

void GeminiPanel::refreshModelList() {
    QString key = ConfigManager::instance().geminiApiKey().trimmed();
    if (key.isEmpty()) {
        qDebug() << "[GeminiPanel] refreshModelList: API Key is missing. Skipping model fetch.";
        return;
    }

    if (m_modelFetchProcess && m_modelFetchProcess->state() != QProcess::NotRunning) return;

    m_modelFetchProcess = new QProcess(this);
    m_modelFetchProcess->setProcessEnvironment(FluxScriptManager::getConfiguredEnvironment());

    connect(m_modelFetchProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &GeminiPanel::onModelFetchFinished);

    QString sPath = QDir(FluxScriptManager::getScriptsDir()).absoluteFilePath("gemini_query.py");
    QString py = FluxScriptManager::getPythonExecutable();

    qDebug() << "[GeminiPanel] Fetching models using:" << py;
    m_modelFetchProcess->start(py, QStringList() << sPath << "--list-models");
}

void GeminiPanel::onModelFetchFinished(int exitCode, QProcess::ExitStatus) {
    if (exitCode != 0 || !m_modelFetchProcess) {
        if (m_modelFetchProcess) {
            QByteArray err = m_modelFetchProcess->readAllStandardError();
            qDebug() << "[GeminiPanel] Model fetch failed with exit code" << exitCode << "Error:" << err;
        }
        return;
    }
    
    QString output = QString::fromUtf8(m_modelFetchProcess->readAllStandardOutput());
    QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
    if (doc.isArray()) {
        QStringList models;
        for (auto v : doc.array()) {
            if (v.isString()) models << v.toString();
            else if (v.isObject()) models << v.toObject()["name"].toString();
        }
        
        // Ensure default model is first if not already present
        const QString defModel = "gemini-2.0-flash";
        if (!models.contains(defModel)) {
            models.prepend(defModel);
        }

        if (m_bridge) {
            m_bridge->updateAvailableModels(models);
            qDebug() << "[GeminiPanel] Models updated:" << models.count() << "found.";
        }
        
        // Persist to config for settings dialog
        ConfigManager::instance().setAvailableGeminiModels(models);
    } else {
        qDebug() << "[GeminiPanel] Failed to parse model list JSON. Output was:" << output;
    }
}

void GeminiPanel::reportError(const QString& title, const QString& details, bool openDialog) {
    appendSystemNote(QString("<b>%1</b>: %2").arg(title, details));
    if (openDialog) {
        // Implementation for dialog
    }
}


void GeminiPanel::appendSystemNote(const QString& text) {
    QVariantMap note;
    note["role"] = "system";
    note["content"] = text;
    note["parts"] = parseMessageParts(text);
    note["timestamp"] = nowTimeChip();
    m_history.append(note);
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
    QString schematicContext = gatherSchematicContext();
    if (!schematicContext.isEmpty()) {
        if (!combined.isEmpty()) combined += "\n\n";
        combined += schematicContext;
    }

    return combined;
}

QString GeminiPanel::gatherSchematicContext() const {
    if (!m_scene || !m_netManager) return QString();
    
    SpiceNetlistGenerator::SimulationParams params;
    params.type = SpiceNetlistGenerator::OP;
    
    QString projectDir = m_projectFilePath.isEmpty() ? QDir::currentPath() : QFileInfo(m_projectFilePath).absolutePath();
    QString netlist = SpiceNetlistGenerator::generate(m_scene, projectDir, m_netManager, params);
    
    if (netlist.trimmed().isEmpty()) return QString();
    
    QString context = "CURRENT SCHEMATIC CONTEXT (SPICE NETLIST):\n";
    context += "This is a netlist representation of the user's active schematic tab.\n";
    context += netlist;
    return context;
}

QString GeminiPanel::gatherFileMentionsContext(const QString& text) const {
        static const QRegularExpression atRe(R"(@(\w+[\w\.]*))");
        auto iter = atRe.globalMatch(text);

        QString context;
        QSet<QString> processedFiles;
        QStringList workspaces = ConfigManager::instance().workspaceFolders();
        if (workspaces.isEmpty()) workspaces << QDir::currentPath();

        while (iter.hasNext()) {
            auto match = iter.next();
            QString fileName = match.captured(1);
            if (processedFiles.contains(fileName)) continue;
            processedFiles.insert(fileName);

            // Find file in workspace
            QString foundPath;
            for (const QString& wsPath : workspaces) {
                QDirIterator it(wsPath, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    QString path = it.next();
                    if (it.fileName().toLower() == fileName.toLower()) {
                        foundPath = path;
                        break;
                    }
                }
                if (!foundPath.isEmpty()) break;
            }

            if (!foundPath.isEmpty()) {
                QFile file(foundPath);
                if (file.open(QIODevice::ReadOnly)) {
                    QString content = QString::fromUtf8(file.readAll());
                    // Limit content size to prevent context overflow (max 20KB per file)
                    if (content.length() > 20000) content = content.left(20000) + "\n... [File Truncated]";

                    if (!context.isEmpty()) context += "\n\n";
                    context += QString("--- FILE CONTENT: %1 ---\n%2\n--------------------------").arg(fileName, content);
                }
            }
        }

        return context;
    }

void GeminiPanel::loadHistory() {
    QString historyDir = QDir::homePath() + "/.viospice/gemini/history";
    if (QDir().mkpath(historyDir)) {
        qDebug() << "[GeminiPanel] History directory ensured:" << historyDir;
    }
}

void GeminiPanel::loadHistoryFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (!doc.isObject()) return;
    
    QJsonObject rootObj = doc.object();
    if (m_bridge) m_bridge->updateTitle(rootObj["title"].toString());
    
    m_history.clear();
    QJsonArray arr = rootObj["messages"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        QVariantMap entry;
        entry["role"] = obj["role"].toString();
        entry["content"] = obj["content"].toString();
        entry["timestamp"] = obj["timestamp"].toString();
        if (obj.contains("thought")) {
            entry["thought"] = obj["thought"].toString();
        }
        m_history.append(entry);
    }
    
    syncHistoryToBridge();
    qDebug() << "[GeminiPanel] History session loaded:" << path;
}

void GeminiPanel::onBridgeShowHistoryRequest() {
    qDebug() << "[GeminiPanel] onBridgeShowHistoryRequest";
    QString historyDir = QDir::homePath() + "/.viospice/gemini/history";
    QDir dir(historyDir);
    
    QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        qDebug() << "[GeminiPanel] No history files found.";
        if (m_bridge) m_bridge->updateStatus("No history found yet.");
        return;
    }
    
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background-color: #1e293b; color: #e2e8f0; border: 1px solid #334155; padding: 4px; } "
                       "QMenu::item { padding: 4px 20px; border-radius: 4px; } "
                       "QMenu::item:selected { background-color: #3b82f6; color: white; }");
    
    for (const QFileInfo& info : files) {
        QString title = info.baseName();
        QFile file(info.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) title = doc.object()["title"].toString();
            file.close();
        }
        
        QAction* action = menu.addAction(title);
        connect(action, &QAction::triggered, this, [this, filePath = info.absoluteFilePath()]() {
            loadHistoryFromFile(filePath);
        });
    }
    
    menu.exec(QCursor::pos());
}
void GeminiPanel::onRefreshModelsClicked() { refreshModelList(); }
void GeminiPanel::onCustomInstructionsClicked() {
    qDebug() << "[GeminiPanel] Opening Custom Instructions Dialog";
    GeminiInstructionsDialog dialog(m_projectFilePath, this);
    dialog.exec();
}
void GeminiPanel::onCopyPromptClicked() {}
void GeminiPanel::handleActionTag(const QString& act) { Q_UNUSED(act); }
void GeminiPanel::handleSuggestionTag(const QString& sug) { Q_UNUSED(sug); }
void GeminiPanel::parseAndExecuteCommandModeInput(const QString& in) {
    QString raw = in.trimmed();
    if (raw.isEmpty()) return;

    // Advanced Parser: Handle quoted strings and multi-line arguments (like netlists)
    QStringList args;
    QString currentArg;
    QChar inQuoteChar; // null character means not in quotes
    for (int i = 0; i < raw.size(); ++i) {
        QChar c = raw[i];
        if (c == '\"' || c == '\'') {
            // Handle escaped quotes
            if (i > 0 && raw[i-1] == '\\') {
                currentArg.chop(1); // remove the backslash
                currentArg += c;
            } else {
                if (inQuoteChar.isNull()) {
                    inQuoteChar = c; // start quote
                } else if (inQuoteChar == c) {
                    inQuoteChar = QChar(); // end quote
                } else {
                    currentArg += c; // quote inside different quote
                }
            }
        } else if (c.isSpace() && inQuoteChar.isNull()) {
            if (!currentArg.isEmpty()) {
                args << currentArg;
                currentArg.clear();
            }
        } else {
            currentArg += c;
        }
    }
    if (!currentArg.isEmpty()) args << currentArg;
    if (args.isEmpty()) return;

    QString cmd = args[0].toLower();
    qDebug() << "[GeminiPanel] Command Mode:" << cmd << "Args count:" << args.size();

    if (cmd == "help") {
        appendSystemNote("<b>Available Commands:</b><br/>"
                         "- <b>help</b>: Show this message<br/>"
                         "- <b>list nodes</b>: List all net names in the schematic<br/>"
                         "- <b>list components</b>: List all parts (R1, U1, etc.)<br/>"
                         "- <b>run sim / run simulation</b>: Start SPICE engine (F8)<br/>"
                         "- <b>run erc</b>: Run Electrical Rules Check<br/>"
                         "- <b>plot <name></b>: Open oscilloscope for a signal<br/>"
                         "- <b>zoom fit</b>: Fit the schematic to view<br/>"
                         "- <b>export netlist</b>: Show raw SPICE output<br/>"
                         "- <b>clear chat / clear history</b>: Wipe all messages<br/>"
                         "- <b>toggle <panel></b>: Show/hide UI panels (e.g. toggle library)");
    } else if (cmd == "generate_schematic_from_netlist" && args.size() >= 2) {
        QString netlist = args[1];
        QString title = (args.size() > 2) ? args[2] : "Generated Circuit";
        appendSystemAction("Synthesis", "Converting netlist to schematic: " + title, "");
        Q_EMIT netlistGenerated(netlist);
        return;
    } else if (cmd == "list nodes" || cmd == "list_nodes") {
        appendSystemNote("<b>Available Commands:</b><br/>"
                         "- <b>help</b>: Show this message<br/>"
                         "- <b>list nodes</b>: List all net names in the schematic<br/>"
                         "- <b>list components</b>: List all parts (R1, U1, etc.)<br/>"
                         "- <b>run sim / run simulation</b>: Start SPICE engine (F8)<br/>"
                         "- <b>run erc</b>: Run Electrical Rules Check<br/>"
                         "- <b>plot <name></b>: Open oscilloscope for a signal<br/>"
                         "- <b>zoom fit</b>: Fit the schematic to view<br/>"
                         "- <b>export netlist</b>: Show raw SPICE output<br/>"
                         "- <b>clear chat / clear history</b>: Wipe all messages<br/>"
                         "- <b>toggle <panel></b>: Show/hide UI panels (e.g. toggle library)");
    } else if (cmd == "list nodes" || cmd == "list_nodes") {
        if (!m_netManager) {
            appendSystemNote("<b>Error:</b> NetManager is not currently linked to the AI panel.");
            return;
        }
        QStringList nets = m_netManager->netNames();
        if (nets.isEmpty()) {
            appendSystemNote("<b>Result:</b> Schematic has no named nets yet.");
        } else {
            appendSystemNote("<b>Nets Found:</b> " + nets.join(", "));
        }
    } else if (cmd == "run erc" || cmd == "run_erc") {
        appendSystemAction("ERC", "Running Electrical Rules Check...", "");
        Q_EMIT runERCRequested();
    } else if (cmd == "run simulation" || cmd == "run sim" || cmd == "run_simulation" || cmd == "run_sim" || cmd == "simulate") {
        appendSystemAction("Simulation", "Initializing SPICE simulation...", "");
        Q_EMIT runSimulationRequested();
    } else if (cmd == "set_property" && args.size() >= 4) {
        QString ref = args[1];
        QString prop = args[2];
        QString val = args[3];
        
        QJsonObject cmdObj;
        cmdObj["cmd"] = "setProperty";
        cmdObj["reference"] = ref;
        cmdObj["property"] = prop;
        cmdObj["value"] = val;
        
        QJsonArray cmdArr;
        cmdArr.append(cmdObj);
        QJsonObject snipObj;
        snipObj["commands"] = cmdArr;
        
        Q_EMIT snippetGenerated(QString(QJsonDocument(snipObj).toJson(QJsonDocument::Compact)));
        appendSystemNote("<b>Property Updated:</b> " + ref + " -> " + prop + " = " + val);
    } else if (cmd == "add_directive" && args.size() >= 2) {
        QString directive = raw.mid(raw.indexOf("add_directive") + 13).trimmed();
        if (directive.startsWith("\"") && directive.endsWith("\"")) {
            directive = directive.mid(1, directive.length() - 2);
        }
        
        QJsonObject props;
        props["value"] = directive;
        
        QJsonObject cmdObj;
        cmdObj["cmd"] = "addComponent";
        cmdObj["type"] = "Spice Directive";
        cmdObj["x"] = -100;
        cmdObj["y"] = -100;
        cmdObj["properties"] = props;
        
        QJsonArray cmdArr;
        cmdArr.append(cmdObj);
        QJsonObject snipObj;
        snipObj["commands"] = cmdArr;
        
        Q_EMIT snippetGenerated(QString(QJsonDocument(snipObj).toJson(QJsonDocument::Compact)));
        appendSystemNote("<b>Directive Added:</b> " + directive);
    } else if (cmd == "execute_commands" && args.size() >= 2) {
        QString jsonStr = args[1];
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
        if (doc.isArray()) {
            QJsonObject snipObj;
            snipObj["commands"] = doc.array();
            Q_EMIT snippetGenerated(QString(QJsonDocument(snipObj).toJson(QJsonDocument::Compact)));
        } else {
            appendSystemNote("<b>Error:</b> Invalid JSON array for execute_commands.");
        }
    } else if (cmd == "list components" || cmd == "list parts" || cmd == "list_components" || cmd == "list_parts") {
        if (!m_scene) {
            appendSystemNote("<b>Error:</b> Schematic scene is not active.");
            return;
        }
        QStringList references;
        for (QGraphicsItem* item : m_scene->items()) {
            // Symbols usually have a "reference" property in their data
            QVariant ref = item->data(0); // 0 is often the reference string in this project
            if (!ref.toString().isEmpty() && !ref.toString().contains("Net")) {
                references << ref.toString();
            }
        }
        if (references.isEmpty()) {
            appendSystemNote("<b>Result:</b> No components found in active scene.");
        } else {
            references.sort();
            appendSystemNote("<b>Components Found:</b> " + references.join(", "));
        }
    } else if (cmd == "zoom fit" || cmd == "zoom all") {
        appendSystemNote("<b>View:</b> Fitting schematic to view...");
        Q_EMIT zoomFitRequested();
    } else if (cmd == "clear chat" || cmd == "clear history" || cmd == "clear") {
        clearHistory();
    } else if (cmd == "export netlist" || cmd == "show netlist") {
        QString netlist = gatherSchematicContext();
        if (netlist.isEmpty()) {
            appendSystemNote("<b>Error:</b> Failed to generate netlist context.");
        } else {
            appendSystemNote("<b>Exported Netlist:</b><br/><pre><code>" + netlist + "</code></pre>");
        }
    } else if (cmd.startsWith("toggle ")) {
        QString panel = cmd.mid(7).trimmed();
        appendSystemNote("<b>UI:</b> Toggling panel: " + panel);
        Q_EMIT togglePanelRequested(panel);
    } else if (cmd.startsWith("import_subckt ")) {
        QString path = in.mid(14).trimmed();
        Q_EMIT checkpointRequested();
        appendSystemAction("Import Subcircuit", "Opening symbol generator for: " + QFileInfo(path).fileName(), "");
        Q_EMIT importSubcircuitRequested(path);
    } else {
        appendSystemNote("<b>Unknown Command:</b> \"" + in + "\".<br/>Type <b>help</b> for a list of available system commands.");
    }
}

void GeminiPanel::ensureErrorDialog() {}
void GeminiPanel::populateErrorDialogHistory() {}
void GeminiPanel::selectErrorHistoryRow(int r) { Q_UNUSED(r); }
void GeminiPanel::appendErrorHistory(const QString& t, const QString& d) { Q_UNUSED(t); Q_UNUSED(d); }
void GeminiPanel::showErrorDialog(const QString& t, const QString& d) { Q_UNUSED(t); Q_UNUSED(d); }
void GeminiPanel::showErrorBanner(const QString& s, const QString& d) { Q_UNUSED(s); Q_UNUSED(d); }
void GeminiPanel::hideErrorBanner() {}
