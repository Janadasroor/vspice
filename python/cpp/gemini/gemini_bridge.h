#ifndef GEMINI_BRIDGE_H
#define GEMINI_BRIDGE_H

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QDateTime>

class GeminiBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(QString currentModel READ currentModel WRITE setCurrentModel NOTIFY currentModelChanged)
    Q_PROPERTY(QVariantList availableModes READ availableModes NOTIFY themeChanged)
    Q_PROPERTY(QString currentMode READ currentMode WRITE setCurrentMode NOTIFY currentModeChanged)
    Q_PROPERTY(bool isWorking READ isWorking NOTIFY isWorkingChanged)
    Q_PROPERTY(QString thinkingText READ thinkingText NOTIFY thinkingTextChanged)
    Q_PROPERTY(QString currentTool READ currentTool NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentAction READ currentAction NOTIFY currentActionChanged)
    Q_PROPERTY(QString conversationTitle READ conversationTitle NOTIFY conversationTitleChanged)
    
    // Theme properties for QML
    Q_PROPERTY(QString textColor READ textColor NOTIFY themeChanged)
    Q_PROPERTY(QString secondaryColor READ secondaryColor NOTIFY themeChanged)
    Q_PROPERTY(QString accentColor READ accentColor NOTIFY themeChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(QString glassBackground READ glassBackground NOTIFY themeChanged)
    Q_PROPERTY(int tokenCount READ tokenCount NOTIFY tokenCountChanged)
    Q_PROPERTY(double usagePercentage READ usagePercentage NOTIFY usagePercentageChanged)
    Q_PROPERTY(double zoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(QVariantList toolCalls READ toolCalls NOTIFY toolCallsChanged)

public:
    explicit GeminiBridge(QObject* parent = nullptr);

    QVariantList messages() const { return m_messages; }
    QStringList availableModels() const { return m_availableModels; }
    QString currentModel() const { return m_currentModel; }
    QVariantList availableModes() const { 
        return {
            QVariantMap{{"name", "Planning"}, {"desc", "Agent can plan before executing tasks. Use for deep research, complex tasks, or collaborative work"}},
            QVariantMap{{"name", "Direct"}, {"desc", "Agent will execute tasks directly. Use for simple tasks that can be completed faster"}},
            QVariantMap{{"name", "Ask"}, {"desc", "General Q&A and explanation of code or concepts"}},
            QVariantMap{{"name", "Cmd"}, {"desc", "Execute system commands and interact with the workspace directly"}}
        };
    }
    QString currentMode() const { return m_currentMode; }
    bool isWorking() const { return m_isWorking; }
    QString thinkingText() const { return m_thinkingText; }
    QString currentTool() const { return m_currentTool; }
    QString currentAction() const { return m_currentAction; }
    QString conversationTitle() const { return m_conversationTitle; }

    // Theme getters
    QString textColor() const;
    QString secondaryColor() const;
    QString accentColor() const;
    QString backgroundColor() const;
    QString glassBackground() const;

    int tokenCount() const { return m_tokenCount; }
    double usagePercentage() const { return m_usagePercentage; }
    double zoomFactor() const { return m_zoomFactor; }
    QVariantList toolCalls() const { return m_toolCalls; }

    void setCurrentModel(const QString& model);
    void setCurrentMode(const QString& mode);
    void setTokenCount(int count);
    void setUsagePercentage(double percentage);
    void setZoomFactor(double zoom);

    Q_INVOKABLE void copyToClipboard(const QString& text);
    Q_INVOKABLE QString getImageFromClipboard();
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void sendMessageWithImage(const QString& text, const QString& imageBase64);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void stopRun();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void closePanel();
    Q_INVOKABLE void showHistory();
    Q_INVOKABLE void startNewChat();
    Q_INVOKABLE void undoToPoint(int messageIndex);
    Q_INVOKABLE void showInstructions();
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void exportChat();
    Q_INVOKABLE void clearToolCalls();
    Q_INVOKABLE QStringList findFiles(const QString& query);

Q_SIGNALS:
    void messagesChanged();
    void availableModelsChanged();
    void currentModelChanged();
    void currentModeChanged();
    void isWorkingChanged();
    void thinkingTextChanged();
    void currentToolChanged();
    void currentActionChanged();
    void conversationTitleChanged();
    void tokenCountChanged();
    void usagePercentageChanged();
    void themeChanged();
    void zoomFactorChanged();
    void toolCallsChanged();
    
    // Internal signals to trigger logic in GeminiPanel
    void sendMessageRequested(const QString& text);
    void sendMessageWithImageRequested(const QString& text, const QString& imageBase64);
    void stopRequested();
    void refreshModelsRequested();
    void clearHistoryRequested();
    void closeRequested();
    void showHistoryRequested();
    void startNewChatRequested();
    void undoToPointRequested(int messageIndex);
    void showInstructionsRequested();
    void exportRequested();

public Q_SLOTS:
    void updateMessages(const QVariantList& msgs);
    void setWorking(bool working, const QString& thinking = "");
    void updateStatus(const QString& status);
    void updateAvailableModels(const QStringList& models);
    void updateTitle(const QString& title);
    void addToolCall(const QVariantMap& call);
    void updateToolResult(const QString& toolName, const QVariantMap& result);
    void notifyThemeChanged() { Q_EMIT themeChanged(); }

private:
    QVariantList m_messages;
    QStringList m_availableModels;
    QString m_currentModel = "gemini-2.0-flash";
    QString m_currentMode = "ask";
    bool m_isWorking = false;
    QString m_thinkingText;
    QString m_currentTool = "ViorAI";
    QString m_currentAction = "Thinking...";
    QString m_conversationTitle = "VIORA AI";
    int m_tokenCount = 0;
    double m_usagePercentage = 0.0;
    double m_zoomFactor = 1.0;
    QVariantList m_toolCalls;
};

#endif // GEMINI_BRIDGE_H
