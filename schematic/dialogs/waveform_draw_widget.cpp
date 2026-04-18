#include "waveform_draw_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QtMath>
#include <algorithm>

WaveformDrawWidget::WaveformDrawWidget(QWidget* parent)
    : QWidget(parent), m_drawing(false), m_snapToGrid(false), m_stepMode(false),
      m_polylineMode(false), m_hasPreview(false) {
    setMinimumSize(360, 180);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void WaveformDrawWidget::setPolylineMode(bool enabled) {
    m_polylineMode = enabled;
    m_hasPreview = false;
    if (m_polylineMode) {
        setCursor(Qt::CrossCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    update();
}

void WaveformDrawWidget::clearPoints() {
    m_points.clear();
    update();
}

void WaveformDrawWidget::setPoints(const QVector<QPointF>& points) {
    m_points = points;
    update();
}

QVector<QPointF> WaveformDrawWidget::points() const {
    return m_points;
}

void WaveformDrawWidget::setSnapToGrid(bool enabled) {
    m_snapToGrid = enabled;
}

void WaveformDrawWidget::setStepMode(bool enabled) {
    m_stepMode = enabled;
}

bool WaveformDrawWidget::isStepMode() const {
    return m_stepMode;
}

void WaveformDrawWidget::reverseTime() {
    if (m_points.isEmpty()) return;
    QVector<QPointF> next;
    for (const auto& p : m_points) {
        next.append(QPointF(1.0 - p.x(), p.y()));
    }
    std::sort(next.begin(), next.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x();
    });
    m_points = next;
    update();
}

void WaveformDrawWidget::shiftTime(double delta) {
    for (auto& p : m_points) {
        p.setX(p.x() + delta);
    }
    QVector<QPointF> next;
    for (const auto& p : m_points) {
        if (p.x() >= 0.0 && p.x() <= 1.0) next.append(p);
    }
    m_points = next;
    update();
}

void WaveformDrawWidget::scaleTime(double factor) {
    for (auto& p : m_points) {
        p.setX(p.x() * factor);
    }
    QVector<QPointF> next;
    for (const auto& p : m_points) {
        if (p.x() <= 1.0) next.append(p);
    }
    m_points = next;
    update();
}

void WaveformDrawWidget::scaleValue(double factor) {
    for (auto& p : m_points) {
        p.setY(qBound(-1.0, p.y() * factor, 1.0));
    }
    update();
}

void WaveformDrawWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_polylineMode) {
            // In polyline mode, we always append
            addPoint(event->pos());
        } else {
            m_drawing = true;
            if (!(event->modifiers() & Qt::ControlModifier)) {
                m_points.clear();
            }
            addPoint(event->pos());
        }
        event->accept();
    } else if (event->button() == Qt::RightButton && m_polylineMode) {
        // Right click to finish current polyline (stop showing preview)
        m_hasPreview = false;
        update();
    }
}

void WaveformDrawWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_polylineMode) {
        m_previewPoint = toNormalized(event->pos());
        if (m_snapToGrid) {
            m_previewPoint.setX(qRound(m_previewPoint.x() * 20.0) / 20.0);
            m_previewPoint.setY(qRound(m_previewPoint.y() * 10.0) / 10.0);
        }
        m_hasPreview = true;
        update();
    } else if (m_drawing && (event->buttons() & Qt::LeftButton)) {
        addPoint(event->pos());
        event->accept();
    }
}

void WaveformDrawWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (!m_polylineMode) {
            m_drawing = false;
            addPoint(event->pos());
        }
        event->accept();
    }
}

void WaveformDrawWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#0f1115"));

    // grid
    p.setPen(QColor(255, 255, 255, 20));
    const int gridX = 20;
    const int gridY = 10;
    for (int i = 1; i < gridX; ++i) {
        int x = rect().left() + i * rect().width() / gridX;
        p.drawLine(x, rect().top(), x, rect().bottom());
    }
    for (int j = 1; j < gridY; ++j) {
        int y = rect().top() + j * rect().height() / gridY;
        p.drawLine(rect().left(), y, rect().right(), y);
    }

    // axes
    p.setPen(QColor(255, 255, 255, 80));
    p.drawLine(rect().left(), rect().center().y(), rect().right(), rect().center().y());

    // waveform
    if (m_points.size() >= 2) {
        QPen pen(QColor("#60a5fa"));
        pen.setWidthF(2.0);
        p.setPen(pen);
        QPainterPath path;
        path.moveTo(toWidget(m_points.first()));
        for (int i = 1; i < m_points.size(); ++i) {
            if (m_stepMode) {
                QPointF p1 = toWidget(m_points[i-1]);
                QPointF p2 = toWidget(m_points[i]);
                path.lineTo(p2.x(), p1.y());
                path.lineTo(p2.x(), p2.y());
            } else {
                path.lineTo(toWidget(m_points[i]));
            }
        }
        p.drawPath(path);
    } else if (m_points.size() == 1) {
        p.setPen(QColor("#60a5fa"));
        p.drawEllipse(toWidget(m_points.first()), 2.5, 2.5);
    }
    
    // Preview line
    if (m_polylineMode && m_hasPreview && !m_points.isEmpty()) {
        QColor previewColor("#60a5fa");
        previewColor.setAlpha(128);
        QPen previewPen(previewColor);
        previewPen.setStyle(Qt::DashLine);
        p.setPen(previewPen);
        
        QPointF lastW = toWidget(m_points.last());
        QPointF nextW = toWidget(m_previewPoint);
        
        if (m_stepMode) {
            p.drawLine(lastW.x(), lastW.y(), nextW.x(), lastW.y());
            p.drawLine(nextW.x(), lastW.y(), nextW.x(), nextW.y());
        } else {
            p.drawLine(lastW, nextW);
        }
    }
    
    if (m_drawing && !m_points.isEmpty()) {
        QPointF n = toNormalized(mapFromGlobal(QCursor::pos()));
        if (n.x() < m_points.last().x()) {
            p.setPen(Qt::red);
            p.drawText(mapFromGlobal(QCursor::pos()) + QPoint(10, -10), "Invalid: Backwards");
        }
    }
}

void WaveformDrawWidget::addPoint(const QPoint& pos) {
    QPointF n = toNormalized(pos);
    if (m_snapToGrid) {
        n.setX(qRound(n.x() * 20.0) / 20.0);
        n.setY(qRound(n.y() * 10.0) / 10.0);
    }
    if (!m_points.isEmpty()) {
        if (n.x() < m_points.last().x()) return;
        if (qAbs(m_points.last().x() - n.x()) < 0.001 && qAbs(m_points.last().y() - n.y()) < 0.001) return;
    }
    m_points.append(n);
    update();
}

QPointF WaveformDrawWidget::toNormalized(const QPoint& pos) const {
    if (width() <= 1 || height() <= 1) return QPointF(0.0, 0.0);
    double x = qBound(0.0, (pos.x() - rect().left()) / double(rect().width()), 1.0);
    double y = qBound(0.0, (pos.y() - rect().top()) / double(rect().height()), 1.0);
    return QPointF(x, 1.0 - 2.0 * y);
}

QPointF WaveformDrawWidget::toWidget(const QPointF& n) const {
    double x = rect().left() + n.x() * rect().width();
    double y = rect().top() + (1.0 - (n.y() + 1.0) * 0.5) * rect().height();
    return QPointF(x, y);
}
