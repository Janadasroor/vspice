#include "schematic_waveform_marker.h"
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <algorithm>
#include <cmath>

SchematicWaveformMarker::SchematicWaveformMarker(const QString& netName, const QString& kind, QGraphicsItem* parent)
    : SchematicItem(parent), m_netName(netName), m_kind(kind) {
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setZValue(1200);
}

QRectF SchematicWaveformMarker::boundingRect() const {
    return QRectF(0, 0, 138, 54);
}

QColor SchematicWaveformMarker::markerColor() const {
    if (m_kind == "I") return QColor("#f59e0b");
    if (m_kind == "P") return QColor("#ef4444");
    return QColor("#22c55e");
}

void SchematicWaveformMarker::updateData(const QVector<double>& x, const QVector<double>& y) {
    m_xData.clear();
    m_yData.clear();
    
    // Filter out NaN and Inf values which can crash the graphics engine or std::minmax
    for (int i = 0; i < y.size(); ++i) {
        if (!std::isnan(y[i]) && !std::isinf(y[i]) && !std::isnan(x[i]) && !std::isinf(x[i])) {
            m_xData.append(x[i]);
            m_yData.append(y[i]);
        }
    }

    if (!m_yData.isEmpty()) {
        auto [min, max] = std::minmax_element(m_yData.begin(), m_yData.end());
        m_minY = *min;
        m_maxY = *max;
        if (m_minY == m_maxY) {
            m_minY -= 1.0;
            m_maxY += 1.0;
        }
    }
    update();
}

void SchematicWaveformMarker::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    // 1) Technical Tag Background
    QRectF rect = boundingRect();
    QColor baseColor(25, 25, 30, 230);
    const QColor accent = markerColor();

    // Shadow
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(rect.translated(2, 2), 2, 2);

    // Main Body
    painter->setBrush(baseColor);
    painter->setPen(QPen(QColor(60, 60, 70), 1));
    painter->drawRoundedRect(rect, 2, 2);

    // Left Accent Bar (Color code)
    painter->setBrush(accent);
    painter->drawRect(QRectF(0, 0, 4, rect.height()));

    // 2) Technical Labels
    QFont titleFont("Inter", 8, QFont::Bold);
    painter->setFont(titleFont);
    painter->setPen(Qt::white);
    
    QString prefix = (m_kind == "I") ? "Current: " : (m_kind == "P") ? "Power: " : "Voltage: ";
    painter->drawText(QRectF(8, 4, 120, 14), Qt::AlignLeft | Qt::AlignVCenter, prefix + m_netName);

    // 3) Waveform Preview (Technical mini-graph)
    if (!m_yData.isEmpty()) {
        QRectF graphRect = rect.adjusted(10, 22, -10, -6);
        
        // Draw sub-grid
        painter->setPen(QPen(QColor(50, 50, 60), 1, Qt::DotLine));
        painter->drawLine(graphRect.left(), graphRect.center().y(), graphRect.right(), graphRect.center().y());

        if (m_yData.size() > 1) {
            QPainterPath path;
            for (int i = 0; i < m_yData.size(); ++i) {
                double xNorm = (double)i / (m_yData.size() - 1);
                double yNorm = (m_yData[i] - m_minY) / (m_maxY - m_minY);

                QPointF p(graphRect.left() + xNorm * graphRect.width(),
                          graphRect.bottom() - yNorm * graphRect.height());

                if (i == 0) path.moveTo(p);
                else path.lineTo(p);
            }

            painter->setPen(QPen(accent, 1.2));
            painter->drawPath(path);
        } else {
            // OP mode: one sample only, draw a point marker.
            const double yNorm = (m_yData.first() - m_minY) / (m_maxY - m_minY);
            const QPointF p(graphRect.center().x(),
                            graphRect.bottom() - yNorm * graphRect.height());
            painter->setPen(QPen(accent, 1.2));
            painter->setBrush(accent);
            painter->drawEllipse(p, 2.6, 2.6);
        }

        // Value Label
        painter->setPen(QColor(180, 180, 190));
        QFont valFont("Inter", 7);
        painter->setFont(valFont);
        QString lastVal = QString::number(m_yData.back(), 'f', 2) + ((m_kind == "I") ? "A" : "V");
        painter->drawText(QRectF(8, rect.height() - 14, 120, 10), Qt::AlignRight | Qt::AlignVCenter, lastVal);
    } else {
        painter->setPen(QColor(100, 100, 110));
        QFont hintFont("Inter", 7, QFont::StyleItalic);
        painter->setFont(hintFont);
        painter->drawText(QRectF(8, 25, 120, 20), Qt::AlignCenter, "Awaiting simulation...");
    }
}

QJsonObject SchematicWaveformMarker::toJson() const {
    QJsonObject j;
    j["type"] = "WaveformMarker";
    j["x"] = pos().x();
    j["y"] = pos().y();
    j["netName"] = m_netName;
    j["kind"] = m_kind;
    return j;
}

bool SchematicWaveformMarker::fromJson(const QJsonObject& json) {
    setPos(json["x"].toDouble(), json["y"].toDouble());
    m_netName = json["netName"].toString();
    m_kind = json["kind"].toString("V");
    return true;
}

SchematicItem* SchematicWaveformMarker::clone() const {
    auto* copy = new SchematicWaveformMarker(m_netName, m_kind);
    copy->setPos(pos());
    return copy;
}
