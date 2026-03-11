#include "schematic_view.h"
#include "../../core/config_manager.h"
#include "schematic_tool_registry.h"
#include "theme_manager.h"
#include "schematic_item.h"
#include "../items/generic_component_item.h"
#include "../items/schematic_sheet_item.h"
#include "schematic_commands.h"
#include "../dialogs/voltage_source_ltspice_dialog.h"
#include "../items/voltage_source_item.h"
#include "../tools/schematic_probe_tool.h"
#include <QPainter>
#include <QWheelEvent>
#include <QDebug>
#include <QGraphicsItem>
#include "schematic_page_item.h"
#include <QMenu>
#include <QAction>
#include <QWidgetAction>
#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QKeySequence>
#include "flux/core/net_manager.h"
#include <QTimer>

#include "../analysis/schematic_erc.h"
#include <QGraphicsEllipseItem>
#include <QToolTip>

SchematicView::SchematicView(QWidget *parent)
    : QGraphicsView(parent),
    m_isPanning(false),
    m_zoomFactor(1.0),
    m_gridSize(15.0), // Increased from 10.0 to match larger symbols
    m_gridStyle(Lines),
    m_snapToGrid(true),
    m_snapToPin(true),
    m_showCrosshair(true),
    m_cursorScenePos(0, 0),
    m_undoStack(nullptr),
    m_currentTool(nullptr) {
    setRenderHint(QPainter::Antialiasing);
    
    m_autoScrollTimer = new QTimer(this);
    connect(m_autoScrollTimer, &QTimer::timeout, this, &SchematicView::handleAutoScroll);
    m_autoScrollDelta = QPoint(0, 0);

    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setMouseTracking(true);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);
    setCacheMode(QGraphicsView::CacheBackground);
    
    // Enable NoIndex optimization for very large scenes
    if (scene()) scene()->setItemIndexMethod(QGraphicsScene::NoIndex);

    // Enable scroll bars for navigating large schematics
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Apply professional dark scrollbar styling
    setStyleSheet(
        "QScrollBar:vertical {"
        "    background: #1e1e1e;"
        "    width: 12px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: #3c3c3c;"
        "    min-height: 20px;"
        "    border-radius: 6px;"
        "    margin: 2px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: #4c4c4c;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    height: 0px;"
        "}"
        "QScrollBar:horizontal {"
        "    background: #1e1e1e;"
        "    height: 12px;"
        "    margin: 0px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "    background: #3c3c3c;"
        "    min-width: 20px;"
        "    border-radius: 6px;"
        "    margin: 2px;"
        "}"
        "QScrollBar::handle:horizontal:hover {"
        "    background: #4c4c4c;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "    width: 0px;"
        "}"
    );

    // Set default tool to Select
    setCurrentTool("Select");
}

SchematicView::~SchematicView() {
    // Tool will be cleaned up by registry
}

void SchematicView::setCurrentTool(const QString& toolName) {
    auto& registry = SchematicToolRegistry::instance();
    SchematicTool* tool = registry.createTool(toolName);

    if (tool) {
        setCurrentTool(tool);
        emit toolChanged(toolName);
        qDebug() << "Switched to schematic tool:" << toolName;
    } else {
        qWarning() << "Failed to create schematic tool:" << toolName;
    }
}

void SchematicView::setCurrentTool(SchematicTool* tool) {
    if (m_currentTool) {
        m_currentTool->deactivate();
        clearHoverHighlights(); // Safety: don't hold pointers to potentially deleted preview items
    }

    m_currentTool = tool;

    // Clear any cursor override left by previous tool (view + viewport can differ).
    unsetCursor();
    viewport()->unsetCursor();

    if (m_currentTool) {
        m_currentTool->activate(this);
        setCursor(m_currentTool->cursor());
        viewport()->setCursor(m_currentTool->cursor());
    } else {
        setCursor(Qt::ArrowCursor);
        viewport()->setCursor(Qt::ArrowCursor);
    }
}

QStringList SchematicView::availableTools() const {
    return SchematicToolRegistry::instance().registeredTools();
}

void SchematicView::setGridSize(double size) {
    if (size <= 0.0) return;
    if (qFuzzyCompare(m_gridSize, size)) return;
    m_gridSize = size;
    resetCachedContent();
    viewport()->update();
}

void SchematicView::setGridStyle(GridStyle style) {
    if (m_gridStyle == style) return;
    m_gridStyle = style;
    resetCachedContent();
    viewport()->update();
}

QPointF SchematicView::snapToGrid(QPointF pos) {
    if (!m_snapToGrid) return pos;
    double x = std::round(pos.x() / m_gridSize) * m_gridSize;
    double y = std::round(pos.y() / m_gridSize) * m_gridSize;
    return QPointF(x, y);
}

SchematicView::SnapResult SchematicView::snapToPin(QPointF scenePos, qreal radius, QGraphicsItem* excludeItem) const {
    SnapResult result;
    result.point = scenePos;
    result.type = GridSnap;

    if (!m_snapToPin || !scene()) return result;

    const qreal radiusSq = radius * radius;
    qreal closestDistSq = radiusSq;

    QRectF searchRect(scenePos.x() - radius, scenePos.y() - radius, radius * 2.0, radius * 2.0);
    const QList<QGraphicsItem*> items = scene()->items(searchRect, Qt::IntersectsItemBoundingRect);

    for (QGraphicsItem* gItem : items) {
        if (!gItem || !gItem->isVisible() || gItem == excludeItem) continue;

        SchematicItem* sItem = dynamic_cast<SchematicItem*>(gItem);
        if (!sItem || sItem->itemType() == SchematicItem::WireType) continue;

        const QList<QPointF> pins = sItem->connectionPoints();
        if (pins.isEmpty()) continue;

        const QTransform tx = sItem->sceneTransform();
        const bool translateOnly = tx.type() <= QTransform::TxTranslate;
        const QPointF offset(tx.dx(), tx.dy());

        for (int i = 0; i < pins.size(); ++i) {
            const QPointF pinScene = translateOnly ? (pins[i] + offset) : tx.map(pins[i]);
            const qreal dx = scenePos.x() - pinScene.x();
            const qreal dy = scenePos.y() - pinScene.y();
            const qreal distSq = dx * dx + dy * dy;

            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                result.point = pinScene;
                result.type = PinSnap;
                result.item = sItem;
                result.pinIndex = i;
            }
        }
    }

    return result;
}

SchematicView::SnapResult SchematicView::snapToGridOrPin(QPointF scenePos, qreal pinRadius, QGraphicsItem* excludeItem) {
    // Try pin snap first — it has highest priority
    SnapResult pinResult = snapToPin(scenePos, pinRadius, excludeItem);
    if (pinResult.type == PinSnap) {
        return pinResult;
    }

    // Fall back to grid snap
    SnapResult gridResult;
    gridResult.point = snapToGrid(scenePos);
    gridResult.type = GridSnap;
    return gridResult;
}

void SchematicView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zooming logic (Ctrl + Wheel)
        const double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            scale(scaleFactor, scaleFactor);
            m_zoomFactor *= scaleFactor;
        } else {
            scale(1.0 / scaleFactor, 1.0 / scaleFactor);
            m_zoomFactor /= scaleFactor;
        }
        event->accept();
    } else if (event->modifiers() & Qt::ShiftModifier) {
        // Horizontal Scrolling (Shift + Wheel)
        int delta = event->angleDelta().y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
        event->accept();
    } else {
        // Vertical Scrolling (Normal Wheel)
        int delta = event->angleDelta().y();
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta);
        event->accept();
    }
}

void SchematicView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        m_panStartPos = event->pos(); // Track start to differentiate from menu click
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        // 1. Let active non-select tool consume right-click first (e.g., finish wire).
        if (m_currentTool && m_currentTool->name() != "Select") {
            m_currentTool->mousePressEvent(event);
            if (event->isAccepted()) {
                return;
            }
            setCurrentTool("Select");
            event->accept();
            return;
        }

        // 2. Identify the selectable SchematicItem under the mouse.
        QGraphicsItem* item = itemAt(event->pos());
        SchematicItem* sItem = nullptr;
        QGraphicsItem* curr = item;
        while (curr) {
            if (auto* candidate = dynamic_cast<SchematicItem*>(curr)) {
                if (candidate->flags() & QGraphicsItem::ItemIsSelectable) {
                    sItem = candidate;
                }
            }
            curr = curr->parentItem();
        }

        // 3. Interaction logic
        if (sItem) {
            // Right-click on item: Select it and prepare for context menu.
            // We do NOT start panning here to avoid "stuck" movement during menu interaction.
            if (!sItem->isSelected()) {
                if (!(event->modifiers() & Qt::ControlModifier))
                    scene()->clearSelection();
                sItem->setSelected(true);
            }
            m_panStartPos = event->pos();
            // We don't set m_isPanning = true yet; let the user drag a bit before panning if started on item?
            // Actually, best EDA practice: Right-drag UNCONDITIONALLY pans if started on background,
            // but if started on item, it usually waits for a threshold or just lets menu handle it.
            // For now, let's allow panning if the tool doesn't consume it.
        }

        // Start panning state
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        m_panStartPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Clear hover highlights on click to avoid artifacts
    clearHoverHighlights();

    if (event->button() == Qt::LeftButton && m_currentTool && m_currentTool->name() == "Select") {
        QPointF scenePos = mapToScene(event->pos());
        QGraphicsItem* item = scene()->itemAt(scenePos, transform());
        if (item) {
            SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
            if (sItem && sItem->isInteractive()) {
                sItem->onInteractivePress(scenePos);
                sItem->onInteractiveClick(scenePos);
                event->accept();
                return;
            }
        }
    }

    // Forward to current tool
    if (m_currentTool) {
        m_currentTool->mousePressEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    // Default behavior
    QGraphicsView::mousePressEvent(event);
}

void SchematicView::mouseMoveEvent(QMouseEvent *event) {
    QPointF scenePos = mapToScene(event->pos());
    
    // Use tool-specific snapping if available, otherwise default grid snapping
    QPointF gridPos;
    if (m_currentTool) {
        gridPos = m_currentTool->snapPoint(scenePos);
    } else {
        gridPos = snapToGrid(scenePos);
    }
    
    emit coordinatesChanged(gridPos);

    // Track cursor scene position for crosshair
    m_cursorScenePos = gridPos;
    if (m_showCrosshair) {
        viewport()->update();
    }

    if (m_isPanning) {
        // Safety: ensure panning buttons are still held
        if (!(event->buttons() & (Qt::RightButton | Qt::MiddleButton))) {
            m_isPanning = false;
            if (m_currentTool) setCursor(m_currentTool->cursor());
            else setCursor(Qt::ArrowCursor);
        } else {
            QPoint delta = event->pos() - m_lastPanPoint;
            m_lastPanPoint = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
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

    // Real-time Hover Highlighting & Automatic Probing
    if (!m_isPanning && !(event->buttons() & Qt::LeftButton)) {
        QGraphicsItem* item = itemAt(event->pos());
        SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
        updateHoverHighlight(sItem);

        // --- LTspice Style: Automatic Voltage Probe on hover during simulation ---
        if (m_simulationRunning) {
            bool isWireOrLabel = false;
            
            // Use a small search area (5x5 pixels) for easier wire hitting with the probe
            QRect searchRect(event->pos().x() - 2, event->pos().y() - 2, 5, 5);
            QList<QGraphicsItem*> foundItems = items(searchRect);
            
            for (QGraphicsItem* it : foundItems) {
                SchematicItem* candidate = nullptr;
                QGraphicsItem* curr = it;
                while (curr) {
                    if (auto* s = dynamic_cast<SchematicItem*>(curr)) {
                        candidate = s;
                        break;
                    }
                    curr = curr->parentItem();
                }
                
                if (candidate) {
                    if (candidate->itemType() == SchematicItem::WireType || 
                        candidate->itemType() == SchematicItem::LabelType ||
                        candidate->itemType() == SchematicItem::NetLabelType) {
                        isWireOrLabel = true;
                        break;
                    }
                }
            }

            if (isWireOrLabel) {
                viewport()->setCursor(SchematicProbeTool::createProbeCursor(SchematicProbeTool::ProbeKind::Voltage));
            } else {
                viewport()->setCursor(m_currentTool ? m_currentTool->cursor() : Qt::ArrowCursor);
            }
        }
    } else if (event->buttons() & Qt::LeftButton) {
        // During drag, skip highlighting entirely for performance
    } else {
        clearHoverHighlights();
    }

    // Default behavior
    QGraphicsView::mouseMoveEvent(event);
}

void SchematicView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        stopAutoScroll();
        if (m_currentTool && m_currentTool->name() == "Select") {
            QPointF scenePos = mapToScene(event->pos());
            QGraphicsItem* item = scene()->itemAt(scenePos, transform());
            if (item) {
                SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
                if (sItem && sItem->isInteractive()) {
                    sItem->onInteractiveRelease(scenePos);
                }
            }
        }
    }

    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        bool wasPanning = m_isPanning;
        m_isPanning = false;
        
        // Restore tool cursor
        if (m_currentTool) {
            setCursor(m_currentTool->cursor());
        } else {
            setCursor(Qt::ArrowCursor);
        }

        // If right click moved more than 5 pixels, it's a pan, don't show menu
        if (event->button() == Qt::RightButton) {
            if ((event->pos() - m_panStartPos).manhattanLength() > 5) {
                event->accept();
                return;
            }
        } else {
            event->accept();
            return;
        }
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

void SchematicView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (m_currentTool) {
        // Only treat tool handling as authoritative when the tool explicitly accepts the event.
        event->setAccepted(false);
        m_currentTool->mouseDoubleClickEvent(event);
        if (event->isAccepted()) return;
    }

    if (event->button() == Qt::LeftButton) {
        QGraphicsItem* item = itemAt(event->pos());
        if (item) {
            if (auto* pageItem = dynamic_cast<SchematicPageItem*>(item)) {
                emit pageTitleBlockDoubleClicked();
                event->accept();
                return;
            }

            // Find the appropriate SchematicItem to handle the click
            SchematicItem* target = nullptr;
            QGraphicsItem* curr = item;
            
            while (curr) {
                if (auto* sItem = dynamic_cast<SchematicItem*>(curr)) {
                    // If we hit a label (sub-item), that's our target (Phase 3.4)
                    if (sItem->itemType() == SchematicItem::LabelType) {
                        target = sItem;
                        break;
                    }
                    
                    // If we hit a non-sub-item (like a component body), that's our target
                    if (!sItem->isSubItem()) {
                        target = sItem;
                        break;
                    }
                    
                    // Otherwise, keep track of this sItem but keep bubbling up
                    // to find the main component body if this sub-item isn't a label.
                    if (!target) target = sItem;
                }
                curr = curr->parentItem();
            }

            if (target) {
                // Bulk Edit Check (Phase 5.4 - Feature Flag)
                bool bulkEnabled = ConfigManager::instance().isFeatureEnabled("ux.smart_properties_v2", true);
                
                QList<SchematicItem*> selection;
                for (auto* it : scene()->selectedItems()) {
                    if (auto* s = dynamic_cast<SchematicItem*>(it)) selection.append(s);
                }

                if (bulkEnabled && selection.size() > 1 && selection.contains(target)) {
                    emit itemSelectionDoubleClicked(selection);
                } else {
                    emit itemDoubleClicked(target);
                }
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void SchematicView::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Undo)) {
        if (m_undoStack && m_undoStack->canUndo()) {
            m_undoStack->undo();
            event->accept();
            return;
        }
    } else if (event->matches(QKeySequence::Redo)) {
        if (m_undoStack && m_undoStack->canRedo()) {
            m_undoStack->redo();
            event->accept();
            return;
        }
    }

    // Smart Context-Aware Delete: Hover takes precedence
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        // 1. Check if we are hovering over a specific item
        if (m_lastHoveredItem && m_undoStack) {
            m_undoStack->push(new RemoveItemCommand(scene(), {m_lastHoveredItem}));
            m_lastHoveredItem = nullptr;
            clearHoverHighlights();
            event->accept();
            return;
        }
        
        // 2. Fallback: Delete selected items
        QList<QGraphicsItem*> selected = scene()->selectedItems();
        QList<SchematicItem*> toRemove;
        for (QGraphicsItem* item : selected) {
            if (SchematicItem* si = dynamic_cast<SchematicItem*>(item)) {
                toRemove.append(si);
            }
        }
        
        if (!toRemove.isEmpty() && m_undoStack) {
            m_undoStack->push(new RemoveItemCommand(scene(), toRemove));
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape && m_currentTool) {
        // Special case: Esc finishes polygon if we have points
        QMouseEvent fakeEvent(QEvent::MouseButtonPress, mapFromGlobal(QCursor::pos()), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        m_currentTool->mousePressEvent(&fakeEvent);
        if (fakeEvent.isAccepted()) {
            event->accept();
            return;
        }
    }

    if (m_currentTool) {
        m_currentTool->keyPressEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }
    
    // Global hotkey handling (if not consumed by tool)
    if (event->key() == Qt::Key_Space) {
        // Rotate selection if tool didn't consume it
        if (!scene()->selectedItems().isEmpty()) {
            QList<SchematicItem*> itemsToRotate;
            for (auto item : scene()->selectedItems()) {
                SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
                if (sItem) {
                    itemsToRotate.append(sItem);
                }
            }
            
            if (!itemsToRotate.isEmpty()) {
                if (m_undoStack) {
                    RotateItemCommand* cmd = new RotateItemCommand(scene(), itemsToRotate, 90.0);
                    m_undoStack->push(cmd);
                } else {
                    for (auto item : itemsToRotate) {
                        item->setRotation(item->rotation() + 90);
                    }
                }
            }
            event->accept();
            return;
        }
    }

    // Default behavior (Propagate to parent for shortcuts)
    QGraphicsView::keyPressEvent(event);
}

QString SchematicView::getNextReference(const QString& prefix) {
    if (!scene()) return prefix + "1";
    
    QSet<int> usedIndices;
    const QList<QGraphicsItem*> items = scene()->items();
    for (QGraphicsItem* item : items) {
        SchematicItem* schItem = dynamic_cast<SchematicItem*>(item);
        if (schItem && schItem->reference().startsWith(prefix)) {
            QString ref = schItem->reference();
            if (ref.length() > prefix.length()) {
                QString numStr = ref.mid(prefix.length());
                bool ok;
                int num = numStr.toInt(&ok);
                if (ok) {
                    usedIndices.insert(num);
                }
            }
        }
    }
    
    int index = 1;
    while (usedIndices.contains(index)) {
        index++;
    }
    
    return prefix + QString::number(index);
}

#include "schematic_menu_registry.h"
#include "../../core/config_manager.h"
#include <QGuiApplication>

void SchematicView::contextMenuEvent(QContextMenuEvent *event) {
    // Only forward to specific tools that need it (not Select tool)
    if (m_currentTool && m_currentTool->name() != "Select") {
        QMouseEvent fakeEvent(QEvent::MouseButtonPress, event->pos(), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        m_currentTool->mousePressEvent(&fakeEvent);
        if (fakeEvent.isAccepted()) {
            return;
        }
    }

    QGraphicsItem* item = itemAt(event->pos());
    SchematicItem* targetSItem = nullptr;
    
    // Find the highest selectable SchematicItem parent (top-down)
    QGraphicsItem* curr = item;
    while (curr) {
        if (auto* candidate = dynamic_cast<SchematicItem*>(curr)) {
            if (candidate->flags() & QGraphicsItem::ItemIsSelectable) {
                targetSItem = candidate;
            }
        }
        curr = curr->parentItem();
    }

    // Ensure item is selected if we clicked on one
    if (targetSItem && !targetSItem->isSelected()) {
        if (!(QGuiApplication::keyboardModifiers() & Qt::ControlModifier))
            scene()->clearSelection();
        targetSItem->setSelected(true);
    }

    // SPECIAL: Direct dialog for Voltage Sources on right-click (LTspice style)
    if (targetSItem && targetSItem->itemType() == SchematicItem::VoltageSourceType) {
        if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(targetSItem)) {
            VoltageSourceLTSpiceDialog dlg(vsrc, m_undoStack, scene(), this);
            dlg.exec();
            return;
        }
    }

    QList<SchematicItem*> selectedItems;
    for (auto* it : scene()->selectedItems()) {
        if (auto* s = dynamic_cast<SchematicItem*>(it)) selectedItems.append(s);
    }

    // Use registry to build menu
    QMenu menu(this);
    if (ThemeManager::theme()) {
        menu.setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    auto actions = (ConfigManager::instance().isFeatureEnabled("ux.context_menu_v2", true))
                   ? SchematicMenuRegistry::instance().getActions(selectedItems)
                   : std::vector<ContextAction>();
    
    if (actions.empty()) {
        // Fallback for empty canvas if no global actions registered
        menu.addAction(QIcon(":/icons/view_fit.svg"), "Zoom Fit (F)", [this](){ 
            if (scene() && scene()->itemsBoundingRect().isValid()) {
                fitInView(scene()->itemsBoundingRect().adjusted(-50,-50,50,50), Qt::KeepAspectRatio);
            }
        });
        menu.addAction("Select All", [this](){
            if (scene()) {
                for (auto* it : scene()->items()) it->setSelected(true);
            }
        });
    } else {
        for (const auto& actionData : actions) {
            if (actionData.isSeparator) {
                menu.addSeparator();
                continue;
            }
            
            QAction* act = menu.addAction(actionData.icon, actionData.label);
            if (!actionData.shortcut.isEmpty()) act->setShortcut(actionData.shortcut);
            act->setEnabled(actionData.isEnabled(selectedItems));
            
            connect(act, &QAction::triggered, this, [this, actionData, selectedItems]() {
                actionData.handler(this, selectedItems);
            });
        }
    }

    // Add standard properties action at the bottom if items are selected
    if (!selectedItems.isEmpty()) {
        menu.addSeparator();
        
        // --- Smart Context Menu: Inline Property Editor ---
        if (selectedItems.size() == 1) {
            auto* si = selectedItems.first();
            bool supportsQuickEdit = (si->itemType() == SchematicItem::ResistorType ||
                                    si->itemType() == SchematicItem::CapacitorType ||
                                    si->itemType() == SchematicItem::InductorType ||
                                    si->itemType() == SchematicItem::VoltageSourceType);
            
            if (supportsQuickEdit) {
                QWidgetAction* wa = new QWidgetAction(&menu);
                QWidget* container = new QWidget();
                QHBoxLayout* layout = new QHBoxLayout(container);
                layout->setContentsMargins(10, 5, 10, 5);
                layout->setSpacing(10);
                
                QString labelText = "Value:";
                if (si->itemType() == SchematicItem::VoltageSourceType) labelText = "Value (DC/Expr):";
                
                QLabel* label = new QLabel(labelText);
                label->setStyleSheet("color: #888; font-weight: bold; font-size: 10px;");
                layout->addWidget(label);
                
                QLineEdit* edit = new QLineEdit(si->value());
                edit->setFixedWidth(120);
                edit->setStyleSheet("background: #1e1e1e; color: #fff; border: 1px solid #444; padding: 3px; border-radius: 2px;");
                layout->addWidget(edit);
                
                connect(edit, &QLineEdit::returnPressed, [this, &menu, si, edit]() {
                    QString newVal = edit->text();
                    if (newVal != si->value()) {
                        if (m_undoStack) {
                            m_undoStack->push(new ChangePropertyCommand(scene(), si, "value", si->value(), newVal));
                        } else {
                            si->setValue(newVal);
                            si->update();
                        }
                    }
                    menu.close();
                });
                
                wa->setDefaultWidget(container);
                menu.addAction(wa);
                menu.addSeparator();
                
                // Focus the line edit after the menu is shown
                QTimer::singleShot(10, [edit]() { edit->setFocus(); edit->selectAll(); });
            }
        }

        menu.addAction("Edit All Properties...", [this, selectedItems]() {
            emit itemDoubleClicked(selectedItems.first());
        });
    }

    menu.exec(event->globalPos());
}

void SchematicView::drawBackground(QPainter *painter, const QRectF &rect) {
    PCBTheme* theme = ThemeManager::theme();
    QColor bgColor = theme ? theme->canvasBackground() : QColor(30, 30, 30);
    painter->fillRect(rect, bgColor);

    if (m_gridSize <= 0) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    const qreal left = rect.left();
    const qreal top = rect.top();
    const qreal right = rect.right();
    const qreal bottom = rect.bottom();

    qreal startX = std::floor(left / m_gridSize) * m_gridSize;
    qreal startY = std::floor(top / m_gridSize) * m_gridSize;

    // Use light, crisp colors for the dark theme visibility
    QColor subColor = theme ? theme->gridSecondary() : QColor(120, 120, 130);
    subColor.setAlpha(110); // Increased visibility for dark theme
    
    QColor mainColor = theme ? theme->gridPrimary() : QColor(180, 180, 190);
    mainColor.setAlpha(200); // Much more visible major grid

    const qreal viewScale = std::abs(transform().m11());
    const qreal gridPixelSpacing = m_gridSize * viewScale;
    const bool drawMinorGrid = gridPixelSpacing >= 6.0;

    if (m_gridStyle == Points) {
        // Point Grid Rendering
        painter->setPen(QPen(subColor, 0));
        for (qreal x = startX; x <= right; x += m_gridSize) {
            for (qreal y = startY; y <= bottom; y += m_gridSize) {
                // Determine if it's a major or minor point
                bool isMajor = (std::fmod(std::abs(x), m_gridSize * 10.0) < 0.1) && 
                               (std::fmod(std::abs(y), m_gridSize * 10.0) < 0.1);
                
                if (isMajor) {
                    painter->setPen(QPen(mainColor, 0));
                    painter->drawPoint(QPointF(x, y));
                    painter->setPen(QPen(subColor, 0));
                } else if (drawMinorGrid) {
                    painter->drawPoint(QPointF(x, y));
                }
            }
        }
    } else {
        // Line Grid Rendering
        qreal majorSize = m_gridSize * 10.0;
        
        // 1. Draw Sub-grid
        painter->setPen(QPen(subColor, 0));
        if (drawMinorGrid) {
            for (qreal x = startX; x <= right; x += m_gridSize) {
                if (std::fmod(std::abs(x), majorSize) > 0.1)
                    painter->drawLine(QPointF(x, top), QPointF(x, bottom));
            }
            for (qreal y = startY; y <= bottom; y += m_gridSize) {
                if (std::fmod(std::abs(y), majorSize) > 0.1)
                    painter->drawLine(QPointF(left, y), QPointF(right, y));
            }
        }

        // 2. Draw Primary Grid
        painter->setPen(QPen(mainColor, 0));
        qreal startMajorX = std::floor(left / majorSize) * majorSize;
        qreal startMajorY = std::floor(top / majorSize) * majorSize;

        for (qreal x = startMajorX; x <= right; x += majorSize) {
            painter->drawLine(QPointF(x, top), QPointF(x, bottom));
        }
        for (qreal y = startMajorY; y <= bottom; y += majorSize) {
            painter->drawLine(QPointF(left, y), QPointF(right, y));
        }
    }

    painter->restore();
}
    void SchematicView::drawForeground(QPainter *painter, const QRectF &rect) {
        if (!m_showCrosshair) return;
    
        painter->save();
        
            // Set up crosshair pen
            PCBTheme* theme = ThemeManager::theme();
            QColor crossColor = theme ? theme->accentColor() : QColor(99, 102, 241);
            crossColor.setAlpha(120); // Semi-transparent
                // Use cosmetic pen so thickness doesn't change with zoom
        QPen pen(crossColor, 0);
        painter->setPen(pen);
        
        // Draw horizontal line
        painter->drawLine(QPointF(rect.left(), m_cursorScenePos.y()), 
                          QPointF(rect.right(), m_cursorScenePos.y()));
        
        // Draw vertical line
        painter->drawLine(QPointF(m_cursorScenePos.x(), rect.top()), 
                          QPointF(m_cursorScenePos.x(), rect.bottom()));
        
        painter->restore();
    }
    
    void SchematicView::updateHoverHighlight(SchematicItem* item) {
    
    if (item == m_lastHoveredItem) return;
    
    clearHoverHighlights();
    m_lastHoveredItem = item;
    
    if (item && m_netManager) {
        // We only highlight if not already selected (as selection highlight is persistent)
        if (!item->isSelected()) {
            m_hoverHighlightedPins = m_netManager->traceNetWithPins(item);
            
            for (auto it = m_hoverHighlightedPins.begin(); it != m_hoverHighlightedPins.end(); ++it) {
                SchematicItem* ni = it.key();
                const QSet<int>& pins = it.value();
                
                // Don't overwrite selection highlights
                if (ni->isHighlighted()) continue;

                if (ni->itemType() == SchematicItem::WireType || 
                    ni->itemType() == SchematicItem::BusType ||
                    ni->itemType() == SchematicItem::JunctionType ||
                    ni->itemType() == SchematicItem::LabelType ||
                    ni->itemType() == SchematicItem::NetLabelType ||
                    ni->itemType() == SchematicItem::HierarchicalPortType) {
                    ni->setHighlighted(true);
                } else {
                    for (int pinIdx : pins) {
                        ni->setHighlightedPin(pinIdx, true);
                    }
                }
            }
        }
    }
}

void SchematicView::clearHoverHighlights() {
    for (auto it = m_hoverHighlightedPins.begin(); it != m_hoverHighlightedPins.end(); ++it) {
        SchematicItem* ni = it.key();
        // Check if it's currently selected to avoid clearing persistent highlights
        // Safety: verify item is still valid and in scene
        if (ni && ni->scene() && !ni->isSelected()) {
            ni->setHighlighted(false);
            ni->clearHighlightedPins();
        }
    }
    m_hoverHighlightedPins.clear();
    m_lastHoveredItem = nullptr;
}

void SchematicView::showLiveERCMarkers(const QList<ERCViolation>& violations) {
    clearLiveERCMarkers();
    if (!scene()) return;

    for (const auto& v : violations) {
        QColor color = (v.severity == ERCViolation::Error || v.severity == ERCViolation::Critical) 
                       ? QColor(255, 0, 0, 150) 
                       : QColor(255, 165, 0, 150);
        
        auto* halo = new QGraphicsEllipseItem(-10, -10, 20, 20);
        halo->setPos(v.position);
        halo->setPen(QPen(color.lighter(), 1));
        
        QRadialGradient grad(0, 0, 10);
        grad.setColorAt(0, color);
        grad.setColorAt(1, Qt::transparent);
        halo->setBrush(grad);
        
        halo->setZValue(2500); // Above items
        halo->setToolTip(v.message);
        
        scene()->addItem(halo);
        m_liveErcMarkers.append(halo);
    }
}

void SchematicView::clearLiveERCMarkers() {
    for (auto* m : m_liveErcMarkers) {
        if (m->scene()) scene()->removeItem(m);
        delete m;
    }
    m_liveErcMarkers.clear();
}

void SchematicView::handleAutoScroll() {
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + m_autoScrollDelta.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + m_autoScrollDelta.y());

    // Update tool to handle new mouse position after scroll
    QPoint pos = mapFromGlobal(QCursor::pos());
    QMouseEvent mouseEvent(QEvent::MouseMove, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseMoveEvent(&mouseEvent);
}

void SchematicView::updateAutoScroll(const QPoint& pos) {
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

void SchematicView::stopAutoScroll() {
    if (m_autoScrollTimer->isActive()) m_autoScrollTimer->stop();
    m_autoScrollDelta = QPoint(0, 0);
}
