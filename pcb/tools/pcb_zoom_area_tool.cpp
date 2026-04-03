#include "pcb_zoom_area_tool.h"
#include "pcb_view.h"
#include <QGraphicsScene>
#include <QMouseEvent>

PCBZoomAreaTool::PCBZoomAreaTool(QObject* parent)
    : PCBTool("Zoom Area", parent)
    , m_rubberBand(nullptr) {
}

PCBZoomAreaTool::~PCBZoomAreaTool() {
    if (m_rubberBand) delete m_rubberBand;
}

void PCBZoomAreaTool::activate(PCBView* view) {
    PCBTool::activate(view);
}

void PCBZoomAreaTool::deactivate() {
    if (m_rubberBand) {
        m_rubberBand->hide();
    }
    PCBTool::deactivate();
}

void PCBZoomAreaTool::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    m_origin = event->pos();
    if (!m_rubberBand) {
        m_rubberBand = new QRubberBand(QRubberBand::Rectangle, view());
    }
    m_rubberBand->setGeometry(QRect(m_origin, QSize()));
    m_rubberBand->show();
    event->accept();
}

void PCBZoomAreaTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBand) {
        m_rubberBand->setGeometry(QRect(m_origin, event->pos()).normalized());
        event->accept();
    }
}

void PCBZoomAreaTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    if (m_rubberBand) {
        QRect rect = m_rubberBand->geometry();
        m_rubberBand->hide();

        if (rect.width() > 5 && rect.height() > 5) {
            QRectF sceneRect = view()->mapToScene(rect).boundingRect();
            view()->fitInView(sceneRect, Qt::KeepAspectRatio);
        }
        event->accept();
    }
}

QCursor PCBZoomAreaTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}
