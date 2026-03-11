#include "mini_scope_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include <cmath>

MiniScopeWidget::MiniScopeWidget(QWidget* parent) : QWidget(parent) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumSize(250, 150);
    setStyleSheet("background-color: #1e1e1e; border: 1px solid #3e3e42; border-radius: 4px;");
}

void MiniScopeWidget::setMultiTraceData(const QMap<QString, QVector<QPointF>>& traces) {
    m_traces.clear();
    
    QList<QColor> palette = {
        QColor(34, 197, 94),  // Green
        QColor(59, 130, 246), // Blue
        QColor(249, 115, 22), // Orange
        QColor(168, 85, 247), // Purple
        QColor(236, 72, 153)  // Pink
    };
    int colorIdx = 0;

    m_globalMinY = 0;
    m_globalMaxY = 0;
    bool first = true;

    for (auto it = traces.begin(); it != traces.end(); ++it) {
        if (it.value().isEmpty()) continue;

        TraceData data;
        data.points = it.value();
        data.color = palette[colorIdx % palette.size()];
        colorIdx++;

        calculateMeasurements(it.key(), it.value());
        
        // Use the calculated min/max
        double localMin = m_traces[it.key()].minV;
        double localMax = m_traces[it.key()].maxV;

        if (first) {
            m_globalMinY = localMin;
            m_globalMaxY = localMax;
            m_minX = data.points.first().x();
            m_maxX = data.points.last().x();
            first = false;
        } else {
            m_globalMinY = std::min(m_globalMinY, localMin);
            m_globalMaxY = std::max(m_globalMaxY, localMax);
            m_minX = std::min(m_minX, data.points.first().x());
            m_maxX = std::max(m_maxX, data.points.last().x());
        }
        
        m_traces[it.key()].points = data.points;
        m_traces[it.key()].color = data.color;
    }

    // Add padding
    double range = m_globalMaxY - m_globalMinY;
    if (range < 0.1) {
        m_globalMinY -= 0.5;
        m_globalMaxY += 0.5;
    } else {
        m_globalMinY -= range * 0.1;
        m_globalMaxY += range * 0.1;
    }

    update();
}

void MiniScopeWidget::setData(const QVector<QPointF>& points) {
    QMap<QString, QVector<QPointF>> t;
    t["out"] = points;
    setMultiTraceData(t);
}

void MiniScopeWidget::calculateMeasurements(const QString& name, const QVector<QPointF>& points) {
    if (points.isEmpty()) return;

    double sumSq = 0;
    double minV = points[0].y();
    double maxV = points[0].y();
    
    for (const auto& p : points) {
        sumSq += p.y() * p.y();
        minV = std::min(minV, p.y());
        maxV = std::max(maxV, p.y());
    }

    m_traces[name].minV = minV;
    m_traces[name].maxV = maxV;
    m_traces[name].rms = std::sqrt(sumSq / points.size());

    // Basic Frequency Detection (Zero-crossing)
    int crossings = 0;
    double avg = (maxV + minV) / 2.0;
    for (int i = 1; i < points.size(); ++i) {
        if ((points[i-1].y() < avg && points[i].y() >= avg) ||
            (points[i-1].y() > avg && points[i].y() <= avg)) {
            crossings++;
        }
    }
    double duration = points.last().x() - points.first().x();
    if (duration > 0 && crossings > 1) {
        m_traces[name].freq = (crossings / 2.0) / duration;
    } else {
        m_traces[name].freq = 0;
    }
}

void MiniScopeWidget::clear() {
    m_traces.clear();
    update();
}

void MiniScopeWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();
    int graphW = w - 100; // Leave space for legend

    // Draw Grid
    painter.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
    painter.drawLine(0, h / 2, graphW, h / 2);
    painter.drawLine(graphW / 2, 0, graphW / 2, h);
    painter.setPen(QPen(QColor(40, 40, 40), 1));
    painter.drawLine(graphW, 0, graphW, h);

    if (m_traces.isEmpty()) {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(QRect(0, 0, graphW, h), Qt::AlignCenter, "No Signal");
        return;
    }

    auto mapX = [&](double x) {
        double range = std::max(1e-9, m_maxX - m_minX);
        return (x - m_minX) / range * graphW;
    };

    auto mapY = [&](double y) {
        double range = std::max(1e-9, m_globalMaxY - m_globalMinY);
        return h - ((y - m_globalMinY) / range * h);
    };

    // Draw Traces
    int legendY = 15;
    for (auto it = m_traces.begin(); it != m_traces.end(); ++it) {
        const auto& data = it.value();
        QPainterPath path;
        path.moveTo(mapX(data.points[0].x()), mapY(data.points[0].y()));
        for (int i = 1; i < data.points.size(); ++i) {
            path.lineTo(mapX(data.points[i].x()), mapY(data.points[i].y()));
        }

        painter.setPen(QPen(data.color, 1.5));
        painter.drawPath(path);

        // Draw Legend & Measurements
        painter.setPen(data.color);
        painter.drawRect(graphW + 5, legendY - 8, 8, 8);
        
        painter.setPen(Qt::white);
        QFont f = font();
        f.setPointSize(7);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(graphW + 18, legendY, it.key().toUpper());
        
        f.setBold(false);
        painter.setFont(f);
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(graphW + 10, legendY + 15, QString("Pk-Pk: %1V").arg(data.maxV - data.minV, 0, 'f', 2));
        painter.drawText(graphW + 10, legendY + 28, QString("RMS:   %1V").arg(data.rms, 0, 'f', 2));
        if (data.freq > 0) {
            QString freqStr = data.freq > 1000 ? QString("%1kHz").arg(data.freq/1000.0, 0, 'f', 1) : QString("%1Hz").arg(data.freq, 0, 'f', 0);
            painter.drawText(graphW + 10, legendY + 41, QString("Freq:  %1").arg(freqStr));
        }
        
        legendY += 60;
    }
    
    // Global Labels
    painter.setPen(QColor(150, 150, 150));
    painter.drawText(5, 12, QString("%1V").arg(m_globalMaxY, 0, 'f', 1));
    painter.drawText(5, h - 5, QString("%1V").arg(m_globalMinY, 0, 'f', 1));
}

