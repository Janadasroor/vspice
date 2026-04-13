#ifndef SCHEMATIC_COMMANDS_H
#define SCHEMATIC_COMMANDS_H

#include <QUndoCommand>
#include <QGraphicsScene>
#include <QPointF>
#include <QJsonObject>

class SchematicItem;

/**
 * @brief Base class for schematic undo commands
 */
class SchematicCommand : public QUndoCommand {
public:
    SchematicCommand(QGraphicsScene* scene, const QString& text, QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent), m_scene(scene) {}
    
protected:
    QGraphicsScene* m_scene;
};

/**
 * @brief Command for adding an item to the schematic
 */
class AddItemCommand : public SchematicCommand {
public:
    AddItemCommand(QGraphicsScene* scene, SchematicItem* item, QUndoCommand* parent = nullptr);
    ~AddItemCommand();
    
    void undo() override;
    void redo() override;

private:
    SchematicItem* m_item;
    bool m_ownsItem;  // True when item is not in scene (after undo)
};

/**
 * @brief Command for removing items from the schematic
 */
class RemoveItemCommand : public SchematicCommand {
public:
    RemoveItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, QUndoCommand* parent = nullptr);
    ~RemoveItemCommand();
    
    void undo() override;
    void redo() override;

private:
    QList<SchematicItem*> m_items;
    QList<QJsonObject> m_itemData;  // Serialized item data for restoration
    bool m_ownsItems;  // True when items are not in scene (after redo)
};

/**
 * @brief Command for moving items
 */
class MoveItemCommand : public SchematicCommand {
public:
    MoveItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, 
                    const QList<QPointF>& oldPositions, const QList<QPointF>& newPositions,
                    QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;
    
    // Merge consecutive moves of the same items
    int id() const override { return 1001; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QList<SchematicItem*> m_items;
    QList<QPointF> m_oldPositions;
    QList<QPointF> m_newPositions;

    struct WireRubberBand {
        class WireItem* wire;
        int pointIndex;
        QPointF originalPoint;
    };
    QList<WireRubberBand> m_rubberBands;
};

/**
 * @brief Command for rotating items
 */
class RotateItemCommand : public SchematicCommand {
public:
    RotateItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items,
                      qreal angleDelta, QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;

private:
    QList<SchematicItem*> m_items;
    QList<qreal> m_oldRotations;
    qreal m_angleDelta;
};

/**
 * @brief Command for changing item properties
 */
class ChangePropertyCommand : public SchematicCommand {
public:
    ChangePropertyCommand(QGraphicsScene* scene, SchematicItem* item,
                          const QString& propertyName,
                          const QVariant& oldValue, const QVariant& newValue,
                          const QString& basePath = QString(),
                          QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;
    
    // Merge consecutive property changes
    int id() const override { return 1002; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    SchematicItem* m_item;
    QString m_propertyName;
    QVariant m_oldValue;
    QVariant m_newValue;
    QString m_basePath;
};

/**
 * @brief Command for changing multiple properties at once using JSON
 */
class BulkChangePropertyCommand : public SchematicCommand {
public:
    BulkChangePropertyCommand(QGraphicsScene* scene, SchematicItem* item,
                             const QJsonObject& newState, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    SchematicItem* m_item;
    QJsonObject m_oldState;
    QJsonObject m_newState;
};

/**
 * @brief Command for updating wire points (rubber banding)
 */
class UpdateWirePointsCommand : public SchematicCommand {
public:
    UpdateWirePointsCommand(QGraphicsScene* scene, class WireItem* wire,
                            const QList<QPointF>& oldPoints, const QList<QPointF>& newPoints,
                            QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;

private:
    class WireItem* m_wire;
    QList<QPointF> m_oldPoints;
    QList<QPointF> m_newPoints;
};

/**
 * @brief Command for flipping items
 */
class FlipItemCommand : public SchematicCommand {
public:
    FlipItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, bool vertical = false, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QList<SchematicItem*> m_items;
    bool m_vertical;
};

/**
 * @brief Command for locking/unlocking items
 */
class LockItemCommand : public SchematicCommand {
public:
    LockItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, bool locked, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QList<SchematicItem*> m_items;
    bool m_locked;
};

/**
 * @brief Command for changing Z-Value (Bring to Front / Send to Back)
 */
class ZValueItemCommand : public SchematicCommand {
public:
    ZValueItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, bool bringToFront, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QList<SchematicItem*> m_items;
    QList<qreal> m_oldZValues;
    QList<qreal> m_newZValues;
    bool m_bringToFront;
};

/**
 * @brief Command for aligning items
 */
class AlignItemCommand : public SchematicCommand {
public:
    enum Alignment { 
        AlignLeft, AlignRight, AlignTop, AlignBottom, 
        AlignCenterX, AlignCenterY,
        DistributeH, DistributeV
    };
    
    AlignItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, Alignment alignment, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QList<SchematicItem*> m_items;
    QList<QPointF> m_oldPositions;
    QList<QPointF> m_newPositions;
};

/**
 * @brief Command for bulk schematic annotation
 */
class SchematicAnnotateCommand : public SchematicCommand {
public:
    SchematicAnnotateCommand(QGraphicsScene* scene, const QMap<SchematicItem*, QString>& oldRefs, 
                             const QMap<SchematicItem*, QString>& newRefs, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QMap<SchematicItem*, QString> m_oldRefs;
    QMap<SchematicItem*, QString> m_newRefs;
};

/**
 * @brief Command for switching a pin's functional mode
 */
class SwitchPinFunctionCommand : public SchematicCommand {
public:
    SwitchPinFunctionCommand(QGraphicsScene* scene, class GenericComponentItem* item, int pinNumber, int newMode, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    class GenericComponentItem* m_item;
    int m_pinNumber;
    int m_oldMode;
    int m_newMode;
};

#include "../items/oscilloscope_item.h"

/**
 * @brief Command for changing oscilloscope configuration
 */
class ChangeOscilloscopeConfigCommand : public SchematicCommand {
public:
    ChangeOscilloscopeConfigCommand(OscilloscopeItem* item, const OscilloscopeItem::Config& oldCfg, const OscilloscopeItem::Config& newCfg, QUndoCommand* parent = nullptr)
        : SchematicCommand(nullptr, "Change Oscilloscope Settings", parent), m_item(item), m_oldCfg(oldCfg), m_newCfg(newCfg) {}
    
    void undo() override { m_item->setConfig(m_oldCfg); }
    void redo() override { m_item->setConfig(m_newCfg); }

private:
    OscilloscopeItem* m_item;
    OscilloscopeItem::Config m_oldCfg;
    OscilloscopeItem::Config m_newCfg;
};

QUndoCommand* createItemTransformCommand(QGraphicsScene* scene,
                                         const QList<SchematicItem*>& items,
                                         SchematicItem::TransformAction action,
                                         QUndoCommand* parent = nullptr);

#endif // SCHEMATIC_COMMANDS_H
