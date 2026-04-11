#include "ws_server.h"

#if VIOSPICE_HAS_QT_WEBSOCKETS

#include <QJsonArray>
#include <QFileInfo>
#include <QApplication>
#include <QDebug>
#include <QTimer>

WsServer* WsServer::s_instance = nullptr;

WsServer* WsServer::instance() { return s_instance; }
void WsServer::setInstance(WsServer* s) { s_instance = s; }

WsServer::WsServer(quint16 port, QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer("VioSpice State Bridge", QWebSocketServer::NonSecureMode, this))
    , m_port(port)
{
    connect(m_server, &QWebSocketServer::newConnection, this, &WsServer::onNewConnection);
}

WsServer::~WsServer()
{
    stop();
}

void WsServer::start()
{
    if (!m_server->listen(QHostAddress::Any, m_port)) {
        qWarning() << "[WsServer] Failed to listen on port" << m_port << ":" << m_server->errorString();
        return;
    }
    qDebug() << "[WsServer] Listening on port" << m_port;
}

void WsServer::stop()
{
    m_server->close();
    for (auto* c : m_clients) {
        c->socket->close();
        delete c;
    }
    m_clients.clear();
    m_subscribers.clear();
}

bool WsServer::isRunning() const
{
    return m_server->isListening();
}

void WsServer::setState(const AppState& state)
{
    QMutexLocker lock(&m_mutex);
    m_state = state;
}

QList<ClientInfo> WsServer::connectedClients() const
{
    QMutexLocker lock(const_cast<QMutex*>(&m_mutex));
    QList<ClientInfo> list;
    for (auto* c : m_clients) {
        if (c && c->socket) {
            list.append({c->name.isEmpty() ? "Unknown Agent" : c->name, c->socket->peerAddress().toString()});
        }
    }
    return list;
}

// ── Connection handling ────────────────────────────────────────────

void WsServer::onNewConnection()
{
    QWebSocket* socket = m_server->nextPendingConnection();
    if (!socket) return;

    auto* client = new WsClient();
    client->socket = socket;

    connect(socket, &QWebSocket::textMessageReceived, this, &WsServer::onTextMessageReceived);
    connect(socket, &QWebSocket::disconnected, this, &WsServer::onSocketDisconnected);

    QMutexLocker lock(&m_mutex);
    m_clients.append(client);

    qDebug() << "[WsServer] WebSocket client connected. Total:" << m_clients.size();
    emit clientConnected();
}

void WsServer::onTextMessageReceived(const QString& message)
{
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) return;

    WsClient* client = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        for (auto* c : m_clients) {
            if (c->socket == socket) {
                client = c;
                break;
            }
        }
    }

    // Parse JSON-RPC
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        sendToClient(client, makeError(-1, -32700, "Parse error: " + err.errorString()));
        return;
    }

    QJsonObject request = doc.object();
    QJsonObject response = handleRequest(request);
    if (!response.isEmpty() && client) {
        // Send response
        socket->sendTextMessage(QJsonDocument(response).toJson(QJsonDocument::Compact));
    }
}

void WsServer::onSocketDisconnected()
{
    auto* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket) return;

    QMutexLocker lock(&m_mutex);
    for (int i = m_clients.size() - 1; i >= 0; --i) {
        if (m_clients[i]->socket == socket) {
            m_subscribers.removeAll(m_clients[i]);
            delete m_clients[i];
            m_clients.removeAt(i);
        }
    }
    qDebug() << "[WsServer] Client disconnected. Total:" << m_clients.size();
    emit clientDisconnected();
}

// ── JSON-RPC dispatch ──────────────────────────────────────────────

QJsonObject WsServer::handleRequest(const QJsonObject& request)
{
    QString jsonrpc = request["jsonrpc"].toString();
    if (jsonrpc != "2.0") {
        int id = request["id"].isDouble() ? request["id"].toInt() : -1;
        return makeError(id, -32600, "Invalid Request: jsonrpc must be 2.0");
    }

    QString method = request["method"].toString();
    if (method.isEmpty()) {
        int id = request["id"].isDouble() ? request["id"].toInt() : -1;
        return makeError(id, -32600, "Invalid Request: method is required");
    }

    QJsonValue idVal = request["id"];
    bool hasId = !idVal.isNull();

    QJsonObject params;
    if (request.contains("params") && request["params"].isObject()) {
        params = request["params"].toObject();
    }

    QJsonObject result = handleMethod(method, params);

    if (hasId && !result.isEmpty()) {
        int id = idVal.isDouble() ? idVal.toInt() : 0;
        return makeResponse(id, result);
    }

    return {};
}

QJsonObject WsServer::handleMethod(const QString& method, const QJsonObject& params)
{
    QMutexLocker lock(&m_mutex);
    auto* sender = qobject_cast<QWebSocket*>(QObject::sender());
    WsClient* client = nullptr;
    for (auto* c : m_clients) { if (c->socket == sender) { client = c; break; } }

    if (method == "state.get") {
        return stateGet();
    } else if (method == "client.identify") {
        QString name = params["name"].toString();
        if (client) {
            client->name = name;
            qDebug() << "[WsServer] Client identified as:" << name;
            emit clientConnected(); // Re-emit so UI can update with name
        }
        return {{"status", "identified"}, {"name", name}};
    } else if (method == "events.subscribe") {
        eventsSubscribe(client);
        return {{"status", "subscribed"}};
    } else if (method == "events.unsubscribe") {
        eventsUnsubscribe(client);
        return {{"status", "unsubscribed"}};
    } else if (method == "command.execute") {
        return commandExecute(params);
    }

    return makeError(0, -32601, "Method not found: " + method);
}

// ── Method implementations ─────────────────────────────────────────

QJsonObject WsServer::stateGet()
{
    QJsonObject state;
    state["protocolVersion"] = 1;
    state["timestamp"] = QDateTime::currentMSecsSinceEpoch() / 1000;
    state["activeWindow"] = m_state.activeWindow;
    state["currentFile"] = m_state.currentFile;
    state["fileType"] = m_state.fileType;
    state["cursorPosition"] = QJsonObject{{"x", m_state.cursorX}, {"y", m_state.cursorY}};

    QJsonArray tabs;
    for (const QString& t : m_state.openTabs) tabs.append(t);
    state["openTabs"] = tabs;

    QJsonArray comps;
    for (const QString& c : m_state.selectedComponents) comps.append(c);
    state["selectedComponents"] = comps;

    state["simulationRunning"] = m_state.simulationRunning;
    state["lastSimulation"] = QJsonObject{
        {"file", m_state.simLastFile},
        {"analysis", m_state.simLastAnalysis},
        {"status", m_state.simLastStatus},
        {"timestamp", m_state.simLastTimestamp}
    };
    state["unsavedChanges"] = m_state.unsavedChanges;

    // Collect all open editor windows from top-level widgets
    QJsonArray editors;
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget* w : widgets) {
        if (!w || !w->isVisible()) continue;
        EditorWindow ew;
        QString objName = w->objectName();

        if (objName == "SchematicEditor") {
            ew.type = "schematic";
            ew.filePath = w->property("currentFilePath").toString();
            ew.unsaved = w->property("unsavedChanges").toBool();
        } else if (objName == "PCBEditor") {
            ew.type = "pcb";
            ew.filePath = w->property("currentFilePath").toString();
            ew.unsaved = w->property("unsavedChanges").toBool();
        } else if (objName == "SymbolEditor") {
            ew.type = "symbol";
            ew.filePath = w->property("currentFilePath").toString();
            ew.unsaved = w->property("unsavedChanges").toBool();
        } else if (objName == "ProjectManager") {
            ew.type = "project";
        } else if (objName == "NetlistEditor") {
            ew.type = "netlist";
        }

        if (ew.type != "none") {
            QFileInfo fi(ew.filePath);
            ew.fileName = fi.fileName();
            ew.windowTitle = w->windowTitle();

            QJsonObject obj;
            obj["type"] = ew.type;
            obj["filePath"] = ew.filePath;
            obj["fileName"] = ew.fileName;
            obj["unsaved"] = ew.unsaved;
            obj["windowTitle"] = ew.windowTitle;
            editors.append(obj);
        }
    }
    state["openEditors"] = editors;

    // Also include a flat list of all active file paths for quick AI access
    QJsonArray activeFiles;
    for (const QJsonValue& ed : editors) {
        QJsonObject obj = ed.toObject();
        if (obj.contains("filePath") && !obj["filePath"].toString().isEmpty()) {
            activeFiles.append(obj["filePath"].toString());
        }
    }
    state["activeFiles"] = activeFiles;

    return state;
}

void WsServer::eventsSubscribe(WsClient* client)
{
    if (!client || m_subscribers.contains(client)) return;
    m_subscribers.append(client);
    qDebug() << "[WsServer] Client subscribed to events. Total:" << m_subscribers.size();
}

void WsServer::eventsUnsubscribe(WsClient* client)
{
    if (!client) return;
    m_subscribers.removeAll(client);
    qDebug() << "[WsServer] Client unsubscribed from events. Total:" << m_subscribers.size();
}

QJsonObject WsServer::commandExecute(const QJsonObject& params)
{
    QString command = params["command"].toString();
    QJsonObject args = params["args"].toObject();

    if (command == "file.open") {
        QString path = args["path"].toString();
        if (path.isEmpty()) return makeError(0, -32602, "path is required");
        return {{"status", "queued"}, {"path", path}};
    } else if (command == "simulation.run") {
        QString analysis = args.value("analysis").toString("tran");
        QString file = args["file"].toString();
        return {{"status", "queued"}, {"analysis", analysis}, {"file", file}};
    } else if (command == "file.getContent") {
        QString type = args["type"].toString();
        QString filePath;

        if (args.contains("path") && !args["path"].toString().isEmpty()) {
            filePath = args["path"].toString();
        } else {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget* w : widgets) {
                if (!w || !w->isVisible()) continue;
                QString objName = w->objectName();
                bool match = (type == "schematic" && objName == "SchematicEditor") ||
                             (type == "pcb" && objName == "PCBEditor") ||
                             (type == "symbol" && objName == "SymbolEditor") ||
                             (type.isEmpty() && (objName == "SchematicEditor" || objName == "PCBEditor" || objName == "SymbolEditor"));
                if (match) {
                    filePath = w->property("currentFilePath").toString();
                    break;
                }
            }
        }

        if (filePath.isEmpty()) {
            return makeError(0, -32602, "No active " + (type.isEmpty() ? "editor" : type) + " file found");
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return makeError(0, -32603, "Cannot read file: " + filePath);
        }

        QString content = QString::fromUtf8(file.readAll());
        file.close();

        QFileInfo fi(filePath);
        return {
            {"path", filePath},
            {"fileName", fi.fileName()},
            {"size", content.size()},
            {"content", content}
        };
    } else if (command == "file.reload") {
        emit remoteFileUpdated("", "");
        return {{"status", "ok"}, {"message", "All editors reloaded from disk"}};
    } else if (command == "file.setContent") {
        QString filePath = args["path"].toString();
        QString content = args["content"].toString();

        if (filePath.isEmpty() || content.isEmpty()) {
            return makeError(0, -32602, "path and content are required");
        }

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return makeError(0, -32603, "Cannot write file: " + filePath);
        }

        file.write(content.toUtf8());
        file.close();

        QFileInfo fi(filePath);
        emit remoteFileUpdated(filePath, content);
        broadcastEvent("file.updated", {
            {"path", filePath},
            {"fileName", fi.fileName()},
            {"size", content.size()}
        });

        return {
            {"status", "ok"},
            {"path", filePath},
            {"fileName", fi.fileName()},
            {"size", content.size()}
        };
    }

    return makeError(0, -32601, "Unknown command: " + command);
}

// ── Broadcast ──────────────────────────────────────────────────────

void WsServer::broadcastEvent(const QString& eventName, const QJsonObject& data)
{
    QMutexLocker lock(&m_mutex);
    QJsonObject notification = makeNotification("events.push", {
        {"event", eventName},
        {"data", data}
    });
    QString json = QJsonDocument(notification).toJson(QJsonDocument::Compact);

    for (auto* sub : m_subscribers) {
        if (sub && sub->socket && sub->socket->state() == QAbstractSocket::ConnectedState) {
            sub->socket->sendTextMessage(json);
        }
    }
}

// ── Helpers ────────────────────────────────────────────────────────

QJsonObject WsServer::makeResponse(int id, const QJsonValue& result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject WsServer::makeError(int id, int code, const QString& message)
{
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", QJsonObject{{"code", code}, {"message", message}}}
    };
}

QJsonObject WsServer::makeNotification(const QString& method, const QJsonObject& params)
{
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}

void WsServer::sendToClient(WsClient* client, const QJsonObject& obj)
{
    if (client && client->socket && client->socket->state() == QAbstractSocket::ConnectedState) {
        client->socket->sendTextMessage(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}

#endif // VIOSPICE_HAS_QT_WEBSOCKETS
