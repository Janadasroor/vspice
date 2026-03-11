#ifndef GEMINI_PANEL_H
#define GEMINI_PANEL_H

#include <QWidget>
#include <QTextBrowser>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QGraphicsScene>
#include <QProcess>
#include <QVariantMap>
#include <functional>
#include <QUrl>
#include <QLabel>

class QTextEdit;
class QTimer;

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

public slots:
    void clearHistory();

signals:
    void fluxScriptGenerated(const QString& code);
    void symbolJsonGenerated(const QString& json);
    void pythonScriptGenerated(const QString& code);
    void suggestionTriggered(const QString& command);
    void itemsHighlighted(const QStringList& references);
    void snippetGenerated(const QString& jsonSnippet);

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

private:
    QGraphicsScene* m_scene;
    class NetManager* m_netManager = nullptr;
    class QUndoStack* m_undoStack = nullptr;
    QTextBrowser* m_chatArea;
    class SyntaxHighlighter* m_highlighter = nullptr;
    QTextEdit* m_thinkingDisplay;
    QLineEdit* m_inputField;
    QPushButton* m_sendButton;
    QPushButton* m_clearButton;
    QPushButton* m_saveKeyButton = nullptr;
    QPushButton* m_copyButton;
    QPushButton* m_historyMenuButton = nullptr;
    QPushButton* m_stopButton;
    QPushButton* m_thinkingToggleButton;
    QCheckBox* m_includeContextCheck;
    QCheckBox* m_includeScreenshotCheck;
    class QComboBox* m_modelCombo = nullptr;
    QPushButton* m_refreshModelsButton = nullptr;

    // Pulse animation
    QTimer* m_thinkingPulseTimer;
    int m_pulseStep = 0;

    // API Key UI
    QWidget* m_apiKeyContainer;
    QLineEdit* m_apiKeyField;
    QWidget* m_errorBanner = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_errorDismissButton = nullptr;

    // Private Process for this panel instance
    QProcess* m_process = nullptr;
    QProcess* m_modelFetchProcess = nullptr;
    QString m_modelFetchStdErr;
    QString m_responseBuffer;
    QString m_thinkingBuffer;
    QString m_errorBuffer;
    QString m_leftover;
    QString m_lastGeneratedCode;
    int m_responseStartPos = 0;
    bool m_isWorking = false;
    QString m_mode = "schematic";
    QString m_projectFilePath;
    QString m_currentChatTitle;
    
    QPushButton* m_statusButton;
    
    QList<QVariantMap> m_history;
    std::function<QString()> m_contextProvider;

    void refreshModelList();
    void showErrorBanner(const QString& errorText);
    void hideErrorBanner();
};

#endif // GEMINI_PANEL_H
