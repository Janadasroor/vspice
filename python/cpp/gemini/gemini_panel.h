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

class QQuickWidget;
class GeminiBridge;

/**
 * @brief GeminiPanel now hosts a QML interface via QQuickWidget.
 */
class GeminiPanel : public QWidget {
    Q_OBJECT
public:
    explicit GeminiPanel(QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);
    virtual ~GeminiPanel();

    void setScene(QGraphicsScene* scene) { m_scene = scene; }
    void setNetManager(class NetManager* netManager) { m_netManager = netManager; }
    void setMode(const QString& mode);
    void setProjectFilePath(const QString& path);
    void setUndoStack(class QUndoStack* stack);
    void saveHistory();
    void loadHistory();
    void loadHistoryFromFile(const QString& filePath);
    QString mode() const { return m_mode; }

    void askPrompt(const QString& prompt, bool includeContext = true, const QString& imageBase64 = QString());
    void askSmartProbe(const QString& prompt,
                      std::function<void(const QString& chunk)> onChunk,
                      std::function<void()> onDone);
    void setContextProvider(std::function<QString()> provider) { m_contextProvider = provider; }

public Q_SLOTS:
    void clearHistory();

Q_SIGNALS:
    void fluxScriptGenerated(const QString& code);
    void symbolJsonGenerated(const QString& json);
    void pythonScriptGenerated(const QString& code);
    void suggestionTriggered(const QString& command);
    void itemsHighlighted(const QStringList& references);
    void snippetGenerated(const QString& jsonSnippet);
    void netlistGenerated(const QString& netlistText);
    void runSimulationRequested();
    void runERCRequested();
    void plotSignalRequested(const QString& signalName);
    void zoomFitRequested();
    void togglePanelRequested(const QString& panelName);
    void importSubcircuitRequested(const QString& filePath);
    void rewindRequested();
    void checkpointRequested();

private Q_SLOTS:
    void onProcessReadyRead();
    void onProcessFinished(int exitCode);
    void onRefreshModelsClicked();
    void onModelFetchFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onCustomInstructionsClicked();
    void onCopyPromptClicked();

    // Bridge Slots (connected to QML signals)
    void onBridgeSendMessage(const QString& text, const QString& imageBase64 = QString());
    void onBridgeStopRequest();
    void onBridgeRefreshModelsRequest();
    void onBridgeCloseRequest();
    void onBridgeShowHistoryRequest();
    void onUndoToPoint(int messageIndex);
    void onExportRequested();

private:
    QGraphicsScene* m_scene;
    class NetManager* m_netManager = nullptr;
    class QUndoStack* m_undoStack = nullptr;
    
    // QML Integration
    QQuickWidget* m_quickWidget = nullptr;
    GeminiBridge* m_bridge = nullptr;

    // Internal Logic Timers
    QTimer* m_thinkingPulseTimer = nullptr;
    QTimer* m_syncTimer = nullptr;
    int m_pulseStep = 0;

    // Backend Process Mangement
    QProcess* m_process = nullptr;
    QProcess* m_probeProcess = nullptr;
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
    bool m_isDestroying = false;
    QString m_mode = "schematic";
    QString m_projectFilePath;
    QString m_currentChatTitle;
    QString m_lastSubmittedPrompt;
    qint64 m_lastSubmitEpochMs = 0;
    
    QList<QVariantMap> m_history;
    std::function<QString()> m_contextProvider;

    // Synchronization and Logic
    void syncHistoryToBridge();
    void refreshModelList();
    void reportError(const QString& title, const QString& detailsText, bool openDialog);
    void appendSystemNote(const QString& text);
    void appendSystemAction(const QString& title, const QString& details, const QString& icon = QString());
    
    void handleActionTag(const QString& actionText);
    void handleSuggestionTag(const QString& suggestionText);
    void processAgentStdoutChunk(const QString& chunkText);
    void parseAndExecuteCommandModeInput(const QString& input);

    // Error History and Dialogs
    struct ErrorRecord {
        QString timestamp;
        QString title;
        QString details;
    };
    QDialog* m_errorDialog = nullptr;
    QList<ErrorRecord> m_errorHistory;
    class QListWidget* m_errorHistoryList = nullptr;
    class QTextBrowser* m_errorSummaryView = nullptr;
    class QPlainTextEdit* m_errorRawView = nullptr;
    class QToolButton* m_errorRawToggle = nullptr;

    void ensureErrorDialog();
    void populateErrorDialogHistory();
    void selectErrorHistoryRow(int row);
    void appendErrorHistory(const QString& title, const QString& detailsText);
    void showErrorDialog(const QString& title, const QString& detailsText);
    void showErrorBanner(const QString& summaryText, const QString& detailsText = QString());
    void hideErrorBanner();

    QString gatherInstructions() const;
    QString gatherSchematicContext() const;
    QString gatherFileMentionsContext(const QString& text) const;
};

#endif // GEMINI_PANEL_H
