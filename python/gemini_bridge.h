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
    Q_PROPERTY(QString currentMode READ currentMode WRITE setCurrentMode NOTIFY currentModeChanged)
    Q_PROPERTY(bool isWorking READ isWorking NOTIFY isWorkingChanged)
    Q_PROPERTY(QString thinkingText READ thinkingText NOTIFY thinkingTextChanged)
    Q_PROPERTY(QString conversationTitle READ conversationTitle NOTIFY conversationTitleChanged)
    
    // Theme properties for QML
    Q_PROPERTY(QString textColor READ textColor NOTIFY themeChanged)
    Q_PROPERTY(QString secondaryColor READ secondaryColor NOTIFY themeChanged)
    Q_PROPERTY(QString accentColor READ accentColor NOTIFY themeChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor NOTIFY themeChanged)
    Q_PROPERTY(QString glassBackground READ glassBackground NOTIFY themeChanged)

public:
    explicit GeminiBridge(QObject* parent = nullptr);

    QVariantList messages() const { return m_messages; }
    QStringList availableModels() const { return m_availableModels; }
    QString currentModel() const { return m_currentModel; }
    QString currentMode() const { return m_currentMode; }
    bool isWorking() const { return m_isWorking; }
    QString thinkingText() const { return m_thinkingText; }
    QString conversationTitle() const { return m_conversationTitle; }

    // Theme getters
    QString textColor() const;
    QString secondaryColor() const;
    QString accentColor() const;
    QString backgroundColor() const;
    QString glassBackground() const;

    void setCurrentModel(const QString& model);
    void setCurrentMode(const QString& mode);

    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void stopRun();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE void closePanel();

signals:
    void messagesChanged();
    void availableModelsChanged();
    void currentModelChanged();
    void currentModeChanged();
    void isWorkingChanged();
    void thinkingTextChanged();
    void conversationTitleChanged();
    void themeChanged();
    
    // Internal signals to trigger logic in GeminiPanel
    void sendMessageRequested(const QString& text);
    void stopRequested();
    void refreshModelsRequested();
    void clearHistoryRequested();
    void closeRequested();

public slots:
    void updateMessages(const QVariantList& msgs);
    void setWorking(bool working, const QString& thinking = "");
    void updateStatus(const QString& status);
    void updateAvailableModels(const QStringList& models);
    void updateTitle(const QString& title);
    void notifyThemeChanged() { emit themeChanged(); }

private:
    QVariantList m_messages;
    QStringList m_availableModels;
    QString m_currentModel = "gemini-2.0-flash";
    QString m_currentMode = "ask";
    bool m_isWorking = false;
    QString m_thinkingText;
    QString m_conversationTitle = "VIORA AI";
};

#endif // GEMINI_BRIDGE_H
