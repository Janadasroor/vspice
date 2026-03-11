#ifndef SCHEMATIC_API_H
#define SCHEMATIC_API_H

#include <QObject>
#include <QGraphicsScene>
#include <QUndoStack>
#include <QJsonObject>
#include <QJsonArray>
#include <QPointF>
#include <QVariant>
#include "../items/schematic_item.h"

/**
 * @brief High-level API for controlling the schematic editor.
 * 
 * This class provides a clean interface for both CLI and AI agents to manipulate
 * the schematic without interacting directly with QGraphicsScene or QUndoCommand.
 */
class SchematicAPI : public QObject {
    Q_OBJECT
public:
    explicit SchematicAPI(QGraphicsScene* scene, QUndoStack* undoStack = nullptr, QObject* parent = nullptr);
    void setScene(QGraphicsScene* scene) { m_scene = scene; }
    QGraphicsScene* scene() const { return m_scene; }
    QUndoStack* undoStack() const { return m_undoStack; }

    /**
     * @brief Execute a batch of commands from a JSON array.
     * Format: [{"cmd": "addComponent", "type": "Resistor", "x": 100, "y": 100, "properties": {"value": "10k"}}, ...]
     * @return Number of successfully executed commands.
     */
    int executeBatch(const QJsonArray& commands);

    /**
     * @brief Execute a single command from a JSON object.
     */
    bool executeCommand(const QJsonObject& command);

    // --- Component Operations ---

    /**
     * @brief Add a component to the schematic.
     * @param type The type of component (e.g., "Resistor", "IC", "GND")
     * @param pos Position in scene coordinates
     * @param reference Optional reference designator (e.g., "R1"). If empty, will be auto-assigned.
     * @param properties Optional JSON properties for the component
     * @return True if successful
     */
    bool addComponent(const QString& type, const QPointF& pos, const QString& reference = "", const QJsonObject& properties = {});

    /**
     * @brief Remove a component by its reference designator.
     */
    bool removeComponent(const QString& reference);

    /**
     * @brief Set a property on a component.
     */
    bool setProperty(const QString& reference, const QString& name, const QVariant& value);

    // --- Wiring Operations ---

    /**
     * @brief Add a wire with multiple segments.
     */
    bool addWire(const QList<QPointF>& points);

    /**
     * @brief Connect two pins together with a wire.
     * @param ref1 Reference of the first component
     * @param pin1 Name or index (1-based string) of the first pin
     * @param ref2 Reference of the second component
     * @param pin2 Name or index (1-based string) of the second pin
     * @return True if successful
     */
    bool connect(const QString& ref1, const QString& pin1, const QString& ref2, const QString& pin2);

    // --- Analysis Operations ---

    /**
     * @brief Run Electrical Rules Check.
     * @return JSON array of violations
     */
    QJsonArray runERC();

    /**
     * @brief Run reference annotator to fix '?' references.
     */
    bool annotate(bool resetAll = false);

    /**
     * @brief Get the netlist of the current schematic.
     */
    QJsonObject getNetlist();

    // --- File Operations ---

    bool load(const QString& path);
    bool save(const QString& path);

    // --- Utility ---
    
    /**
     * @brief Find a schematic item by its reference designator.
     */
    SchematicItem* findByReference(const QString& reference) const;

private:
    void pushCommand(class QUndoCommand* cmd);

    QGraphicsScene* m_scene;
    QUndoStack* m_undoStack;
};

#endif // SCHEMATIC_API_H
