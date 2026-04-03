#ifndef GERBER_VIEW_H
#define GERBER_VIEW_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include "gerber_layer.h"

/**
 * @brief High-performance renderer for Gerber layers
 */
class GerberView : public QGraphicsView {
    Q_OBJECT
public:
    explicit GerberView(QWidget* parent = nullptr);
    
    void addLayer(GerberLayer* layer);
    void clear();
    void zoomFit();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    void renderLayer(GerberLayer* layer);

    QGraphicsScene* m_scene;
    QList<GerberLayer*> m_layers;
    
    bool m_isPanning;
    QPoint m_lastPanPoint;
};

#endif // GERBER_VIEW_H