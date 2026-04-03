#include "pcb_view.h"
#include "pcb_tool_registry.h"
#include <QPainter>
#include <QGraphicsEllipseItem>
#include <QLineF>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QKeyEvent>
#include <QSet>
#include "theme_manager.h"
#include "pcb_item.h"
#include "pcb_commands.h"
#include <QLineF>
#include <QTimer>

PCBView::PCBView(QWidget *parent)
    : QGraphicsView(parent),
    m_isPanning(false),
    m_zoomFactor(1.0),
    m_gridSize(2.54), // 0.1 inch
    m_currentTool(nullptr),
    m_undoStack(nullptr),
    m_snapToGrid(true) {
    setRenderHint(QPainter::Antialiasing);

    m_autoScrollTimer = new QTimer(this);
    connect(m_autoScrollTimer, &QTimer::timeout, this, &PCBView::handleAutoScroll);
    m_autoScrollDelta = QPoint(0, 0);

    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);
    setCacheMode(QGraphicsView::CacheBackground);
    setBackgroundBrush(QBrush(QColor(30, 30, 35)));
    setMouseTracking(true);

    // Set default tool to Select
    setCurrentTool("Select");

    // Improve initial zoom level for mm units (1 scene unit = 1mm)
    // 10.0 scale means 1mm = 10px, which is a comfortable default.
    scale(10.0, 10.0);
    m_zoomFactor = 10.0;
}

PCBView::~PCBView() {
    // Tool will be cleaned up by registry
}

void PCBView::setCurrentTool(const QString& toolName) {
    auto& registry = PCBToolRegistry::instance();
    PCBTool* tool = registry.createTool(toolName);

    if (tool) {
        setCurrentTool(tool);
        emit toolChanged(toolName);
        qDebug() << "Switched to tool:" << toolName;
    } else {
        qWarning() << "Failed to create tool:" << toolName;
    }
}

void PCBView::setCurrentTool(PCBTool* tool) {
    if (m_currentTool) {
        m_currentTool->deactivate();
    }

    m_currentTool = tool;

    if (m_currentTool) {
        m_currentTool->activate(this);
        setCursor(m_currentTool->cursor());
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

QStringList PCBView::availableTools() const {
    return PCBToolRegistry::instance().registeredTools();
}

bool PCBView::isSnappedToPad(QPointF scenePos) {
    QList<QGraphicsItem*> items = scene()->items(scenePos);
    for (auto* item : items) {
        QGraphicsItem* current = item;
        while (current) {
            int t = current->type();
            if (t == QGraphicsItem::UserType + 1 || t == QGraphicsItem::UserType + 2) {
                if (current->scenePos() == scenePos) return true;
            }
            current = current->parentItem();
        }
    }
    return false;
}

QPointF PCBView::snapToGrid(QPointF pos) {
    // 1. Magnetic Snapping to Pad/Via centers (Smart Routing)
    // This "pulls" the cursor towards pad/via centers for precise routing.
    double magneticRadius = 2.0; // mm
    
    QList<QGraphicsItem*> items = scene()->items(QRectF(pos.x() - magneticRadius, pos.y() - magneticRadius, 
                                                       magneticRadius * 2, magneticRadius * 2));
    
    QGraphicsItem* closestSnapPoint = nullptr;
    double minDistance = magneticRadius + 0.1;

    for (auto* item : items) {
        // Look for Pads or Vias (ItemType::PadType or ItemType::ViaType)
        QGraphicsItem* current = item;
        while (current) {
            int t = current->type();
            if (t == QGraphicsItem::UserType + 1 || t == QGraphicsItem::UserType + 2) { 
                double dist = QLineF(pos, current->scenePos()).length();
                if (dist < minDistance) {
                    minDistance = dist;
                    closestSnapPoint = current;
                }
                break;
            }
            current = current->parentItem();
        }
    }

    if (closestSnapPoint) {
        // Magnetic pull! Return the exact center of the pad/via
        return closestSnapPoint->scenePos();
    }

    // 2. Standard Grid Snapping fallback
    if (!m_snapToGrid) return pos;

    double x = std::round(pos.x() / m_gridSize) * m_gridSize;
    double y = std::round(pos.y() / m_gridSize) * m_gridSize;
    return QPointF(x, y);
}

void PCBView::wheelEvent(QWheelEvent *event) {
    const double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
        m_zoomFactor *= scaleFactor;
    } else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
        m_zoomFactor /= scaleFactor;
    }
}

void PCBView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Forward to current tool FIRST
    if (m_currentTool) {
        m_currentTool->mousePressEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    // Consistent quick-exit behavior: right-click returns to Select tool,
    // but only if the active tool did not consume the event itself.
    if (event->button() == Qt::RightButton) {
        if (m_currentTool && m_currentTool->name() != "Select") {
            setCurrentTool("Select");
        }
        event->accept();
        return;
    }

    // Default behavior
    QGraphicsView::mousePressEvent(event);
}

void PCBView::mouseMoveEvent(QMouseEvent *event) {
    QPointF scenePos = mapToScene(event->pos());
    QPointF gridPos = snapToGrid(scenePos);
    emit coordinatesChanged(gridPos);

    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    // Auto-scroll during drag
    if (event->buttons() & Qt::LeftButton) {
        updateAutoScroll(event->pos());
    } else {
        stopAutoScroll();
    }

    // Forward to current tool
    if (m_currentTool) {
        m_currentTool->mouseMoveEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    // Default behavior
    QGraphicsView::mouseMoveEvent(event);
}

void PCBView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        stopAutoScroll();
    }

    if (event->button() == Qt::MiddleButton) {
        m_isPanning = false;
        if (m_currentTool) {
            setCursor(m_currentTool->cursor());
        } else {
            setCursor(Qt::ArrowCursor);
        }
        event->accept();
        return;
    }

    // Forward to current tool
    if (m_currentTool) {
        m_currentTool->mouseReleaseEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    // Default behavior
    QGraphicsView::mouseReleaseEvent(event);
}

void PCBView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (m_currentTool) {
        m_currentTool->mouseDoubleClickEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void PCBView::keyPressEvent(QKeyEvent *event) {
    // Forward to current tool FIRST
    if (m_currentTool) {
        m_currentTool->keyPressEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    auto trySwitchTool = [&](const QString& name) {
        if (PCBToolRegistry::instance().isToolRegistered(name)) {
            setCurrentTool(name);
            emit toolChanged(name);
            event->accept();
            return true;
        }
        return false;
    };

    auto deleteSelection = [&]() -> bool {
        if (!scene()) return false;
        QList<QGraphicsItem*> selected = scene()->selectedItems();
        if (selected.isEmpty()) return false;

        QSet<PCBItem*> itemsToDelete;
        for (QGraphicsItem* it : selected) {
            PCBItem* pcbItem = nullptr;
            QGraphicsItem* current = it;
            while (current) {
                if (PCBItem* candidate = dynamic_cast<PCBItem*>(current)) {
                    if (candidate->flags() & QGraphicsItem::ItemIsSelectable) {
                        pcbItem = candidate;
                    }
                }
                current = current->parentItem();
            }

            if (pcbItem && !pcbItem->isLocked()) {
                itemsToDelete.insert(pcbItem);
            } else if (!pcbItem) {
                if (it->flags() & QGraphicsItem::ItemIsSelectable) {
                    scene()->removeItem(it);
                    delete it;
                }
            }
        }

        QList<PCBItem*> finalItems;
        for (PCBItem* item : itemsToDelete) {
            bool parentInSet = false;
            QGraphicsItem* p = item->parentItem();
            while (p) {
                if (PCBItem* pi = dynamic_cast<PCBItem*>(p)) {
                    if (itemsToDelete.contains(pi)) {
                        parentInSet = true;
                        break;
                    }
                }
                p = p->parentItem();
            }
            if (!parentInSet) finalItems.append(item);
        }

        if (finalItems.isEmpty()) return false;

        scene()->clearSelection();

        if (m_undoStack) {
            m_undoStack->push(new PCBRemoveItemCommand(scene(), finalItems));
        } else {
            for (PCBItem* p : finalItems) {
                scene()->removeItem(p);
                delete p;
            }
        }
        
        emit selectionChanged();
        viewport()->update();
        event->accept();
        return true;
    };

    // Single-key hotkeys (schematic-style workflow).
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (event->modifiers() == Qt::NoModifier) {
            if (deleteSelection()) return;
        }
        break;
    case Qt::Key_W: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Trace")) return; break;
    case Qt::Key_E: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Erase")) return; break;
    case Qt::Key_D: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Diff Pair")) return; break;
    case Qt::Key_V: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Via")) return; break;
    case Qt::Key_Z: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Zoom Area")) return; break;
    case Qt::Key_M: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Measure")) return; break;
    case Qt::Key_P: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Pad")) return; break;
    case Qt::Key_R: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Rectangle")) return; break;
    case Qt::Key_O: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Polygon Pour")) return; break;
    case Qt::Key_T: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Length Tuning")) return; break;
    case Qt::Key_C: if (event->modifiers() == Qt::NoModifier && trySwitchTool("Component")) return; break;
    default: break;
    }

    if (event->key() == Qt::Key_Escape) {
        if (m_currentTool && m_currentTool->name() != "Select") {
            setCurrentTool("Select");
            emit toolChanged("Select");
            event->accept();
            return;
        }
    }

    QGraphicsView::keyPressEvent(event);
}

void PCBView::contextMenuEvent(QContextMenuEvent *event) {
    QGraphicsItem* item = itemAt(event->pos());
    if (!item) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    // Find the selectable PCBItem parent
    PCBItem* pcbItem = nullptr;
    QGraphicsItem* current = item;
    while (current) {
        PCBItem* candidate = dynamic_cast<PCBItem*>(current);
        if (candidate && (candidate->flags() & QGraphicsItem::ItemIsSelectable)) {
            pcbItem = candidate;
        }
        current = current->parentItem();
    }

    if (!pcbItem) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    // Ensure item is selected
    if (!pcbItem->isSelected()) {
        scene()->clearSelection();
        pcbItem->setSelected(true);
    }

    QList<QGraphicsItem*> selected = scene()->selectedItems();
    QList<PCBItem*> selectedPCBItems;
    for (auto* it : selected) {
        if (auto* p = dynamic_cast<PCBItem*>(it)) selectedPCBItems.append(p);
    }

    if (selectedPCBItems.isEmpty()) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    if (ThemeManager::theme()) {
        menu.setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    QAction* rotateAct = menu.addAction("Rotate (90°)");
    rotateAct->setShortcut(QKeySequence("R"));
    
    QAction* deleteAct = menu.addAction("Delete");
    deleteAct->setShortcut(QKeySequence::Delete);

    menu.addSeparator();
    QAction* flipAct = menu.addAction("Flip (Switch Layer)");
    flipAct->setShortcut(QKeySequence("F"));

    QAction* lockAct = menu.addAction("Lock");
    lockAct->setCheckable(true);
    // Check if all selected items are locked
    bool allLocked = true;
    for (auto* it : selectedPCBItems) {
        if (!it->isLocked()) {
            allLocked = false;
            break;
        }
    }
    lockAct->setChecked(allLocked);
    lockAct->setText(allLocked ? "Unlock" : "Lock");

    menu.addSeparator();
    QAction* zoomSelAct = menu.addAction(QIcon::fromTheme("zoom-in"), "Zoom to Selection");
    
    menu.addSeparator();
    QAction* propsAct = menu.addAction("Properties...");

    QAction* selectedAct = menu.exec(event->globalPos());

    if (selectedAct == rotateAct) {
        if (m_undoStack) {
            m_undoStack->push(new PCBRotateItemCommand(scene(), selectedPCBItems, 90));
        } else {
            for (auto* it : selectedPCBItems) {
                if (!it->isLocked()) it->setRotation(it->rotation() + 90);
            }
        }
    } else if (selectedAct == deleteAct) {
        QSet<PCBItem*> itemsToDelete;
        for (auto* it : selected) {
            PCBItem* pcbItem = nullptr;
            QGraphicsItem* current = it;
            while (current) {
                if (PCBItem* candidate = dynamic_cast<PCBItem*>(current)) {
                    if (candidate->flags() & QGraphicsItem::ItemIsSelectable) {
                        pcbItem = candidate;
                    }
                }
                current = current->parentItem();
            }
            if (pcbItem && !pcbItem->isLocked()) {
                itemsToDelete.insert(pcbItem);
            }
        }

        QList<PCBItem*> finalItems;
        for (PCBItem* item : itemsToDelete) {
            bool parentInSet = false;
            QGraphicsItem* p = item->parentItem();
            while (p) {
                if (PCBItem* pi = dynamic_cast<PCBItem*>(p)) {
                    if (itemsToDelete.contains(pi)) {
                        parentInSet = true;
                        break;
                    }
                }
                p = p->parentItem();
            }
            if (!parentInSet) finalItems.append(item);
        }

        if (!finalItems.isEmpty()) {
            scene()->clearSelection();
            if (m_undoStack) {
                m_undoStack->push(new PCBRemoveItemCommand(scene(), finalItems));
            } else {
                for (auto* it : finalItems) {
                    scene()->removeItem(it);
                    delete it;
                }
            }
            emit selectionChanged();
            viewport()->update();
        }
    } else if (selectedAct == flipAct) {
        if (m_undoStack) {
            m_undoStack->push(new PCBFlipItemCommand(scene(), selectedPCBItems));
        } else {
            for (auto* it : selectedPCBItems) {
                if (!it->isLocked()) {
                    it->setLayer(it->layer() == 0 ? 1 : 0);
                }
            }
        }
    } else if (selectedAct == lockAct) {
        if (m_undoStack) {
            m_undoStack->push(new PCBLockItemCommand(scene(), selectedPCBItems, !allLocked));
        } else {
            bool newLockedState = !allLocked;
            for (auto* item : selectedPCBItems) {
                item->setLocked(newLockedState);
            }
        }
    } else if (selectedAct == zoomSelAct) {
        fitInView(scene()->selectionArea().boundingRect(), Qt::KeepAspectRatio);
        // Zoom out slightly for padding
        scale(0.8, 0.8);
    }
}

void PCBView::drawBackground(QPainter *painter, const QRectF &rect) {
    // Fill background
    QColor bgColor(30, 30, 30); // Dark background
    painter->fillRect(rect, bgColor);

    // Don't draw grid if it's too fine for the current zoom level (performance)
    double viewScale = transform().m11();
    if (m_gridSize * viewScale < 5.0) {
        return; 
    }

    // Draw grid points (Dot Grid)
    QColor gridColor(60, 60, 60);
    painter->setPen(Qt::NoPen);
    painter->setBrush(gridColor);

    // Calculate dot size - proportional to grid but with a minimum visual size
    qreal dotSize = m_gridSize * 0.1;
    if (dotSize * viewScale < 1.5) {
        dotSize = 1.5 / viewScale; // Ensure at least 1.5 pixels on screen
    }
    
    // Calculate start points aligned to grid
    qreal left = std::floor(rect.left() / m_gridSize) * m_gridSize;
    qreal top = std::floor(rect.top() / m_gridSize) * m_gridSize;

    // Draw dots
    for (qreal x = left; x < rect.right(); x += m_gridSize) {
        for (qreal y = top; y < rect.bottom(); y += m_gridSize) {
            painter->drawEllipse(QPointF(x, y), dotSize/2, dotSize/2);
        }
    }
}

void PCBView::handleAutoScroll() {
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + m_autoScrollDelta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + m_autoScrollDelta.y());

    // Update tool to handle new mouse position after scroll
    QPoint pos = mapFromGlobal(QCursor::pos());
    QMouseEvent mouseEvent(QEvent::MouseMove, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseMoveEvent(&mouseEvent);
}

void PCBView::updateAutoScroll(const QPoint& pos) {
    const int margin = 20;
    const int scrollSpeed = 15;
    m_autoScrollDelta = QPoint(0, 0);

    if (pos.x() < margin) m_autoScrollDelta.setX(-scrollSpeed);
    else if (pos.x() > viewport()->width() - margin) m_autoScrollDelta.setX(scrollSpeed);

    if (pos.y() < margin) m_autoScrollDelta.setY(-scrollSpeed);
    else if (pos.y() > viewport()->height() - margin) m_autoScrollDelta.setY(scrollSpeed);

    if (m_autoScrollDelta != QPoint(0, 0)) {
        if (!m_autoScrollTimer->isActive()) m_autoScrollTimer->start(30);
    } else {
        stopAutoScroll();
    }
}

void PCBView::stopAutoScroll() {
    if (m_autoScrollTimer->isActive()) m_autoScrollTimer->stop();
    m_autoScrollDelta = QPoint(0, 0);
}
