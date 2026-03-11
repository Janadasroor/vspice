#include "schematic_wire_tool.h"
#include "schematic_view.h"
#include "wire_item.h"
#include "schematic_commands.h"
#include "schematic_connectivity.h"
#include "../analysis/wire_router.h"
#include "flux/core/config_manager.h"
#include <QUndoStack>
#include <QKeyEvent>
#include <QDebug>
#include <QPixmap>
#include <QGraphicsEllipseItem>
#include <QLabel>
#include <QStringList>

SchematicWireTool::SchematicWireTool(QObject* parent)
    : SchematicTool("Wire", parent)
    , m_currentWire(nullptr)
    , m_isDrawing(false)
    , m_hFirst(true) {
    
    // Load persisted defaults
    auto& config = ConfigManager::instance();
    m_width = config.toolProperty("Wire", "Width", 2.25).toDouble();
    m_color = config.toolProperty("Wire", "Color", "#10b981").toString();
    m_style = config.toolProperty("Wire", "Line Style", "Solid").toString();
}

QMap<QString, QVariant> SchematicWireTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Width"] = m_width;
    props["Color"] = m_color;
    props["Line Style"] = m_style;
    return props;
}

void SchematicWireTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Width") m_width = value.toDouble();
    else if (name == "Color") m_color = value.toString();
    else if (name == "Line Style") m_style = value.toString();

    // Persist changes immediately
    ConfigManager::instance().setToolProperty("Wire", name, value);
}

QCursor SchematicWireTool::cursor() const {
    QPixmap pixmap(":/icons/cursor_pencil.svg");
    if (!pixmap.isNull()) {
        return QCursor(pixmap.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation), 0, 23);
    }
    return QCursor(Qt::CrossCursor);
}

void SchematicWireTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
    if (!m_router && view && view->scene()) {
        m_router = std::make_unique<WireRouter>(view->scene());
    }
    if (!m_previewThrottle.isValid()) m_previewThrottle.start();
    ensureModeBadge();
    updateModeBadge();
    reset();
}

void SchematicWireTool::deactivate() {
    if (m_isDrawing) finishWire();
    clearSnapIndicator();
    clearCommittedPointMarkers();
    if (view()) {
        view()->unsetCursor();
        view()->viewport()->unsetCursor();
    }
    m_captureActive = false;
    if (m_modeBadge) m_modeBadge->hide();
    reset();
    SchematicTool::deactivate();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mouse Events
// ─────────────────────────────────────────────────────────────────────────────

void SchematicWireTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    m_lastModifiers = event->modifiers();
    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snapped = snapToConnection(scenePos);

    if (event->button() == Qt::LeftButton) {
        if (!m_isDrawing) {
            // --- Start a new wire ---
            if (m_router) m_router->updateObstaclesFromScene();
            m_committedPoints.clear();
            m_committedPoints.append(snapped);
            m_lastSnappedPos = snapped;
            
            if (!m_currentWire) {
                m_currentWire = new WireItem(snapped, snapped);
            } else {
                m_currentWire->setPoints({snapped, snapped});
            }
            m_currentWire->setPen(QPen(QColor(m_color), m_width, 
                (m_style == "Dash" ? Qt::DashLine : (m_style == "Dot" ? Qt::DotLine : Qt::SolidLine)),
                Qt::RoundCap, Qt::RoundJoin));
            if (!m_currentWire->scene()) {
                view()->scene()->addItem(m_currentWire);
            }
            m_currentWire->setVisible(true);
            m_isDrawing = true;
            updateCommittedPointMarkers();
        } else {
            // --- Commit an intermediate point ---
            QPointF start = m_committedPoints.last();
            
            if (snapped == start) {
                // Clicked same point — finish wire
                finishWire();
            } else {
                const QList<QPointF> routedSegments = buildRoutePoints(start, snapped, m_lastModifiers);
                
                if (routedSegments.size() >= 2) {
                    for (int i = 1; i < routedSegments.size(); ++i) {
                        m_committedPoints.append(routedSegments[i]);
                    }
                }
                
                updateCommittedPointMarkers();
                
                if (m_snappedToConnection) {
                    // Snapped to a pin or wire endpoint — auto-finish
                    finishWire();
                } else {
                    updatePreview();
                }
            }
        }
    } else if (event->button() == Qt::RightButton) {
        if (m_isDrawing) {
            // Right-click: if we have committed segments, finish the wire up to them
            if (m_committedPoints.size() >= 2) {
                finishWire();
            } else {
                // Only the start point — cancel
                cancelWire();
            }
        }
    }
    event->accept();
}

void SchematicWireTool::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!view()) return;
    
    if (event->button() == Qt::LeftButton && m_isDrawing) {
        // Double-click finishes the wire at the current position
        QPointF scenePos = view()->mapToScene(event->pos());
        QPointF snapped = snapToConnection(scenePos);
        QPointF start = m_committedPoints.last();
        
        if (snapped != start) {
            const QList<QPointF> routedSegments = buildRoutePoints(start, snapped, event->modifiers());
            if (routedSegments.size() >= 2) {
                for (int i = 1; i < routedSegments.size(); ++i) {
                    m_committedPoints.append(routedSegments[i]);
                }
            }
        }
        
        finishWire();
        event->accept();
    }
}

void SchematicWireTool::mouseMoveEvent(QMouseEvent* event) {
    if (!view()) return;

    // Force wire cursor each move to override any item-level hover cursor.
    view()->setCursor(cursor());
    view()->viewport()->setCursor(cursor());

    m_lastModifiers = event->modifiers();
    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = snapToConnection(scenePos);
    updateModeBadge(m_lastModifiers);

    // Proteus-style capture cue: show target marker and cursor even before placing first point.
    updateSnapIndicator(snappedPos, m_captureType);
    updateCaptureCursor(m_snappedToConnection);

    if (m_isDrawing && m_currentWire) {
        const bool movedToNewTarget = (snappedPos != m_lastPreviewTarget);
        const bool throttleElapsed = !m_previewThrottle.isValid() || m_previewThrottle.elapsed() >= 12;
        if (movedToNewTarget && throttleElapsed) {
            m_lastSnappedPos = snappedPos;
            m_lastPreviewTarget = snappedPos;
            updatePreview();
            m_previewThrottle.restart();
        }
    }
    
    event->accept();
}

void SchematicWireTool::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event)
}

// ─────────────────────────────────────────────────────────────────────────────
//  Keyboard Events
// ─────────────────────────────────────────────────────────────────────────────

void SchematicWireTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && (event->modifiers() & Qt::ShiftModifier)) {
        // Shift+Space: cycle routing mode
        if (m_routingMode == ManhattanMode) m_routingMode = FortyFiveMode;
        else if (m_routingMode == FortyFiveMode) m_routingMode = FreeMode;
        else m_routingMode = ManhattanMode;
        updateModeBadge(m_lastModifiers);
        if (m_isDrawing) updatePreview();
        event->accept();
    } else if (event->key() == Qt::Key_Space && m_isDrawing) {
        // Space: toggle H-V vs V-H routing direction
        m_hFirst = !m_hFirst;
        updatePreview();
        event->accept();
    } else if (event->key() == Qt::Key_Backspace && m_isDrawing) {
        // Backspace: undo the last committed segment
        undoLastSegment();
        event->accept();
    } else if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ControlModifier) && m_isDrawing) {
        // Ctrl+Z while drawing: same as backspace — undo last segment
        undoLastSegment();
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        if (m_isDrawing) {
            // Escape: cancel current wire
            cancelWire();
        }
        // Return to Select tool when pressing Escape
        if (view()) {
            view()->setCurrentTool("Select");
        }
        event->accept();
    } else if (event->key() == Qt::Key_K || event->key() == Qt::Key_Return) {
        if (m_isDrawing) {
            finishWire();
        }
        event->accept();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Preview & Wire Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void SchematicWireTool::updatePreview() {
    if (!m_currentWire || m_committedPoints.isEmpty()) return;

    QPointF start = m_committedPoints.last();
    QPointF target = m_lastSnappedPos;
    
    QList<QPointF> previewPoints = m_committedPoints;
    
    if (start == target) {
        m_currentWire->setPoints(previewPoints);
        return;
    }

    const QList<QPointF> routedSegments = buildRoutePoints(start, target, m_lastModifiers);
    
    if (routedSegments.size() >= 2) {
        for (int i = 1; i < routedSegments.size(); ++i) {
            previewPoints.append(routedSegments[i]);
        }
    }

    m_currentWire->setPoints(previewPoints);
}

void SchematicWireTool::finishWire() {
    if (m_currentWire) {
        if (m_currentWire->scene()) {
            view()->scene()->removeItem(m_currentWire);
        }
        m_currentWire->setVisible(false);
        if (m_committedPoints.size() < 2) {
            m_isDrawing = false;
            m_committedPoints.clear();
            clearSnapIndicator();
            clearCommittedPointMarkers();
            return;
        }
    }

    QList<WireItem*> allWires;
    for (int i = 0; i < m_committedPoints.size() - 1; ++i) {
        if (QLineF(m_committedPoints[i], m_committedPoints[i+1]).length() < 0.1) continue;
        
        WireItem* segmentWire = new WireItem();
        segmentWire->setPen(QPen(QColor(m_color), m_width, 
            (m_style == "Dash" ? Qt::DashLine : (m_style == "Dot" ? Qt::DotLine : Qt::SolidLine)),
            Qt::RoundCap, Qt::RoundJoin));
        segmentWire->setPoints({m_committedPoints[i], m_committedPoints[i+1]});
        
        QList<WireItem*> cutWires = handleComponentIntersections(segmentWire);
        allWires.append(cutWires);
    }
            
    // Add wires to undo stack
    if (!allWires.isEmpty()) {
        if (view()->undoStack()) {
            view()->undoStack()->beginMacro("Draw Wire");
            for (WireItem* wire : allWires) {
                if (wire->scene()) view()->scene()->removeItem(wire);
                AddItemCommand* cmd = new AddItemCommand(view()->scene(), wire);
                view()->undoStack()->push(cmd);
            }
            view()->undoStack()->endMacro();
        } else {
            for (WireItem* wire : allWires) {
                if (!wire->scene()) view()->scene()->addItem(wire);
            }
        }
    }
    SchematicConnectivity::updateVisualConnections(view()->scene());
    m_isDrawing = false;
    m_committedPoints.clear();
    clearSnapIndicator();
    clearCommittedPointMarkers();
}

void SchematicWireTool::cancelWire() {
    if (m_currentWire && view() && m_currentWire->scene()) {
        view()->scene()->removeItem(m_currentWire);
    }
    m_isDrawing = false;
    m_committedPoints.clear();
    m_lastPreviewTarget = QPointF();
    clearSnapIndicator();
    clearCommittedPointMarkers();
    m_captureActive = false;
}

void SchematicWireTool::undoLastSegment() {
    if (!m_isDrawing || m_committedPoints.size() <= 1) {
        // Nothing to undo — cancel entirely
        cancelWire();
        return;
    }
    
    // Remove the last committed point
    m_committedPoints.removeLast();
    updateCommittedPointMarkers();
    
    if (m_committedPoints.size() <= 1) {
        // Only the start point remains — just keep drawing from there
    }
    
    updatePreview();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Component Intersection Handling (Wire Splitting)
// ─────────────────────────────────────────────────────────────────────────────

QList<WireItem*> SchematicWireTool::handleComponentIntersections(WireItem* wire) {
    if (!wire || !view() || !view()->scene()) return {wire};

    QList<QPointF> points = wire->points();
    if (points.size() < 2) return {wire};

    struct PinOnWire {
        QPointF scenePos;
        SchematicItem* owner;
        qreal t; 
    };

    QList<WireItem*> result;
    QList<QPointF> currentPiece;
    currentPiece.append(points.first());

    for (int i = 0; i < points.size() - 1; ++i) {
        QPointF start = points[i];
        QPointF end = points[i + 1];
        QLineF segment(start, end);
        
        QList<PinOnWire> pinsOnSegment;
        
        QRectF segmentRect = QRectF(start, end).normalized().adjusted(-2, -2, 2, 2);
        QList<QGraphicsItem*> items = view()->scene()->items(segmentRect);
        
        for (auto* item : items) {
            SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
            if (!sItem || sItem == wire || sItem->itemType() == SchematicItem::WireType) continue;
            
            for (const QPointF& pinLocal : sItem->connectionPoints()) {
                QPointF pinScene = sItem->mapToScene(pinLocal);
                
                qreal dist;
                qreal t;
                if (qAbs(segment.dx()) > qAbs(segment.dy())) { // Horizontal
                    dist = qAbs(pinScene.y() - start.y());
                    t = (segment.dx() != 0) ? (pinScene.x() - start.x()) / segment.dx() : 0;
                } else { // Vertical
                    dist = qAbs(pinScene.x() - start.x());
                    t = (segment.dy() != 0) ? (pinScene.y() - start.y()) / segment.dy() : 0;
                }
                
                if (dist < 2.0 && t >= -0.001 && t <= 1.001) {
                    pinsOnSegment.append({pinScene, sItem, t});
                }
            }
        }
        
        std::sort(pinsOnSegment.begin(), pinsOnSegment.end(), [](const PinOnWire& a, const PinOnWire& b) {
            return a.t < b.t;
        });
        
        QPointF lastPointInSegment = start;
        for (int j = 0; j < pinsOnSegment.size(); ++j) {
            const auto& pin = pinsOnSegment[j];
            
            // Check for short-circuit (two pins of same component on same segment/path)
            if (j + 1 < pinsOnSegment.size() && pinsOnSegment[j].owner == pinsOnSegment[j + 1].owner) {
                // Cut interval [pin j, pin j+1]
                if ((pin.scenePos - lastPointInSegment).manhattanLength() > 0.1) {
                    currentPiece.append(pin.scenePos);
                }
                
                if (currentPiece.size() >= 2) {
                    WireItem* newWirePart = new WireItem();
                    newWirePart->setPoints(currentPiece);
                    result.append(newWirePart);
                }
                currentPiece.clear();
                
                lastPointInSegment = pinsOnSegment[j + 1].scenePos;
                currentPiece.append(lastPointInSegment);
                j++; // Skip the bridge segment
            } else {
                if ((pin.scenePos - lastPointInSegment).manhattanLength() > 0.1) {
                    currentPiece.append(pin.scenePos);
                }
                lastPointInSegment = pin.scenePos;
            }
        }
        
        if ((end - lastPointInSegment).manhattanLength() > 0.1) {
            currentPiece.append(end);
        }
    }
    
    if (currentPiece.size() >= 2) {
        WireItem* lastWirePart = new WireItem();
        lastWirePart->setPoints(currentPiece);
        result.append(lastWirePart);
    }
    
    if (result.size() == 1) {
        // Double check it's not the exact same wire
        if (result.first()->points() == points) {
            delete result.first();
            return {wire};
        }
    }
    
    if (result.isEmpty()) return {wire}; // Should not happen

    // If we reached here, the wire was split or shortened
    if (wire->scene()) {
        view()->scene()->removeItem(wire);
    }
    if (!result.contains(wire)) {
        delete wire;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  State Reset
// ─────────────────────────────────────────────────────────────────────────────

void SchematicWireTool::reset() {
    if (m_currentWire && view() && m_currentWire->scene()) {
        view()->scene()->removeItem(m_currentWire);
    }
    m_isDrawing = false;
    m_committedPoints.clear();
    m_lastPreviewTarget = QPointF();
    clearSnapIndicator();
    clearCommittedPointMarkers();
    m_captureActive = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Snap Logic
// ─────────────────────────────────────────────────────────────────────────────

QPointF SchematicWireTool::snapToConnection(QPointF pos) {
    m_snappedToConnection = false;
    m_captureType = GridCapture;
    if (!view() || !view()->scene()) return view()->snapToGrid(pos);

    QPointF snapped = view()->snapToGrid(pos);
    const qreal searchRadius = 15.0;
    qreal closestDistSq = searchRadius * searchRadius;
    QPointF closestPoint = snapped;

    // --- 1. Pin snap (highest priority) via view-level API ---
    SchematicView::SnapResult pinResult = view()->snapToPin(pos, searchRadius, m_currentWire);
    if (pinResult.type == SchematicView::PinSnap) {
        const qreal dx = pos.x() - pinResult.point.x();
        const qreal dy = pos.y() - pinResult.point.y();
        const qreal pinDistSq = dx * dx + dy * dy;
        if (pinDistSq < closestDistSq) {
            closestDistSq = pinDistSq;
            closestPoint = pinResult.point;
            m_snappedToConnection = true;
            m_captureType = PinCapture;
        }
    }

    // --- 2. Wire vertex / segment snap ---
    QRectF searchRect(pos.x() - searchRadius, pos.y() - searchRadius, searchRadius * 2.0, searchRadius * 2.0);
    QList<QGraphicsItem*> items = view()->scene()->items(searchRect, Qt::IntersectsItemBoundingRect);

    auto distSqToPos = [&pos](const QPointF& p) -> qreal {
        const qreal dx = pos.x() - p.x();
        const qreal dy = pos.y() - p.y();
        return dx * dx + dy * dy;
    };
    
    for (QGraphicsItem* item : items) {
        if (!item || !item->isVisible()) continue;

        WireItem* wItem = dynamic_cast<WireItem*>(item);
        if (!wItem || wItem == m_currentWire) continue;

        const QList<QPointF> wPoints = wItem->points();
        if (wPoints.isEmpty()) continue;

        const QTransform tx = wItem->sceneTransform();
        const bool translateOnly = tx.type() <= QTransform::TxTranslate;
        const QPointF offset(tx.dx(), tx.dy());

        auto toScene = [&](const QPointF& p) -> QPointF {
            return translateOnly ? (p + offset) : tx.map(p);
        };

        // Snap to wire vertices.
        for (const QPointF& p : wPoints) {
            const QPointF sceneP = toScene(p);
            const qreal distSq = distSqToPos(sceneP);
            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                closestPoint = sceneP;
                m_snappedToConnection = true;
                m_captureType = WireVertexCapture;
            }
        }

        // Snap to wire segments (for T-junctions).
        for (int i = 0; i < wPoints.size() - 1; ++i) {
            const QPointF p1 = toScene(wPoints[i]);
            const QPointF p2 = toScene(wPoints[i + 1]);
            const qreal vx = p2.x() - p1.x();
            const qreal vy = p2.y() - p1.y();
            const qreal lenSq = vx * vx + vy * vy;

            if (lenSq <= 0.0) continue;

            const qreal wx = pos.x() - p1.x();
            const qreal wy = pos.y() - p1.y();
            const qreal u = (wx * vx + wy * vy) / lenSq;
            if (u < 0.0 || u > 1.0) continue;

            const QPointF proj(p1.x() + u * vx, p1.y() + u * vy);
            const qreal distSq = distSqToPos(proj);
            if (distSq < closestDistSq) {
                QPointF gridProj = view()->snapToGrid(proj);
                // Keep snapped point on the segment axis while quantizing movement to grid.
                if (qAbs(vx) >= qAbs(vy)) {
                    gridProj.setY(p1.y());
                } else {
                    gridProj.setX(p1.x());
                }
                closestDistSq = distSq;
                closestPoint = gridProj;
                m_snappedToConnection = true;
                m_captureType = WireSegmentCapture;
            }
        }
    }

    return closestPoint;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Routing Logic (refactored: decomposed into specialized methods)
// ─────────────────────────────────────────────────────────────────────────────

QList<QPointF> SchematicWireTool::buildRoutePoints(const QPointF& start, const QPointF& target, Qt::KeyboardModifiers modifiers) const {
    if (start == target) return {start};

    switch (m_routingMode) {
    case FreeMode:
        return {start, target};
    case FortyFiveMode:
        return buildFortyFiveRoute(start, target, modifiers);
    case ManhattanMode:
    default:
        break;
    }

    // Manhattan mode: try smart routing first, fall back to simple L-shape
    const bool forceH = modifiers.testFlag(Qt::ControlModifier);
    const bool forceV = modifiers.testFlag(Qt::AltModifier);
    
    if (forceH || forceV) {
        // Forced direction — use simple Manhattan without pathfinding
        const bool hFirst = forceH ? true : false;
        QPointF corner = hFirst ? QPointF(target.x(), start.y()) : QPointF(start.x(), target.y());
        QList<QPointF> points{start};
        if (corner != start && corner != target) {
            points.append(corner);
        }
        points.append(target);
        return points;
    }

    return buildSmartRoute(start, target);
}

QList<QPointF> SchematicWireTool::buildSmartRoute(const QPointF& start, const QPointF& target) const {
    // Try obstacle-aware A* routing first
    if (m_router) {
        QList<QPointF> smartPath = m_router->routeOrthogonal(start, target, {}, 10.0, m_hFirst);
        if (smartPath.size() >= 2) {
            // Ensure endpoints are exact (router may snap to grid internally)
            smartPath.first() = start;
            smartPath.last() = target;
            return smartPath;
        }
    }
    
    // Fallback: simple Manhattan L-shape
    return buildManhattanRoute(start, target);
}

QList<QPointF> SchematicWireTool::buildManhattanRoute(const QPointF& start, const QPointF& target) const {
    QPointF corner = m_hFirst ? QPointF(target.x(), start.y()) : QPointF(start.x(), target.y());
    QList<QPointF> points{start};
    if (corner != start && corner != target) {
        points.append(corner);
    }
    points.append(target);
    return points;
}

QList<QPointF> SchematicWireTool::buildFortyFiveRoute(const QPointF& start, const QPointF& target, Qt::KeyboardModifiers modifiers) const {
    const qreal dx = target.x() - start.x();
    const qreal dy = target.y() - start.y();
    const qreal adx = std::abs(dx);
    const qreal ady = std::abs(dy);

    const bool forceH = modifiers.testFlag(Qt::ControlModifier);
    const bool forceV = modifiers.testFlag(Qt::AltModifier);
    const bool hFirst = forceH ? true : (forceV ? false : m_hFirst);

    QList<QPointF> points{start};

    if (forceH || forceV) {
        QPointF corner = hFirst ? QPointF(target.x(), start.y()) : QPointF(start.x(), target.y());
        if (corner != start) points.append(corner);
        if (target != corner) points.append(target);
        return points;
    }

    // Pure 45° diagonal
    if (qFuzzyCompare(adx + 1.0, ady + 1.0)) {
        points.append(target);
        return points;
    }

    const qreal sx = dx >= 0.0 ? 1.0 : -1.0;
    const qreal sy = dy >= 0.0 ? 1.0 : -1.0;
    if (adx > ady) {
        QPointF p1(start.x() + sx * ady, start.y() + sy * ady); // diagonal first
        if (p1 != start) points.append(p1);
        if (target != p1) points.append(target);
    } else {
        QPointF p1(start.x() + sx * adx, start.y() + sy * adx); // diagonal first
        if (p1 != start) points.append(p1);
        if (target != p1) points.append(target);
    }
    return points;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Visual Feedback
// ─────────────────────────────────────────────────────────────────────────────

void SchematicWireTool::updateSnapIndicator(const QPointF& pos, CaptureType captureType) {
    if (!view() || !view()->scene()) return;

    if (!m_snapIndicator) {
        m_snapIndicator = new QGraphicsEllipseItem();
        m_snapIndicator->setZValue(2006);
        view()->scene()->addItem(m_snapIndicator);
    } else if (!m_snapIndicator->scene()) {
        view()->scene()->addItem(m_snapIndicator);
    }

    QColor c;
    qreal r = 4.0;
    switch (captureType) {
    case PinCapture:
        c = QColor(239, 68, 68);
        r = 5.0;
        break;
    case WireVertexCapture:
        c = QColor(245, 158, 11);
        r = 4.8;
        break;
    case WireSegmentCapture:
        c = QColor(251, 191, 36);
        r = 4.8;
        break;
    case GridCapture:
    default:
        c = QColor(56, 189, 248);
        r = 3.4;
        break;
    }

    QColor fill = c;
    fill.setAlpha(captureType == GridCapture ? 18 : 44);
    m_snapIndicator->setPen(QPen(c, captureType == GridCapture ? 1.2 : 1.8));
    m_snapIndicator->setBrush(fill);
    m_snapIndicator->setRect(QRectF(pos.x() - r, pos.y() - r, 2.0 * r, 2.0 * r));
    m_snapIndicator->setVisible(true);
}

void SchematicWireTool::clearSnapIndicator() {
    if (m_snapIndicator) {
        m_snapIndicator->setVisible(false);
    }
}

void SchematicWireTool::updateCaptureCursor(bool captured) {
    if (!view()) return;
    m_captureActive = captured;
    view()->setCursor(cursor());
    view()->viewport()->setCursor(cursor());
}

void SchematicWireTool::updateCommittedPointMarkers() {
    clearCommittedPointMarkers();
    if (!view() || !view()->scene() || !m_isDrawing) return;
    
    // Show small dots at each committed point so the user can see their wire path
    for (int i = 0; i < m_committedPoints.size(); ++i) {
        const QPointF& p = m_committedPoints[i];
        auto* marker = new QGraphicsEllipseItem();
        marker->setZValue(2005);
        
        QColor c;
        qreal r;
        if (i == 0) {
            // Start point: green accent
            c = QColor(16, 185, 129);
            r = 4.0;
        } else {
            // Intermediate points: subtle blue
            c = QColor(56, 189, 248);
            r = 3.2;
        }
        
        QColor fill = c;
        fill.setAlpha(180);
        marker->setPen(QPen(c, 1.5));
        marker->setBrush(fill);
        marker->setRect(QRectF(p.x() - r, p.y() - r, 2.0 * r, 2.0 * r));
        view()->scene()->addItem(marker);
        m_committedMarkers.append(marker);
    }
}

void SchematicWireTool::clearCommittedPointMarkers() {
    for (auto* marker : m_committedMarkers) {
        if (marker->scene()) {
            marker->scene()->removeItem(marker);
        }
        delete marker;
    }
    m_committedMarkers.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mode Badge
// ─────────────────────────────────────────────────────────────────────────────

void SchematicWireTool::ensureModeBadge() {
    if (!view()) return;
    if (!m_modeBadge) {
        m_modeBadge = new QLabel(view()->viewport());
        m_modeBadge->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_modeBadge->setStyleSheet(
            "QLabel {"
            " color: #f8fafc;"
            " background: rgba(15, 23, 42, 170);"
            " border: 1px solid rgba(148, 163, 184, 120);"
            " border-radius: 4px;"
            " padding: 3px 7px;"
            " font-size: 11px;"
            " font-weight: 600;"
            "}"
        );
    }
    m_modeBadge->move(12, 12);
    m_modeBadge->show();
}

void SchematicWireTool::updateModeBadge(Qt::KeyboardModifiers modifiers) {
    ensureModeBadge();
    if (!m_modeBadge) return;

    QStringList tags;
    if (modifiers.testFlag(Qt::ControlModifier)) tags << "H";
    if (modifiers.testFlag(Qt::AltModifier)) tags << "V";
    const QString suffix = tags.isEmpty() ? QString() : QString("  [%1]").arg(tags.join(","));
    
    // Show segment count info when drawing
    QString drawInfo;
    if (m_isDrawing && m_committedPoints.size() > 1) {
        drawInfo = QString("  •  %1 pts").arg(m_committedPoints.size());
    }
    
    m_modeBadge->setText(QString("Wire: %1%2%3").arg(routingModeName(), suffix, drawInfo));
    m_modeBadge->adjustSize();
    m_modeBadge->move(12, 12);
}

QString SchematicWireTool::routingModeName() const {
    switch (m_routingMode) {
    case FortyFiveMode: return "45°";
    case FreeMode: return "Free";
    case ManhattanMode:
    default: return "Manhattan";
    }
}
