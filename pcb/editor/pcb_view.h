#ifndef PCBVIEW_H
#define PCBVIEW_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QScrollBar>
#include <cmath>
#include "pcb_tool.h"

#include <QUndoStack>

class PCBView : public QGraphicsView {
    Q_OBJECT

public:
    PCBView(QWidget *parent = nullptr);
    ~PCBView();

    // Tool management
    void setCurrentTool(const QString& toolName);
    void setCurrentTool(PCBTool* tool);
    PCBTool* currentTool() const { return m_currentTool; }
    QStringList availableTools() const;

    // Undo/Redo
    void setUndoStack(QUndoStack* stack) { m_undoStack = stack; }
    QUndoStack* undoStack() const { return m_undoStack; }

    QPointF snapToGrid(QPointF pos);
    bool isSnappedToPad(QPointF scenePos);
    void setSnapToGrid(bool enabled) { m_snapToGrid = enabled; update(); }
    bool snapToGridEnabled() const { return m_snapToGrid; }

    void setGridSize(double size) { m_gridSize = size; update(); }
    double gridSize() const { return m_gridSize; }

signals:
    void coordinatesChanged(QPointF pos);
    void toolChanged(const QString& toolName);
    void selectionChanged();
    void statusMessage(const QString& msg);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;

private slots:
    void handleAutoScroll();

private:
    bool m_isPanning;
    QPoint m_lastPanPoint;
    double m_zoomFactor;
    double m_gridSize;

    PCBTool* m_currentTool;
    QUndoStack* m_undoStack;
    bool m_snapToGrid;

    QTimer* m_autoScrollTimer;
    QPoint m_autoScrollDelta;

    void updateAutoScroll(const QPoint& pos);
    void stopAutoScroll();
};

#endif // PCBVIEW_H
