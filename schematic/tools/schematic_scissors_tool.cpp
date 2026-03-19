#include "schematic_scissors_tool.h"
#include "schematic_view.h"
#include "schematic_item.h"
#include "schematic_commands.h"
#include "wire_item.h"
#include <QGraphicsScene>
#include <QUndoStack>
#include <QMouseEvent>
#include <QGraphicsRectItem>
#include "flux/core/theme_manager.h"

SchematicScissorsTool::SchematicScissorsTool(QObject* parent)
    : SchematicTool("Scissors", parent), m_rubberBandActive(false), m_rubberBandItem(nullptr) {
}

SchematicScissorsTool::~SchematicScissorsTool() {
    delete m_rubberBandItem;
}

QCursor SchematicScissorsTool::cursor() const {
    QPixmap pix(":/icons/tool_scissors.svg");
    return QCursor(pix.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation), 25, 9); 
}

void SchematicScissorsTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || !view()->scene()) return;

    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = view()->mapToScene(event->pos());
        QGraphicsItem* hit = view()->itemAt(event->pos());
        
        if (!hit) {
            // Start rubber band selection
            m_rubberBandActive = true;
            m_rubberBandOrigin = scenePos;
            
            if (m_rubberBandItem) {
                view()->scene()->removeItem(m_rubberBandItem);
                delete m_rubberBandItem;
            }
            
            m_rubberBandItem = new QGraphicsRectItem();
            QColor selColor = Qt::red;
            if (ThemeManager::theme()) selColor = ThemeManager::theme()->selectionBox();
            
            QColor fillColor = selColor;
            fillColor.setAlpha(40);
            
            m_rubberBandItem->setPen(QPen(selColor, 1, Qt::DashLine));
            m_rubberBandItem->setBrush(QBrush(fillColor));
            m_rubberBandItem->setZValue(1000);
            view()->scene()->addItem(m_rubberBandItem);
            m_rubberBandItem->setRect(QRectF(m_rubberBandOrigin, QSize(0, 0)));
            event->accept();
            return;
        }

        // Single click cut
        SchematicItem* item = dynamic_cast<SchematicItem*>(hit);
        if (!item && hit->parentItem()) {
            item = dynamic_cast<SchematicItem*>(hit->parentItem());
        }

        if (item) {
            if (view()->undoStack()) {
                view()->undoStack()->push(new RemoveItemCommand(view()->scene(), {item}));
            } else {
                view()->scene()->removeItem(item);
                delete item;
            }
        }
    }
}

void SchematicScissorsTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBandActive && m_rubberBandItem && view()) {
        QPointF currentPos = view()->mapToScene(event->pos());
        QRectF rect = QRectF(m_rubberBandOrigin, currentPos).normalized();
        m_rubberBandItem->setRect(rect);
        event->accept();
        return;
    }
    SchematicTool::mouseMoveEvent(event);
}

void SchematicScissorsTool::mouseReleaseEvent(QMouseEvent* event) {
    if (m_rubberBandActive && m_rubberBandItem && view()) {
        QRectF rect = m_rubberBandItem->rect();
        
        QList<QGraphicsItem*> itemsInRect = view()->scene()->items(rect, Qt::IntersectsItemShape);
        QList<SchematicItem*> toRemove;
        for (QGraphicsItem* item : itemsInRect) {
            if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
                toRemove.append(sItem);
            }
        }
        
        if (!toRemove.isEmpty()) {
            if (view()->undoStack()) {
                view()->undoStack()->push(new RemoveItemCommand(view()->scene(), toRemove));
            } else {
                for (SchematicItem* item : toRemove) {
                    view()->scene()->removeItem(item);
                    delete item;
                }
            }
        }
        
        view()->scene()->removeItem(m_rubberBandItem);
        delete m_rubberBandItem;
        m_rubberBandItem = nullptr;
        m_rubberBandActive = false;
        event->accept();
    }
    SchematicTool::mouseReleaseEvent(event);
}
