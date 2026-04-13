#include "schematic_view.h"
#include "../../core/config_manager.h"
#include "schematic_tool_registry.h"
#include "schematic_editor.h"
#include "../tools/schematic_component_tool.h"
#include "theme_manager.h"
#include "schematic_item.h"
#include "../items/generic_component_item.h"
#include "../items/schematic_sheet_item.h"
#include "schematic_commands.h"
#include "../../symbols/items/symbol_primitive_item.h"
#include "../dialogs/voltage_source_ltspice_dialog.h"
#include "../dialogs/spice_directive_dialog.h"
#include "../dialogs/current_source_ltspice_dialog.h"
#include "../dialogs/batch_edit_dialog.h"
#include "../dialogs/cccs_properties_dialog.h"
#include "../dialogs/ccvs_properties_dialog.h"
#include "../dialogs/transmission_line_properties_dialog.h"
#include "../dialogs/bjt_properties_dialog.h"
#include "../dialogs/jfet_properties_dialog.h"
#include "../dialogs/mos_properties_dialog.h"
#include "../dialogs/mesfet_properties_dialog.h"
#include "../items/voltage_source_item.h"
#include "../items/current_source_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../tools/schematic_probe_tool.h"
#include "../items/wire_item.h"
#include "../analysis/net_manager.h"
#include "../ui/smart_probe_overlay.h"
#include "../ui/smart_probe_engine.h"
#include "../../simulator/core/sim_results.h"
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
#include <QLineF>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QKeySequence>
#include "flux/core/net_manager.h"
#include <QTimer>
#include <QGraphicsPixmapItem>
#include <QGraphicsEllipseItem>
#include <QJsonObject>

#include "../analysis/schematic_erc.h"
#include <QGraphicsEllipseItem>
#include <QToolTip>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include "../items/schematic_hint_item.h"

namespace {
SchematicItem* owningSchematicItem(QGraphicsItem* item) {
    QGraphicsItem* current = item;
    SchematicItem* lastSchematic = nullptr;
    while (current) {
        if (auto* schematic = dynamic_cast<SchematicItem*>(current)) {
            lastSchematic = schematic;
            if (!schematic->isSubItem()) {
                return schematic;
            }
        }
        current = current->parentItem();
    }
    return lastSchematic;
}

bool isProbeableSchematicComponent(SchematicItem* candidate) {
    if (!candidate || candidate->isSubItem()) return false;
    const auto type = candidate->itemType();
    return type != SchematicItem::WireType &&
           type != SchematicItem::LabelType &&
           type != SchematicItem::NetLabelType &&
           type != SchematicItem::JunctionType &&
           type != SchematicItem::BusType &&
           type != SchematicItem::NoConnectType &&
           type != SchematicItem::SpiceDirectiveType &&
           type != SchematicItem::SheetType;
}

SchematicItem* findProbeableComponentAt(SchematicView* view, const QPoint& viewPos, const QPointF& scenePos) {
    if (!view || !view->scene()) return nullptr;

    auto findFromItems = [](const QList<QGraphicsItem*>& items) -> SchematicItem* {
        for (QGraphicsItem* it : items) {
            SchematicItem* candidate = owningSchematicItem(it);
            if (isProbeableSchematicComponent(candidate)) {
                return candidate;
            }
        }
        return nullptr;
    };

    const QRect exactRect(viewPos.x() - 2, viewPos.y() - 2, 5, 5);
    if (SchematicItem* candidate = findFromItems(view->items(exactRect))) {
        return candidate;
    }

    // Symbol interiors are often hollow, so shape hits can miss even when the cursor is visibly over the body.
    constexpr qreal kBodyHitRadius = 8.0;
    const QRectF sceneRect(scenePos.x() - kBodyHitRadius,
                           scenePos.y() - kBodyHitRadius,
                           kBodyHitRadius * 2.0,
                           kBodyHitRadius * 2.0);
    return findFromItems(view->scene()->items(sceneRect,
                                              Qt::IntersectsItemBoundingRect,
                                              Qt::DescendingOrder,
                                              view->transform()));
}

QString findNearbyProbeNet(SchematicView* view, NetManager* netManager, const QPoint& viewPos, const QPointF& scenePos) {
    if (!view || !netManager) return {};

    auto resolveFromItems = [&](const QList<QGraphicsItem*>& items) -> QString {
        for (QGraphicsItem* it : items) {
            SchematicItem* candidate = owningSchematicItem(it);
            if (!candidate) continue;

            const auto type = candidate->itemType();
            if (type == SchematicItem::WireType) {
                if (auto* wire = dynamic_cast<WireItem*>(candidate)) {
                    QString net = wire->pinNet(0).trimmed();
                    if (!net.isEmpty()) return net;
                    net = wire->pinNet(1).trimmed();
                    if (!net.isEmpty()) return net;
                }
            }

            if (type != SchematicItem::WireType &&
                type != SchematicItem::LabelType &&
                type != SchematicItem::NetLabelType) {
                continue;
            }

            const QList<QPointF> pins = candidate->connectionPoints();
            for (int i = 0; i < pins.size(); ++i) {
                const QPointF pinScene = candidate->mapToScene(pins.at(i));
                QString net = netManager->findNetAtPoint(pinScene).trimmed();
                if (net.isEmpty()) {
                    net = candidate->pinNet(i).trimmed();
                }
                if (!net.isEmpty()) return net;
            }
        }
        return {};
    };

    QString net = netManager->findNetAtPoint(scenePos).trimmed();
    if (!net.isEmpty()) return net;

    const QRect exactRect(viewPos.x() - 3, viewPos.y() - 3, 7, 7);
    net = resolveFromItems(view->items(exactRect));
    if (!net.isEmpty()) return net;

    constexpr qreal kWireHitRadius = 10.0;
    const QRectF sceneRect(scenePos.x() - kWireHitRadius,
                           scenePos.y() - kWireHitRadius,
                           kWireHitRadius * 2.0,
                           kWireHitRadius * 2.0);
    net = resolveFromItems(view->scene()->items(sceneRect,
                                                Qt::IntersectsItemBoundingRect,
                                                Qt::DescendingOrder,
                                                view->transform()));
    return net;
}

QString findNearbyComponentPinNet(SchematicItem* component, NetManager* netManager, const QPointF& scenePos) {
    if (!component || !netManager) return {};

    constexpr qreal kPinCaptureRadius = 16.0;
    const QList<QPointF> pins = component->connectionPoints();
    qreal bestDistance = kPinCaptureRadius;
    QString bestNet;

    for (int i = 0; i < pins.size(); ++i) {
        const QPointF pinScene = component->mapToScene(pins.at(i));
        const qreal distance = QLineF(scenePos, pinScene).length();
        if (distance > bestDistance) continue;

        QString net = netManager->findNetAtPoint(pinScene).trimmed();
        if (net.isEmpty()) {
            net = component->pinNet(i).trimmed();
        }
        if (net.isEmpty()) continue;

        bestDistance = distance;
        bestNet = net;
    }

    return bestNet;
}
}

SchematicView::SchematicView(QWidget *parent)
    : QGraphicsView(parent),
    m_isPanning(false),
    m_zoomFactor(1.0),
    m_gridSize(15.0), // Increased from 10.0 to match larger symbols
    m_gridStyle(Lines),
    m_snapToGrid(true),
    m_snapToPin(true),
    m_showCrosshair(true),
    m_cursorScenePos(0, 0) {
    setLastSimResults(nullptr);
    clearSimulationResults();
    m_simNodeVoltages.clear();
    setRenderHint(QPainter::Antialiasing);
    
    m_autoScrollTimer = new QTimer(this);
    connect(m_autoScrollTimer, &QTimer::timeout, this, &SchematicView::handleAutoScroll);
    m_autoScrollDelta = QPoint(0, 0);

    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setOptimizationFlags(QGraphicsView::DontSavePainterState);
    
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

    // Initialize Smart Probe UI
    m_smartProbeOverlay = new SmartProbeOverlay(viewport());
}

SchematicView::~SchematicView() {
    if (m_currentTool) {
        m_currentTool->deactivate();
    }
}

void SchematicView::setCurrentTool(const QString& toolName) {
    auto& registry = SchematicToolRegistry::instance();
    SchematicTool* tool = registry.createTool(toolName);

    if (tool) {
        setCurrentTool(tool);
        Q_EMIT toolChanged(toolName);
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

    if (m_probeCursorVisible) {
        clearProbeCursorOverlay();
    }
    if (m_probeStartMarker) {
        clearProbeStartMarker();
        m_probeStartNet.clear();
    }

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
    Q_EMIT transformationChanged();
}

void SchematicView::mousePressEvent(QMouseEvent *event) {
    setFocus();
    if (m_currentTool) m_currentTool->ensureView(this);

    if (m_spacePressed && event->button() == Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        event->accept();
        return;
    }

    // Manual panning for Middle/Right button still uses m_isPanning
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastPanPoint = event->globalPosition().toPoint();
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

        // Apply selection filter
        if (sItem) {
            if (m_selectionFilter == SelectComponents && sItem->itemType() == SchematicItem::WireType) sItem = nullptr;
            else if (m_selectionFilter == SelectWires && sItem->itemType() != SchematicItem::WireType) sItem = nullptr;
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
        m_lastPanPoint = event->globalPosition().toPoint();
        m_panStartPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    m_probeClickActive = false;

    if (event->button() == Qt::LeftButton && m_currentTool && m_currentTool->name() == "Select") {
        QPointF scenePos = mapToScene(event->pos());
        
        // --- LTspice Style: Interactive Probing ---
        bool toolIsInstrument = m_currentTool && (m_currentTool->name().contains("meter", Qt::CaseInsensitive) || m_currentTool->name().contains("probe", Qt::CaseInsensitive));
        if (!toolIsInstrument && (m_simulationRunning || m_probingEnabled)) {
            // Detect wire/label under cursor
            bool isWireOrLabel = false;
            QString probedNet;
            
            QRect searchRect(event->pos().x() - 2, event->pos().y() - 2, 5, 5);
            QList<QGraphicsItem*> foundItems = items(searchRect);
            for (QGraphicsItem* it : foundItems) {
                SchematicItem* candidate = nullptr;
                QGraphicsItem* curr = it;
                while (curr) {
                    if (auto* s = dynamic_cast<SchematicItem*>(curr)) { candidate = s; break; }
                    curr = curr->parentItem();
                }
                if (candidate && (candidate->itemType() == SchematicItem::WireType || 
                                  candidate->itemType() == SchematicItem::LabelType ||
                                  candidate->itemType() == SchematicItem::NetLabelType)) {
                    isWireOrLabel = true;
                    break;
                }
            }
            if (m_netManager) {
                probedNet = findNearbyProbeNet(this, m_netManager, event->pos(), scenePos);
                if (!probedNet.isEmpty()) isWireOrLabel = true;
                if (probedNet.isEmpty() && isWireOrLabel) {
                    m_netManager->updateNets(scene());
                    probedNet = findNearbyProbeNet(this, m_netManager, event->pos(), scenePos);
                }
            }
            
            if (isWireOrLabel && !probedNet.isEmpty()) {
                bool ctrlHeld = event->modifiers() & Qt::ControlModifier;
                
                if (!m_probeStartNet.isEmpty()) {
                    // --- Second click: complete differential probe ---
                    if (probedNet != m_probeStartNet) {
                        // Different net: differential V(net1, net2)
                        Q_EMIT netProbed(QString("V(%1,%2)").arg(m_probeStartNet, probedNet));
                    } else {
                        // Same net: single probe
                        Q_EMIT netProbed(QString("V(%1)").arg(probedNet));
                    }
                    m_probeStartNet.clear();
                    clearProbeStartMarker();
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, scenePos);
                } else if (ctrlHeld) {
                    // --- First click with Ctrl: arm differential probe ---
                    m_probeStartNet = probedNet;
                    m_probeStartPos = scenePos;
                    showProbeStartMarker(scenePos);
                    // Switch to black probe to signal "waiting for second net"
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Current, scenePos);
                } else {
                    // --- Normal click: single probe ---
                    Q_EMIT netProbed(QString("V(%1)").arg(probedNet));
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, scenePos);
                }
                event->accept();
                return;
            } else {
                // Not over a wire/label, check for component body
                SchematicItem* compItem = findProbeableComponentAt(this, event->pos(), scenePos);
                const bool powerHeld = event->modifiers() & Qt::AltModifier;
                const bool ctrlHeld = event->modifiers() & Qt::ControlModifier;

                // Try voltage probe first (but not when Alt held = power probe intent)
                QString bodyNet;
                if (!powerHeld && m_netManager) {
                    bodyNet = findNearbyProbeNet(this, m_netManager, event->pos(), scenePos);
                    if (bodyNet.isEmpty() && compItem) {
                        bodyNet = findNearbyComponentPinNet(compItem, m_netManager, scenePos);
                    }
                    // If nets are stale, refresh once on-demand and retry the same nearby-net lookup.
                    if (bodyNet.isEmpty() && compItem) {
                        m_netManager->updateNets(scene());
                        bodyNet = findNearbyProbeNet(this, m_netManager, event->pos(), scenePos);
                        if (bodyNet.isEmpty()) {
                            bodyNet = findNearbyComponentPinNet(compItem, m_netManager, scenePos);
                        }
                    }
                }

                if (!bodyNet.isEmpty()) {
                    // Voltage probe on net at/under component body
                    if (!m_probeStartNet.isEmpty()) {
                        if (bodyNet != m_probeStartNet) {
                            Q_EMIT netProbed(QString("V(%1,%2)").arg(m_probeStartNet, bodyNet));
                        } else {
                            Q_EMIT netProbed(QString("V(%1)").arg(bodyNet));
                        }
                        m_probeStartNet.clear();
                        clearProbeStartMarker();
                    } else if (ctrlHeld) {
                        m_probeStartNet = bodyNet;
                        m_probeStartPos = scenePos;
                        showProbeStartMarker(scenePos);
                        setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Current, scenePos);
                        event->accept();
                        return;
                    } else {
                        Q_EMIT netProbed(QString("V(%1)").arg(bodyNet));
                    }
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, scenePos);
                    event->accept();
                    return;
                }

                // Fallback: current/power probe on the component itself
                if (compItem) {
                    QString ref = compItem->reference();
                    if (!ref.isEmpty()) {
                        Q_EMIT netProbed(QString("%1(%2)").arg(powerHeld ? "P" : "I", ref));
                        setProbeCursorOverlay(powerHeld ? SchematicProbeTool::ProbeKind::Power
                                                        : SchematicProbeTool::ProbeKind::Current,
                                              scenePos);
                        event->accept();
                        return;
                    }
                }

                if (!m_probeStartNet.isEmpty()) {
                    // Clicked empty space while armed: cancel
                    m_probeStartNet.clear();
                    clearProbeStartMarker();
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, scenePos);
                }
            }
        }

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
    if (m_currentTool) m_currentTool->ensureView(this);
    QPointF scenePos = mapToScene(event->pos());
    
    // Use tool-specific snapping if available, otherwise default grid snapping
    QPointF gridPos;
    if (m_currentTool) {
        gridPos = m_currentTool->snapPoint(scenePos);
    } else {
        gridPos = snapToGrid(scenePos);
    }
    
    Q_EMIT coordinatesChanged(gridPos);

    // Track cursor scene position for crosshair
    m_cursorScenePos = gridPos;
    if (m_showCrosshair) {
        viewport()->update();
    }

    if (m_spacePressed) {
        QGraphicsView::mouseMoveEvent(event);
        event->accept();
        return;
    }

    if (m_isPanning) {
        // Safety: ensure panning buttons are still held
        if (!(event->buttons() & (Qt::RightButton | Qt::MiddleButton))) {
            m_isPanning = false;
            if (m_currentTool) viewport()->setCursor(m_currentTool->cursor());
            else viewport()->setCursor(Qt::ArrowCursor);
        } else {
            QPoint delta = event->globalPosition().toPoint() - m_lastPanPoint;
            if (delta.manhattanLength() > 1) { // Filter out tiny jitters
                m_lastPanPoint = event->globalPosition().toPoint();
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
                verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            }
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

    // --- LTspice Style: Probe cursor BEFORE tool forwarding ---
    // This must run before the tool forwarding because the Select tool
    // always accepts the mouseMoveEvent, which would prevent this code
    // from executing if placed after.
    bool isSelectTool = (!m_currentTool || m_currentTool->name() == "Select");
    bool probeCursorActive = false;
    // Allow probe cursor if hovering normally OR if we are currently clicking a wire
    if (!m_isPanning && (m_probeClickActive || !(event->buttons() & Qt::LeftButton)) && isSelectTool) {
        // Hover highlight
        QGraphicsItem* item = itemAt(event->pos());
        SchematicItem* sItem = owningSchematicItem(item);
        updateHoverHighlight(sItem);

        // Probe cursor when simulation/probing is active
        if (m_simulationRunning || m_probingEnabled) {
            bool isWireOrLabel = false;
            
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

            // Fallback: try net lookup directly (more robust for thin wires)
            if (!isWireOrLabel && m_netManager) {
                const QPointF scenePos = mapToScene(event->pos());
                const QString netName = m_netManager->findNetAtPoint(scenePos);
                if (!netName.isEmpty()) isWireOrLabel = true;
            }

            SchematicItem* hoveredComponent = nullptr;
            if (!isWireOrLabel) {
                hoveredComponent = findProbeableComponentAt(this, event->pos(), scenePos);
                if (!hoveredComponent && isProbeableSchematicComponent(sItem)) {
                    hoveredComponent = sItem;
                }
            }

            if (isWireOrLabel) {
                // If differential mode is armed (Ctrl+Click waiting for second net),
                // show the black probe to clearly indicate second-net capture mode
                if (!m_probeStartNet.isEmpty()) {
                    QString currentNet;
                    if (m_netManager) currentNet = m_netManager->findNetAtPoint(mapToScene(event->pos()));
                    if (!currentNet.isEmpty() && currentNet != m_probeStartNet) {
                        setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Current, mapToScene(event->pos()));
                    } else {
                        setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, mapToScene(event->pos()));
                    }
                } else {
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, mapToScene(event->pos()));
                }
                probeCursorActive = true;
            } else {
                // Not over a wire, check for component body (Current Probe)
                if (hoveredComponent && !hoveredComponent->reference().trimmed().isEmpty()) {
                    const bool powerHeld = event->modifiers() & Qt::AltModifier;
                    setProbeCursorOverlay(powerHeld ? SchematicProbeTool::ProbeKind::Power
                                                    : SchematicProbeTool::ProbeKind::Current,
                                          mapToScene(event->pos()));
                    probeCursorActive = true;
                } else if (!m_probeStartNet.isEmpty()) {
                    // Still show black probe in "armed" mode so user knows they need to click a wire
                    setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Current, mapToScene(event->pos()));
                    probeCursorActive = true;
                } else {
                    clearProbeCursorOverlay();
                }
            }
        }
    } else if (!m_isPanning && isSelectTool && !(event->buttons() & Qt::LeftButton)) {
        clearHoverHighlights();
    }

    // --- Viora Smart Probe (the new AI logic) ---
    if (m_hasLastSimResults && m_smartProbeEngine && isSelectTool && !(event->buttons() & Qt::LeftButton)) {
        QString hoveredNet;
        if (m_netManager) {
            hoveredNet = m_netManager->findNetAtPoint(scenePos);
        }
        
        if (!hoveredNet.isEmpty()) {
            // Find connected components for context
            QString context;
            if (m_netManager) {
                QList<SchematicItem*> conns = m_netManager->getItemsForNet(hoveredNet);
                QStringList componentRefs;
                for (auto* item : conns) {
                    if (!item->reference().isEmpty()) {
                        componentRefs.append(item->reference());
                    }
                }
                if (!componentRefs.isEmpty()) {
                    context = componentRefs.join(", ");
                }
            }
            
            // Trigger Smart Probe logic (Follow mouse or handle hide grace period)
            m_smartProbeEngine->probe(hoveredNet, m_lastSimResults, context, event->pos() + QPoint(26, 20));
        } else {
            m_smartProbeEngine->probe("", m_lastSimResults, "", event->pos());
        }
    } else if (m_smartProbeEngine) {
        m_smartProbeEngine->probe("", m_lastSimResults, "", event->pos());
    }

    // Heatmap / On-Canvas Results Tooltip
    if (m_heatmapEnabled && (!m_simNodeVoltages.isEmpty() || !m_simBranchCurrents.isEmpty()) && !m_isPanning && isSelectTool) {
        QString tooltipText;
        if (m_netManager) {
            QString netName = m_netManager->findNetAtPoint(scenePos);
            if (!netName.isEmpty() && m_simNodeVoltages.contains(netName)) {
                tooltipText = QString("Net: %1\nVoltage: %2 V").arg(netName).arg(m_simNodeVoltages[netName], 0, 'f', 3);
            }
        }
        if (tooltipText.isEmpty()) {
            QGraphicsItem* item = itemAt(event->pos());
            SchematicItem* sItem = owningSchematicItem(item);
            if (sItem && isProbeableSchematicComponent(sItem)) {
                QString ref = sItem->reference();
                if (!ref.isEmpty() && m_simBranchCurrents.contains(ref)) {
                    tooltipText = QString("Component: %1\nCurrent: %2 A").arg(ref).arg(m_simBranchCurrents[ref], 0, 'f', 3);
                }
            }
        }

        if (!tooltipText.isEmpty()) {
            QToolTip::showText(event->globalPosition().toPoint(), tooltipText, this);
        } else {
            QToolTip::hideText();
        }
    } else if (isSelectTool) {
        QToolTip::hideText();
    }

    if (!probeCursorActive && m_probeCursorVisible) {
        clearProbeCursorOverlay();
    }

    // Forward to current tool — but skip when probe cursor is shown
    // to prevent the Select tool's hover cue from resetting the cursor
    if (m_currentTool && !probeCursorActive) {
        m_currentTool->mouseMoveEvent(event);
        if (event->isAccepted()) {
            return;
        }
    }

    // Default behavior
    QGraphicsView::mouseMoveEvent(event);
}

void SchematicView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_currentTool) m_currentTool->ensureView(this);
    m_probeClickActive = false;

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
        if (m_spacePressed) {
            viewport()->setCursor(Qt::OpenHandCursor);
        } else if (m_currentTool) {
            viewport()->setCursor(m_currentTool->cursor());
        } else {
            viewport()->setCursor(Qt::ArrowCursor);
        }

        // If right click moved more than 5 pixels, it's a pan, don't show menu
        if (event->button() == Qt::RightButton) {
            if ((event->pos() - m_panStartPos).manhattanLength() > 10) {
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
    if (m_currentTool) m_currentTool->ensureView(this);
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
                Q_EMIT pageTitleBlockDoubleClicked();
                event->accept();
                return;
            }

            // Check if we clicked on a SymbolTextItem (component label like "M1" or "NMOS")
            using namespace Flux::Item;
            if (auto* symbolText = dynamic_cast<SymbolTextItem*>(item)) {
                // Find the parent GenericComponentItem
                if (auto* parentComp = dynamic_cast<GenericComponentItem*>(symbolText->parentItem())) {
                    // Determine if this is the reference label or value label.
                    // SymbolTextItem is not a QObject, so use model text tokens as heuristic.
                    const QString textToken = symbolText->model().data.value("text").toString();
                    const QString upper = textToken.toUpper();
                    const bool isReference = upper.contains("REFERENCE") || upper.contains("REF");
                    Q_EMIT componentLabelDoubleClicked(parentComp, isReference);
                    event->accept();
                    return;
                }
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
                    Q_EMIT itemSelectionDoubleClicked(selection);
                } else {
                    Q_EMIT itemDoubleClicked(target);
                }
                event->accept();
                return;
            }
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void SchematicView::keyPressEvent(QKeyEvent *event) {
    // Reserve placement transform keys for component placement tools.
    // Always consume H/Shift+V/Ctrl+R here so they never fall through to global shortcuts.
    if (m_currentTool && dynamic_cast<SchematicComponentTool*>(m_currentTool)) {
        const bool isBareH = (event->key() == Qt::Key_H &&
                              !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)));
        const bool isShiftV = (event->key() == Qt::Key_V &&
                               (event->modifiers() & Qt::ShiftModifier) &&
                               !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)));
        const bool isCtrlR = (event->key() == Qt::Key_R && (event->modifiers() & Qt::ControlModifier));
        if (isBareH || isShiftV || isCtrlR) {
            m_currentTool->keyPressEvent(event);
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_H && !event->isAutoRepeat()) {
        // Do not let the global Hand toggle steal `H` while placing parts.
        if (SchematicEditor* editor = qobject_cast<SchematicEditor*>(window())) {
            if (editor->isPlacementModeActive()) {
                // Placement mode key handling is owned by SchematicEditor::eventFilter.
                // Consume here as a safety net so it never toggles Hand/cancels placement.
                event->accept();
                return;
            } else {
                setHandToolActive(!m_handToolActive);
                event->accept();
            }
        } else {
            setHandToolActive(!m_handToolActive);
            event->accept();
        }
        if (event->isAccepted()) return;
    }

    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setDragMode(QGraphicsView::ScrollHandDrag);
        viewport()->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    if (m_currentTool) m_currentTool->ensureView(this);

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
            if (SchematicItem* si = owningSchematicItem(item)) {
                toRemove.append(si);
            }
        }
        
        if (!toRemove.isEmpty() && m_undoStack) {
            scene()->clearSelection();
            m_undoStack->push(new RemoveItemCommand(scene(), toRemove));
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape) {
        // Cancel placement mode (after paste/duplicate)
        if (SchematicEditor* editor = qobject_cast<SchematicEditor*>(window())) {
            if (editor->isPlacementModeActive()) {
                editor->cancelPlacementMode();
                event->accept();
                return;
            }
        }
    }

    if (event->key() == Qt::Key_Escape) {
        // Cancel armed differential probe mode
        if (!m_probeStartNet.isEmpty()) {
            m_probeStartNet.clear();
            clearProbeStartMarker();
            setProbeCursorOverlay(SchematicProbeTool::ProbeKind::Voltage, mapToScene(mapFromGlobal(QCursor::pos())));
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
    if (event->modifiers() == Qt::NoModifier) {
        QWidget* fw = QApplication::focusWidget();
        if (!qobject_cast<QLineEdit*>(fw) && !qobject_cast<QTextEdit*>(fw)) {
            if (event->key() == Qt::Key_L) {
                setCurrentTool("Inductor");
                event->accept();
                return;
            }
            if (event->key() == Qt::Key_S) {
                setCurrentTool("Switch");
                event->accept();
                return;
            }
        }
    }

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
    if (event->key() == Qt::Key_Escape && m_currentTool && m_currentTool->name() != "Select") {
        setCurrentTool("Select");
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void SchematicView::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        m_isPanning = false;
        setDragMode(QGraphicsView::NoDrag);
        if (m_currentTool) viewport()->setCursor(m_currentTool->cursor());
        else viewport()->unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

QString SchematicView::getNextReference(const QString& prefix) {
    if (!scene()) return prefix + "1";
    
    QSet<int> usedIndices;
    const QList<QGraphicsItem*> items = scene()->items();
    for (QGraphicsItem* item : items) {
        SchematicItem* schItem = dynamic_cast<SchematicItem*>(item);
        if (schItem && schItem->reference().startsWith(prefix)) {
            QString ref = schItem->reference().trimmed();
            const int unitSep = ref.indexOf(':');
            if (unitSep >= 0) ref = ref.left(unitSep).trimmed();
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
#include "../dialogs/led_properties_dialog.h"
#include "../dialogs/switch_properties_dialog.h"
#include "../dialogs/voltage_controlled_switch_dialog.h"
#include "../dialogs/behavioral_current_source_dialog.h"
#include "../dialogs/diode_model_picker_dialog.h"
#include "../dialogs/diode_properties_dialog.h"
#include "flux/symbols/symbol_library.h"
#include "../items/voltage_controlled_switch_item.h"
#include "../items/behavioral_current_source_item.h"
#include "../items/switch_item.h"
#include "../items/generic_component_item.h"
#include <QGuiApplication>

void SchematicView::setLastSimResults(const SimResults* results) {
    if (results) {
        m_lastSimResults = *results;
        m_hasLastSimResults = true;
    } else {
        m_hasLastSimResults = false;
    }
    if (m_smartProbeEngine) {
        m_smartProbeEngine->clearCache();
    }
}

void SchematicView::setGeminiPanel(GeminiPanel* panel) {
    if (!m_smartProbeEngine && panel && m_smartProbeOverlay) {
        m_smartProbeEngine = new SmartProbeEngine(panel, m_smartProbeOverlay, this);
    }
}

void SchematicView::contextMenuEvent(QContextMenuEvent *event) {
    if (m_currentTool) m_currentTool->ensureView(this);
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

    // (selection logic above ensures targetSItem is selected)

    QList<SchematicItem*> selectedItems;
    for (auto* it : scene()->selectedItems()) {
        if (auto* s = dynamic_cast<SchematicItem*>(it)) selectedItems.append(s);
    }

    // Use registry to build menu
    QMenu menu(this);
    if (ThemeManager::theme()) {
        menu.setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    auto actions = SchematicMenuRegistry::instance().getActions(selectedItems);
    
    bool hasWireInfoAction = false;
    if (actions.empty()) {
        // Standard fallback if no actions registered (unlikely with global actions)
        menu.addAction("No Actions Available")->setEnabled(false);
    } else {
        for (const auto& actionData : actions) {
            if (actionData.isSeparator) {
                menu.addSeparator();
                continue;
            }
            if (actionData.label == "Info...") hasWireInfoAction = true;
            
            QAction* act = menu.addAction(actionData.icon, actionData.label);
            if (!actionData.shortcut.isEmpty()) {
                act->setShortcut(actionData.shortcut);
                act->setToolTip(actionData.label + " (" + actionData.shortcut.toString() + ")");
            }
            act->setEnabled(actionData.isEnabled(selectedItems));
            
            connect(act, &QAction::triggered, this, [this, actionData, selectedItems]() {
                actionData.handler(this, selectedItems);
            });
        }
    }

    auto showWireInfo = [this](WireItem* wire) {
        if (!wire || !scene()) return;
        const QList<QPointF> pts = wire->points();
        if (pts.isEmpty()) return;

        NetManager* nm = netManager();
        QString netName = "N/A";
        QString netEndName;
        QPointF startScene = wire->mapToScene(pts.first());
        QPointF endScene = wire->mapToScene(pts.last());
        if (nm) {
            nm->updateNets(scene());
            netName = nm->findNetAtPoint(startScene);
            netEndName = nm->findNetAtPoint(endScene);
            if (netName.isEmpty()) netName = "N/A";
            if (!netEndName.isEmpty() && netEndName != netName) {
                netName = QString("%1 -> %2").arg(netName, netEndName);
            }
        }

        double totalLength = 0.0;
        for (int i = 0; i < pts.size() - 1; ++i) {
            const QPointF a = wire->mapToScene(pts[i]);
            const QPointF b = wire->mapToScene(pts[i + 1]);
            totalLength += QLineF(a, b).length();
        }

        QString wireType = (wire->wireType() == WireItem::PowerWire) ? "Power" : "Signal";
        QString styleStr = "Solid";
        if (wire->pen().style() == Qt::DashLine) styleStr = "Dash";
        else if (wire->pen().style() == Qt::DotLine) styleStr = "Dot";

        QString info = QString(
            "Net: %1\n"
            "Type: %2\n"
            "Points: %3\n"
            "Segments: %4\n"
            "Length (scene units): %5\n"
            "Start: (%6, %7)\n"
            "End: (%8, %9)\n"
            "Junctions: %10\n"
            "Jump-overs: %11\n"
            "Line width: %12\n"
            "Line style: %13")
            .arg(netName)
            .arg(wireType)
            .arg(pts.size())
            .arg(qMax(0, pts.size() - 1))
            .arg(QString::number(totalLength, 'f', 2))
            .arg(QString::number(startScene.x(), 'f', 2))
            .arg(QString::number(startScene.y(), 'f', 2))
            .arg(QString::number(endScene.x(), 'f', 2))
            .arg(QString::number(endScene.y(), 'f', 2))
            .arg(wire->junctions().size())
            .arg(wire->jumpOvers().size())
            .arg(QString::number(wire->pen().widthF(), 'f', 2))
            .arg(styleStr);

        QMessageBox::information(this, "Wire Info", info);
    };

    const bool singleWireSelected =
        (selectedItems.size() == 1 && selectedItems.first()->itemType() == SchematicItem::WireType);
    if (singleWireSelected && !hasWireInfoAction) {
        auto* wire = dynamic_cast<WireItem*>(selectedItems.first());
        if (wire) {
            menu.addSeparator();
            menu.addAction("Info...", [showWireInfo, wire]() { showWireInfo(wire); });
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
            // Open BatchEditDialog for multiple selected components
            if (selectedItems.size() > 1) {
                // Filter to only items with editable values
                QList<SchematicItem*> editableItems;
                for (auto* item : selectedItems) {
                    if (item && !item->value().isEmpty()) {
                        editableItems.append(item);
                    }
                }
                
                if (!editableItems.isEmpty()) {
                    BatchEditDialog dialog(editableItems, this);
                    dialog.exec();
                }
            } else if (!selectedItems.isEmpty()) {
                // Single item - use existing double-click behavior
                Q_EMIT itemDoubleClicked(selectedItems.first());
            }
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

void SchematicView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    Q_EMIT transformationChanged();
}
void SchematicView::drawForeground(QPainter *painter, const QRectF &rect) {
    if (m_heatmapEnabled) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QList<QGraphicsItem*> allItems = scene()->items(rect);
        for (QGraphicsItem* item : allItems) {
            SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
            if (sItem && !sItem->isSubItem() && sItem->powerDissipation() > 1e-9) {
                double p = sItem->powerDissipation();
                // Scale radius: 1mW -> ~20px, 1W -> ~60px
                double radius = 20.0 + std::log10(p * 1e6 + 1.0) * 5.0; 
                // Color: Green (low) -> Yellow -> Red (high, >100mW)
                double heatFactor = std::min(1.0, p / 0.1); 
                QColor heatColor = QColor::fromHsvF((1.0 - heatFactor) * 0.33, 1.0, 1.0, 0.4);
                
                QPointF center = sItem->sceneBoundingRect().center();
                QRadialGradient grad(center, radius);
                grad.setColorAt(0, heatColor);
                grad.setColorAt(1, Qt::transparent);
                
                painter->setBrush(grad);
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(center, radius, radius);
                
                // Optional: draw power value text
                painter->setPen(QColor(255, 255, 255, 180));
                painter->setFont(QFont("Inter", 8));
                painter->drawText(center + QPointF(radius*0.7, -radius*0.7), 
                                 QString("%1W").arg(p, 0, 'g', 3));
            }
        }
        painter->restore();
    }

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

void SchematicView::setSimulationResults(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents) {
    m_simNodeVoltages = nodeVoltages;
    m_simBranchCurrents = branchCurrents;
}

void SchematicView::clearSimulationResults() {
    m_simNodeVoltages.clear();
    m_simBranchCurrents.clear();
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

void SchematicView::ensureProbeCursorItem() {
    if (m_probeCursorItem || !scene()) return;
    m_probeCursorItem = new QGraphicsPixmapItem();
    m_probeCursorItem->setZValue(2000000.0);
    m_probeCursorItem->setAcceptedMouseButtons(Qt::NoButton);
    m_probeCursorItem->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    m_probeCursorItem->setVisible(false);
    scene()->addItem(m_probeCursorItem);
}

void SchematicView::applyProbeCursorPixmap(SchematicProbeTool::ProbeKind kind) {
    if (!m_probeCursorItem) return;
    const auto art = SchematicProbeTool::createProbeCursorArt(kind);
    m_probeCursorItem->setPixmap(art.pixmap);
    m_probeCursorItem->setOffset(-art.hotspot);
}

void SchematicView::setProbeCursorOverlay(SchematicProbeTool::ProbeKind kind, const QPointF& scenePos) {
    if (!scene()) return;
    ensureProbeCursorItem();
    if (!m_probeCursorItem) return;

    if (!m_probeCursorVisible || kind != m_probeCursorKind) {
        m_probeCursorKind = kind;
        applyProbeCursorPixmap(kind);
    }

    m_probeCursorItem->setPos(scenePos);
    m_probeCursorItem->setVisible(true);
    m_probeCursorVisible = true;

    setCursor(Qt::BlankCursor);
    viewport()->setCursor(Qt::BlankCursor);
}

void SchematicView::clearProbeCursorOverlay() {
    if (m_probeCursorItem) m_probeCursorItem->setVisible(false);
    m_probeCursorVisible = false;

    if (m_currentTool) {
        setCursor(m_currentTool->cursor());
        viewport()->setCursor(m_currentTool->cursor());
    } else {
        setCursor(Qt::ArrowCursor);
        viewport()->setCursor(Qt::ArrowCursor);
    }
}

void SchematicView::showProbeStartMarker(const QPointF& scenePos) {
    if (!scene()) return;
    if (!m_probeStartMarker) {
        m_probeStartMarker = new QGraphicsEllipseItem(-5, -5, 10, 10);
        m_probeStartMarker->setPen(QPen(Qt::white, 1));
        m_probeStartMarker->setBrush(QColor(31, 41, 55));
        m_probeStartMarker->setZValue(1500);
        m_probeStartMarker->setAcceptedMouseButtons(Qt::NoButton);
        scene()->addItem(m_probeStartMarker);
    }
    m_probeStartMarker->setPos(scenePos);
    m_probeStartMarker->setVisible(true);
}

void SchematicView::clearProbeStartMarker() {
    if (!m_probeStartMarker) return;
    scene()->removeItem(m_probeStartMarker);
    delete m_probeStartMarker;
    m_probeStartMarker = nullptr;
}

void SchematicView::setHandToolActive(bool active) {
    if (m_handToolActive == active) return;
    m_handToolActive = active;
    
    if (m_handToolActive) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        viewport()->setCursor(Qt::OpenHandCursor);
    } else {
        setDragMode(QGraphicsView::NoDrag);
        if (m_currentTool) viewport()->setCursor(m_currentTool->cursor());
        else viewport()->unsetCursor();
    }
    
    Q_EMIT toolChanged(m_handToolActive ? "Hand" : (m_currentTool ? m_currentTool->name() : "Select"));
}
void SchematicView::addHint(const QString& text, const QPointF& pos, const QString& ref) {
    if (!scene()) return;

    QPointF hintPos = pos;
    // If a reference is provided, try to find the component and anchor to it
    if (!ref.isEmpty()) {
        for (QGraphicsItem* item : scene()->items()) {
            if (auto* sItem = dynamic_cast<SchematicItem*>(item)) {
                if (sItem->reference() == ref && !sItem->isSubItem()) {
                    // Anchor to the top-right of the component's bounding rect
                    QRectF br = sItem->boundingRect();
                    hintPos = sItem->scenePos() + QPointF(br.right(), br.top());
                    break;
                }
            }
        }
    }

    SchematicHintItem* hint = new SchematicHintItem(text);
    hint->setPos(hintPos);
    hint->setZValue(2000); // Always on top
    scene()->addItem(hint);
}

void SchematicView::clearHints() {
    if (!scene()) return;
    for (QGraphicsItem* item : scene()->items()) {
        if (dynamic_cast<SchematicHintItem*>(item)) {
            scene()->removeItem(item);
            delete item;
        }
    }
}

void SchematicView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-viospice-snippet") ||
        event->mimeData()->hasFormat("application/x-viospice-netlist")) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        QGraphicsView::dragEnterEvent(event);
    }
}

void SchematicView::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    QPointF scenePos = mapToScene(event->position().toPoint());
    
    if (mime->hasFormat("application/x-viospice-snippet")) {
        QString json = QString::fromUtf8(mime->data("application/x-viospice-snippet"));
        Q_EMIT snippetDropped(json, scenePos);
        event->acceptProposedAction();
    } else if (mime->hasFormat("application/x-viospice-netlist")) {
        QString netlistText = QString::fromUtf8(mime->data("application/x-viospice-netlist"));
        Q_EMIT netlistDropped(netlistText, scenePos);
        event->acceptProposedAction();
    } else {
        QGraphicsView::dropEvent(event);
    }
}
