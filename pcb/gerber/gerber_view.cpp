#include "gerber_view.h"
#include <QWheelEvent>
#include <QScrollBar>
#include <QGraphicsPathItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsRectItem>

GerberView::GerberView(QWidget* parent)
    : QGraphicsView(parent), m_isPanning(false) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::NoDrag);
    setBackgroundBrush(Qt::black);
    
    // Improved zoom level
    scale(10.0, -10.0); // Y-axis is inverted in Gerber (up is positive)
}

void GerberView::addLayer(GerberLayer* layer) {
    m_layers.append(layer);
    renderLayer(layer);
}

void GerberView::clear() {
    m_scene->clear();
    m_layers.clear();
}

void GerberView::renderLayer(GerberLayer* layer) {
    if (!layer->isVisible()) return;

    for (const auto& prim : layer->primitives()) {
        GerberAperture ap = layer->getAperture(prim.apertureId);
        
        if (prim.type == GerberPrimitive::Line) {
            double width = ap.params.isEmpty() ? 0.2 : ap.params[0];
            QGraphicsPathItem* item = m_scene->addPath(prim.path);
            item->setPen(QPen(layer->color(), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            item->setVisible(true);
        } else if (prim.type == GerberPrimitive::Flash) {
            double d = ap.params.isEmpty() ? 0.5 : ap.params[0];
            if (ap.type == GerberAperture::Circle) {
                QGraphicsEllipseItem* item = m_scene->addEllipse(prim.center.x() - d/2, prim.center.y() - d/2, d, d);
                item->setBrush(layer->color());
                item->setPen(Qt::NoPen);
                item->setVisible(true);
            } else if (ap.type == GerberAperture::Rectangle || ap.type == GerberAperture::Obround) {
                double h = ap.params.size() > 1 ? ap.params[1] : d;
                if (ap.type == GerberAperture::Obround) {
                    QPainterPath path;
                    qreal r = std::min(d, h) / 2.0;
                    path.addRoundedRect(prim.center.x() - d/2, prim.center.y() - h/2, d, h, r, r);
                    QGraphicsPathItem* item = m_scene->addPath(path);
                    item->setBrush(layer->color());
                    item->setPen(Qt::NoPen);
                    item->setVisible(true);
                } else {
                    QGraphicsRectItem* item = m_scene->addRect(prim.center.x() - d/2, prim.center.y() - h/2, d, h);
                    item->setBrush(layer->color());
                    item->setPen(Qt::NoPen);
                    item->setVisible(true);
                }
            }
        }
    }
}

void GerberView::zoomFit() {
    if (!m_scene->items().isEmpty()) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    }
}

void GerberView::wheelEvent(QWheelEvent* event) {
    const double factor = 1.15;
    if (event->angleDelta().y() > 0) scale(factor, factor);
    else scale(1.0 / factor, 1.0 / factor);
}

void GerberView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GerberView::mouseMoveEvent(QMouseEvent* event) {
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPoint;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPanPoint = event->pos();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GerberView::mouseReleaseEvent(QMouseEvent* event) {
    m_isPanning = false;
    setCursor(Qt::ArrowCursor);
    QGraphicsView::mouseReleaseEvent(event);
}

void GerberView::drawBackground(QPainter* painter, const QRectF& rect) {
    painter->fillRect(rect, Qt::black);
}
