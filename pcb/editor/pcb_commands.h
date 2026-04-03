#ifndef PCB_COMMANDS_H
#define PCB_COMMANDS_H

#include <QUndoCommand>
#include <QGraphicsScene>
#include <QPointF>
#include <QJsonObject>
#include <QList>

class PCBItem;

/**
 * @brief Base class for PCB undo commands
 */
class PCBCommand : public QUndoCommand {
public:
    PCBCommand(QGraphicsScene* scene, const QString& text, QUndoCommand* parent = nullptr)
        : QUndoCommand(text, parent), m_scene(scene) {}
    
protected:
    QGraphicsScene* m_scene;
};

/**
 * @brief Command for adding an item to the PCB
 */
class PCBAddItemCommand : public PCBCommand {
public:
    PCBAddItemCommand(QGraphicsScene* scene, PCBItem* item, QUndoCommand* parent = nullptr);
    ~PCBAddItemCommand();
    
    void undo() override;
    void redo() override;

private:
    PCBItem* m_item;
    bool m_ownsItem;
};

/**
 * @brief Command for adding multiple items to the PCB
 */
class PCBAddItemsCommand : public PCBCommand {
public:
    PCBAddItemsCommand(QGraphicsScene* scene, QList<PCBItem*> items, QUndoCommand* parent = nullptr);
    ~PCBAddItemsCommand();
    
    void undo() override;
    void redo() override;

private:
    QList<PCBItem*> m_items;
    bool m_ownsItems;
};

/**
 * @brief Command for removing items from the PCB
 */
class PCBRemoveItemCommand : public PCBCommand {
public:
    PCBRemoveItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, QUndoCommand* parent = nullptr);
    ~PCBRemoveItemCommand();
    
    void undo() override;
    void redo() override;

private:
    QList<PCBItem*> m_items;
    QList<QGraphicsItem*> m_parents;
    bool m_ownsItems;
};

/**
 * @brief Command for moving items on the PCB
 */
class PCBMoveItemCommand : public PCBCommand {
public:
    PCBMoveItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, 
                       const QList<QPointF>& oldPositions, const QList<QPointF>& newPositions,
                       QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;
    
    int id() const override { return 2001; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QList<PCBItem*> m_items;
    QList<QPointF> m_oldPositions;
    QList<QPointF> m_newPositions;
};

/**
 * @brief Command for rotating items on the PCB
 */
class PCBRotateItemCommand : public PCBCommand {
public:
    PCBRotateItemCommand(QGraphicsScene* scene, QList<PCBItem*> items,
                         qreal angleDelta, QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;

private:
    QList<PCBItem*> m_items;
    QList<qreal> m_oldRotations;
    qreal m_angleDelta;
};

/**
 * @brief Command for flipping items (changing layer)
 */
class PCBFlipItemCommand : public PCBCommand {
public:
    PCBFlipItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;

private:
    QList<PCBItem*> m_items;
    QList<int> m_oldLayers;
};

/**
 * @brief Command for locking/unlocking items
 */
class PCBLockItemCommand : public PCBCommand {
public:
    PCBLockItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, bool locked, QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;

private:
    QList<PCBItem*> m_items;
    bool m_locked;
};

/**
 * @brief Command for changing a property of an item
 */
class PCBPropertyCommand : public PCBCommand {
public:
    PCBPropertyCommand(QGraphicsScene* scene, PCBItem* item, const QString& propertyName, 
                       const QVariant& oldValue, const QVariant& newValue, QUndoCommand* parent = nullptr);
    
    void undo() override;
    void redo() override;

private:
    void applyValue(const QVariant& value);
    PCBItem* m_item;
    QString m_propertyName;
    QVariant m_oldValue;
    QVariant m_newValue;
};

/**
 * @brief Command for aligning PCB items
 */
class PCBAlignItemCommand : public PCBCommand {
public:
    enum Alignment { 
        AlignLeft, AlignRight, AlignTop, AlignBottom, 
        AlignCenterX, AlignCenterY,
        DistributeH, DistributeV
    };
    
    PCBAlignItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, Alignment alignment, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QList<PCBItem*> m_items;
    QList<QPointF> m_oldPositions;
    QList<QPointF> m_newPositions;
};

/**
 * @brief Command for bulk shoving of traces
 */
class PCBShoveTracesCommand : public PCBCommand {
public:
    PCBShoveTracesCommand(QGraphicsScene* scene, const QList<PCBItem*>& items,
                          const QList<QPair<QPointF, QPointF>>& oldGeoms,
                          const QList<QPair<QPointF, QPointF>>& newGeoms,
                          QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;
private:
    QList<PCBItem*> m_items;
    QList<QPair<QPointF, QPointF>> m_oldGeoms;
    QList<QPair<QPointF, QPointF>> m_newGeoms;
};

#endif // PCB_COMMANDS_H
