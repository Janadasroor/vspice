#ifndef GEMINI_PANEL_H
#define GEMINI_PANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QGraphicsScene>
#include <QProcess>
#include <QVariantMap>
#include <functional>
#include <QUrl>
#include <QLabel>
#include <QList>
#include <QSet>

class QTextEdit;
class QTimer;
class QDialog;
class QListWidget;
class QPlainTextEdit;
class QToolButton;
class QResizeEvent;
class QScrollArea;
class QVBoxLayout;

/**
 * @brief Reusable AI Assistant panel for both dock widgets and dialogs.
 */
class GeminiPanel : public QWidget {
    Q_OBJECT
public:
    explicit GeminiPanel(QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);

    void setScene(QGraphicsScene* scene) { m_scene = scene; }
    void setNetManager(class NetManager* netManager) { m_netManager = netManager; }
    void setMode(const QString& mode);
    void setProjectFilePath(const QString& path);
    void setUndoStack(class QUndoStack* stack);
    void saveHistory();
    void loadHistory();
    void loadHistoryFromFile(const QString& filePath);
    QString mode() const { return m_mode; }

    void askPrompt(const QString& prompt, bool includeContext = true);

    void setContextProvider(std::function<QString()> provider) { m_contextProvider = provider; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

public slots:
    void clearHistory();

signals:
    void fluxScriptGenerated(const QString& code);
    void symbolJsonGenerated(const QString& json);
    void pythonScriptGenerated(const QString& code);
    void suggestionTriggered(const QString& command);
    void itemsHighlighted(const QStringList& references);
    void snippetGenerated(const QString& jsonSnippet);
    void netlistGenerated(const QString& netlistText);
    void runSimulationRequested();
    void runERCRequested();
    void togglePanelRequested(const QString& panelName);

private slots:
    void onSendClicked();
    void onSaveKeyClicked();
    void onProcessReadyRead();
    void onProcessFinished(int exitCode);
    void updateApiKeyVisibility();
    void updateContextCheckbox();
    void onCopyClicked();
    void onStopClicked();
    void onAnchorClicked(const QUrl& url);
    void updateThinkingPulse();
    void onRefreshModelsClicked();
    void onModelFetchFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDismissErrorClicked();
    void onViewErrorDetailsClicked();
    void onVoiceClicked();
    void onCustomInstructionsClicked();

private:
    struct ErrorRecord {
        QString timestamp;
        QString title;
        QString details;
    };

    struct ChatMessage {
        enum class Kind {
            User,
            ModelMarkdown,
            SystemHtml
        };
        Kind kind = Kind::SystemHtml;
        QString body;
        QString meta;
        QString timestamp;
    };

    QGraphicsScene* m_scene;
    class NetManager* m_netManager = nullptr;
    class QUndoStack* m_undoStack = nullptr;
    QScrollArea* m_chatScroll = nullptr;
    QWidget* m_chatContainer = nullptr;
    QVBoxLayout* m_chatLayout = nullptr;
    QList<QWidget*> m_chatMessageWidgets;
    QTextEdit* m_thinkingDisplay;
    QTextEdit* m_inputField;
    QPushButton* m_sendButton;
    QPushButton* m_clearButton;
    QPushButton* m_saveKeyButton = nullptr;
    QPushButton* m_copyButton;
    QPushButton* m_historyMenuButton = nullptr;
    QPushButton* m_stopButton;
    QPushButton* m_thinkingToggleButton;
    QPushButton* m_voiceButton = nullptr;
    QWidget* m_toolCallBanner = nullptr;
    QLabel* m_toolCallTitle = nullptr;
    QLabel* m_toolCallSubtitle = nullptr;
    QCheckBox* m_includeContextCheck;
    QCheckBox* m_includeScreenshotCheck;
    class QComboBox* m_modelCombo = nullptr;
    QPushButton* m_refreshModelsButton = nullptr;

    // Pulse animation
    QTimer* m_thinkingPulseTimer;
    QTimer* m_rerenderTimer = nullptr;
    int m_pulseStep = 0;

    // API Key UI
    QWidget* m_apiKeyContainer;
    QLineEdit* m_apiKeyField;
    QWidget* m_errorBanner = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_errorDismissButton = nullptr;
    QPushButton* m_errorDetailsButton = nullptr;
    QList<ErrorRecord> m_errorHistory;
    QDialog* m_errorDialog = nullptr;
    QListWidget* m_errorHistoryList = nullptr;
    QTextEdit* m_errorSummaryView = nullptr;
    QPlainTextEdit* m_errorRawView = nullptr;
    QToolButton* m_errorRawToggle = nullptr;

    // Private Process for this panel instance
    QProcess* m_process = nullptr;
    QProcess* m_modelFetchProcess = nullptr;
    QString m_modelFetchStdErr;
    QString m_responseBuffer;
    QString m_thinkingBuffer;
    QString m_errorBuffer;
    QString m_leftover;
    QString m_lastGeneratedCode;
    QString m_lastErrorTitle;
    QString m_lastErrorDetails;
    bool m_isWorking = false;
    QString m_mode = "schematic";
    QString m_projectFilePath;
    QString m_currentChatTitle;
    QString m_lastSubmittedPrompt;
    qint64 m_lastSubmitEpochMs = 0;
    
    QPushButton* m_statusButton;
    
    QList<QVariantMap> m_history;
    QList<ChatMessage> m_chatMessages;
    std::function<QString()> m_contextProvider;

    void refreshModelList();
    void showErrorBanner(const QString& summaryText, const QString& detailsText = QString());
    void hideErrorBanner();
    void showErrorDialog(const QString& title, const QString& detailsText);
    void reportError(const QString& title, const QString& detailsText, bool openDialog);
    void appendErrorHistory(const QString& title, const QString& detailsText);
    void ensureErrorDialog();
    void populateErrorDialogHistory();
    void selectErrorHistoryRow(int row);
    void showToolCallBanner(const QString& actionText = QString());
    void hideToolCallBanner();
    void appendChatMessage(const ChatMessage& message);
    QString chatMessageToHtml(const ChatMessage& message) const;
    void renderChatMessage(const ChatMessage& message);
    void resizeChatCards();
    void rerenderChatFromModel();
    void appendUserMessageCard(const QString& text, const QString& headerHtml = QString());
    void appendModelMarkdownCard(const QString& markdownText);
    void appendSystemNote(const QString& html);
    void scrollChatToBottom();
    void beginAssistantRunUi();
    void finishAssistantRunUi(int exitCode);
    void handleActionTag(const QString& actionText);
    void handleSuggestionTag(const QString& suggestionText);
    void appendSnippetActionButton(const QString& snippetJson);
    void appendNetlistActionButton(const QString& netlistText);
    void processAgentStdoutChunk(const QString& chunkText);
    void parseAndExecuteCommandModeInput(const QString& input);
    void updateSendEnabled();
    void clearSuggestionButtons();
    void addSuggestionButton(const QString& label, const QString& command);
    void triggerSuggestionCommand(const QString& command);

    QSet<QString> m_suggestionKeys;
    QPoint m_dragStartPosition;
    QString gatherInstructions() const;
    QString m_pressedAnchor;
};

#endif // GEMINI_PANEL_H
