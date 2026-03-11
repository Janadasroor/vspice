#include "schematic_shape_tool.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>

SchematicShapeTool::SchematicShapeTool(SchematicShapeItem::ShapeType type, QObject* parent)
    : SchematicTool(type == SchematicShapeItem::Rectangle ? "Rectangle" :
                    type == SchematicShapeItem::Circle ? "Circle" : 
                    type == SchematicShapeItem::Polygon ? "Polygon" : 
                    type == SchematicShapeItem::Bezier ? "Bezier" : "Line", parent)
    , m_type(type)
    , m_previewItem(nullptr)
    , m_isDragging(false)
{
}

QCursor SchematicShapeTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void SchematicShapeTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
}

void SchematicShapeTool::deactivate() {
    if (m_previewItem) {
        view()->scene()->removeItem(m_previewItem);
        delete m_previewItem;
        m_previewItem = nullptr;
    }
    m_points.clear();
    m_isDragging = false;
}

void SchematicShapeTool::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        if (m_type == SchematicShapeItem::Polygon && m_points.size() > 2) {
            // Finalize polygon
            SchematicShapeItem* finalItem = new SchematicShapeItem(m_type);
            finalItem->setPreviewOpen(false);
            finalItem->setPoints(m_points);
            view()->undoStack()->push(new AddItemCommand(view()->scene(), finalItem));
            deactivate();
        } else {
            deactivate();
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));
        
        if (m_type == SchematicShapeItem::Polygon) {
            // Close if near start (finish)
            if (!m_points.isEmpty() && QLineF(pos, m_points.first()).length() < 10.0 && m_points.size() > 2) {
                SchematicShapeItem* finalItem = new SchematicShapeItem(m_type);
                finalItem->setPreviewOpen(false);
                finalItem->setPoints(m_points);
                view()->undoStack()->push(new AddItemCommand(view()->scene(), finalItem));
                deactivate();
                return;
            }
            
            m_points.append(pos);
            if (!m_previewItem) {
                m_previewItem = new SchematicShapeItem(m_type);
                m_previewItem->setPreviewOpen(true);
                view()->scene()->addItem(m_previewItem);
            }
            m_previewItem->setPoints(m_points);
        } else if (m_type == SchematicShapeItem::Bezier) {
            m_points.append(pos);
            if (!m_previewItem) {
                m_previewItem = new SchematicShapeItem(m_type);
                view()->scene()->addItem(m_previewItem);
            }
            
            if (m_points.size() == 4) {
                SchematicShapeItem* finalItem = new SchematicShapeItem(m_type);
                finalItem->setPoints(m_points);
                view()->undoStack()->push(new AddItemCommand(view()->scene(), finalItem));
                deactivate();
            } else {
                m_previewItem->setPoints(m_points);
            }
        } else {
            // 2-Click shapes
            m_startPos = pos;
            m_isDragging = true;
            m_previewItem = new SchematicShapeItem(m_type, m_startPos, m_startPos);
            view()->scene()->addItem(m_previewItem);
        }
    }
}

void SchematicShapeTool::mouseMoveEvent(QMouseEvent* event) {
    QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));
    
    if (m_type == SchematicShapeItem::Polygon) {
        if (m_previewItem && !m_points.isEmpty()) {
            QList<QPointF> previewPoints = m_points;
            previewPoints.append(pos);
            m_previewItem->setPoints(previewPoints);
        }
    } else if (m_type == SchematicShapeItem::Bezier) {
        if (m_previewItem && !m_points.isEmpty()) {
            QList<QPointF> previewPoints = m_points;
            while (previewPoints.size() < 4) previewPoints.append(pos);
            m_previewItem->setPoints(previewPoints);
        }
    } else if (m_isDragging && m_previewItem) {
        m_previewItem->setEndPoint(pos);
    }
}

void SchematicShapeTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isDragging && m_type != SchematicShapeItem::Polygon) {
        m_isDragging = false;
        
        if (m_previewItem) {
            QPointF endPos = view()->snapToGrid(view()->mapToScene(event->pos()));
            
            if (QLineF(m_startPos, endPos).length() > 0) {
                SchematicShapeItem* finalItem = new SchematicShapeItem(m_type, m_startPos, endPos);
                if (view()->undoStack()) {
                    view()->undoStack()->push(new AddItemCommand(view()->scene(), finalItem));
                } else {
                    view()->scene()->addItem(finalItem);
                }
            }
            
            view()->scene()->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
    }
}

void SchematicShapeTool::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_type == SchematicShapeItem::Polygon) {
        if (m_points.size() > 2) {
            // Remove the last point if it was just added by the second click of the double-click
            // (Note: mousePressEvent is called before mouseDoubleClickEvent)
            if (!m_points.isEmpty()) m_points.removeLast();
            
            SchematicShapeItem* finalItem = new SchematicShapeItem(m_type);
            finalItem->setPreviewOpen(false);
            finalItem->setPoints(m_points);
            view()->undoStack()->push(new AddItemCommand(view()->scene(), finalItem));
            deactivate();
            event->accept();
        }
    }
}
