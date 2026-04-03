#ifndef PCBTRACETOOL_H
#define PCBTRACETOOL_H

#include "pcb_tool.h"
#include <QPointF>
#include <QGraphicsLineItem>

class TraceItem;
class CopperPourItem;
class ViaItem;
class PCBItem;

/**
 * PCB Trace Tool - Interactive copper trace routing tool
 * 
 * Features:
 * - Click to start trace, click again to place segment
 * - Double-click or Escape to finish
 * - Shows preview line while routing
 * - Snaps to grid
 * - 45°/90° angle constraints with Shift key
 */
class PCBTraceTool : public PCBTool {
    Q_OBJECT
public:
    enum RoutingMode {
        Orthogonal,
        WalkAround,
        Hugging
    };

    explicit PCBTraceTool(QObject* parent = nullptr);
    ~PCBTraceTool() override;

    QCursor cursor() const override;

    void activate(PCBView* view) override;
    void deactivate() override;
    QString tooltip() const override { return "Trace Routing: Click to start, [W] Trace, [V] Via, [Bksp] Undo, [1/2] Layer, Auto Teardrops"; }

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // Trace properties
    double traceWidth() const { return m_traceWidth; }
    void setTraceWidth(double width);
    
    int currentLayer() const { return m_currentLayer; }
    void setCurrentLayer(int layer);

    // PCBTool interface
    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

protected:
    struct LiveViolation {
        QPointF pos;
        bool hard = false; // hard=true short/intersection, false=clearance warning
    };

    void startTrace(QPointF pos);
    void addSegment(QPointF pos);
    void updatePreview(QPointF pos);
    void finishTrace();
    void cancelTrace();
    void cleanupPreview();
    void undoLastSegment();
    void placeVia();
    void splitNewTraceItemsAtIntersections();
    bool splitTraceAtScenePoints(class TraceItem* trace, const QList<QPointF>& scenePoints);
    void mergeCollinearTouchingSegments();
    void rebuildJunctionDots();
    QPointF chooseWalkAroundElbow(const QPointF& from, const QPointF& to);
    QPainterPath calculateHuggingPath(const QPointF& from, const QPointF& to);
    int routeCollisionScore(const QPointF& a, const QPointF& elbow, const QPointF& b);
    QPainterPath buildClearanceHaloPath(const QPointF& a, const QPointF& b);
    void createAutoTeardropsForRoute();
    CopperPourItem* buildTeardropAtEndpoint(TraceItem* trace, const QPointF& endpointScene, const QPointF& otherScene);
    QList<LiveViolation> collectLiveClearanceViolations(const TraceItem& probe) const;
    
    QPointF constrainAngle(QPointF from, QPointF to, bool use45);
    bool checkClearance(const QPainterPath& path);
    
    // Shoving Router logic (Professional Recursive Implementation)
    bool shoveObstacles(const QPainterPath& routingPath);
    bool shoveTraceItemRecursive(TraceItem* obstacle, const QPainterPath& collider, int depth = 0);
    bool shoveViaItemRecursive(ViaItem* obstacle, const QPainterPath& collider, int depth = 0);
    double requiredClearanceTo(const PCBItem* other) const;
    void revertShovedItems();
    void commitShovedItems();

    bool m_isRouting;           // Currently drawing a trace
    QString m_currentNet;       // Net being routed
    QPointF m_startPoint;       // Initial start point of the whole trace
    QPointF m_lastPoint;        // Last placed anchor point
    QPointF m_currentLevelPoint; // Used for dual-segment preview
    
    double m_traceWidth;        // Current trace width (mm)
    int m_currentLayer;         // Current PCB layer (0 = Top, 1 = Bottom)
    RoutingMode m_routingMode;  // Current routing strategy
    bool m_enableShove;         // Interactive Shoving enabled
    bool m_autoTeardrops;       // Create tapered pads/via transitions at route endpoints
    
    QGraphicsLineItem* m_previewLine;      // First segment preview
    QGraphicsLineItem* m_previewLine2;     // Second segment preview (octilinear)
    QGraphicsPathItem* m_clearanceHalo;    // Real-time DRC halo
    QGraphicsPathItem* m_clearanceHalo2;   // Real-time DRC halo for second segment
    
    QList<PCBItem*> m_currentTraceItems;   // All items in current route (tracks + vias)
    QList<QGraphicsItem*> m_netHighlights; // Visual cues for current net targets
    QList<QGraphicsLineItem*> m_guideLines; // Dynamic guidelines while routing
    QList<QPointF> m_guideTargets;          // Cached positions of target pads
    QList<QGraphicsItem*> m_drcMarkers;     // Red halos for live clearance violations
    QString m_targetNet;                   // The net currently being routed

    // SI Radar
    QGraphicsSimpleTextItem* m_siRadarText;
    void updateSIRadar(const QPointF& pos);

    // Shove State
    QSet<class PCBItem*> m_shovedItems;
    QMap<class PCBItem*, QPair<QPointF, QPointF>> m_originalGeometries; // For traces
    QMap<class PCBItem*, QPointF> m_originalPositions; // For vias/pads
};

#endif // PCBTRACETOOL_H
