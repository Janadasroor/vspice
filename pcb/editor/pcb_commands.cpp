#include "pcb_commands.h"
#include "pcb_item.h"
#include "trace_item.h"
#include "pad_item.h"
#include "via_item.h"
#include "copper_pour_item.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include <QDebug>
#include <QVariant>

// === PCBAddItemCommand ===
PCBAddItemCommand::PCBAddItemCommand(QGraphicsScene* scene, PCBItem* item, QUndoCommand* parent)
    : PCBCommand(scene, QString("Add %1").arg(item->itemTypeName()), parent)
    , m_item(item)
    , m_ownsItem(false) {
}

PCBAddItemCommand::~PCBAddItemCommand() {
    if (m_ownsItem && m_item) {
        delete m_item;
    }
}

void PCBAddItemCommand::undo() {
    if (m_item && m_scene) {
        m_scene->removeItem(m_item);
        m_ownsItem = true;
        PCBRatsnestManager::instance().update();
    }
}

void PCBAddItemCommand::redo() {
    if (m_item && m_scene) {
        if (m_item->scene() != m_scene) {
            m_scene->addItem(m_item);
        }
        m_ownsItem = false;
        PCBRatsnestManager::instance().update();
    }
}

// === PCBAddItemsCommand ===
PCBAddItemsCommand::PCBAddItemsCommand(QGraphicsScene* scene, QList<PCBItem*> items, QUndoCommand* parent)
    : PCBCommand(scene, QString("Add %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_ownsItems(false) {
}

PCBAddItemsCommand::~PCBAddItemsCommand() {
    if (m_ownsItems) {
        qDeleteAll(m_items);
    }
}

void PCBAddItemsCommand::undo() {
    for (PCBItem* item : m_items) {
        if (item && m_scene) {
            m_scene->removeItem(item);
        }
    }
    m_ownsItems = true;
    PCBRatsnestManager::instance().update();
}

void PCBAddItemsCommand::redo() {
    for (PCBItem* item : m_items) {
        if (item && m_scene && item->scene() != m_scene) {
            m_scene->addItem(item);
        }
    }
    m_ownsItems = false;
    PCBRatsnestManager::instance().update();
}

// === PCBRemoveItemCommand ===
PCBRemoveItemCommand::PCBRemoveItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, QUndoCommand* parent)
    : PCBCommand(scene, QString("Delete %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_ownsItems(false) {
    for (PCBItem* item : items) {
        m_parents.append(item->parentItem());
    }
}

PCBRemoveItemCommand::~PCBRemoveItemCommand() {
    if (m_ownsItems) {
        qDeleteAll(m_items);
    }
}

void PCBRemoveItemCommand::undo() {
    for (int i = 0; i < m_items.size(); ++i) {
        PCBItem* item = m_items[i];
        QGraphicsItem* parent = m_parents[i];
        if (item && m_scene) {
            item->setParentItem(parent);
            m_scene->addItem(item);
        }
    }
    m_ownsItems = false;
    PCBRatsnestManager::instance().update();
}

void PCBRemoveItemCommand::redo() {
    for (PCBItem* item : m_items) {
        if (item && m_scene) {
            m_scene->removeItem(item);
        }
    }
    m_ownsItems = true;
    PCBRatsnestManager::instance().update();
}

// === PCBMoveItemCommand ===
PCBMoveItemCommand::PCBMoveItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, 
                                       const QList<QPointF>& oldPositions, const QList<QPointF>& newPositions,
                                       QUndoCommand* parent)
    : PCBCommand(scene, QString("Move %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_oldPositions(oldPositions)
    , m_newPositions(newPositions) {
}

void PCBMoveItemCommand::undo() {
    for (int i = 0; i < m_items.size() && i < m_oldPositions.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_oldPositions[i]);
            m_items[i]->update();
        }
    }
    PCBRatsnestManager::instance().update();
}

void PCBMoveItemCommand::redo() {
    for (int i = 0; i < m_items.size() && i < m_newPositions.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_newPositions[i]);
            m_items[i]->update();
        }
    }
    PCBRatsnestManager::instance().update();
}

bool PCBMoveItemCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != id()) return false;
    const PCBMoveItemCommand* moveCmd = static_cast<const PCBMoveItemCommand*>(other);
    if (moveCmd->m_items != m_items) return false;
    
    m_newPositions = moveCmd->m_newPositions;
    return true;
}

// === PCBRotateItemCommand ===
PCBRotateItemCommand::PCBRotateItemCommand(QGraphicsScene* scene, QList<PCBItem*> items,
                                           qreal angleDelta, QUndoCommand* parent)
    : PCBCommand(scene, QString("Rotate %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_angleDelta(angleDelta) {
    for (PCBItem* item : items) {
        m_oldRotations.append(item ? item->rotation() : 0);
    }
}

void PCBRotateItemCommand::undo() {
    for (int i = 0; i < m_items.size() && i < m_oldRotations.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setRotation(m_oldRotations[i]);
            m_items[i]->update();
        }
    }
}

void PCBRotateItemCommand::redo() {
    for (int i = 0; i < m_items.size() && i < m_oldRotations.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setRotation(m_oldRotations[i] + m_angleDelta);
            m_items[i]->update();
        }
    }
}

// === PCBFlipItemCommand ===
PCBFlipItemCommand::PCBFlipItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, QUndoCommand* parent)
    : PCBCommand(scene, QString("Flip %1 item(s)").arg(items.size()), parent)
    , m_items(items) {
    for (PCBItem* item : items) {
        m_oldLayers.append(item ? item->layer() : 0);
    }
}

void PCBFlipItemCommand::undo() {
    for (int i = 0; i < m_items.size() && i < m_oldLayers.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setLayer(m_oldLayers[i]);
            m_items[i]->update();
        }
    }
}

void PCBFlipItemCommand::redo() {
    for (int i = 0; i < m_items.size() && i < m_oldLayers.size(); ++i) {
        if (m_items[i]) {
            // Toggle layer between 0 and 1
            m_items[i]->setLayer(m_oldLayers[i] == 0 ? 1 : 0);
            m_items[i]->update();
        }
    }
}

// === PCBLockItemCommand ===
PCBLockItemCommand::PCBLockItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, bool locked, QUndoCommand* parent)
    : PCBCommand(scene, QString(locked ? "Lock %1 item(s)" : "Unlock %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_locked(locked) {
}

void PCBLockItemCommand::undo() {
    for (auto* item : m_items) {
        if (item) item->setLocked(!m_locked);
    }
}

void PCBLockItemCommand::redo() {
    for (auto* item : m_items) {
        if (item) item->setLocked(m_locked);
    }
}

// === PCBPropertyCommand ===
PCBPropertyCommand::PCBPropertyCommand(QGraphicsScene* scene, PCBItem* item, const QString& propertyName, 
                                       const QVariant& oldValue, const QVariant& newValue, QUndoCommand* parent)
    : PCBCommand(scene, QString("Change %1").arg(propertyName), parent)
    , m_item(item)
    , m_propertyName(propertyName)
    , m_oldValue(oldValue)
    , m_newValue(newValue) {
}

void PCBPropertyCommand::undo() {
    if (!m_item) return;
    applyValue(m_oldValue);
    m_item->update();
}

void PCBPropertyCommand::redo() {
    if (!m_item) return;
    applyValue(m_newValue);
    m_item->update();
}

void PCBPropertyCommand::applyValue(const QVariant& value) {
    if (m_propertyName == "Name") m_item->setName(value.toString());
    else if (m_propertyName == "Net") m_item->setNetName(value.toString());
    else if (m_propertyName == "Layer") m_item->setLayer(value.toInt());
    else if (m_propertyName == "Height (mm)") m_item->setHeight(value.toDouble());
    else if (m_propertyName == "3D Model Path") m_item->setModelPath(value.toString());
    else if (m_propertyName == "3D Model Scale") m_item->setModelScale(value.toDouble());
    else if (m_propertyName == "Locked") m_item->setLocked(value.toBool());
    else if (m_propertyName == "Width (mm)") {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(m_item)) trace->setWidth(value.toDouble());
    }
    else if (m_propertyName == "Position X (mm)") m_item->setX(value.toDouble());
    else if (m_propertyName == "Position Y (mm)") m_item->setY(value.toDouble());
    else if (m_propertyName == "Rotation (deg)") m_item->setRotation(value.toDouble());
    else if (m_propertyName == "Pad Shape") {
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setPadShape(value.toString());
    }
    else if (m_propertyName == "Size X (mm)" || m_propertyName == "Size Y (mm)") {
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) {
            QSizeF s = pad->size();
            if (m_propertyName == "Size X (mm)") s.setWidth(value.toDouble());
            else s.setHeight(value.toDouble());
            pad->setSize(s);
        }
    }
    else if (m_propertyName == "Diameter (mm)") {
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setDiameter(value.toDouble());
        else if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setDiameter(value.toDouble());
    }
    else if (m_propertyName == "Drill Size (mm)") {
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setDrillSize(value.toDouble());
        else if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setDrillSize(value.toDouble());
    }
    else if (m_propertyName == "Via Start Layer") {
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setStartLayer(value.toInt());
    }
    else if (m_propertyName == "Via End Layer") {
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setEndLayer(value.toInt());
    }
    else if (m_propertyName == "Microvia") {
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setMicrovia(value.toBool() || value.toString() == "True");
    }
    else if (m_propertyName == "Mask Expansion Mode") {
        const bool custom = value.toString().compare("Custom", Qt::CaseInsensitive) == 0;
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setMaskExpansionOverrideEnabled(custom);
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setMaskExpansionOverrideEnabled(custom);
    }
    else if (m_propertyName == "Mask Expansion (mm)") {
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setMaskExpansion(value.toDouble());
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setMaskExpansion(value.toDouble());
    }
    else if (m_propertyName == "Paste Expansion Mode") {
        const bool custom = value.toString().compare("Custom", Qt::CaseInsensitive) == 0;
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setPasteExpansionOverrideEnabled(custom);
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setPasteExpansionOverrideEnabled(custom);
    }
    else if (m_propertyName == "Paste Expansion (mm)") {
        if (PadItem* pad = dynamic_cast<PadItem*>(m_item)) pad->setPasteExpansion(value.toDouble());
        if (ViaItem* via = dynamic_cast<ViaItem*>(m_item)) via->setPasteExpansion(value.toDouble());
    }
    else if (m_propertyName == "Start X (mm)" || m_propertyName == "Start Y (mm)") {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(m_item)) {
            QPointF p = trace->startPoint();
            if (m_propertyName == "Start X (mm)") p.setX(value.toDouble());
            else p.setY(value.toDouble());
            trace->setStartPoint(p);
        }
    }
    else if (m_propertyName == "End X (mm)" || m_propertyName == "End Y (mm)") {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(m_item)) {
            QPointF p = trace->endPoint();
            if (m_propertyName == "End X (mm)") p.setX(value.toDouble());
            else p.setY(value.toDouble());
            trace->setEndPoint(p);
        }
    }
    else if (m_propertyName == "Clearance (mm)") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setClearance(value.toDouble());
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Priority") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setPriority(qRound(value.toDouble()));
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Remove Islands") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setRemoveIslands(value.toBool() || value.toString() == "True");
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Min Island Width (mm)") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setMinWidth(value.toDouble());
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Filled") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setFilled(value.toBool() || value.toString() == "True");
        }
    }
    else if (m_propertyName == "Pour Type") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setPourType(static_cast<CopperPourItem::PourType>(value.toInt()));
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Hatch Width (mm)") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setHatchWidth(value.toDouble());
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Solid") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setSolid(value.toBool());
            pour->rebuild();
        }
    }
    else if (m_propertyName == "Use Thermal Reliefs") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setUseThermalReliefs(value.toBool() || value.toString() == "True");
        }
    }
    else if (m_propertyName == "Thermal Spoke Width (mm)") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setThermalSpokeWidth(value.toDouble());
        }
    }
    else if (m_propertyName == "Thermal Spoke Count") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setThermalSpokeCount(value.toInt());
        }
    }
    else if (m_propertyName == "Thermal Angle (deg)") {
        if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(m_item)) {
            pour->setThermalSpokeAngleDeg(value.toDouble());
        }
    }
}

// === PCBAlignItemCommand ===
PCBAlignItemCommand::PCBAlignItemCommand(QGraphicsScene* scene, QList<PCBItem*> items, Alignment alignment, QUndoCommand* parent)
    : PCBCommand(scene, "Align Items", parent)
    , m_items(items) {
    
    if (items.isEmpty()) return;

    // Calculate bounding rect of all items
    QRectF totalRect = items.first()->sceneBoundingRect();
    for (int i = 1; i < items.size(); ++i) {
        totalRect = totalRect.united(items[i]->sceneBoundingRect());
    }

    // Capture old positions
    for (auto* item : items) {
        m_oldPositions.append(item->pos());
    }

    // Calculate new positions
    for (auto* item : items) {
        QRectF itemRect = item->sceneBoundingRect();
        QPointF newPos = item->pos();

        switch (alignment) {
            case AlignLeft:
                newPos.setX(item->pos().x() + (totalRect.left() - itemRect.left()));
                break;
            case AlignRight:
                newPos.setX(item->pos().x() + (totalRect.right() - itemRect.right()));
                break;
            case AlignTop:
                newPos.setY(item->pos().y() + (totalRect.top() - itemRect.top()));
                break;
            case AlignBottom:
                newPos.setY(item->pos().y() + (totalRect.bottom() - itemRect.bottom()));
                break;
            case AlignCenterX:
                newPos.setX(item->pos().x() + (totalRect.center().x() - itemRect.center().x()));
                break;
            case AlignCenterY:
                newPos.setY(item->pos().y() + (totalRect.center().y() - itemRect.center().y()));
                break;
            default: break;
        }
        m_newPositions.append(newPos);
    }

    // Distribution logic
    if (alignment == DistributeH || alignment == DistributeV) {
        m_newPositions.clear();
        
        if (items.size() < 3) {
            for (auto* item : items) m_newPositions.append(item->pos());
            return;
        }

        struct ItemInfo { PCBItem* item; QPointF center; };
        QList<ItemInfo> sorted;
        for (auto* it : items) sorted.append({it, it->sceneBoundingRect().center()});

        if (alignment == DistributeH) {
            std::sort(sorted.begin(), sorted.end(), [](const ItemInfo& a, const ItemInfo& b){
                return a.center.x() < b.center.x();
            });
            
            qreal startX = sorted.first().center.x();
            qreal endX = sorted.last().center.x();
            qreal step = (endX - startX) / (items.size() - 1);

            QMap<PCBItem*, QPointF> itemToNewPos;
            for (int i = 0; i < sorted.size(); ++i) {
                qreal targetCenterX = startX + i * step;
                QPointF currentCenter = sorted[i].item->sceneBoundingRect().center();
                QPointF pos = sorted[i].item->pos();
                pos.setX(pos.x() + (targetCenterX - currentCenter.x()));
                itemToNewPos[sorted[i].item] = pos;
            }
            for (auto* it : items) m_newPositions.append(itemToNewPos[it]);
        } else {
            std::sort(sorted.begin(), sorted.end(), [](const ItemInfo& a, const ItemInfo& b){
                return a.center.y() < b.center.y();
            });
            
            qreal startY = sorted.first().center.y();
            qreal endY = sorted.last().center.y();
            qreal step = (endY - startY) / (items.size() - 1);

            QMap<PCBItem*, QPointF> itemToNewPos;
            for (int i = 0; i < sorted.size(); ++i) {
                qreal targetCenterY = startY + i * step;
                QPointF currentCenter = sorted[i].item->sceneBoundingRect().center();
                QPointF pos = sorted[i].item->pos();
                pos.setY(pos.y() + (targetCenterY - currentCenter.y()));
                itemToNewPos[sorted[i].item] = pos;
            }
            for (auto* it : items) m_newPositions.append(itemToNewPos[it]);
        }
    }
}

void PCBAlignItemCommand::undo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_oldPositions[i]);
        }
    }
    PCBRatsnestManager::instance().update();
}

void PCBAlignItemCommand::redo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_newPositions[i]);
        }
    }
    PCBRatsnestManager::instance().update();
}

// === PCBShoveTracesCommand ===
PCBShoveTracesCommand::PCBShoveTracesCommand(QGraphicsScene* scene, const QList<PCBItem*>& items,
                                             const QList<QPair<QPointF, QPointF>>& oldGeoms,
                                             const QList<QPair<QPointF, QPointF>>& newGeoms,
                                             QUndoCommand* parent)
    : PCBCommand(scene, "Shove Traces", parent)
    , m_items(items), m_oldGeoms(oldGeoms), m_newGeoms(newGeoms) {
}

void PCBShoveTracesCommand::undo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (TraceItem* t = dynamic_cast<TraceItem*>(m_items[i])) {
            t->setStartPoint(m_oldGeoms[i].first);
            t->setEndPoint(m_oldGeoms[i].second);
            t->update();
        }
    }
}

void PCBShoveTracesCommand::redo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (TraceItem* t = dynamic_cast<TraceItem*>(m_items[i])) {
            t->setStartPoint(m_newGeoms[i].first);
            t->setEndPoint(m_newGeoms[i].second);
            t->update();
        }
    }
}
