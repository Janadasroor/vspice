#include "schematic_zoom_area_tool.h"
#include "schematic_view.h"
#include <QGraphicsScene>

SchematicZoomAreaTool::SchematicZoomAreaTool(QObject* parent)
    : SchematicTool("Zoom Area", parent)
    , m_rubberBand(nullptr) {
}

SchematicZoomAreaTool::~SchematicZoomAreaTool() {
    if (m_rubberBand) delete m_rubberBand;
}

void SchematicZoomAreaTool::activate(SchematicView* view) {
    SchematicTool::activate(view);
}

void SchematicZoomAreaTool::deactivate() {
    if (m_rubberBand) {
        m_rubberBand->hide();
    }
    SchematicTool::deactivate();
}

void SchematicZoomAreaTool::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    m_origin = event->pos();
    if (!m_rubberBand) {
        m_rubberBand = new QRubberBand(QRubberBand::Rectangle, view());
    }
    m_rubberBand->setGeometry(QRect(m_origin, QSize()));
    m_rubberBand->show();
    event->accept();
}

void SchematicZoomAreaTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_rubberBand) {
        m_rubberBand->setGeometry(QRect(m_origin, event->pos()).normalized());
        event->accept();
    }
}

void SchematicZoomAreaTool::mouseReleaseEvent(QMouseEvent* event) {
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

QCursor SchematicZoomAreaTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}
