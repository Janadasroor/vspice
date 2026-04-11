#pragma once

#ifndef VIOSPICE_HAS_QT_WEBSOCKETS
#if __has_include(<QtWebSockets/QWebSocketServer>)
#define VIOSPICE_HAS_QT_WEBSOCKETS 1
#else
#define VIOSPICE_HAS_QT_WEBSOCKETS 0
#endif
#endif

#if VIOSPICE_HAS_QT_WEBSOCKETS

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QList>
#include <QMutex>
#include <QWidget>

/**
 * Per-editor window state exposed to WebSocket clients.
 */
struct EditorWindow {
    QString type;           // schematic | pcb | symbol | footprint | netlist | project | none
    QString filePath;
    QString fileName;       // just the basename
    bool unsaved = false;
    QString windowTitle;
};

/**
 * App state snapshot exposed to WebSocket clients.
 */
struct AppState {
    QString activeWindow;       // schematic | pcb | project | netlist | symbol | none
    QString currentFile;
    QString fileType;
    QStringList openTabs;
    QStringList selectedComponents;
    int cursorX = 0;
    int cursorY = 0;
    bool simulationRunning = false;
    QString simLastFile;
    QString simLastAnalysis;
    QString simLastStatus;
    qint64 simLastTimestamp = 0;
    bool unsavedChanges = false;
    QList<EditorWindow> openEditors;  // all open editor windows
};

struct ClientInfo {
    QString name;
    QString address;
};

/**
 * WebSocket JSON-RPC 2.0 server for VioSpice state bridge.
 *
 * Listens on ws://127.0.0.1:18420 by default.
 * Uses Qt's QWebSocketServer for proper WebSocket protocol handling.
 *
 * JSON-RPC 2.0 Protocol:
 *   - state.get              → returns current app state
 *   - events.subscribe       → start receiving push events
 *   - events.unsubscribe     → stop receiving push events
 *   - command.execute        → trigger an action (open file, run sim, etc.)
 */
class WsServer : public QObject
{
    Q_OBJECT
public:
    explicit WsServer(quint16 port = 18420, QObject* parent = nullptr);
    ~WsServer();

    void start();
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }

    /** Global singleton accessor. Returns nullptr if not started. */
    static WsServer* instance();
    static void setInstance(WsServer* s);

    /** Update the current app state (call from editors when state changes). */
    void setState(const AppState& state);

    /** Broadcast a state-change event to all subscribed clients. */
    void broadcastEvent(const QString& eventName, const QJsonObject& data);

    /** Get list of currently connected clients. */
    QList<ClientInfo> connectedClients() const;

Q_SIGNALS:
    void clientConnected();
    void clientDisconnected();

    /**
     * Signal emitted when a remote client (VioCode) updates a file's content.
     * Editors can connect to this to reload their UI.
     */
    void remoteFileUpdated(const QString& filePath, const QString& content);

private Q_SLOTS:
    void onNewConnection();
    void onTextMessageReceived(const QString& message);
    void onSocketDisconnected();

private:
    struct WsClient {
        QWebSocket* socket;
        QString name;
        bool subscribed = false;
    };

    // JSON-RPC dispatch
    QJsonObject handleRequest(const QJsonObject& request);
    QJsonObject handleMethod(const QString& method, const QJsonObject& params);

    // Method implementations
    QJsonObject stateGet();
    void eventsSubscribe(WsClient* client);
    void eventsUnsubscribe(WsClient* client);
    QJsonObject commandExecute(const QJsonObject& params);

    // Helpers
    static QJsonObject makeResponse(int id, const QJsonValue& result);
    static QJsonObject makeError(int id, int code, const QString& message);
    static QJsonObject makeNotification(const QString& method, const QJsonObject& params);

    void sendToClient(WsClient* client, const QJsonObject& obj);

    QWebSocketServer* m_server;
    QList<WsClient*> m_clients;
    QList<WsClient*> m_subscribers;   // clients subscribed to events
    QMutex m_mutex;

    quint16 m_port;
    AppState m_state;                    // current app state

    static WsServer* s_instance;
};

#endif // VIOSPICE_HAS_QT_WEBSOCKETS
