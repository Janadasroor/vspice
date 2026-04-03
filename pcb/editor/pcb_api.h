#ifndef PCB_API_H
#define PCB_API_H

#include <QObject>
#include <QGraphicsScene>
#include <QUndoStack>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QVariant>
#include "../items/pcb_item.h"

/**
 * @brief High-level API for controlling the PCB editor.
 */
class PCBAPI : public QObject {
    Q_OBJECT
public:
    explicit PCBAPI(QGraphicsScene* scene, QUndoStack* undoStack = nullptr, QObject* parent = nullptr);
    void setScene(QGraphicsScene* scene) { m_scene = scene; }

    /**
     * @brief Execute a batch of commands from a JSON array.
     */
    int executeBatch(const QJsonArray& commands);

    /**
     * @brief Execute a single command from a JSON object.
     */
    bool executeCommand(const QJsonObject& command);

    // --- Component Operations ---

    bool addComponent(const QString& footprint, const QPointF& pos, const QString& reference = "", const QJsonObject& properties = {});
    bool removeComponent(const QString& reference);
    bool setProperty(const QString& reference, const QString& name, const QVariant& value);

    // --- Routing Operations ---

    bool addTrace(const QList<QPointF>& points, double width, int layer);
    bool addVia(const QPointF& pos, int fromLayer, int toLayer);

    // --- Analysis Operations ---

    QJsonArray runDRC();

    // --- File Operations ---

    bool load(const QString& path);
    bool save(const QString& path);

    // --- Utility ---
    
    PCBItem* findByReference(const QString& reference) const;

private:
    void pushCommand(class QUndoCommand* cmd);

    QGraphicsScene* m_scene;
    QUndoStack* m_undoStack;
};

#endif // PCB_API_H
