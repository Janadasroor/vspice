#ifndef WIRE_ROUTER_H
#define WIRE_ROUTER_H

#include <QPointF>
#include <QList>
#include <QRectF>
#include <QGraphicsScene>
#include <QSet>
#include <QHash>
#include <queue>
#include <vector>

class WireRouter {
public:
    struct Obstacle {
        QRectF bounds;
        bool isBlocking;  // Some obstacles might be passable (like other wires)
    };

    WireRouter(QGraphicsScene* scene = nullptr);

    // Main routing function
    QList<QPointF> routeOrthogonal(const QPointF& start, const QPointF& end,
                                   const QList<Obstacle>& obstacles = QList<Obstacle>(),
                                   qreal gridSize = 10.0,
                                   bool preferredHFirst = true);

    // Set scene for automatic obstacle detection
    void setScene(QGraphicsScene* scene) { m_scene = scene; }

    // Update obstacle list from scene
    void updateObstaclesFromScene();

    // Get current obstacles
    QList<Obstacle> obstacles() const { return m_obstacles; }

    // Utility functions
    static QString pointToKey(const QPointF& p);

private:
    struct Node {
        QPointF point;
        qreal gCost;  // Cost from start
        qreal hCost;  // Heuristic cost to end
        qreal fCost;  // Total cost
        Node* parent;

        Node(const QPointF& p = QPointF()) : point(p), gCost(0), hCost(0), fCost(0), parent(nullptr) {}
        qreal fCostTotal() const { return gCost + hCost; }

        bool operator==(const Node& other) const {
            return point == other.point;
        }
    };

    // A* pathfinding
    QList<QPointF> findPath(const QPointF& start, const QPointF& end);
    qreal heuristic(const QPointF& a, const QPointF& b);
    bool isValidPosition(const QPointF& point);
    bool isObstacleAt(const QPointF& point);

    // Orthogonal path optimization
    QList<QPointF> optimizeOrthogonalPath(const QList<QPointF>& path);
    QList<QPointF> smoothCorners(const QList<QPointF>& path);

    // Scene interaction
    QGraphicsScene* m_scene;
    QList<Obstacle> m_obstacles;
    qreal m_gridSize;
    qreal m_obstacleMargin;
    QRectF m_searchBounds;
    bool m_hasSearchBounds;

    // A* algorithm helpers
    QList<QPointF> reconstructPath(Node* endNode);

    // Utility functions
    QPointF snapToGrid(const QPointF& point);
    qreal distance(const QPointF& a, const QPointF& b);
    QList<QPointF> createSimpleOrthogonalPath(const QPointF& start, const QPointF& end, bool hFirst);
    bool isPathObstacleFree(const QList<QPointF>& path);
};

#endif // WIRE_ROUTER_H
