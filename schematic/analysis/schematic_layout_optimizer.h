#ifndef SCHEMATICLAYOUTOPTIMIZER_H
#define SCHEMATICLAYOUTOPTIMIZER_H

#include <QObject>
#include <QPointF>
#include <QList>
#include <QGraphicsScene>

class SchematicItem;
class WireItem;

class SchematicLayoutOptimizer : public QObject {
    Q_OBJECT

public:
    explicit SchematicLayoutOptimizer(QObject* parent = nullptr);

    // Main optimization methods
    void optimizeLayout(QGraphicsScene* scene);
    void applyOrthogonalRouting(QGraphicsScene* scene);
    void minimizeWireCrossings(QGraphicsScene* scene);
    void createBusGroups(QGraphicsScene* scene);
    void applyHDIClearance(QGraphicsScene* scene);

    // Analysis methods
    QList<WireItem*> findCrossingWires(QGraphicsScene* scene);
    QList<QPointF> findOptimalPath(const QPointF& start, const QPointF& end,
                                  const QList<WireItem*>& existingWires);
    bool validateClearance(const QPointF& point, qreal requiredClearance);

    // Helper methods
    bool lineSegmentsIntersect(const QPointF& p1, const QPointF& p2,
                              const QPointF& p3, const QPointF& p4);
    bool lineSegmentIntersection(const QPointF& p1, const QPointF& p2,
                                const QPointF& p3, const QPointF& p4,
                                QPointF& intersection);
    bool isParallelAndClose(WireItem* wire1, WireItem* wire2);

    // Bus grouping
    QList<QList<WireItem*>> identifyBusGroups(QGraphicsScene* scene);
    void alignBusWires(QList<WireItem*>& busWires, qreal spacing = 20.0);

signals:
    void optimizationProgress(int percentage);
    void optimizationComplete(int improvementsMade);

private:
    // Internal optimization algorithms
    void optimizeWireRouting(WireItem* wire, const QList<WireItem*>& allWires);
    void applyMiteredCorners(WireItem* wire);
    bool wiresCross(const WireItem* wire1, const WireItem* wire2);
    QPointF findWireIntersection(const WireItem* wire1, const WireItem* wire2);

    // HDI compliance
    qreal m_minimumClearance;
    qreal m_traceWidth;
    qreal m_viaClearance;

    // Optimization settings
    bool m_enableOrthogonalRouting;
    bool m_enableMiteredCorners;
    bool m_enableBusGrouping;
    qreal m_gridSize;
};

#endif // SCHEMATICLAYOUTOPTIMIZER_H
