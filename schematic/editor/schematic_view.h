#ifndef SCHEMATICVIEW_H
#define SCHEMATICVIEW_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <QUndoStack>
#include <cmath>
#include "schematic_tool.h"

class WireItem;

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

public:
    void setGridSize(double size);
    double gridSize() const { return m_gridSize; }

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
    void contextMenuEvent(QContextMenuEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private slots:
    void handleAutoScroll();

private:
    bool m_isPanning;
    QPoint m_lastPanPoint;
    QPoint m_panStartPos;
    double m_zoomFactor;
    double m_gridSize;
    GridStyle m_gridStyle;
    bool m_snapToGrid;
    bool m_snapToPin;
    bool m_showCrosshair;
    bool m_simulationRunning = false;
    bool m_probingEnabled = false;
    QPointF m_cursorScenePos;
    QUndoStack* m_undoStack;
    class NetManager* m_netManager = nullptr;

    SchematicTool* m_currentTool;
    SchematicItem* m_lastHoveredItem = nullptr;
    QMap<SchematicItem*, QSet<int>> m_hoverHighlightedPins;
    QList<QGraphicsItem*> m_liveErcMarkers;

    QTimer* m_autoScrollTimer;
    QPoint m_autoScrollDelta;

    void updateHoverHighlight(SchematicItem* item);
    void clearHoverHighlights();
    void updateAutoScroll(const QPoint& pos);
    void stopAutoScroll();
};

#endif // SCHEMATICVIEW_H
