#include "pcb_trace_tool.h"
#include "pcb_view.h"
#include "trace_item.h"
#include "via_item.h"
#include "pad_item.h"
#include "copper_pour_item.h"
#include "theme_manager.h"
#include "pcb_commands.h"
#include "../layers/pcb_layer.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsScene>
#include <QDebug>
#include <QGraphicsEllipseItem>
#include <QLineF>
#include <QPainterPathStroker>
#include "../drc/pcb_drc.h"
#include "../analysis/signal_integrity_engine.h"
#include "net_class.h"
#include <cmath>
#include <algorithm>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QMainWindow>
#include <QStatusBar>

namespace {
bool pointsNear(const QPointF& a, const QPointF& b, double eps = 1e-3) {
    return QLineF(a, b).length() <= eps;
}

bool pcbItemConnectableOnLayer(const PCBItem* item, int layerId) {
    if (!item) return false;
    if (item->layer() == layerId) return true;

    if (const auto* via = dynamic_cast<const ViaItem*>(item)) {
        return via->spansLayer(layerId);
    }
    if (const auto* pad = dynamic_cast<const PadItem*>(item)) {
        return pad->drillSize() > 0.001;
    }
    return false;
}

bool traceEndpointTouchesAnchor(const TraceItem& trace, const PCBItem* anchor, double tol = 0.05) {
    if (!anchor) return false;

    const QPointF startScene = trace.mapToScene(trace.startPoint());
    const QPointF endScene = trace.mapToScene(trace.endPoint());
    const QPainterPath anchorShape = anchor->sceneTransform().map(anchor->shape());

    auto touches = [&](const QPointF& p) {
        if (anchorShape.contains(p)) return true;
        return QLineF(p, anchor->scenePos()).length() <= tol;
    };

    return touches(startScene) || touches(endScene);
}

QString pointKey(const QPointF& p, double grid = 1e-3) {
    const qint64 x = static_cast<qint64>(std::llround(p.x() / grid));
    const qint64 y = static_cast<qint64>(std::llround(p.y() / grid));
    return QString::number(x) + ":" + QString::number(y);
}

bool isPointInternalToTrace(const TraceItem* trace, const QPointF& scenePoint) {
    if (!trace) return false;
    const QPointF s = trace->mapToScene(trace->startPoint());
    const QPointF e = trace->mapToScene(trace->endPoint());
    return !pointsNear(scenePoint, s) && !pointsNear(scenePoint, e);
}

bool shareEndpoint(const TraceItem* a, const TraceItem* b, QPointF* sharedPoint, QPointF* aOther, QPointF* bOther) {
    if (!a || !b) return false;
    const QPointF aS = a->mapToScene(a->startPoint());
    const QPointF aE = a->mapToScene(a->endPoint());
    const QPointF bS = b->mapToScene(b->startPoint());
    const QPointF bE = b->mapToScene(b->endPoint());

    if (pointsNear(aS, bS)) {
        if (sharedPoint) *sharedPoint = aS;
        if (aOther) *aOther = aE;
        if (bOther) *bOther = bE;
        return true;
    }
    if (pointsNear(aS, bE)) {
        if (sharedPoint) *sharedPoint = aS;
        if (aOther) *aOther = aE;
        if (bOther) *bOther = bS;
        return true;
    }
    if (pointsNear(aE, bS)) {
        if (sharedPoint) *sharedPoint = aE;
        if (aOther) *aOther = aS;
        if (bOther) *bOther = bE;
        return true;
    }
    if (pointsNear(aE, bE)) {
        if (sharedPoint) *sharedPoint = aE;
        if (aOther) *aOther = aS;
        if (bOther) *bOther = bS;
        return true;
    }
    return false;
}

double cross2d(const QPointF& a, const QPointF& b) {
    return a.x() * b.y() - a.y() * b.x();
}

bool collinearAroundShared(const QPointF& shared, const QPointF& aOther, const QPointF& bOther) {
    const QPointF va = aOther - shared;
    const QPointF vb = bOther - shared;
    const double la = std::hypot(va.x(), va.y());
    const double lb = std::hypot(vb.x(), vb.y());
    if (la < 1e-6 || lb < 1e-6) return false;
    return std::abs(cross2d(va, vb)) <= 1e-4 * (la + lb);
}

QList<TraceItem*> collectSceneTraces(QGraphicsScene* scene) {
    QList<TraceItem*> traces;
    if (!scene) return traces;
    for (QGraphicsItem* item : scene->items()) {
        if (TraceItem* t = dynamic_cast<TraceItem*>(item)) traces.append(t);
    }
    return traces;
}

constexpr int kJunctionDotTagKey = 0x534A44; // "SJD"

QString routeNodeKey(const QPointF& p, double grid) {
    const qint64 x = static_cast<qint64>(std::llround(p.x() / grid));
    const qint64 y = static_cast<qint64>(std::llround(p.y() / grid));
    return QString::number(x) + ":" + QString::number(y);
}
}

PCBTraceTool::PCBTraceTool(QObject* parent)
    : PCBTool("Trace", parent)
    , m_isRouting(false)
    , m_traceWidth(0.25)
    , m_currentLayer(0)
    , m_routingMode(Hugging)
    , m_enableShove(true)
    , m_autoTeardrops(true)
    , m_previewLine(nullptr)
    , m_previewLine2(nullptr)
    , m_siRadarText(nullptr) {
}

PCBTraceTool::~PCBTraceTool() {
    cancelTrace();
}

QCursor PCBTraceTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

void PCBTraceTool::setTraceWidth(double width) {
    m_traceWidth = width;
    if (m_isRouting) updatePreview(view()->mapToScene(view()->mapFromGlobal(QCursor::pos())));
}

void PCBTraceTool::setCurrentLayer(int layer) {
    if (m_currentLayer != layer) {
        m_currentLayer = layer;
        PCBLayerManager::instance().setActiveLayer(layer);
        if (m_isRouting) updatePreview(view()->mapToScene(view()->mapFromGlobal(QCursor::pos())));
    }
}

void PCBTraceTool::activate(PCBView* view) {
    PCBTool::activate(view);
    m_isRouting = false;
    m_currentTraceItems.clear();
    m_currentLayer = PCBLayerManager::instance().activeLayerId();
}

void PCBTraceTool::deactivate() {
    if (m_isRouting) {
        finishTrace();
    }
    cancelTrace(); 
    PCBTool::deactivate();
}

void PCBTraceTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;
    
    if (event->button() != Qt::LeftButton) {
        event->ignore(); // Let PCBView handle right-click tool switching
        return;
    }

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);
    const bool constrain45 = event->modifiers() & Qt::ShiftModifier;
    if (m_isRouting && constrain45) {
        snappedPos = constrainAngle(m_lastPoint, snappedPos, true);
    }

    if (!m_isRouting) {
        startTrace(snappedPos);
    } else {
        addSegment(snappedPos);
    }
    
    event->accept();
}

void PCBTraceTool::mouseMoveEvent(QMouseEvent* event) {
    if (!view()) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    
    if (m_isRouting && !m_targetNet.isEmpty() && m_targetNet != "No Net") {
        double netPullRadius = 4.0;
        QPointF bestTarget;
        double minTargetDist = netPullRadius + 0.1;

        for (const QPointF& targetPos : m_guideTargets) {
            double dist = QLineF(scenePos, targetPos).length();
            if (dist < minTargetDist) {
                minTargetDist = dist;
                bestTarget = targetPos;
            }
        }

        if (minTargetDist <= netPullRadius) {
            updatePreview(bestTarget);
            event->accept();
            return;
        }
    }

    QPointF snappedPos = view()->snapToGrid(scenePos);
    if (m_isRouting && (event->modifiers() & Qt::ShiftModifier)) {
        snappedPos = constrainAngle(m_lastPoint, snappedPos, true);
    }
    if (m_isRouting) {
        updatePreview(snappedPos);
    }
    
    event->accept();
}

void PCBTraceTool::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;
    if (m_isRouting) finishTrace();
    event->accept();
}

void PCBTraceTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_isRouting) {
            finishTrace();
        } else {
            PCBTool::keyPressEvent(event);
        }
        event->accept();
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finishTrace();
        event->accept();
    } else if (event->key() == Qt::Key_V) {
        if (m_isRouting) placeVia();
        event->accept();
    } else if (event->key() == Qt::Key_1) {
        setCurrentLayer(0);
        event->accept();
    } else if (event->key() == Qt::Key_2) {
        setCurrentLayer(1);
        event->accept();
    } else if (event->key() == Qt::Key_Backspace) {
        undoLastSegment();
        event->accept();
    } else {
        PCBTool::keyPressEvent(event);
    }
}

void PCBTraceTool::startTrace(QPointF pos) {
    m_isRouting = true;
    m_startPoint = pos;
    m_lastPoint = pos;
    m_currentTraceItems.clear();

    if (!view()->scene()) return;

    m_targetNet = "";
    m_guideTargets.clear();
    QList<QGraphicsItem*> startItems = view()->scene()->items(pos);
    for (auto* item : startItems) {
        PCBItem* pcbItem = nullptr;
        QGraphicsItem* current = item;
        while (current) {
            pcbItem = dynamic_cast<PCBItem*>(current);
            if (pcbItem) break;
            current = current->parentItem();
        }

        if (pcbItem) {
            const bool onLayer = pcbItemConnectableOnLayer(pcbItem, m_currentLayer);

            if (onLayer && (pcbItem->itemType() == PCBItem::PadType || pcbItem->itemType() == PCBItem::ViaType)) {
                m_targetNet = pcbItem->netName();
                break;
            }
        }
    }

    if (!m_targetNet.isEmpty() && m_targetNet != "No Net") {
        NetClass nc = NetClassManager::instance().getClassForNet(m_targetNet);
        m_traceWidth = nc.traceWidth;
        if (view()) view()->toolChanged("Trace"); 
        
        PCBTheme* theme = ThemeManager::theme();
        for (auto* item : view()->scene()->items()) {
            if (PCBItem* pcbItem = dynamic_cast<PCBItem*>(item)) {
                if (pcbItem->netName() == m_targetNet && pcbItem->scenePos() != pos) {
                    if (pcbItem->itemType() == PCBItem::PadType || pcbItem->itemType() == PCBItem::ViaType) {
                        m_guideTargets.append(pcbItem->scenePos());
                        
                        QGraphicsLineItem* guide = new QGraphicsLineItem();
                        QPen guidePen(theme->accentColor(), 0.1, Qt::DashLine);
                        guidePen.setCosmetic(true);
                        guide->setPen(guidePen);
                        guide->setZValue(1999);
                        view()->scene()->addItem(guide);
                        m_guideLines.append(guide);
                        
                        auto* highlight = new QGraphicsEllipseItem(pcbItem->scenePos().x() - 1.5, pcbItem->scenePos().y() - 1.5, 3.0, 3.0);
                        highlight->setPen(QPen(theme->accentColor(), 0.2, Qt::SolidLine));
                        highlight->setBrush(QBrush(QColor(theme->accentColor().red(), theme->accentColor().green(), theme->accentColor().blue(), 40)));
                        highlight->setZValue(1998);
                        view()->scene()->addItem(highlight);
                        m_netHighlights.append(highlight);
                    }
                }
            }
        }
    }

    m_previewLine = new QGraphicsLineItem();
    m_previewLine2 = new QGraphicsLineItem();
    
    PCBTheme* theme = ThemeManager::theme();
    QColor color = (m_currentLayer == 0) ? theme->topCopper() : theme->bottomCopper();
    color.setAlpha(150);
    
    QPen p(color, m_traceWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    m_previewLine->setPen(p);
    m_previewLine2->setPen(p);
    m_previewLine->setZValue(2000);
    m_previewLine2->setZValue(2000);

    view()->scene()->addItem(m_previewLine);
    view()->scene()->addItem(m_previewLine2);

    m_clearanceHalo = new QGraphicsPathItem();
    m_clearanceHalo2 = new QGraphicsPathItem();
    m_clearanceHalo->setZValue(1990);
    m_clearanceHalo2->setZValue(1990);
    view()->scene()->addItem(m_clearanceHalo);
    view()->scene()->addItem(m_clearanceHalo2);

    m_siRadarText = new QGraphicsSimpleTextItem();
    m_siRadarText->setZValue(2100);
    m_siRadarText->setBrush(QColor(255, 255, 255));
    m_siRadarText->setFont(QFont("Inter", 8));
    view()->scene()->addItem(m_siRadarText);

    updatePreview(pos);
}

void PCBTraceTool::addSegment(QPointF pos) {
    if (!view() || !view()->scene()) return;
    bool routeBlocked = false;

    if (m_routingMode == Hugging) {
        QPainterPath path = calculateHuggingPath(m_lastPoint, pos);
        for (int i = 0; i < path.elementCount(); ++i) {
            QPointF p(path.elementAt(i).x, path.elementAt(i).y);
            if (pointsNear(p, m_lastPoint)) continue;
            if (segmentBlockedForRouting(m_lastPoint, p)) {
                routeBlocked = true;
                break;
            }
            
            TraceItem* seg = new TraceItem(m_lastPoint, p, m_traceWidth);
            seg->setLayer(m_currentLayer);
            seg->setNetName(m_targetNet);
            view()->scene()->addItem(seg);
            seg->updateConnectivity();
            m_currentTraceItems.append(seg);
            m_lastPoint = p;
        }
    } else {
        if (m_routingMode == WalkAround) {
            m_currentLevelPoint = chooseWalkAroundElbow(m_lastPoint, pos);
        } else {
            m_currentLevelPoint = QPointF(pos.x(), m_lastPoint.y());
        }

        if (m_currentLevelPoint != m_lastPoint &&
            segmentBlockedForRouting(m_lastPoint, m_currentLevelPoint)) {
            routeBlocked = true;
        }
        
        if (!routeBlocked && m_currentLevelPoint != m_lastPoint) {
            TraceItem* seg1 = new TraceItem(m_lastPoint, m_currentLevelPoint, m_traceWidth);
            seg1->setLayer(m_currentLayer);
            seg1->setNetName(m_targetNet);
            view()->scene()->addItem(seg1);
            seg1->updateConnectivity();
            m_currentTraceItems.append(seg1);
            m_lastPoint = m_currentLevelPoint;
        }

        if (!routeBlocked && pos != m_lastPoint &&
            segmentBlockedForRouting(m_lastPoint, pos)) {
            routeBlocked = true;
        }

        if (!routeBlocked && pos != m_lastPoint) {
            TraceItem* seg2 = new TraceItem(m_lastPoint, pos, m_traceWidth);
            seg2->setLayer(m_currentLayer);
            seg2->setNetName(m_targetNet);
            view()->scene()->addItem(seg2);
            seg2->updateConnectivity();
            m_currentTraceItems.append(seg2);
            m_lastPoint = pos;
        }
    }

    if (routeBlocked) {
        revertShovedItems();
        if (view() && view()->window()) {
            if (QMainWindow* mainWindow = qobject_cast<QMainWindow*>(view()->window())) {
                if (mainWindow->statusBar()) {
                    mainWindow->statusBar()->showMessage("Route blocked: no free path to cursor destination.", 2500);
                }
            }
        }
        updatePreview(pos);
        return;
    }

    commitShovedItems();
    updatePreview(pos);
}

QPainterPath PCBTraceTool::calculateHuggingPath(const QPointF& from, const QPointF& to) {
    QPainterPath path;
    path.moveTo(from);

    QPainterPath direct;
    direct.moveTo(from);
    direct.lineTo(to);
    QPainterPathStroker stroker;
    PCBDRC drc;
    stroker.setWidth(m_traceWidth + drc.rules().minClearance() * 2.0);
    if (!checkClearance(stroker.createStroke(direct))) {
        path.lineTo(to);
        return path;
    }

    const QVector<QPointF> autoRoute = findAutoRoutePoints(from, to);
    if (autoRoute.size() >= 2) {
        for (int i = 1; i < autoRoute.size(); ++i) {
            path.lineTo(autoRoute[i]);
        }
        return path;
    }

    QPointF current = from;
    const double step = 0.5;
    const int maxSteps = 200;
    
    QList<QPointF> points;
    points.append(from);

    for (int i = 0; i < maxSteps; ++i) {
        if (QLineF(current, to).length() < step * 1.1) {
            points.append(to);
            break;
        }

        QPointF dir = (to - current);
        double angle = std::atan2(dir.y(), dir.x());
        
        bool found = false;
        for (double dAng : {0.0, 0.2, -0.2, 0.5, -0.5, 0.8, -0.8, 1.2, -1.2, 1.57, -1.57}) {
            QPointF next(current.x() + std::cos(angle + dAng) * step,
                         current.y() + std::sin(angle + dAng) * step);
            
            QPainterPath seg;
            seg.moveTo(current);
            seg.lineTo(next);
            if (!checkClearance(stroker.createStroke(seg))) {
                current = next;
                points.append(next);
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    for (int i = 1; i < points.size(); ++i) {
        path.lineTo(points[i]);
    }
    return path;
}

QVector<QPointF> PCBTraceTool::findAutoRoutePoints(const QPointF& from, const QPointF& to) {
    QVector<QPointF> failed;
    if (!view() || !view()->scene()) return failed;

    const double grid = 0.25;
    const QPointF start = view()->snapToGrid(from);
    const QPointF goal = view()->snapToGrid(to);
    if (QLineF(start, goal).length() < 1e-6) return {start, goal};

    struct NodeState {
        QPointF point;
        double g = 0.0;
        double f = 0.0;
        int dir = -1;
    };

    const QVector<QPointF> dirs = {
        QPointF(grid, 0.0), QPointF(-grid, 0.0), QPointF(0.0, grid), QPointF(0.0, -grid)
    };

    auto heuristic = [&](const QPointF& p) {
        return QLineF(p, goal).length();
    };

    QRectF bounds(start, goal);
    bounds = bounds.normalized().adjusted(-20.0, -20.0, 20.0, 20.0);
    if (bounds.width() < 10.0) bounds.adjust(-5.0, 0.0, 5.0, 0.0);
    if (bounds.height() < 10.0) bounds.adjust(0.0, -5.0, 0.0, 5.0);

    QVector<NodeState> open;
    QHash<QString, int> openIndexByKey;
    QHash<QString, double> bestCost;
    QHash<QString, QString> cameFrom;
    QHash<QString, QPointF> keyToPoint;
    QHash<QString, int> keyToDir;
    QSet<QString> closed;

    const QString startKey = routeNodeKey(start, grid);
    const QString goalKey = routeNodeKey(goal, grid);
    open.append({start, 0.0, heuristic(start), -1});
    openIndexByKey.insert(startKey, 0);
    bestCost.insert(startKey, 0.0);
    keyToPoint.insert(startKey, start);
    keyToDir.insert(startKey, -1);

    int expanded = 0;
    const int maxExpanded = 6000;

    auto pushOrRelax = [&](const QString& parentKey, const QPointF& nextPoint, int dirIndex, double tentativeG) {
        if (!bounds.contains(nextPoint)) return;
        const QString nextKey = routeNodeKey(nextPoint, grid);
        if (closed.contains(nextKey)) return;

        auto oldIt = bestCost.constFind(nextKey);
        if (oldIt != bestCost.constEnd() && tentativeG >= oldIt.value() - 1e-9) return;

        bestCost[nextKey] = tentativeG;
        cameFrom[nextKey] = parentKey;
        keyToPoint[nextKey] = nextPoint;
        keyToDir[nextKey] = dirIndex;

        const double f = tentativeG + heuristic(nextPoint);
        if (openIndexByKey.contains(nextKey)) {
            const int idx = openIndexByKey.value(nextKey);
            open[idx].g = tentativeG;
            open[idx].f = f;
            open[idx].dir = dirIndex;
            open[idx].point = nextPoint;
        } else {
            open.append({nextPoint, tentativeG, f, dirIndex});
            openIndexByKey[nextKey] = open.size() - 1;
        }
    };

    while (!open.isEmpty() && expanded < maxExpanded) {
        int bestIndex = 0;
        for (int i = 1; i < open.size(); ++i) {
            if (open[i].f < open[bestIndex].f) bestIndex = i;
        }

        const NodeState current = open.takeAt(bestIndex);
        openIndexByKey.clear();
        for (int i = 0; i < open.size(); ++i) {
            openIndexByKey[routeNodeKey(open[i].point, grid)] = i;
        }

        const QString currentKey = routeNodeKey(current.point, grid);
        if (closed.contains(currentKey)) continue;
        closed.insert(currentKey);
        ++expanded;

        if (currentKey == goalKey || QLineF(current.point, goal).length() <= grid * 0.6) {
            QVector<QPointF> result;
            QString walkKey = currentKey;
            result.prepend(goal);
            while (true) {
                result.prepend(keyToPoint.value(walkKey, start));
                if (walkKey == startKey) break;
                walkKey = cameFrom.value(walkKey);
                if (walkKey.isEmpty()) break;
            }

            QVector<QPointF> simplified;
            for (const QPointF& p : result) {
                if (simplified.isEmpty() || QLineF(simplified.last(), p).length() > 1e-6) {
                    simplified.append(p);
                }
            }

            QVector<QPointF> merged;
            for (const QPointF& p : simplified) {
                if (merged.size() < 2) {
                    merged.append(p);
                    continue;
                }
                const QPointF a = merged[merged.size() - 2];
                const QPointF b = merged[merged.size() - 1];
                const QPointF ab = b - a;
                const QPointF bc = p - b;
                if (std::abs(cross2d(ab, bc)) <= 1e-6) {
                    merged.last() = p;
                } else {
                    merged.append(p);
                }
            }
            QVector<QPointF> tightened;
            for (int i = 0; i < merged.size(); ) {
                tightened.append(merged[i]);
                if (i == merged.size() - 1) break;

                int furthest = i + 1;
                for (int j = merged.size() - 1; j > i + 1; --j) {
                    if (!segmentBlockedForRouting(merged[i], merged[j])) {
                        furthest = j;
                        break;
                    }
                }
                i = furthest;
            }
            return tightened;
        }

        const int prevDir = keyToDir.value(currentKey, -1);
        for (int dirIndex = 0; dirIndex < dirs.size(); ++dirIndex) {
            const QPointF nextPoint = view()->snapToGrid(current.point + dirs[dirIndex]);
            if (nextPoint == current.point) continue;
            if (segmentBlockedForRouting(current.point, nextPoint)) continue;

            const double stepCost = QLineF(current.point, nextPoint).length();
            const double bendPenalty = (prevDir >= 0 && prevDir != dirIndex) ? 0.35 : 0.0;
            const double tentativeG = bestCost.value(currentKey, 0.0) + stepCost + bendPenalty;
            pushOrRelax(currentKey, nextPoint, dirIndex, tentativeG);
        }
    }

    return failed;
}

bool PCBTraceTool::segmentBlockedForRouting(const QPointF& a, const QPointF& b) const {
    if (!view() || !view()->scene()) return true;
    if (QLineF(a, b).length() < 1e-6) return false;

    TraceItem probe(a, b, m_traceWidth);
    probe.setLayer(m_currentLayer);
    probe.setNetName(m_targetNet);
    const QList<LiveViolation> violations = collectLiveClearanceViolations(probe);

    const bool freeMode = standaloneFreeRoutingMode() && isUnassignedNet(m_targetNet);
    if (freeMode) {
        for (const LiveViolation& violation : violations) {
            if (violation.hard) return true;
        }
        return false;
    }

    return !violations.isEmpty();
}

void PCBTraceTool::updatePreview(QPointF pos) {
    if (!m_previewLine || !m_previewLine2) return;

    if (m_routingMode == Hugging) {
        QPainterPath path = calculateHuggingPath(m_lastPoint, pos);
        m_clearanceHalo->setPath(path);
        m_clearanceHalo->setPen(m_previewLine->pen());
        m_clearanceHalo->setBrush(Qt::NoBrush);
        m_clearanceHalo->show();
        
        m_previewLine->hide();
        m_previewLine2->hide();
        m_clearanceHalo2->hide();
    } else {
        if (m_routingMode == WalkAround) {
            m_currentLevelPoint = chooseWalkAroundElbow(m_lastPoint, pos);
        } else {
            m_currentLevelPoint = QPointF(pos.x(), m_lastPoint.y());
        }

        m_previewLine->show();
        m_previewLine2->show();
        m_previewLine->setLine(m_lastPoint.x(), m_lastPoint.y(), m_currentLevelPoint.x(), m_currentLevelPoint.y());
        m_previewLine2->setLine(m_currentLevelPoint.x(), m_currentLevelPoint.y(), pos.x(), pos.y());
        
        PCBDRC drc;
        double clearance = drc.rules().minClearance();
        double haloWidth = m_traceWidth + (clearance * 2.0);
        
        auto createHaloPath = [&](QPointF p1, QPointF p2) {
            QPainterPath path;
            QLineF line(p1, p2);
            if (line.length() < 0.01) return path;
            QPainterPath linePath;
            linePath.moveTo(p1);
            linePath.lineTo(p2);
            QPainterPathStroker stroker;
            stroker.setWidth(haloWidth);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            return stroker.createStroke(linePath);
        };

        if (m_clearanceHalo && m_clearanceHalo2) {
            QPainterPath path1 = createHaloPath(m_lastPoint, m_currentLevelPoint);
            QPainterPath path2 = createHaloPath(m_currentLevelPoint, pos);
            
            m_clearanceHalo->setPath(path1);
            m_clearanceHalo2->setPath(path2);

            bool collision1 = checkClearance(path1);
            bool collision2 = checkClearance(path2);

            if (m_enableShove) {
                bool didShove = false;
                if (collision1) didShove |= shoveObstacles(path1);
                if (collision2) didShove |= shoveObstacles(path2);
                if (didShove) {
                    collision1 = checkClearance(path1);
                    collision2 = checkClearance(path2);
                }
            }

            QColor safeColor(0, 240, 255, 30);
            QColor errorColor(255, 50, 50, 90);

            m_clearanceHalo->setBrush(collision1 ? errorColor : safeColor);
            m_clearanceHalo->setPen(QPen(collision1 ? QColor(255, 0, 0, 180) : QColor(0, 200, 255, 80), 0.05));
            m_clearanceHalo2->setBrush(collision2 ? errorColor : safeColor);
            m_clearanceHalo2->setPen(QPen(collision2 ? QColor(255, 0, 0, 180) : QColor(0, 200, 255, 80), 0.05));
            
            m_clearanceHalo->show();
            m_clearanceHalo2->show();
        }
    }

    for (int i = 0; i < m_guideLines.size() && i < m_guideTargets.size(); ++i) {
        m_guideLines[i]->setLine(pos.x(), pos.y(), m_guideTargets[i].x(), m_guideTargets[i].y());
    }

    PCBTheme* theme = ThemeManager::theme();
    QColor color = (m_currentLayer == 0) ? theme->topCopper() : theme->bottomCopper();
    color.setAlpha(150);
    QPen p(color, m_traceWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    m_previewLine->setPen(p);
    m_previewLine2->setPen(p);

    for (auto* m : m_drcMarkers) { view()->scene()->removeItem(m); delete m; }
    m_drcMarkers.clear();

    QList<LiveViolation> allViolations;
    auto collectAndAppend = [&](const QPointF& a, const QPointF& b) {
        if (QLineF(a, b).length() < 0.01) return;
        TraceItem probe(a, b, m_traceWidth);
        probe.setLayer(m_currentLayer);
        probe.setNetName(m_targetNet);
        const QList<LiveViolation> v = collectLiveClearanceViolations(probe);
        for (const LiveViolation& lv : v) allViolations.append(lv);
    };
    collectAndAppend(m_lastPoint, m_currentLevelPoint);
    collectAndAppend(m_currentLevelPoint, pos);

    bool hasHard = false;
    for (const LiveViolation& lv : allViolations) {
        if (lv.hard) hasHard = true;
        const QPointF vPos = lv.pos;
        auto* halo = new QGraphicsEllipseItem(vPos.x() - 0.4, vPos.y() - 0.4, 0.8, 0.8);
        if (lv.hard) {
            halo->setPen(QPen(QColor(255, 0, 0), 0.1));
            halo->setBrush(QBrush(QColor(255, 0, 0, 120)));
        } else {
            halo->setPen(QPen(QColor(255, 190, 0), 0.1));
            halo->setBrush(QBrush(QColor(255, 190, 0, 110)));
        }
        halo->setZValue(2500);
        view()->scene()->addItem(halo);
        m_drcMarkers.append(halo);
    }
    if (!allViolations.isEmpty()) {
        QPen violPen(hasHard ? QColor(255, 0, 0) : QColor(255, 190, 0),
                     m_traceWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        m_previewLine->setPen(violPen);
        m_previewLine2->setPen(violPen);
    }

    updateSIRadar(pos);
}

void PCBTraceTool::updateSIRadar(const QPointF& pos) {
    if (!m_siRadarText) return;

    auto stackup = PCBLayerManager::instance().stackup();
    auto results = Flux::Analysis::SignalIntegrityEngine::analyzeLive(view()->scene(), pos, m_traceWidth, m_currentLayer, stackup, m_targetNet);

    if (results.valid) {
        QString text = QString("Z0: %1 Ω  |  %2 ps/mm")
                        .arg(results.characteristicImpedance, 0, 'f', 1)
                        .arg(results.propagationDelay, 0, 'f', 1);
        
        if (results.crosstalkCoupling > 0.01) {
            QString couplingStr = QString::number(results.crosstalkCoupling * 100.0, 'f', 1);
            text += QString("\nCrosstalk: %1% to %2").arg(couplingStr, results.nearestVictimNet);
            m_siRadarText->setBrush(results.crosstalkCoupling > 0.05 ? Qt::red : QColor(255, 165, 0));
        } else {
            m_siRadarText->setBrush(QColor(0, 255, 255)); // Cyan for safe
        }

        m_siRadarText->setText(text);
        m_siRadarText->setPos(pos + QPointF(10, -35));
        m_siRadarText->show();
    } else {
        m_siRadarText->hide();
    }
}

void PCBTraceTool::finishTrace() {
    commitShovedItems();
    if (!m_currentTraceItems.isEmpty()) {
        splitNewTraceItemsAtIntersections();
        createAutoTeardropsForRoute();

        if (view() && view()->undoStack()) {
            QList<PCBItem*> items = m_currentTraceItems;
            view()->undoStack()->push(new PCBAddItemsCommand(view()->scene(), items));
        }
        m_currentTraceItems.clear();
    }
    cleanupPreview();
    rebuildJunctionDots();
    m_isRouting = false;
}

void PCBTraceTool::splitNewTraceItemsAtIntersections() {
    if (!view() || !view()->scene()) return;

    QSet<TraceItem*> routeTraces;
    for (PCBItem* routeItem : m_currentTraceItems) {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(routeItem)) {
            routeTraces.insert(trace);
        }
    }

    if (routeTraces.isEmpty()) return;

    QMap<TraceItem*, QList<QPointF>> splitPoints;
    const QList<TraceItem*> traces = collectSceneTraces(view()->scene());
    for (TraceItem* routeTrace : routeTraces) {
        if (!routeTrace || routeTrace->scene() != view()->scene()) continue;
        const QString net = routeTrace->netName();
        if (net.isEmpty() || net == "No Net") continue;
        const int layer = routeTrace->layer();

        const QPointF rStart = routeTrace->mapToScene(routeTrace->startPoint());
        const QPointF rEnd = routeTrace->mapToScene(routeTrace->endPoint());
        QLineF rLine(rStart, rEnd);
        if (pointsNear(rStart, rEnd)) continue;

        for (TraceItem* other : traces) {
            if (!other || other == routeTrace) continue;
            if (other->scene() != view()->scene()) continue;
            if (other->layer() != layer) continue;
            if (other->netName() != net) continue;

            QPointF oStart = other->mapToScene(other->startPoint());
            QPointF oEnd = other->mapToScene(other->endPoint());
            QLineF oLine(oStart, oEnd);
            QPointF ip;
            if (rLine.intersects(oLine, &ip) != QLineF::BoundedIntersection) continue;
            if (!isPointInternalToTrace(routeTrace, ip)) continue;
            if (!isPointInternalToTrace(other, ip)) continue;

            splitPoints[routeTrace].append(ip);
            splitPoints[other].append(ip);
        }
    }

    if (!splitPoints.isEmpty()) {
        const QList<TraceItem*> keys = splitPoints.keys();
        for (TraceItem* trace : keys) {
            if (!trace || trace->scene() != view()->scene()) continue;
            splitTraceAtScenePoints(trace, splitPoints.value(trace));
        }
    }

    mergeCollinearTouchingSegments();
}

void PCBTraceTool::createAutoTeardropsForRoute() {
    if (!m_autoTeardrops || !view() || !view()->scene()) return;

    QList<TraceItem*> routeTraces;
    for (PCBItem* item : m_currentTraceItems) {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
            routeTraces.append(trace);
        }
    }
    if (routeTraces.isEmpty()) return;

    QSet<QString> producedKeys;
    QList<PCBItem*> newTeardrops;
    for (TraceItem* trace : routeTraces) {
        const QPointF s = trace->mapToScene(trace->startPoint());
        const QPointF e = trace->mapToScene(trace->endPoint());
        const QString sKey = QString("%1|%2|%3").arg(trace->layer()).arg(trace->netName()).arg(pointKey(s));
        const QString eKey = QString("%1|%2|%3").arg(trace->layer()).arg(trace->netName()).arg(pointKey(e));

        if (!producedKeys.contains(sKey)) {
            if (CopperPourItem* td = buildTeardropAtEndpoint(trace, s, e)) {
                view()->scene()->addItem(td);
                newTeardrops.append(td);
                producedKeys.insert(sKey);
            }
        }
        if (!producedKeys.contains(eKey)) {
            if (CopperPourItem* td = buildTeardropAtEndpoint(trace, e, s)) {
                view()->scene()->addItem(td);
                newTeardrops.append(td);
                producedKeys.insert(eKey);
            }
        }
    }

    for (PCBItem* td : newTeardrops) {
        m_currentTraceItems.append(td);
    }
}

bool PCBTraceTool::splitTraceAtScenePoints(TraceItem* trace, const QList<QPointF>& scenePoints) {
    if (!trace || !view() || !view()->scene() || scenePoints.isEmpty()) return false;
    if (trace->scene() != view()->scene()) return false;

    const QPointF start = trace->mapToScene(trace->startPoint());
    const QPointF end = trace->mapToScene(trace->endPoint());
    if (pointsNear(start, end)) return false;

    QList<QPointF> dedup;
    for (const QPointF& p : scenePoints) {
        if (!isPointInternalToTrace(trace, p)) continue;
        bool exists = false;
        for (const QPointF& q : dedup) {
            if (pointsNear(p, q, 1e-2)) {
                exists = true;
                break;
            }
        }
        if (!exists) dedup.append(p);
    }
    if (dedup.isEmpty()) return false;

    std::sort(dedup.begin(), dedup.end(),
              [&](const QPointF& a, const QPointF& b) {
                  return QLineF(start, a).length() < QLineF(start, b).length();
              });

    QList<QPointF> points;
    points.append(start);
    for (const QPointF& p : dedup) points.append(p);
    points.append(end);

    QList<TraceItem*> segments;
    for (int i = 0; i + 1 < points.size(); ++i) {
        if (pointsNear(points[i], points[i + 1])) continue;
        TraceItem* seg = new TraceItem(points[i], points[i + 1], trace->width());
        seg->setLayer(trace->layer());
        seg->setNetName(trace->netName());
        view()->scene()->addItem(seg);
        seg->updateConnectivity();
        segments.append(seg);
    }
    if (segments.isEmpty()) return false;

    const int oldIndex = m_currentTraceItems.indexOf(trace);
    if (oldIndex >= 0) {
        m_currentTraceItems.removeAll(trace);
        for (TraceItem* seg : segments) {
            m_currentTraceItems.append(seg);
        }
    }

    view()->scene()->removeItem(trace);
    delete trace;
    return true;
}

CopperPourItem* PCBTraceTool::buildTeardropAtEndpoint(TraceItem* trace, const QPointF& endpointScene, const QPointF& otherScene) {
    if (!trace || !view() || !view()->scene()) return nullptr;
    if (pointsNear(endpointScene, otherScene)) return nullptr;

    PCBItem* anchor = nullptr;
    const QRectF probe(endpointScene.x() - 0.35, endpointScene.y() - 0.35, 0.7, 0.7);
    const QList<QGraphicsItem*> hits = view()->scene()->items(probe);
    for (QGraphicsItem* g : hits) {
        QGraphicsItem* cur = g;
        while (cur) {
            PCBItem* item = dynamic_cast<PCBItem*>(cur);
            if (item && (item->itemType() == PCBItem::PadType || item->itemType() == PCBItem::ViaType)) {
                if (!trace->netName().isEmpty() && item->netName() != trace->netName()) break;
                if (!pcbItemConnectableOnLayer(item, trace->layer())) break;
                anchor = item;
                break;
            }
            cur = cur->parentItem();
        }
        if (anchor) break;
    }
    if (!anchor) return nullptr;

    const QPointF dirRaw = otherScene - endpointScene;
    const double dirLen = std::hypot(dirRaw.x(), dirRaw.y());
    if (dirLen < 1e-6) return nullptr;
    const QPointF dir = dirRaw / dirLen;
    const QPointF normal(-dir.y(), dir.x());

    double anchorRadius = 0.6;
    if (PadItem* pad = dynamic_cast<PadItem*>(anchor)) {
        const QSizeF s = pad->size();
        anchorRadius = 0.5 * std::max(s.width(), s.height());
    } else if (ViaItem* via = dynamic_cast<ViaItem*>(anchor)) {
        anchorRadius = 0.5 * via->diameter();
    }

    const double traceW = std::max(0.05, trace->width());
    const double tipHalf = traceW * 0.5;
    const double baseHalf = std::max(tipHalf * 1.35, std::min(anchorRadius * 0.70, traceW * 2.6));
    const double length = std::max(traceW * 1.4, std::min(anchorRadius * 1.2, traceW * 4.2));

    QPolygonF poly;
    poly << (endpointScene + normal * baseHalf);
    poly << (endpointScene + normal * (baseHalf * 0.55) + dir * (length * 0.25));
    poly << (endpointScene + normal * tipHalf + dir * length);
    poly << (endpointScene - normal * tipHalf + dir * length);
    poly << (endpointScene - normal * (baseHalf * 0.55) + dir * (length * 0.25));
    poly << (endpointScene - normal * baseHalf);
    if (poly.size() < 3) return nullptr;

    const QRectF dedupeRect(endpointScene.x() - 0.5, endpointScene.y() - 0.5, 1.0, 1.0);
    for (QGraphicsItem* g : view()->scene()->items(dedupeRect)) {
        CopperPourItem* existing = dynamic_cast<CopperPourItem*>(g);
        if (!existing) continue;
        if (existing->name() != "TEARDROP_AUTO") continue;
        if (existing->layer() != trace->layer()) continue;
        if (existing->netName() != trace->netName()) continue;
        if (existing->shape().contains(existing->mapFromScene(endpointScene))) return nullptr;
    }

    CopperPourItem* td = new CopperPourItem();
    td->setName("TEARDROP_AUTO");
    td->setNetName(trace->netName());
    td->setLayer(trace->layer());
    td->setSolid(true);
    td->setPriority(100);
    td->setRemoveIslands(false);
    td->setUseThermalReliefs(false);
    td->setClearance(0.0);
    td->setPolygon(poly);
    return td;
}

void PCBTraceTool::undoLastSegment() {
    if (m_currentTraceItems.isEmpty()) return;
    PCBItem* last = m_currentTraceItems.takeLast();
    view()->scene()->removeItem(last);
    if (TraceItem* t = dynamic_cast<TraceItem*>(last)) {
        m_lastPoint = t->startPoint();
    } else if (ViaItem* v = dynamic_cast<ViaItem*>(last)) {
        m_lastPoint = v->pos();
        m_currentLayer = (m_currentLayer == 0) ? 1 : 0;
    }
    delete last;
    updatePreview(m_lastPoint);
}

void PCBTraceTool::cancelTrace() {
    revertShovedItems();
    if (view() && view()->scene()) {
        for (PCBItem* item : m_currentTraceItems) {
            view()->scene()->removeItem(item);
            delete item;
        }
        cleanupPreview();
    }
    m_currentTraceItems.clear();
    m_isRouting = false;
}

void PCBTraceTool::cleanupPreview() {
    if (!view() || !view()->scene()) return;
    for (auto* h : m_netHighlights) { view()->scene()->removeItem(h); delete h; }
    m_netHighlights.clear();
    for (auto* g : m_guideLines) { view()->scene()->removeItem(g); delete g; }
    m_guideLines.clear();
    for (auto* m : m_drcMarkers) { view()->scene()->removeItem(m); delete m; }
    m_drcMarkers.clear();
    if (m_previewLine) { view()->scene()->removeItem(m_previewLine); delete m_previewLine; }
    if (m_previewLine2) { view()->scene()->removeItem(m_previewLine2); delete m_previewLine2; }
    if (m_clearanceHalo) { view()->scene()->removeItem(m_clearanceHalo); delete m_clearanceHalo; }
    if (m_clearanceHalo2) { view()->scene()->removeItem(m_clearanceHalo2); delete m_clearanceHalo2; }
    if (m_siRadarText) { view()->scene()->removeItem(m_siRadarText); delete m_siRadarText; }
    m_previewLine = nullptr; m_previewLine2 = nullptr; m_clearanceHalo = nullptr; m_clearanceHalo2 = nullptr;
    m_siRadarText = nullptr;
    m_targetNet = ""; m_guideTargets.clear();
}

void PCBTraceTool::mergeCollinearTouchingSegments() {
    if (!view() || !view()->scene()) return;

    bool mergedAny = true;
    while (mergedAny) {
        mergedAny = false;
        QList<TraceItem*> traces = collectSceneTraces(view()->scene());

        QMap<QString, int> endpointCounts;
        for (TraceItem* t : traces) {
            const QPointF s = t->mapToScene(t->startPoint());
            const QPointF e = t->mapToScene(t->endPoint());
            endpointCounts[pointKey(s)]++;
            endpointCounts[pointKey(e)]++;
        }

        for (int i = 0; i < traces.size() && !mergedAny; ++i) {
            TraceItem* a = traces[i];
            if (!a || a->scene() != view()->scene()) continue;
            for (int j = i + 1; j < traces.size() && !mergedAny; ++j) {
                TraceItem* b = traces[j];
                if (!b || b->scene() != view()->scene()) continue;
                if (a->layer() != b->layer()) continue;
                if (a->netName() != b->netName() || a->netName().isEmpty()) continue;
                if (std::abs(a->width() - b->width()) > 1e-6) continue;

                QPointF shared, aOther, bOther;
                if (!shareEndpoint(a, b, &shared, &aOther, &bOther)) continue;
                if (endpointCounts.value(pointKey(shared), 0) != 2) continue;
                if (!collinearAroundShared(shared, aOther, bOther)) continue;
                if (pointsNear(aOther, bOther)) continue;

                TraceItem* merged = new TraceItem(aOther, bOther, a->width());
                merged->setLayer(a->layer());
                merged->setNetName(a->netName());
                view()->scene()->addItem(merged);
                merged->updateConnectivity();

                const bool aInRoute = m_currentTraceItems.contains(a);
                const bool bInRoute = m_currentTraceItems.contains(b);
                if (aInRoute || bInRoute) {
                    m_currentTraceItems.removeAll(a);
                    m_currentTraceItems.removeAll(b);
                    m_currentTraceItems.append(merged);
                }

                view()->scene()->removeItem(a);
                view()->scene()->removeItem(b);
                delete a;
                delete b;

                mergedAny = true;
            }
        }
    }
}

void PCBTraceTool::rebuildJunctionDots() {
    if (!view() || !view()->scene()) return;
    QGraphicsScene* scene = view()->scene();

    const QList<QGraphicsItem*> existing = scene->items();
    for (QGraphicsItem* item : existing) {
        if (item && item->data(kJunctionDotTagKey).toBool()) {
            scene->removeItem(item);
            delete item;
        }
    }

    QMap<QString, int> endpointCounts;
    QMap<QString, QPointF> keyToPoint;
    for (TraceItem* t : collectSceneTraces(scene)) {
        if (!t || t->netName().isEmpty() || t->netName() == "No Net") continue;
        const QPointF s = t->mapToScene(t->startPoint());
        const QPointF e = t->mapToScene(t->endPoint());
        const QString ks = QString::number(t->layer()) + "|" + t->netName() + "|" + pointKey(s);
        const QString ke = QString::number(t->layer()) + "|" + t->netName() + "|" + pointKey(e);
        endpointCounts[ks]++;
        endpointCounts[ke]++;
        keyToPoint[ks] = s;
        keyToPoint[ke] = e;
    }

    PCBTheme* theme = ThemeManager::theme();
    QColor dotColor = theme ? theme->accentColor() : QColor(255, 215, 0);
    dotColor.setAlpha(230);

    for (auto it = endpointCounts.constBegin(); it != endpointCounts.constEnd(); ++it) {
        if (it.value() < 3) continue;
        const QPointF p = keyToPoint.value(it.key());
        const double r = 0.45;
        auto* dot = new QGraphicsEllipseItem(p.x() - r, p.y() - r, 2.0 * r, 2.0 * r);
        dot->setPen(QPen(dotColor, 0));
        dot->setBrush(QBrush(dotColor));
        dot->setZValue(2050);
        dot->setAcceptedMouseButtons(Qt::NoButton);
        dot->setData(kJunctionDotTagKey, true);
        scene->addItem(dot);
    }
}

QList<PCBTraceTool::LiveViolation> PCBTraceTool::collectLiveClearanceViolations(const TraceItem& probe) const {
    QList<LiveViolation> results;
    if (!view() || !view()->scene()) return results;

    PCBDRC drc;
    const double globalMin = drc.rules().minClearance();
    const QRectF searchRect = probe.sceneBoundingRect().adjusted(-globalMin, -globalMin, globalMin, globalMin);
    const QList<QGraphicsItem*> candidates = view()->scene()->items(searchRect);

    const QString probeNet = probe.netName();
    double probeClearance = globalMin;
    if (!isUnassignedNet(probeNet)) {
        probeClearance = NetClassManager::instance().getClassForNet(probeNet).clearance;
    }

    for (QGraphicsItem* otherItem : candidates) {
        PCBItem* other = nullptr;
        QGraphicsItem* cur = otherItem;
        while (cur) {
            other = dynamic_cast<PCBItem*>(cur);
            if (other) break;
            cur = cur->parentItem();
        }
        if (!other) continue;
        
        const bool onLayer = pcbItemConnectableOnLayer(other, probe.layer());
        if (!onLayer) continue;

        const PCBItem::ItemType type = other->itemType();
        if (type != PCBItem::PadType && type != PCBItem::ViaType &&
            type != PCBItem::TraceType && type != PCBItem::CopperPourType) {
            continue;
        }

        // PERMISSIVE CONNECTION LOGIC:
        // If we are touching a Pad or Via at its exact center, it's a connection intent, not a collision.
        if ((type == PCBItem::PadType || type == PCBItem::ViaType) &&
            traceEndpointTouchesAnchor(probe, other)) {
            continue;
        }

        if (m_currentTraceItems.contains(other)) continue;
        if (!probeNet.isEmpty() && probeNet == other->netName()) continue;
        double otherClearance = globalMin;
        if (!isUnassignedNet(other->netName())) {
            otherClearance = NetClassManager::instance().getClassForNet(other->netName()).clearance;
        }
        double required = std::max(probeClearance, otherClearance);

        bool customMatched = false;
        if (!isUnassignedNet(probeNet) && !isUnassignedNet(other->netName())) {
            const double custom = NetClassManager::instance().getCustomClearanceForNets(
                probeNet, other->netName(), &customMatched);
            if (customMatched) required = custom;
        }

        QPointF vPos;
        if (drc.checkItemClearance(const_cast<TraceItem*>(&probe), other, required, vPos)) {
            const QPainterPath pProbe = probe.sceneTransform().map(probe.shape());
            const QPainterPath pOther = other->sceneTransform().map(other->shape());
            const bool isHard = pProbe.intersects(pOther);
            results.append({vPos, isHard});
        }
    }
    return results;
}

bool PCBTraceTool::shoveTraceItemRecursive(TraceItem* obstacle, const QPainterPath& collider, int depth) {
    if (depth > 5 || obstacle->isLocked()) return false;
    
    if (!m_shovedItems.contains(obstacle)) {
        m_shovedItems.insert(obstacle);
        m_originalGeometries[obstacle] = {obstacle->startPoint(), obstacle->endPoint()};
    }

    QLineF obsLine(obstacle->startPoint(), obstacle->endPoint());
    QPointF normal = obsLine.normalVector().unitVector().p2() - obsLine.normalVector().unitVector().p1();
    const double clearance = requiredClearanceTo(obstacle);
    double stepSize = 0.1;
    bool resolved = false;
    
    for (int step = 1; step <= 10; ++step) {
        QPointF shift = normal * (stepSize * step);
        for (int dir : {1, -1}) {
            QPointF currentShift = shift * dir;
            QPointF newStart = m_originalGeometries[obstacle].first + currentShift;
            QPointF newEnd = m_originalGeometries[obstacle].second + currentShift;
            QPainterPathStroker stroker;
            stroker.setWidth(obstacle->width() + (clearance * 2.0));
            QPainterPath obsPath;
            obsPath.moveTo(newStart);
            obsPath.lineTo(newEnd);
            QPainterPath collisionTest = stroker.createStroke(obsPath);
            if (!collisionTest.intersects(collider)) {
                obstacle->setStartPoint(newStart);
                obstacle->setEndPoint(newEnd);
                QList<QGraphicsItem*> newCollisions = view()->scene()->items(collisionTest);
                bool secondaryCollision = false;
                for (auto* other : newCollisions) {
                    PCBItem* otherPCB = dynamic_cast<PCBItem*>(other);
                    if (!otherPCB || otherPCB == obstacle || m_currentTraceItems.contains(otherPCB)) continue;
                    
                    const bool onLayer = pcbItemConnectableOnLayer(otherPCB, m_currentLayer);
                    if (!onLayer) continue;

                    if (otherPCB->netName() == obstacle->netName() && !obstacle->netName().isEmpty()) continue;
                    if (TraceItem* nextObs = dynamic_cast<TraceItem*>(otherPCB)) {
                        if (!shoveTraceItemRecursive(nextObs, collisionTest, depth + 1)) { secondaryCollision = true; break; }
                    } else if (ViaItem* viaObs = dynamic_cast<ViaItem*>(otherPCB)) {
                        if (!shoveViaItemRecursive(viaObs, collisionTest, depth + 1)) { secondaryCollision = true; break; }
                    } else { secondaryCollision = true; break; }
                }
                if (!secondaryCollision) { resolved = true; break; }
            }
        }
        if (resolved) break;
    }
    return resolved;
}

bool PCBTraceTool::shoveViaItemRecursive(ViaItem* obstacle, const QPainterPath& collider, int depth) {
    if (depth > 5 || !obstacle || obstacle->isLocked() || !view() || !view()->scene()) return false;
    if (!m_shovedItems.contains(obstacle)) {
        m_shovedItems.insert(obstacle);
        m_originalPositions[obstacle] = obstacle->pos();
    }
    const QPointF original = m_originalPositions.value(obstacle, obstacle->pos());
    const QPointF c = collider.boundingRect().center();
    QPointF away = original - c;
    double len = std::hypot(away.x(), away.y());
    if (len < 1e-6) away = QPointF(1.0, 0.0); else away /= len;
    const double clearance = requiredClearanceTo(obstacle);
    for (int step = 1; step <= 12; ++step) {
        const double d = step * 0.15;
        for (int side = -1; side <= 1; side += 2) {
            const QPointF tangent(-away.y(), away.x());
            const QPointF shifted = original + away * d + tangent * (0.08 * step * side);
            obstacle->setPos(shifted);
            obstacle->update();
            QPainterPath viaPath;
            viaPath.addEllipse(shifted, obstacle->diameter() * 0.5, obstacle->diameter() * 0.5);
            QPainterPathStroker stroker;
            stroker.setWidth(clearance * 2.0);
            QPainterPath collisionTest = stroker.createStroke(viaPath);
            collisionTest.addPath(viaPath);
            if (collisionTest.intersects(collider)) continue;
            bool secondaryCollision = false;
            const QList<QGraphicsItem*> newCollisions = view()->scene()->items(collisionTest);
            for (QGraphicsItem* other : newCollisions) {
                PCBItem* otherPCB = dynamic_cast<PCBItem*>(other);
                if (!otherPCB || otherPCB == obstacle || m_currentTraceItems.contains(otherPCB)) continue;

                const bool onLayer = pcbItemConnectableOnLayer(otherPCB, m_currentLayer);
                if (!onLayer) continue;

                if (otherPCB->netName() == obstacle->netName() && !obstacle->netName().isEmpty()) continue;

                if (TraceItem* t = dynamic_cast<TraceItem*>(otherPCB)) {
                    if (!shoveTraceItemRecursive(t, collisionTest, depth + 1)) { secondaryCollision = true; break; }
                } else if (ViaItem* v = dynamic_cast<ViaItem*>(otherPCB)) {
                    if (!shoveViaItemRecursive(v, collisionTest, depth + 1)) { secondaryCollision = true; break; }
                } else { secondaryCollision = true; break; }
            }
            if (!secondaryCollision) return true;
        }
    }
    return false;
}

void PCBTraceTool::revertShovedItems() {
    for (auto* item : m_shovedItems) {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
            trace->setStartPoint(m_originalGeometries[trace].first);
            trace->setEndPoint(m_originalGeometries[trace].second);
            trace->update();
        } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
            via->setPos(m_originalPositions.value(via, via->pos()));
            via->update();
        }
    }
    m_shovedItems.clear();
    m_originalGeometries.clear();
    m_originalPositions.clear();
}

void PCBTraceTool::commitShovedItems() {
    if (m_shovedItems.isEmpty()) return;
    QList<PCBItem*> items;
    QList<QPair<QPointF, QPointF>> oldGeoms, newGeoms;
    for (auto* item : m_shovedItems) {
        if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
            items.append(trace);
            oldGeoms.append(m_originalGeometries[trace]);
            newGeoms.append({trace->startPoint(), trace->endPoint()});
        }
    }
    if (!items.isEmpty() && view()->undoStack()) {
        view()->undoStack()->push(new PCBShoveTracesCommand(view()->scene(), items, oldGeoms, newGeoms));
    }
    m_shovedItems.clear();
    m_originalGeometries.clear();
    m_originalPositions.clear();
}

bool PCBTraceTool::shoveObstacles(const QPainterPath& routingPath) {
    if (!m_enableShove || routingPath.isEmpty() || !view() || !view()->scene()) return false;
    
    revertShovedItems();
    
    QList<QGraphicsItem*> collidingItems = view()->scene()->items(routingPath);
    bool anyShoved = false;
    
    for (auto* item : collidingItems) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem) {
            if (item->parentItem()) pcbItem = dynamic_cast<PCBItem*>(item->parentItem());
        }

        if (pcbItem) {
            const bool onLayer = pcbItemConnectableOnLayer(pcbItem, m_currentLayer);

            if (onLayer) {
                if (pcbItem->netName() == m_targetNet && !m_targetNet.isEmpty()) continue;
                if (m_currentTraceItems.contains(pcbItem)) continue;

                if (TraceItem* obstacle = dynamic_cast<TraceItem*>(pcbItem)) {
                    if (shoveTraceItemRecursive(obstacle, routingPath, 0)) {
                        anyShoved = true;
                    }
                } else if (ViaItem* obstacleVia = dynamic_cast<ViaItem*>(pcbItem)) {
                    if (shoveViaItemRecursive(obstacleVia, routingPath, 0)) {
                        anyShoved = true;
                    }
                }
            }
        }

    }
    return anyShoved;
}

bool PCBTraceTool::isUnassignedNet(const QString& netName) const {
    const QString trimmed = netName.trimmed();
    return trimmed.isEmpty() || trimmed.compare("No Net", Qt::CaseInsensitive) == 0;
}

bool PCBTraceTool::standaloneFreeRoutingMode() const {
    if (!view() || !view()->scene()) return false;

    for (QGraphicsItem* item : view()->scene()->items()) {
        PCBItem* pcbItem = nullptr;
        QGraphicsItem* current = item;
        while (current) {
            pcbItem = dynamic_cast<PCBItem*>(current);
            if (pcbItem) break;
            current = current->parentItem();
        }
        if (!pcbItem) continue;

        const PCBItem::ItemType type = pcbItem->itemType();
        if (type != PCBItem::PadType && type != PCBItem::ViaType &&
            type != PCBItem::TraceType && type != PCBItem::CopperPourType) {
            continue;
        }

        if (!isUnassignedNet(pcbItem->netName())) {
            return false;
        }
    }

    return true;
}

double PCBTraceTool::requiredClearanceTo(const PCBItem* other) const {
    PCBDRC drc;
    const double globalMin = drc.rules().minClearance();
    double mine = globalMin;
    if (!isUnassignedNet(m_targetNet)) {
        mine = NetClassManager::instance().getClassForNet(m_targetNet).clearance;
    }

    double theirs = globalMin;
    if (other && !isUnassignedNet(other->netName())) {
        theirs = NetClassManager::instance().getClassForNet(other->netName()).clearance;
    }

    double required = std::max(mine, theirs);
    if (other && !isUnassignedNet(m_targetNet) && !isUnassignedNet(other->netName())) {
        bool customMatched = false;
        const double custom = NetClassManager::instance().getCustomClearanceForNets(
            m_targetNet, other->netName(), &customMatched);
        if (customMatched) required = custom;
    }
    return required;
}

void PCBTraceTool::placeVia() {
    if (!view() || !view()->scene()) return;
    ViaItem* via = new ViaItem(m_lastPoint, 0.6);
    via->setNetName(m_targetNet);
    const int nextLayer = (m_currentLayer == 0) ? 1 : 0;
    via->setStartLayer(m_currentLayer);
    via->setEndLayer(nextLayer);
    via->setLayer(m_currentLayer);
    view()->scene()->addItem(via);
    m_currentTraceItems.append(via);
    setCurrentLayer(nextLayer);
    updatePreview(m_lastPoint);
}

QPointF PCBTraceTool::constrainAngle(QPointF from, QPointF to, bool use45) {
    QPointF delta = to - from;
    const double len = std::hypot(delta.x(), delta.y());
    if (len < 1e-9) return to;
    const double stepDeg = use45 ? 45.0 : 90.0;
    const double angle = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
    const double snappedAngle = std::round(angle / stepDeg) * stepDeg;
    const double rad = snappedAngle * M_PI / 180.0;
    QPointF constrained(from.x() + std::cos(rad) * len, from.y() + std::sin(rad) * len);
    if (view()) constrained = view()->snapToGrid(constrained);
    return constrained;
}

bool PCBTraceTool::checkClearance(const QPainterPath& path) {
    if (path.isEmpty() || !view() || !view()->scene()) return false;
    QList<QGraphicsItem*> collidingItems = view()->scene()->items(path);
    for (auto* item : collidingItems) {
        PCBItem* pcbItem = nullptr;
        QGraphicsItem* current = item;
        while (current) {
            pcbItem = dynamic_cast<PCBItem*>(current);
            if (pcbItem) break;
            current = current->parentItem();
        }
        if (pcbItem) {
            if (pcbItem->netName() == m_targetNet && !m_targetNet.isEmpty()) continue;
            if (m_currentTraceItems.contains(pcbItem)) continue;
            
            const bool collidesOnLayer = pcbItemConnectableOnLayer(pcbItem, m_currentLayer);

            if (collidesOnLayer) {
                // PERMISSIVE CONNECTION LOGIC:
                // If the collision is with a Pad or Via at the current route endpoints, it's a valid connection.
                if (pcbItem->itemType() == PCBItem::PadType || pcbItem->itemType() == PCBItem::ViaType) {
                    TraceItem probe(m_lastPoint, view()->mapToScene(view()->mapFromGlobal(QCursor::pos())), m_traceWidth);
                    probe.setLayer(m_currentLayer);
                    if (traceEndpointTouchesAnchor(probe, pcbItem, 0.5)) continue;
                }

                if (pcbItem->itemType() == PCBItem::PadType || pcbItem->itemType() == PCBItem::ViaType || pcbItem->itemType() == PCBItem::TraceType) return true;
            }
        }
    }
    return false;
}

QMap<QString, QVariant> PCBTraceTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Trace Width (mm)"] = m_traceWidth;
    props["Active Layer"] = m_currentLayer;
    props["Enable Shoving"] = m_enableShove;
    props["Routing Mode"] = (m_routingMode == Hugging) ? "Hugging" : 
                            (m_routingMode == WalkAround ? "Walk-Around" : "Orthogonal");
    props["Auto Teardrops"] = m_autoTeardrops;
    return props;
}

void PCBTraceTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Trace Width (mm)") setTraceWidth(value.toDouble());
    else if (name == "Active Layer") setCurrentLayer(value.toInt());
    else if (name == "Enable Shoving") m_enableShove = value.toBool() || value.toString() == "True";
    else if (name == "Routing Mode") {
        QString mode = value.toString();
        if (mode == "Hugging") m_routingMode = Hugging;
        else if (mode == "Walk-Around") m_routingMode = WalkAround;
        else m_routingMode = Orthogonal;
    } else if (name == "Auto Teardrops") m_autoTeardrops = value.toBool() || value.toString() == "True";
}

QPainterPath PCBTraceTool::buildClearanceHaloPath(const QPointF& a, const QPointF& b) {
    QPainterPath out;
    if (QLineF(a, b).length() < 0.01) return out;
    PCBDRC drc;
    const double clearance = drc.rules().minClearance();
    const double haloWidth = m_traceWidth + (clearance * 2.0);
    QPainterPath line;
    line.moveTo(a);
    line.lineTo(b);
    QPainterPathStroker stroker;
    stroker.setWidth(haloWidth);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(line);
}

int PCBTraceTool::routeCollisionScore(const QPointF& a, const QPointF& elbow, const QPointF& b) {
    int score = 0;
    if (segmentBlockedForRouting(a, elbow)) score++;
    if (segmentBlockedForRouting(elbow, b)) score++;
    return score;
}

QPointF PCBTraceTool::chooseWalkAroundElbow(const QPointF& from, const QPointF& to) {
    const QPointF a(to.x(), from.y());
    const QPointF b(from.x(), to.y());
    const int scoreA = routeCollisionScore(from, a, to);
    const int scoreB = routeCollisionScore(from, b, to);
    if (scoreA < scoreB) return a;
    if (scoreB < scoreA) return b;
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    if (std::abs(dx) > std::abs(dy)) return QPointF(from.x() + std::copysign(std::abs(dy), dx), to.y());
    return QPointF(to.x(), from.y() + std::copysign(std::abs(dx), dy));
}
