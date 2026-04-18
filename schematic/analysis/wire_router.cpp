#include "wire_router.h"
#include "diagnostics/runtime_diagnostics.h"
#include <QGraphicsItem>
#include <QDebug>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include "../items/schematic_item.h"

WireRouter::WireRouter(QGraphicsScene* scene)
    : m_scene(scene)
    , m_gridSize(10.0)
    , m_obstacleMargin(5.0)
    , m_hasSearchBounds(false) {
}

QString WireRouter::pointToKey(const QPointF& p) {
    return QString("%1,%2").arg(std::round(p.x())).arg(std::round(p.y()));
}

QList<QPointF> WireRouter::routeOrthogonal(const QPointF& start, const QPointF& end,
                                          const QList<Obstacle>& obstacles,
                                          qreal gridSize,
                                          bool preferredHFirst) {
    FLUX_DIAG_SCOPE("WireRouter::routeOrthogonal");
    m_gridSize = gridSize;
    m_obstacles = obstacles;

    if (m_scene && obstacles.isEmpty()) {
        updateObstaclesFromScene();
    }

    // Check if the simple preferred path is obstacle-free first
    QList<QPointF> simplePath = createSimpleOrthogonalPath(start, end, preferredHFirst);
    if (isPathObstacleFree(simplePath)) {
        return simplePath;
    }

    // Find optimal path using A* algorithm
    QList<QPointF> rawPath = findPath(start, end);

    if (rawPath.isEmpty()) {
        // Fallback to simple orthogonal path if A* fails
        rawPath = simplePath;
    }

    // Optimize the path
    QList<QPointF> optimizedPath = optimizeOrthogonalPath(rawPath);

    return optimizedPath;
}

void WireRouter::updateObstaclesFromScene() {
    if (!m_scene) return;

    m_obstacles.clear();

    // Get all items from the scene as obstacles
    QList<QGraphicsItem*> items = m_scene->items();
    for (QGraphicsItem* item : items) {
        SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem) continue;

        // Skip wires and other non-obstacle items
        if (sItem->itemType() == SchematicItem::WireType || 
            sItem->itemType() == SchematicItem::JunctionType ||
            sItem->itemType() == SchematicItem::LabelType ||
            sItem->itemType() == SchematicItem::NetLabelType ||
            sItem->itemTypeName() == "BusEntry" ||
            sItem->itemTypeName() == "NoConnect") {
            continue;
        }

        QRectF bounds = sItem->boundingRect();
        bounds = sItem->mapRectToScene(bounds);
        bounds.adjust(-m_obstacleMargin, -m_obstacleMargin,
                     m_obstacleMargin, m_obstacleMargin);

        Obstacle obstacle;
        obstacle.bounds = bounds;
        obstacle.isBlocking = true;

        m_obstacles.append(obstacle);
    }
}

QList<QPointF> WireRouter::findPath(const QPointF& start, const QPointF& end) {
    // Snap points to grid
    QPointF startSnapped = snapToGrid(start);
    QPointF endSnapped = snapToGrid(end);

    if (startSnapped == endSnapped) return {startSnapped};

    // Constrain search area
    const qreal span = qMax(qAbs(endSnapped.x() - startSnapped.x()),
                            qAbs(endSnapped.y() - startSnapped.y()));
    const qreal margin = qMax(300.0, span + (m_gridSize * 30.0));
    m_searchBounds = QRectF(QPointF(qMin(startSnapped.x(), endSnapped.x()) - margin,
                                    qMin(startSnapped.y(), endSnapped.y()) - margin),
                            QPointF(qMax(startSnapped.x(), endSnapped.x()) + margin,
                                    qMax(startSnapped.y(), endSnapped.y()) + margin)).normalized();
    m_hasSearchBounds = true;

    auto cmp = [](Node* a, Node* b) { return a->fCostTotal() > b->fCostTotal(); };
    std::priority_queue<Node*, std::vector<Node*>, decltype(cmp)> openQueue(cmp);
    QHash<QString, Node*> allNodes; // For fast lookup and memory management
    QSet<QString> closedKeys;

    Node* startNode = new Node(startSnapped);
    startNode->gCost = 0;
    startNode->hCost = heuristic(startNode->point, endSnapped);
    
    QString startKey = pointToKey(startSnapped);
    allNodes[startKey] = startNode;
    openQueue.push(startNode);

    QElapsedTimer timer;
    timer.start();
    int expandedNodes = 0;
    constexpr int kMaxExpandedNodes = 5000;
    constexpr qint64 kMaxTimeMs = 40; // 40ms limit to keep UI responsive

    while (!openQueue.empty()) {
        // Safety breaks
        if (expandedNodes++ > kMaxExpandedNodes || timer.elapsed() > kMaxTimeMs) break;

        Node* currentNode = openQueue.top();
        openQueue.pop();

        QString currentKey = pointToKey(currentNode->point);
        if (closedKeys.contains(currentKey)) continue;
        
        if (currentNode->point == endSnapped) {
            QList<QPointF> path = reconstructPath(currentNode);
            qDeleteAll(allNodes);
            return path;
        }

        closedKeys.insert(currentKey);

        // Directions: Up, Down, Left, Right
        static const QVector<QPointF> directions = { {0, -1}, {0, 1}, {-1, 0}, {1, 0} };

        for (const QPointF& dir : directions) {
            QPointF nextPt = currentNode->point + dir * m_gridSize;
            
            if (!isValidPosition(nextPt)) continue;
            
            QString nextKey = pointToKey(nextPt);
            if (closedKeys.contains(nextKey)) continue;

            qreal tentativeGCost = currentNode->gCost + m_gridSize;
            
            // Penalty for changing direction (prefer straight lines)
            if (currentNode->parent) {
                QPointF currentDir = currentNode->point - currentNode->parent->point;
                if (currentDir != dir * m_gridSize) {
                    tentativeGCost += m_gridSize * 0.5;
                }
            }

            Node* nextNode = allNodes.value(nextKey, nullptr);
            if (!nextNode) {
                nextNode = new Node(nextPt);
                nextNode->parent = currentNode;
                nextNode->gCost = tentativeGCost;
                nextNode->hCost = heuristic(nextPt, endSnapped);
                allNodes[nextKey] = nextNode;
                openQueue.push(nextNode);
            } else if (tentativeGCost < nextNode->gCost) {
                nextNode->parent = currentNode;
                nextNode->gCost = tentativeGCost;
                openQueue.push(nextNode); // Re-push updated node
            }
        }
    }

    qDeleteAll(allNodes);
    return QList<QPointF>(); // No path found or timed out
}

qreal WireRouter::heuristic(const QPointF& a, const QPointF& b) {
    // Manhattan distance for orthogonal paths
    return qAbs(a.x() - b.x()) + qAbs(a.y() - b.y());
}

bool WireRouter::isValidPosition(const QPointF& point) {
    if (m_hasSearchBounds && !m_searchBounds.contains(point)) {
        return false;
    }

    // Check if point is within reasonable bounds
    if (point.x() < -10000 || point.x() > 10000 ||
        point.y() < -10000 || point.y() > 10000) {
        return false;
    }

    // Check for obstacles
    return !isObstacleAt(point);
}

bool WireRouter::isObstacleAt(const QPointF& point) {
    for (const Obstacle& obstacle : m_obstacles) {
        if (obstacle.isBlocking && obstacle.bounds.contains(point)) {
            return true;
        }
    }
    return false;
}

QList<QPointF> WireRouter::reconstructPath(Node* endNode) {
    QList<QPointF> path;
    Node* currentNode = endNode;

    while (currentNode != nullptr) {
        path.prepend(currentNode->point);
        currentNode = currentNode->parent;
    }

    return path;
}

QList<QPointF> WireRouter::optimizeOrthogonalPath(const QList<QPointF>& path) {
    if (path.size() < 3) return path;

    QList<QPointF> optimized = path;

    // Remove unnecessary waypoints (colinear points)
    for (int i = 1; i < optimized.size() - 1; ++i) {
        QPointF prev = optimized[i - 1];
        QPointF curr = optimized[i];
        QPointF next = optimized[i + 1];

        // Check if three points are colinear
        qreal cross = (curr.x() - prev.x()) * (next.y() - prev.y()) -
                     (curr.y() - prev.y()) * (next.x() - prev.x());

        if (qAbs(cross) < 0.1) { // Nearly colinear
            // Check if we can remove the middle point
            bool canRemove = true;

            // Make sure removal doesn't create obstacle collisions
            // Check all four edges of the obstacle rectangle
            QLineF directLine(prev, next);
            for (const Obstacle& obstacle : m_obstacles) {
                if (obstacle.isBlocking) {
                    // Check if the line intersects any edge of the obstacle
                    QRectF rect = obstacle.bounds;
                    QLineF edges[4] = {
                        QLineF(rect.topLeft(), rect.topRight()),
                        QLineF(rect.topRight(), rect.bottomRight()),
                        QLineF(rect.bottomRight(), rect.bottomLeft()),
                        QLineF(rect.bottomLeft(), rect.topLeft())
                    };
                    
                    for (const QLineF& edge : edges) {
                        QPointF intersection;
                        if (directLine.intersects(edge, &intersection) == QLineF::BoundedIntersection) {
                            canRemove = false;
                            break;
                        }
                    }
                    if (!canRemove) break;
                }
            }

            if (canRemove) {
                optimized.removeAt(i);
                i--; // Adjust index after removal
            }
        }
    }

    return optimized;
}

QPointF WireRouter::snapToGrid(const QPointF& point) {
    qreal x = qRound(point.x() / m_gridSize) * m_gridSize;
    qreal y = qRound(point.y() / m_gridSize) * m_gridSize;
    return QPointF(x, y);
}

qreal WireRouter::distance(const QPointF& a, const QPointF& b) {
    return QPointF(a - b).manhattanLength();
}

QList<QPointF> WireRouter::createSimpleOrthogonalPath(const QPointF& start, const QPointF& end, bool hFirst) {
    QList<QPointF> path;
    path.append(start);

    // Simple orthogonal path: horizontal then vertical, or vertical then horizontal
    QPointF intermediate;

    if (hFirst) {
        // Go horizontal first, then vertical
        intermediate = QPointF(end.x(), start.y());
    } else {
        // Go vertical first, then horizontal
        intermediate = QPointF(start.x(), end.y());
    }

    // Check if intermediate point is valid
    if (isValidPosition(intermediate)) {
        path.append(intermediate);
    }

    path.append(end);
    return path;
}

bool WireRouter::isPathObstacleFree(const QList<QPointF>& path) {
    if (path.size() < 2) return true;
    for (int i = 0; i < path.size() - 1; ++i) {
        QLineF segment(path[i], path[i + 1]);
        for (const Obstacle& obstacle : m_obstacles) {
            if (obstacle.isBlocking) {
                QRectF rect = obstacle.bounds;
                QLineF edges[4] = {
                    QLineF(rect.topLeft(), rect.topRight()),
                    QLineF(rect.topRight(), rect.bottomRight()),
                    QLineF(rect.bottomRight(), rect.bottomLeft()),
                    QLineF(rect.bottomLeft(), rect.topLeft())
                };
                for (const QLineF& edge : edges) {
                    QPointF intersection;
                    if (segment.intersects(edge, &intersection) == QLineF::BoundedIntersection) {
                        return false;
                    }
                }
                // Also check if path points are strictly inside an obstacle
                if (rect.contains(path[i]) || rect.contains(path[i + 1])) {
                    return false;
                }
            }
        }
    }
    return true;
}
