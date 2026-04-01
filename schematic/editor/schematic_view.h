#ifndef SCHEMATICVIEW_H
#define SCHEMATICVIEW_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QUndoStack>
#include <cmath>
#include "schematic_tool.h"
#include "../tools/schematic_probe_tool.h"
#include "../../simulator/core/sim_results.h"

class WireItem;
class QGraphicsPixmapItem;
class QGraphicsEllipseItem;

class SchematicView : public QGraphicsView {
    Q_OBJECT

public:
    enum GridStyle {
        Lines,
        Points
    };

    // Snap capture types — used by tools and visual indicators
    enum SnapType {
        GridSnap,
        PinSnap,
        WireVertexSnap,
        WireSegmentSnap
    };

    // Result of a smart snap operation
    struct SnapResult {
        QPointF point;          // The snapped position
        SnapType type = GridSnap;
        SchematicItem* item = nullptr;  // The item we snapped to (nullptr for grid)
        int pinIndex = -1;      // Pin index if PinSnap
    };

    SchematicView(QWidget *parent = nullptr);
    ~SchematicView();

    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    QUndoStack* undoStack() const { return m_undoStack; }

    void setNetManager(class NetManager* nm) { m_netManager = nm; }
    class NetManager* netManager() const { return m_netManager; }

    // Tool management
    void setCurrentTool(const QString& toolName);
    void setCurrentTool(SchematicTool* tool);
    SchematicTool* currentTool() const { return m_currentTool; }
    QStringList availableTools() const;

    // --- Snapping ---
    QPointF snapToGrid(QPointF pos);
    SnapResult snapToPin(QPointF scenePos, qreal radius = 15.0, QGraphicsItem* excludeItem = nullptr) const;
    SnapResult snapToGridOrPin(QPointF scenePos, qreal pinRadius = 15.0, QGraphicsItem* excludeItem = nullptr);
    
    // Auto-numbering
    QString getNextReference(const QString& prefix);

    // Probe cursor overlay (avoid OS cursor size limits)
    void setProbeCursorOverlay(SchematicProbeTool::ProbeKind kind, const QPointF& scenePos);
    void clearProbeCursorOverlay();
    bool isProbeCursorOverlayVisible() const { return m_probeCursorVisible; }
    void showProbeStartMarker(const QPointF& scenePos);
    void clearProbeStartMarker();

signals:
    void coordinatesChanged(QPointF pos);
    void toolChanged(const QString& toolName);
    void selectionChanged();
    void itemRightClicked(class SchematicItem* item);
    void itemDoubleClicked(class SchematicItem* item);
    void itemSelectionDoubleClicked(const QList<SchematicItem*>& items);
    void pageTitleBlockDoubleClicked();
    void syncSheetRequested(class SchematicSheetItem* sheet);
    void runLiveERC(const QList<SchematicItem*>& items);
    void netProbed(const QString& netName);
    void snippetDropped(const QString& json, const QPointF& pos);
    void netlistDropped(const QString& netlist, const QPointF& pos);
    void editSimulationDirective(const QString& commandText);
    void transformationChanged();

public:
    void setGridSize(double size);
    double gridSize() const { return m_gridSize; }

    void setGridVisible(bool visible);
    bool isGridVisible() const { return m_gridVisible; }

    // AI Hints
    void addHint(const QString& text, const QPointF& pos, const QString& ref = "");
    void clearHints();

    void setGridStyle(GridStyle style);
    GridStyle gridStyle() const { return m_gridStyle; }

    void setSimulationRunning(bool running) { m_simulationRunning = running; }
    bool isSimulationRunning() const { return m_simulationRunning; }

    void setProbingEnabled(bool enabled) { m_probingEnabled = enabled; }
    bool isProbingEnabled() const { return m_probingEnabled; }

    void setSnapToGrid(bool enabled) { m_snapToGrid = enabled; }
    bool isSnapToGridEnabled() const { return m_snapToGrid; }

    void setSnapToPin(bool enabled) { m_snapToPin = enabled; }
    bool isSnapToPinEnabled() const { return m_snapToPin; }

    void setShowCrosshair(bool enabled) { m_showCrosshair = enabled; viewport()->update(); }
    bool isCrosshairEnabled() const { return m_showCrosshair; }

    enum SelectionFilter { SelectAll, SelectComponents, SelectWires };
    void setSelectionFilter(SelectionFilter filter) { m_selectionFilter = filter; }
    SelectionFilter selectionFilter() const { return m_selectionFilter; }
    
    void setHeatmapEnabled(bool enabled) { m_heatmapEnabled = enabled; viewport()->update(); }
    bool isHeatmapEnabled() const { return m_heatmapEnabled; }
    
    void setSimulationResults(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents);
    void setLastSimResults(const class SimResults* results);
    void setGeminiPanel(class GeminiPanel* panel);
    void clearSimulationResults();

    void setHandToolActive(bool active);
    bool isHandToolActive() const { return m_handToolActive; }

    // Live ERC Feedback
    void showLiveERCMarkers(const QList<struct ERCViolation>& violations);
    void clearLiveERCMarkers();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void scrollContentsBy(int dx, int dy) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void handleAutoScroll();

private:
    bool m_isPanning;
    QPoint m_lastPanPoint;
    QPoint m_panStartPos;
    double m_zoomFactor;
    double m_gridSize;
    GridStyle m_gridStyle;
    bool m_gridVisible = true;
    bool m_snapToGrid;
    bool m_snapToPin;
    bool m_showCrosshair;
    bool m_simulationRunning = false;
    bool m_probingEnabled = false;
    QPointF m_cursorScenePos;
    QUndoStack* m_undoStack = nullptr;
    class NetManager* m_netManager = nullptr;

    SchematicTool* m_currentTool = nullptr;
    SchematicItem* m_lastHoveredItem = nullptr;
    QMap<SchematicItem*, QSet<int>> m_hoverHighlightedPins;
    QList<QGraphicsItem*> m_liveErcMarkers;
    bool m_probeClickActive = false;
    QString m_probeStartNet;
    QPointF m_probeStartPos;
    QGraphicsEllipseItem* m_probeStartMarker = nullptr;
    QGraphicsPixmapItem* m_probeCursorItem = nullptr;
    bool m_probeCursorVisible = false;
    SchematicProbeTool::ProbeKind m_probeCursorKind = SchematicProbeTool::ProbeKind::Voltage;
    bool m_spacePressed = false;
    bool m_handToolActive = false;
    SelectionFilter m_selectionFilter = SelectAll;
    bool m_heatmapEnabled = false;

    QMap<QString, double> m_simNodeVoltages;
    QMap<QString, double> m_simBranchCurrents;

    QTimer* m_autoScrollTimer;
    QPoint m_autoScrollDelta;

    class SmartProbeOverlay* m_smartProbeOverlay = nullptr;
    class SmartProbeEngine* m_smartProbeEngine = nullptr;
    SimResults m_lastSimResults;
    bool m_hasLastSimResults = false;

    void updateHoverHighlight(SchematicItem* item);
    void clearHoverHighlights();
    void updateAutoScroll(const QPoint& pos);
    void stopAutoScroll();
    void ensureProbeCursorItem();
    void applyProbeCursorPixmap(SchematicProbeTool::ProbeKind kind);
};

#endif // SCHEMATICVIEW_H
