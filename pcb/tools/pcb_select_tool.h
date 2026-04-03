#ifndef PCBSELECTTOOL_H
#define PCBSELECTTOOL_H

#include "pcb_tool.h"
#include <QList>
#include <QMap>
#include <QPointF>
#include <QSet>

class PCBSelectTool : public PCBTool {
    Q_OBJECT

public:
    PCBSelectTool(QObject* parent = nullptr);

    // PCBTool interface
    QString tooltip() const override { return "Select and move items"; }
    QString iconName() const override { return "select"; }
    bool isSelectable() const override { return true; }
    void deactivate() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum class TraceDragMode {
        None,
        StartPoint,
        EndPoint,
        Segment
    };

    class TraceItem* selectedSingleTrace() const;
    void updateTraceEditHandles();
    void cleanupTraceEditHandles();
    void rebuildTraceJunctionDots();
    QSet<class PCBItem*> collectDragComponentCollisions() const;
    void updateClearanceVisuals();
    void cleanupClearanceVisuals();
    void updateDragCollisionPreview();
    void cleanupDragCollisionPreview();

    bool m_isDragging = false;
    QPoint m_lastMousePos;
    QMap<class PCBItem*, QPointF> m_initialPositions;
    QList<class QGraphicsPathItem*> m_dragHalos;
    QList<class QGraphicsRectItem*> m_collisionOverlays;
    class QGraphicsEllipseItem* m_traceStartHandle = nullptr;
    class QGraphicsEllipseItem* m_traceEndHandle = nullptr;
    class TraceItem* m_traceDragItem = nullptr;
    TraceDragMode m_traceDragMode = TraceDragMode::None;
    QPointF m_traceDragMouseOrigin;
    QPointF m_traceDragStartInitialScene;
    QPointF m_traceDragEndInitialScene;
    QPointF m_traceDragStartInitialLocal;
    QPointF m_traceDragEndInitialLocal;
    QPointF m_dragStartScenePos;
    QPointF m_dragAnchorInitialPos;
    bool m_hasDragAnchor = false;

    bool m_rubberBandActive = false;
    QPointF m_rubberBandOrigin;
    class QGraphicsRectItem* m_rubberBandItem = nullptr;
};

#endif // PCBSELECTTOOL_H
