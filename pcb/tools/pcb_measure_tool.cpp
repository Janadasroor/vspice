#include "pcb_measure_tool.h"
#include "pcb_view.h"
#include <QGraphicsScene>
#include <QMouseEvent>
#include <cmath>

PCBMeasureTool::PCBMeasureTool(QObject* parent)
    : PCBTool("Measure", parent)
    , m_isMeasuring(false)
    , m_line(nullptr)
    , m_label(nullptr)
    , m_labelBg(nullptr) {
}

PCBMeasureTool::~PCBMeasureTool() {
    cleanup();
}

void PCBMeasureTool::activate(PCBView* view) {
    PCBTool::activate(view);
    m_isMeasuring = false;
}

void PCBMeasureTool::deactivate() {
    cleanup();
    PCBTool::deactivate();
}

void PCBMeasureTool::cleanup() {
    if (view() && view()->scene()) {
        if (m_line) { view()->scene()->removeItem(m_line); delete m_line; }
        if (m_label) { view()->scene()->removeItem(m_label); delete m_label; }
        if (m_labelBg) { view()->scene()->removeItem(m_labelBg); delete m_labelBg; }
    }
    m_line = nullptr;
    m_label = nullptr;
    m_labelBg = nullptr;
    m_isMeasuring = false;
}

void PCBMeasureTool::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    cleanup();
    m_startPos = view()->snapToGrid(view()->mapToScene(event->pos()));
    m_isMeasuring = true;

    m_line = new QGraphicsLineItem();
    m_line->setPen(QPen(Qt::cyan, 0, Qt::DashLine));
    m_line->setZValue(5000);
    view()->scene()->addItem(m_line);

    m_labelBg = new QGraphicsRectItem();
    m_labelBg->setBrush(QBrush(QColor(0, 0, 0, 180)));
    m_labelBg->setPen(Qt::NoPen);
    m_labelBg->setZValue(5001);
    view()->scene()->addItem(m_labelBg);

    m_label = new QGraphicsSimpleTextItem();
    m_label->setBrush(Qt::white);
    m_label->setZValue(5002);
    view()->scene()->addItem(m_label);

    updateMeasureGraphics(m_startPos);
    event->accept();
}

void PCBMeasureTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isMeasuring) {
        QPointF currentPos = view()->snapToGrid(view()->mapToScene(event->pos()));
        updateMeasureGraphics(currentPos);
        event->accept();
    }
}

void PCBMeasureTool::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isMeasuring) {
        // Keep the measurement visible until next click or tool change
        m_isMeasuring = false;
        event->accept();
    }
}

void PCBMeasureTool::updateMeasureGraphics(QPointF endPos) {
    if (!m_line || !m_label) return;

    m_line->setLine(QLineF(m_startPos, endPos));

    double dx = endPos.x() - m_startPos.x();
    double dy = endPos.y() - m_startPos.y();
    double dist = std::sqrt(dx*dx + dy*dy);

    QString text = QString("L: %1mm\nΔX: %2\nΔY: %3")
                   .arg(dist, 0, 'f', 3)
                   .arg(dx, 0, 'f', 3)
                   .arg(dy, 0, 'f', 3);
    
    m_label->setText(text);
    
    // Scale text to be readable
    double viewScale = view()->transform().m11();
    if (viewScale > 0) {
        m_label->setScale(1.2 / viewScale); // Constant size on screen
    }

    QRectF textRect = m_label->boundingRect();
    double s = m_label->scale();
    m_label->setPos(endPos + QPointF(5/viewScale, 5/viewScale));
    
    m_labelBg->setRect(m_label->pos().x() - 2/viewScale, 
                       m_label->pos().y() - 2/viewScale, 
                       (textRect.width() * s) + 4/viewScale, 
                       (textRect.height() * s) + 4/viewScale);
}

QCursor PCBMeasureTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}
