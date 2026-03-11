#ifndef SCHEMATICSELECTTOOL_H
#define SCHEMATICSELECTTOOL_H

#include "schematic_tool.h"

class SchematicSelectTool : public SchematicTool {
    Q_OBJECT

public:
    SchematicSelectTool(QObject* parent = nullptr);

    // SchematicTool interface
    QString tooltip() const override { return "Select and move schematic items"; }
    QString iconName() const override { return "select"; }
    bool isSelectable() const override { return true; }

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void deactivate() override;

    QPointF snapPoint(QPointF scenePos) override;

private:
    struct AttachedWire {
        class WireItem* wire;
        bool isStartPoint;
        class SchematicItem* anchor;
        int anchorPointIndex;
        bool isHorizontal; 
        bool isVertical;
        QPointF initialNeighborPos; // Store neighbor position at drag start
        bool neighborExternallyAnchored = false;
    };
    QList<AttachedWire> m_attachedWires;

    struct TJunctionTracker {
        class WireItem* movingWire;
        int segmentIndex;
        qreal u;
        class WireItem* attachedWire;
        bool attachedWireIsStart;
    };
    QList<TJunctionTracker> m_tJunctions;
    QPoint m_lastMousePos;
    QPoint m_lastMousePosOrigin;
    QPointF m_initialMouseScenePos;
    bool m_isDragging = false;
    
    // Rubber band selection
    bool m_rubberBandActive = false;
    QPointF m_rubberBandOrigin;
    class QGraphicsRectItem* m_rubberBandItem = nullptr;
    
    // Undo/Redo tracking
    QMap<class SchematicItem*, QPointF> m_initialPositions;
    QMap<class WireItem*, QList<QPointF>> m_initialWirePoints;

    // Segment drag mode for selected wires
    bool m_segmentDragActive = false;
    class WireItem* m_segmentWire = nullptr;
    int m_segmentIndex = -1;
    bool m_segmentIsHorizontal = false;
    bool m_segmentIsVertical = false;
    QPointF m_segmentDragStartScenePos;

    // Vertex drag mode for selected wires
    bool m_vertexDragActive = false;
    class WireItem* m_vertexWire = nullptr;
    int m_vertexIndex = -1;
    QPointF m_vertexDragStartScenePos;

    // Hover cue for pin connection
    class QGraphicsEllipseItem* m_hoverPinIndicator = nullptr;

    // Hover cue for segment drag
    class QGraphicsLineItem* m_hoverSegmentItem = nullptr;
    class QGraphicsEllipseItem* m_hoverHandleStart = nullptr;
    class QGraphicsEllipseItem* m_hoverHandleEnd = nullptr;
    class QGraphicsEllipseItem* m_hoverVertexHandle = nullptr;
    class WireItem* m_hoverWire = nullptr;
    int m_hoverSegmentIndex = -1;
    int m_hoverVertexIndex = -1;

    int findWireVertexAt(class WireItem* wire, const QPointF& scenePos, qreal tolerance = 5.0) const;
    int findWireSegmentAt(class WireItem* wire, const QPointF& scenePos, qreal tolerance = 4.0) const;
    void updateSegmentHoverCue(const QPointF& scenePos);
    void clearSegmentHoverCue();

    void updatePinHoverCue(const QPointF& scenePos);
    void clearPinHoverCue();
    QList<QPointF> simplifyWirePoints(const QList<QPointF>& points) const;
};

#endif // SCHEMATICSELECTTOOL_H
