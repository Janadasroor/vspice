#ifndef FOOTPRINT_EDITOR_VIEW_H
#define FOOTPRINT_EDITOR_VIEW_H

#include <QGraphicsView>
#include <QMouseEvent>
#include <QWheelEvent>

class FootprintEditorView : public QGraphicsView {
    Q_OBJECT
    
public:
    explicit FootprintEditorView(QWidget* parent = nullptr);
    
    void setCurrentTool(int tool) { m_currentTool = tool; }
    int currentTool() const { return m_currentTool; }
    
    QPointF snapToGrid(QPointF pos) const;
    qreal gridSize() const { return m_gridSize; }
    void setGridSize(qreal size);
    
    void setSnapToGrid(bool enabled) { m_snapToGrid = enabled; update(); }
    bool snapToGridEnabled() const { return m_snapToGrid; }
    
signals:
    void toolCancelled();
    void pointClicked(QPointF scenePos);
    void mouseMoved(QPointF scenePos);
    void lineDragged(QPointF start, QPointF end);
    void drawingFinished(QPointF start, QPointF end);
    void rectResizeStarted(const QString& corner, QPointF scenePos);
    void rectResizeUpdated(QPointF scenePos);
    void rectResizeFinished(QPointF scenePos);
    void contextMenuRequested(QPoint pos);
    
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    int m_currentTool;
    qreal m_gridSize;
    bool m_isPanning;
    bool m_isDrawing;
    bool m_isMeasuring;
    bool m_rectResizeActive = false;
    QPoint m_lastPanPoint;
    QPointF m_drawStart;
    QPointF m_measureCurrent;
    bool m_snapToGrid;
};

#endif // FOOTPRINT_EDITOR_VIEW_H
