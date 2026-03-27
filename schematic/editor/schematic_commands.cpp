#include "schematic_commands.h"
#include "schematic_item.h"
#include "../analysis/schematic_connectivity.h"
#include "../items/generic_component_item.h"
#include "../items/net_label_item.h"
#include "wire_item.h"
#include "../items/schematic_text_item.h"
#include "../items/schematic_shape_item.h"
#include "../items/schematic_sheet_item.h"
#include "../items/voltage_source_item.h"
#include <QDebug>
#include <QGraphicsView>
#include <QApplication>
#include <QSet>

namespace {
bool isConnectivitySensitiveSchematicItem(const SchematicItem* item) {
    if (!item) return false;
    return item->itemType() == SchematicItem::WireType ||
           item->itemType() == SchematicItem::BusType ||
           item->itemTypeName() == "BusEntry";
}

SchematicItem* owningSchematicItem(SchematicItem* item) {
    SchematicItem* current = item;
    while (current && current->isSubItem()) {
        current = dynamic_cast<SchematicItem*>(current->parentItem());
    }
    return current ? current : item;
}

QList<SchematicItem*> normalizeOwnedItems(const QList<SchematicItem*>& items) {
    QList<SchematicItem*> normalized;
    QSet<SchematicItem*> seen;
    for (SchematicItem* item : items) {
        SchematicItem* owner = owningSchematicItem(item);
        if (!owner || seen.contains(owner)) continue;
        seen.insert(owner);
        normalized.append(owner);
    }
    return normalized;
}

QRectF itemsSceneBounds(const QList<SchematicItem*>& items) {
    QRectF bounds;
    bool first = true;
    for (SchematicItem* item : items) {
        if (!item) continue;
        const QRectF itemBounds = item->sceneBoundingRect().adjusted(-8, -8, 8, 8);
        if (first) {
            bounds = itemBounds;
            first = false;
        } else {
            bounds = bounds.united(itemBounds);
        }
    }
    return bounds;
}

void refreshSceneViews(QGraphicsScene* scene, const QRectF& dirtyRect = QRectF()) {
    if (!scene) return;

    if (dirtyRect.isValid() && !dirtyRect.isEmpty()) {
        scene->invalidate(dirtyRect, QGraphicsScene::AllLayers);
        scene->update(dirtyRect);
    } else {
        scene->invalidate(scene->sceneRect(), QGraphicsScene::AllLayers);
        scene->update();
    }

    for (auto* view : scene->views()) {
        if (!view || !view->viewport()) continue;
        view->resetCachedContent();
        view->viewport()->update();
        view->viewport()->repaint();
    }
}
}

// ============================================================================
// AddItemCommand
// ============================================================================

AddItemCommand::AddItemCommand(QGraphicsScene* scene, SchematicItem* item, QUndoCommand* parent)
    : SchematicCommand(scene, QString("Add %1").arg(item->itemTypeName()), parent)
    , m_item(item)
    , m_ownsItem(false) {
}

AddItemCommand::~AddItemCommand() {
    if (m_ownsItem && m_item) {
        delete m_item;
    }
}

void AddItemCommand::undo() {
    if (m_item && m_scene) {
        m_item->setVisible(false);
        m_scene->removeItem(m_item);
        if (isConnectivitySensitiveSchematicItem(m_item)) {
            SchematicConnectivity::updateVisualConnections(m_scene);
        }
        refreshSceneViews(m_scene, m_item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
        m_ownsItem = true;
    }
}

void AddItemCommand::redo() {
    if (m_item && m_scene) {
        m_scene->addItem(m_item);
        m_item->setVisible(true);
        if (isConnectivitySensitiveSchematicItem(m_item)) {
            SchematicConnectivity::updateVisualConnections(m_scene);
        }
        refreshSceneViews(m_scene, m_item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
        m_ownsItem = false;
    }
}

// ============================================================================
// RemoveItemCommand
// ============================================================================

RemoveItemCommand::RemoveItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, QUndoCommand* parent)
    : SchematicCommand(scene, QString("Delete %1 item(s)").arg(normalizeOwnedItems(items).size()), parent)
    , m_items(normalizeOwnedItems(items))
    , m_ownsItems(false) {
    
    // Store serialized data for each item
    for (SchematicItem* item : items) {
        QJsonObject data = item->toJson();
        data["rotation"] = item->rotation();
        m_itemData.append(data);
    }
}

RemoveItemCommand::~RemoveItemCommand() {
    if (m_ownsItems) {
        qDeleteAll(m_items);
    }
}

void RemoveItemCommand::undo() {
    QRectF dirtyRect = itemsSceneBounds(m_items);
    for (SchematicItem* item : m_items) {
        if (item && m_scene && item->scene() != m_scene) {
            m_scene->addItem(item);
            item->setVisible(true);
        }
    }
    dirtyRect = dirtyRect.united(itemsSceneBounds(m_items));
    if (m_scene) {
        bool hasWire = false;
        for (SchematicItem* item : m_items) {
            if (item) {
                QString typeName = item->itemTypeName();
                if (typeName == "Wire" || typeName == "Bus" || typeName == "BusEntry") {
                    hasWire = true;
                    break;
                }
            }
        }
        if (hasWire) SchematicConnectivity::updateVisualConnections(m_scene);
        refreshSceneViews(m_scene, dirtyRect);
    }
    m_ownsItems = false;
}

void RemoveItemCommand::redo() {
    QRectF dirtyRect = itemsSceneBounds(m_items);
    for (SchematicItem* item : m_items) {
        if (item && m_scene && item->scene() == m_scene) {
            item->setSelected(false);
            item->setVisible(false);
            m_scene->removeItem(item);
        }
    }
    if (m_scene) {
        bool hasWire = false;
        for (SchematicItem* item : m_items) {
            if (item) {
                QString typeName = item->itemTypeName();
                if (typeName == "Wire" || typeName == "Bus" || typeName == "BusEntry") {
                    hasWire = true;
                    break;
                }
            }
        }
        if (hasWire) SchematicConnectivity::updateVisualConnections(m_scene);
        refreshSceneViews(m_scene, dirtyRect);
    }
    m_ownsItems = true;
}

// ============================================================================
// MoveItemCommand
// ============================================================================

MoveItemCommand::MoveItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items,
                                 const QList<QPointF>& oldPositions, const QList<QPointF>& newPositions,
                                 QUndoCommand* parent)
    : SchematicCommand(scene, QString("Move %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_oldPositions(oldPositions)
    , m_newPositions(newPositions) {
    
    if (!m_scene) return;
    
    // Build rubber banding list: find wires connected to moved items
    QList<WireItem*> allWires;
    for (QGraphicsItem* it : m_scene->items()) {
        if (WireItem* w = dynamic_cast<WireItem*>(it)) allWires.append(w);
    }
    
    for (int i = 0; i < m_items.size(); ++i) {
        SchematicItem* item = m_items[i];
        if (!item || isConnectivitySensitiveSchematicItem(item)) continue;
        
        QList<QPointF> pinPoints = item->connectionPoints();
        for (const QPointF& pinPt : pinPoints) {
            for (WireItem* wire : allWires) {
                if (m_items.contains(wire)) continue;
                
                QList<QPointF> wPts = wire->points();
                if (wPts.size() < 2) continue;
                
                if (QLineF(pinPt, wPts.first()).length() < 1.0) {
                    m_rubberBands.append({wire, 0, wPts.first()});
                } else if (QLineF(pinPt, wPts.last()).length() < 1.0) {
                    m_rubberBands.append({wire, (int)wPts.size() - 1, wPts.last()});
                }
            }
        }
    }
}

void MoveItemCommand::undo() {
    for (int i = 0; i < m_items.size() && i < m_oldPositions.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_oldPositions[i]);
        }
    }

    // Restore wires
    for (const auto& rb : m_rubberBands) {
        QList<QPointF> pts = rb.wire->points();
        if (rb.pointIndex < pts.size()) {
            pts[rb.pointIndex] = rb.originalPoint;
            rb.wire->setPoints(pts);
        }
    }

    if (m_scene) {
        SchematicConnectivity::updateVisualConnections(m_scene);
        m_scene->update();
    }
}

void MoveItemCommand::redo() {
    for (int i = 0; i < m_items.size() && i < m_newPositions.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_newPositions[i]);
        }
    }

    // Calculate delta and move wire endpoints
    if (!m_items.isEmpty() && !m_oldPositions.isEmpty() && !m_newPositions.isEmpty()) {
        QPointF delta = m_newPositions[0] - m_oldPositions[0];
        for (const auto& rb : m_rubberBands) {
            QList<QPointF> pts = rb.wire->points();
            if (rb.pointIndex < pts.size()) {
                pts[rb.pointIndex] = rb.originalPoint + delta;
                rb.wire->setPoints(pts);
            }
        }
    }

    if (m_scene) {
        SchematicConnectivity::updateVisualConnections(m_scene);
        m_scene->update();
    }
}

bool MoveItemCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != id()) {
        return false;
    }
    
    const MoveItemCommand* moveCmd = static_cast<const MoveItemCommand*>(other);
    
    // Only merge if same items are being moved
    if (moveCmd->m_items != m_items) {
        return false;
    }
    
    // Keep original old positions, update to latest new positions
    m_newPositions = moveCmd->m_newPositions;
    return true;
}

// ============================================================================
// RotateItemCommand
// ============================================================================

RotateItemCommand::RotateItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items,
                                     qreal angleDelta, QUndoCommand* parent)
    : SchematicCommand(scene, QString("Rotate %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_angleDelta(angleDelta) {
    
    // Store original rotations
    for (SchematicItem* item : items) {
        m_oldRotations.append(item ? item->rotation() : 0);
    }
}

void RotateItemCommand::undo() {
    for (int i = 0; i < m_items.size() && i < m_oldRotations.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setRotation(m_oldRotations[i]);
        }
    }
}

void RotateItemCommand::redo() {
    for (int i = 0; i < m_items.size() && i < m_oldRotations.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setRotation(m_oldRotations[i] + m_angleDelta);
        }
    }
}

// ============================================================================
// ChangePropertyCommand
// ============================================================================

ChangePropertyCommand::ChangePropertyCommand(QGraphicsScene* scene, SchematicItem* item,
                                             const QString& propertyName,
                                             const QVariant& oldValue, const QVariant& newValue,
                                             const QString& basePath,
                                             QUndoCommand* parent)
    : SchematicCommand(scene, QString("Change %1").arg(propertyName), parent)
    , m_item(item)
    , m_propertyName(propertyName)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
    , m_basePath(basePath) {
}

void ChangePropertyCommand::undo() {
    if (!m_item) return;
    
    if (m_propertyName == "value") {
        m_item->setValue(m_oldValue.toString());
    } else if (m_propertyName == "name") {
        m_item->setName(m_oldValue.toString());
    } else if (m_propertyName == "reference") {
        m_item->setReference(m_oldValue.toString());
    } else if (m_propertyName == "footprint") {
        m_item->setFootprint(m_oldValue.toString());
    } else if (m_propertyName == "manufacturer") {
        m_item->setManufacturer(m_oldValue.toString());
    } else if (m_propertyName == "mpn") {
        m_item->setMpn(m_oldValue.toString());
    } else if (m_propertyName == "description") {
        m_item->setDescription(m_oldValue.toString());
    } else if (m_propertyName == "netClass") {
        if (auto* netLabel = dynamic_cast<NetLabelItem*>(m_item)) {
            netLabel->setNetClassName(m_oldValue.toString());
        }
    } else if (auto* textItem = dynamic_cast<SchematicTextItem*>(m_item)) {
        if (m_propertyName == "Text") {
            textItem->setText(m_oldValue.toString());
        } else if (m_propertyName == "Font Size") {
            QFont f = textItem->font();
            f.setPointSize(m_oldValue.toInt());
            textItem->setFont(f);
        } else if (m_propertyName == "Color") {
            textItem->setColor(QColor(m_oldValue.toString()));
        } else if (m_propertyName == "Bold") {
            QFont f = textItem->font();
            f.setBold(m_oldValue.toString() == "True");
            textItem->setFont(f);
        } else if (m_propertyName == "Italic") {
            QFont f = textItem->font();
            f.setItalic(m_oldValue.toString() == "True");
            textItem->setFont(f);
        } else if (m_propertyName == "Alignment") {
            if (m_oldValue.toString() == "Left") textItem->setAlignment(Qt::AlignLeft);
            else if (m_oldValue.toString() == "Center") textItem->setAlignment(Qt::AlignCenter);
            else if (m_oldValue.toString() == "Right") textItem->setAlignment(Qt::AlignRight);
        }
    } else if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(m_item)) {
        if (m_propertyName == "Source Type") vsrc->setSourceType(m_oldValue.toString() == "DC" ? VoltageSourceItem::DC : VoltageSourceItem::Sine);
        else if (m_propertyName == "DC Voltage") vsrc->setDcVoltage(m_oldValue.toString());
        else if (m_propertyName == "AC Amplitude") vsrc->setSineAmplitude(m_oldValue.toString());
        else if (m_propertyName == "AC Frequency") vsrc->setSineFrequency(m_oldValue.toString());
        else if (m_propertyName == "AC Offset") vsrc->setSineOffset(m_oldValue.toString());
    } else if (auto* shape = dynamic_cast<SchematicShapeItem*>(m_item)) {
        if (m_propertyName == "Start X") {
            QPointF p = shape->startPoint(); p.setX(m_oldValue.toDouble()); shape->setStartPoint(p);
        } else if (m_propertyName == "Start Y") {
            QPointF p = shape->startPoint(); p.setY(m_oldValue.toDouble()); shape->setStartPoint(p);
        } else if (m_propertyName == "End X") {
            QPointF p = shape->endPoint(); p.setX(m_oldValue.toDouble()); shape->setEndPoint(p);
        } else if (m_propertyName == "End Y") {
            QPointF p = shape->endPoint(); p.setY(m_oldValue.toDouble()); shape->setEndPoint(p);
        } else if (m_propertyName == "Width") {
            QPen p = shape->pen(); p.setWidthF(m_oldValue.toDouble()); shape->setPen(p);
        } else if (m_propertyName == "Color") {
            QPen p = shape->pen(); p.setColor(QColor(m_oldValue.toString())); shape->setPen(p);
        } else if (m_propertyName == "Fill Color") {
            QBrush b = shape->brush();
            b.setColor(QColor(m_oldValue.toString()));
            // Check if we should revert fill style too (tricky if only color stored. Revert to nobrush if color is empty?)
            // Assuming simplified "SolidPattern" toggle for now based on color validity
            if (m_oldValue.toString() == "#000000" || m_oldValue.toString().isEmpty()) b.setStyle(Qt::NoBrush);
            else b.setStyle(Qt::SolidPattern);
            shape->setBrush(b);
        } else if (m_propertyName == "Line Style") {
            QPen p = shape->pen();
            if (m_oldValue.toString() == "Solid") p.setStyle(Qt::SolidLine);
            else if (m_oldValue.toString() == "Dash") p.setStyle(Qt::DashLine);
            else if (m_oldValue.toString() == "Dot") p.setStyle(Qt::DotLine);
            shape->setPen(p);
        } else if (m_propertyName == "Ctrl1 X" || m_propertyName == "Ctrl1 Y" || m_propertyName == "Ctrl2 X" || m_propertyName == "Ctrl2 Y") {
            if (shape->shapeType() == SchematicShapeItem::Bezier && shape->points().size() == 4) {
                QList<QPointF> pts = shape->points();
                int idx = m_propertyName.contains("1") ? 1 : 2;
                if (m_propertyName.contains("X")) pts[idx].setX(m_oldValue.toDouble());
                else pts[idx].setY(m_oldValue.toDouble());
                shape->setPoints(pts);
            }
        }
    }
    
    if (m_propertyName == "Position X") {
        m_item->setX(m_oldValue.toDouble());
    } else if (m_propertyName == "Position Y") {
        m_item->setY(m_oldValue.toDouble());
    } else if (m_propertyName == "Locked") {
        m_item->setLocked(m_oldValue.toBool() || m_oldValue.toString() == "True");
    } else if (m_propertyName == "exclude_simulation") {
        m_item->setExcludeFromSimulation(m_oldValue.toBool());
    } else if (m_propertyName == "exclude_pcb") {
        m_item->setExcludeFromPcb(m_oldValue.toBool());
    } else if (m_propertyName == "Mirrored") {
        m_item->setMirroredX(m_oldValue.toBool() || m_oldValue.toString() == "True");
    } else if (auto* sheet = dynamic_cast<SchematicSheetItem*>(m_item)) {
        if (m_propertyName == "fileName") {
            sheet->setFileName(m_oldValue.toString());
            sheet->updatePorts(m_basePath);
        } else if (m_propertyName == "sheetName") {
            sheet->setSheetName(m_oldValue.toString());
        }
    }
    
    m_item->update();
}

void ChangePropertyCommand::redo() {
    if (!m_item) return;
    
    if (m_propertyName == "value") {
        m_item->setValue(m_newValue.toString());
    } else if (m_propertyName == "name") {
        m_item->setName(m_newValue.toString());
    } else if (m_propertyName == "reference") {
        m_item->setReference(m_newValue.toString());
    } else if (m_propertyName == "footprint") {
        m_item->setFootprint(m_newValue.toString());
    } else if (m_propertyName == "manufacturer") {
        m_item->setManufacturer(m_newValue.toString());
    } else if (m_propertyName == "mpn") {
        m_item->setMpn(m_newValue.toString());
    } else if (m_propertyName == "description") {
        m_item->setDescription(m_newValue.toString());
    } else if (m_propertyName == "netClass") {
        if (auto* netLabel = dynamic_cast<NetLabelItem*>(m_item)) {
            netLabel->setNetClassName(m_newValue.toString());
        }
    } else if (auto* textItem = dynamic_cast<SchematicTextItem*>(m_item)) {
        if (m_propertyName == "Text") {
            textItem->setText(m_newValue.toString());
        } else if (m_propertyName == "Font Size") {
            QFont f = textItem->font();
            f.setPointSize(m_newValue.toInt());
            textItem->setFont(f);
        } else if (m_propertyName == "Color") {
            textItem->setColor(QColor(m_newValue.toString()));
        } else if (m_propertyName == "Bold") {
            QFont f = textItem->font();
            f.setBold(m_newValue.toString() == "True");
            textItem->setFont(f);
        } else if (m_propertyName == "Italic") {
            QFont f = textItem->font();
            f.setItalic(m_newValue.toString() == "True");
            textItem->setFont(f);
        } else if (m_propertyName == "Alignment") {
            if (m_newValue.toString() == "Left") textItem->setAlignment(Qt::AlignLeft);
            else if (m_newValue.toString() == "Center") textItem->setAlignment(Qt::AlignCenter);
            else if (m_newValue.toString() == "Right") textItem->setAlignment(Qt::AlignRight);
        }
    } else if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(m_item)) {
        if (m_propertyName == "Source Type") vsrc->setSourceType(m_newValue.toString() == "DC" ? VoltageSourceItem::DC : VoltageSourceItem::Sine);
        else if (m_propertyName == "DC Voltage") vsrc->setDcVoltage(m_newValue.toString());
        else if (m_propertyName == "AC Amplitude") vsrc->setSineAmplitude(m_newValue.toString());
        else if (m_propertyName == "AC Frequency") vsrc->setSineFrequency(m_newValue.toString());
        else if (m_propertyName == "AC Offset") vsrc->setSineOffset(m_newValue.toString());
    } else if (auto* shape = dynamic_cast<SchematicShapeItem*>(m_item)) {
        if (m_propertyName == "Start X") {
            QPointF p = shape->startPoint(); p.setX(m_newValue.toDouble()); shape->setStartPoint(p);
        } else if (m_propertyName == "Start Y") {
            QPointF p = shape->startPoint(); p.setY(m_newValue.toDouble()); shape->setStartPoint(p);
        } else if (m_propertyName == "End X") {
            QPointF p = shape->endPoint(); p.setX(m_newValue.toDouble()); shape->setEndPoint(p);
        } else if (m_propertyName == "End Y") {
            QPointF p = shape->endPoint(); p.setY(m_newValue.toDouble()); shape->setEndPoint(p);
        } else if (m_propertyName == "Width") {
            QPen p = shape->pen(); p.setWidthF(m_newValue.toDouble()); shape->setPen(p);
        } else if (m_propertyName == "Color") {
            QPen p = shape->pen(); p.setColor(QColor(m_newValue.toString())); shape->setPen(p);
        } else if (m_propertyName == "Fill Color") {
            QBrush b = shape->brush(); 
            b.setColor(QColor(m_newValue.toString())); 
            if (b.style() == Qt::NoBrush && m_newValue.toString() != "#000000" && !m_newValue.toString().isEmpty()) b.setStyle(Qt::SolidPattern);
            shape->setBrush(b);
        } else if (m_propertyName == "Line Style") {
            QPen p = shape->pen();
            if (m_newValue.toString() == "Solid") p.setStyle(Qt::SolidLine);
            else if (m_newValue.toString() == "Dash") p.setStyle(Qt::DashLine);
            else if (m_newValue.toString() == "Dot") p.setStyle(Qt::DotLine);
            shape->setPen(p);
        } else if (m_propertyName == "Ctrl1 X" || m_propertyName == "Ctrl1 Y" || m_propertyName == "Ctrl2 X" || m_propertyName == "Ctrl2 Y") {
            if (shape->shapeType() == SchematicShapeItem::Bezier && shape->points().size() == 4) {
                QList<QPointF> pts = shape->points();
                int idx = m_propertyName.contains("1") ? 1 : 2;
                if (m_propertyName.contains("X")) pts[idx].setX(m_newValue.toDouble());
                else pts[idx].setY(m_newValue.toDouble());
                shape->setPoints(pts);
            }
        }
    }
    
    if (m_propertyName == "Position X") {
        m_item->setX(m_newValue.toDouble());
    } else if (m_propertyName == "Position Y") {
        m_item->setY(m_newValue.toDouble());
    } else if (m_propertyName == "Locked") {
        m_item->setLocked(m_newValue.toBool() || m_newValue.toString() == "True");
    } else if (m_propertyName == "exclude_simulation") {
        m_item->setExcludeFromSimulation(m_newValue.toBool());
    } else if (m_propertyName == "exclude_pcb") {
        m_item->setExcludeFromPcb(m_newValue.toBool());
    } else if (m_propertyName == "Mirrored") {
        m_item->setMirroredX(m_newValue.toBool() || m_newValue.toString() == "True");
    } else if (auto* sheet = dynamic_cast<SchematicSheetItem*>(m_item)) {
        if (m_propertyName == "fileName") {
            sheet->setFileName(m_newValue.toString());
            sheet->updatePorts(m_basePath);
        } else if (m_propertyName == "sheetName") {
            sheet->setSheetName(m_newValue.toString());
        }
    }
    
    m_item->update();
}

bool ChangePropertyCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != id()) {
        return false;
    }
    
    const ChangePropertyCommand* propCmd = static_cast<const ChangePropertyCommand*>(other);
    
    // Only merge if same item and same property
    if (propCmd->m_item != m_item || propCmd->m_propertyName != m_propertyName) {
        return false;
    }
    
    // Keep original old value, update to latest new value
    m_newValue = propCmd->m_newValue;
    return true;
}

// ─── BulkChangePropertyCommand ──────────────────────────────────────────────

BulkChangePropertyCommand::BulkChangePropertyCommand(QGraphicsScene* scene, SchematicItem* item,
                                                   const QJsonObject& newState, QUndoCommand* parent)
    : SchematicCommand(scene, "Bulk Update Properties", parent), m_item(item), m_newState(newState) {
    if (m_item) {
        m_oldState = m_item->toJson();
    }
    setText(QString("Update %1 Properties").arg(m_item ? m_item->reference() : "Item"));
}

void BulkChangePropertyCommand::undo() {
    if (m_item) {
        m_item->fromJson(m_oldState);
        m_item->update();
    }
}

void BulkChangePropertyCommand::redo() {
    if (m_item) {
        m_item->fromJson(m_newState);
        m_item->update();
    }
}

// ============================================================================
// UpdateWirePointsCommand
// ============================================================================

UpdateWirePointsCommand::UpdateWirePointsCommand(QGraphicsScene* scene, WireItem* wire,
                                                 const QList<QPointF>& oldPoints, const QList<QPointF>& newPoints,
                                                 QUndoCommand* parent)
    : SchematicCommand(scene, "Stretch Wire", parent)
    , m_wire(wire)
    , m_oldPoints(oldPoints)
    , m_newPoints(newPoints) {
}

void UpdateWirePointsCommand::undo() {
    if (m_wire) {
        m_wire->setPoints(m_oldPoints);
    }
    if (m_scene) SchematicConnectivity::updateVisualConnections(m_scene);
}

void UpdateWirePointsCommand::redo() {
    if (m_wire) {
        m_wire->setPoints(m_newPoints);
    }
    if (m_scene) SchematicConnectivity::updateVisualConnections(m_scene);
}

// ============================================================================
// FlipItemCommand
// ============================================================================

FlipItemCommand::FlipItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, bool vertical, QUndoCommand* parent)
    : SchematicCommand(scene, QString("Flip %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_vertical(vertical) {
}

void FlipItemCommand::undo() {
    for (auto* item : m_items) {
        if (item) {
            if (m_vertical) item->setMirroredY(!item->isMirroredY());
            else item->setMirroredX(!item->isMirroredX());
        }
    }
}

void FlipItemCommand::redo() {
    for (auto* item : m_items) {
        if (item) {
            if (m_vertical) item->setMirroredY(!item->isMirroredY());
            else item->setMirroredX(!item->isMirroredX());
        }
    }
}

// ============================================================================
// LockItemCommand
// ============================================================================

LockItemCommand::LockItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, bool locked, QUndoCommand* parent)
    : SchematicCommand(scene, QString(locked ? "Lock %1 item(s)" : "Unlock %1 item(s)").arg(items.size()), parent)
    , m_items(items)
    , m_locked(locked) {
}

void LockItemCommand::undo() {
    for (auto* item : m_items) {
        if (item) item->setLocked(!m_locked);
    }
}

void LockItemCommand::redo() {
    for (auto* item : m_items) {
        if (item) item->setLocked(m_locked);
    }
}

// ============================================================================
// ZValueItemCommand
// ============================================================================

ZValueItemCommand::ZValueItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, bool bringToFront, QUndoCommand* parent)
    : SchematicCommand(scene, bringToFront ? "Bring to Front" : "Send to Back", parent)
    , m_items(items)
    , m_bringToFront(bringToFront) {
    
    // Store old Z values
    for (auto* item : m_items) {
        m_oldZValues.append(item->zValue());
    }
}

void ZValueItemCommand::undo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]) m_items[i]->setZValue(m_oldZValues[i]);
    }
}

void ZValueItemCommand::redo() {
    if (m_items.isEmpty()) return;

    // Determine target Z value
    qreal targetZ = 0;
    
    if (m_bringToFront) {
        // Find max Z in scene
        qreal maxZ = -10000;
        for (auto* item : m_scene->items()) {
            maxZ = qMax(maxZ, item->zValue());
        }
        targetZ = maxZ + 1;
    } else {
        // Find min Z in scene
        qreal minZ = 10000;
        for (auto* item : m_scene->items()) {
            minZ = qMin(minZ, item->zValue());
        }
        targetZ = minZ - 1;
    }

    // Apply new Z value (simple stack: all selected get same top/bottom Z, preserving relative order could be better but this is "Bring Selection to Front")
    // To preserve relative order: sort items by Z, then apply range.
    // Simplifying: just set all to targetZ, or targetZ + i
    
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]) {
            // Add slight offset to keep internal order of selection if needed, or just flat
            m_items[i]->setZValue(targetZ + (m_bringToFront ? i * 0.001 : -i * 0.001));
        }
    }
}

// ============================================================================
// AlignItemCommand
// ============================================================================

AlignItemCommand::AlignItemCommand(QGraphicsScene* scene, QList<SchematicItem*> items, Alignment alignment, QUndoCommand* parent)
    : SchematicCommand(scene, "Align Items", parent)
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
        QPointF offset = newPos - itemRect.center(); // Offset from center to pos

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

    // Special handling for distribution (needs a second pass after m_newPositions is initially filled or separate logic)
    if (alignment == DistributeH || alignment == DistributeV) {
        m_newPositions.clear(); // Recompute
        
        if (items.size() < 3) {
            // Can't distribute 1 or 2 items usefully in this context, just keep original
            for (auto* item : items) m_newPositions.append(item->pos());
            return;
        }

        struct ItemInfo { SchematicItem* item; QPointF center; };
        QList<ItemInfo> sorted;
        for (auto* it : items) sorted.append({it, it->sceneBoundingRect().center()});

        if (alignment == DistributeH) {
            std::sort(sorted.begin(), sorted.end(), [](const ItemInfo& a, const ItemInfo& b){
                return a.center.x() < b.center.x();
            });
            
            qreal startX = sorted.first().center.x();
            qreal endX = sorted.last().center.x();
            qreal step = (endX - startX) / (items.size() - 1);

            QMap<SchematicItem*, QPointF> itemToNewPos;
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

            QMap<SchematicItem*, QPointF> itemToNewPos;
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

void AlignItemCommand::undo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_oldPositions[i]);
        }
    }
}

void AlignItemCommand::redo() {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i]) {
            m_items[i]->setPos(m_newPositions[i]);
        }
    }
}

// ============================================================================
// SchematicAnnotateCommand
// ============================================================================

SchematicAnnotateCommand::SchematicAnnotateCommand(QGraphicsScene* scene, 
                                                   const QMap<SchematicItem*, QString>& oldRefs, 
                                                   const QMap<SchematicItem*, QString>& newRefs, 
                                                   QUndoCommand* parent)
    : SchematicCommand(scene, "Annotate Components", parent)
    , m_oldRefs(oldRefs), m_newRefs(newRefs) {
}

void SchematicAnnotateCommand::undo() {
    for (auto it = m_oldRefs.begin(); it != m_oldRefs.end(); ++it) {
        it.key()->setReference(it.value());
    }
}

void SchematicAnnotateCommand::redo() {
    for (auto it = m_newRefs.begin(); it != m_newRefs.end(); ++it) {
        it.key()->setReference(it.value());
    }
}

// ============================================================================
// SwitchPinFunctionCommand
// ============================================================================

SwitchPinFunctionCommand::SwitchPinFunctionCommand(QGraphicsScene* scene, GenericComponentItem* item, int pinNumber, int newMode, QUndoCommand* parent)
    : SchematicCommand(scene, QString("Switch Pin %1 Function").arg(pinNumber), parent)
    , m_item(item)
    , m_pinNumber(pinNumber)
    , m_newMode(newMode) {
    m_oldMode = item ? item->getPinMode(pinNumber) : -1;
}

void SwitchPinFunctionCommand::undo() {
    if (m_item) m_item->setPinMode(m_pinNumber, m_oldMode);
}

void SwitchPinFunctionCommand::redo() {
    if (m_item) m_item->setPinMode(m_pinNumber, m_newMode);
}
