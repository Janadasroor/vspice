#include "gemini_bridge.h"
#include "../core/theme_manager.h"
#include "../core/config_manager.h"
#include <QColor>
#include <QPalette>
#include <QDebug>
#include <QClipboard>
#include <QGuiApplication>
#include <QDir>
#include <QDirIterator>
#include <QMimeData>
#include <QBuffer>
#include <QImage>

GeminiBridge::GeminiBridge(QObject* parent) : QObject(parent) {
    m_currentModel = ConfigManager::instance().geminiChatModel();
    if (m_currentModel.isEmpty()) m_currentModel = ConfigManager::instance().geminiSelectedModel();
    
    m_currentMode = ConfigManager::instance().geminiSelectedMode();
    m_availableModels = ConfigManager::instance().availableGeminiModels();
    if (!m_availableModels.contains(m_currentModel)) {
        m_availableModels << m_currentModel;
    }
}

QString GeminiBridge::textColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->textColor().name() : "#000000";
}

QString GeminiBridge::secondaryColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->textSecondary().name() : "#888888";
}

QString GeminiBridge::accentColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->accentColor().name() : "#3b82f6";
}

QString GeminiBridge::backgroundColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->windowBackground().name() : "#ffffff";
}

QString GeminiBridge::glassBackground() const {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return "rgba(255, 255, 255, 0.95)";
    
    if (theme->type() == PCBTheme::Light) {
        return "rgba(255, 255, 255, 0.95)";
    } else {
        return "rgba(13, 17, 23, 0.92)";
    }
}

void GeminiBridge::setCurrentModel(const QString& model) {
    if (m_currentModel != model) {
        m_currentModel = model;
        ConfigManager::instance().setGeminiSelectedModel(m_currentModel);
        Q_EMIT currentModelChanged();
    }
}

void GeminiBridge::setCurrentMode(const QString& mode) {
    if (m_currentMode != mode) {
        m_currentMode = mode;
        ConfigManager::instance().setGeminiSelectedMode(m_currentMode);
        Q_EMIT currentModeChanged();
    }
}

void GeminiBridge::sendMessage(const QString& text) {
    qDebug() << "[GeminiBridge] sendMessage:" << text;
    Q_EMIT sendMessageRequested(text);
}

void GeminiBridge::sendMessageWithImage(const QString& text, const QString& imageBase64) {
    qDebug() << "[GeminiBridge] sendMessageWithImage:" << text << " (image attached)";
    Q_EMIT sendMessageWithImageRequested(text, imageBase64);
}

void GeminiBridge::clearHistory() {
    Q_EMIT clearHistoryRequested();
}

void GeminiBridge::stopRun() {
    Q_EMIT stopRequested();
}

void GeminiBridge::refreshModels() {
    Q_EMIT refreshModelsRequested();
}

void GeminiBridge::updateMessages(const QVariantList& msgs) {
    m_messages = msgs;
    Q_EMIT messagesChanged();
}

void GeminiBridge::setWorking(bool working, const QString& thinking) {
    if (m_isWorking != working) {
        m_isWorking = working;
        Q_EMIT isWorkingChanged();
    }
    if (!thinking.isEmpty()) {
        updateStatus(thinking);
    }
}

void GeminiBridge::updateStatus(const QString& status) {
    if (m_thinkingText != status) {
        m_thinkingText = status;
        
        // Parse "Tool: Action" format
        int colonIdx = status.indexOf(':');
        if (colonIdx != -1) {
            m_currentTool = status.left(colonIdx).trimmed();
            m_currentAction = status.mid(colonIdx + 1).trimmed();
        } else {
            m_currentTool = "ViorAI";
            m_currentAction = status.trimmed();
        }

        Q_EMIT thinkingTextChanged();
        Q_EMIT currentToolChanged();
        Q_EMIT currentActionChanged();
    }
}

void GeminiBridge::updateAvailableModels(const QStringList& models) {
    if (m_availableModels != models) {
        m_availableModels = models;
        Q_EMIT availableModelsChanged();
    }
}

void GeminiBridge::updateTitle(const QString& title) {
    if (m_conversationTitle != title) {
        m_conversationTitle = title;
        Q_EMIT conversationTitleChanged();
    }
}

void GeminiBridge::closePanel() {
    Q_EMIT closeRequested();
}

void GeminiBridge::showHistory() {
    Q_EMIT showHistoryRequested();
}

void GeminiBridge::startNewChat() {
    Q_EMIT startNewChatRequested();
}

void GeminiBridge::undoToPoint(int messageIndex) {
    Q_EMIT undoToPointRequested(messageIndex);
}
void GeminiBridge::setTokenCount(int count) {
    if (m_tokenCount != count) {
        m_tokenCount = count;
        Q_EMIT tokenCountChanged();
    }
}

void GeminiBridge::setUsagePercentage(double percentage) {
    if (m_usagePercentage != percentage) {
        m_usagePercentage = percentage;
        Q_EMIT usagePercentageChanged();
    }
}

void GeminiBridge::copyToClipboard(const QString& text) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text);
    }
}

QString GeminiBridge::getImageFromClipboard() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) return QString();

    const QMimeData *mimeData = clipboard->mimeData();
    if (mimeData && mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            // Scale down if too large to save tokens/bandwidth (max 1024 width)
            if (image.width() > 1024) {
                image = image.scaledToWidth(1024, Qt::SmoothTransformation);
            }

            QByteArray ba;
            QBuffer buffer(&ba);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            return QString::fromLatin1(ba.toBase64());
        }
    }
    return QString();
}

void GeminiBridge::showInstructions() {
    Q_EMIT showInstructionsRequested();
}

void GeminiBridge::setZoomFactor(double zoom) {
    // Keep zoom between 0.5 and 3.0
    double boundedZoom = qBound(0.5, zoom, 3.0);
    if (m_zoomFactor != boundedZoom) {
        m_zoomFactor = boundedZoom;
        Q_EMIT zoomFactorChanged();
    }
}

void GeminiBridge::zoomIn() {
    setZoomFactor(m_zoomFactor + 0.1);
}

void GeminiBridge::zoomOut() {
    setZoomFactor(m_zoomFactor - 0.1);
}

void GeminiBridge::resetZoom() {
    setZoomFactor(1.0);
}

void GeminiBridge::exportChat() {
    Q_EMIT exportRequested();
}

void GeminiBridge::addToolCall(const QVariantMap& call) {
    m_toolCalls.append(call);
    Q_EMIT toolCallsChanged();
}

void GeminiBridge::updateToolResult(const QString& toolName, const QVariantMap& result) {
    // Find the LAST tool with this name that is still 'pending' or 'running'
    for (int i = m_toolCalls.size() - 1; i >= 0; --i) {
        QVariantMap tool = m_toolCalls[i].toMap();
        if (tool["name"].toString() == toolName) {
            // Update the tool entry
            for (auto it = result.begin(); it != result.end(); ++it) {
                tool[it.key()] = it.value();
            }
            tool["status"] = "success";
            m_toolCalls[i] = tool;
            Q_EMIT toolCallsChanged();
            return;
        }
    }
}

void GeminiBridge::clearToolCalls() {
    if (!m_toolCalls.isEmpty()) {
        m_toolCalls.clear();
        Q_EMIT toolCallsChanged();
    }
}

QStringList GeminiBridge::findFiles(const QString& query) {
    QStringList results;
    QStringList workspaces = ConfigManager::instance().workspaceFolders();
    
    if (workspaces.isEmpty()) {
        // Fallback to current path if no workspace is defined, 
        // but typically VioSpice has at least one workspace open.
        workspaces << QDir::currentPath();
    }

    int count = 0;
    const int maxResults = 10;

    for (const QString& wsPath : workspaces) {
        QDir dir(wsPath);
        if (!dir.exists()) continue;

        QDirIterator it(dir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && count < maxResults) {
            QString filePath = it.next();
            QString fileName = it.fileName();
            
            // Skip hidden folders and common noise
            if (filePath.contains("/.git/") || filePath.contains("/node_modules/") || 
                filePath.contains("/build/") || filePath.contains("/__pycache__/") ||
                fileName.startsWith(".")) {
                continue;
            }

            if (query.isEmpty() || fileName.contains(query, Qt::CaseInsensitive)) {
                // If it's already in results, skip
                if (!results.contains(fileName)) {
                    results << fileName;
                    count++;
                }
            }
        }
        if (count >= maxResults) break;
    }
    
    return results;
}
