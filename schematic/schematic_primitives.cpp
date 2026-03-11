#include "schematic_primitives.h"
#include <QPolygonF>
#include <cmath>

// --- LinePrimitive ---

LinePrimitive::LinePrimitive(const QPointF& start, const QPointF& end)
    : m_start(start), m_end(end) {}

void LinePrimitive::paint(QPainter* painter, const QPen& pen, const QBrush&) const {
    painter->setPen(pen);
    painter->drawLine(m_start, m_end);
}

QRectF LinePrimitive::boundingRect() const {
    qreal minX = qMin(m_start.x(), m_end.x());
    qreal minY = qMin(m_start.y(), m_end.y());
    qreal maxX = qMax(m_start.x(), m_end.x());
    qreal maxY = qMax(m_start.y(), m_end.y());
    return QRectF(minX, minY, maxX - minX, maxY - minY).adjusted(-1, -1, 1, 1);
}

QJsonObject LinePrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["x1"] = m_start.x();
    obj["y1"] = m_start.y();
    obj["x2"] = m_end.x();
    obj["y2"] = m_end.y();
    return obj;
}

std::unique_ptr<SchematicPrimitive> LinePrimitive::clone() const {
    return std::make_unique<LinePrimitive>(m_start, m_end);
}

// --- CirclePrimitive ---

CirclePrimitive::CirclePrimitive(const QPointF& center, qreal radius, bool filled)
    : m_center(center), m_radius(radius), m_filled(filled) {}

void CirclePrimitive::paint(QPainter* painter, const QPen& pen, const QBrush& brush) const {
    painter->setPen(pen);
    painter->setBrush(m_filled ? brush : Qt::NoBrush);
    painter->drawEllipse(m_center, m_radius, m_radius);
}

QRectF CirclePrimitive::boundingRect() const {
    return QRectF(m_center.x() - m_radius, m_center.y() - m_radius,
                  m_radius * 2, m_radius * 2).adjusted(-1, -1, 1, 1);
}

QJsonObject CirclePrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["cx"] = m_center.x();
    obj["cy"] = m_center.y();
    obj["r"] = m_radius;
    obj["filled"] = m_filled;
    return obj;
}

std::unique_ptr<SchematicPrimitive> CirclePrimitive::clone() const {
    return std::make_unique<CirclePrimitive>(m_center, m_radius, m_filled);
}

// --- ArcPrimitive ---

ArcPrimitive::ArcPrimitive(const QRectF& rect, int startAngle, int spanAngle)
    : m_rect(rect), m_startAngle(startAngle), m_spanAngle(spanAngle) {}

void ArcPrimitive::paint(QPainter* painter, const QPen& pen, const QBrush&) const {
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(m_rect, m_startAngle * 16, m_spanAngle * 16);
}

QRectF ArcPrimitive::boundingRect() const {
    return m_rect.adjusted(-1, -1, 1, 1);
}

QJsonObject ArcPrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["x"] = m_rect.x();
    obj["y"] = m_rect.y();
    obj["w"] = m_rect.width();
    obj["h"] = m_rect.height();
    obj["start"] = m_startAngle;
    obj["span"] = m_spanAngle;
    return obj;
}

std::unique_ptr<SchematicPrimitive> ArcPrimitive::clone() const {
    return std::make_unique<ArcPrimitive>(m_rect, m_startAngle, m_spanAngle);
}

// --- PolygonPrimitive ---

PolygonPrimitive::PolygonPrimitive(const QList<QPointF>& points, bool filled)
    : m_points(points), m_filled(filled) {}

void PolygonPrimitive::paint(QPainter* painter, const QPen& pen, const QBrush& brush) const {
    if (m_points.isEmpty()) return;
    painter->setPen(pen);
    painter->setBrush(m_filled ? brush : Qt::NoBrush);
    QPolygonF poly(m_points);
    painter->drawPolygon(poly);
}

QRectF PolygonPrimitive::boundingRect() const {
    if (m_points.isEmpty()) return QRectF();
    QPolygonF poly(m_points);
    return poly.boundingRect().adjusted(-1, -1, 1, 1);
}

QJsonObject PolygonPrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["filled"] = m_filled;
    QJsonArray pts;
    for (const QPointF& p : m_points) {
        QJsonObject pt;
        pt["x"] = p.x();
        pt["y"] = p.y();
        pts.append(pt);
    }
    obj["points"] = pts;
    return obj;
}

std::unique_ptr<SchematicPrimitive> PolygonPrimitive::clone() const {
    return std::make_unique<PolygonPrimitive>(m_points, m_filled);
}

// --- PolylinePrimitive ---

PolylinePrimitive::PolylinePrimitive(const QList<QPointF>& points)
    : m_points(points) {}

void PolylinePrimitive::paint(QPainter* painter, const QPen& pen, const QBrush&) const {
    if (m_points.size() < 2) return;
    painter->setPen(pen);
    for (int i = 0; i < m_points.size() - 1; ++i) {
        painter->drawLine(m_points[i], m_points[i + 1]);
    }
}

QRectF PolylinePrimitive::boundingRect() const {
    if (m_points.isEmpty()) return QRectF();
    QPolygonF poly(m_points);
    return poly.boundingRect().adjusted(-1, -1, 1, 1);
}

QJsonObject PolylinePrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    QJsonArray pts;
    for (const QPointF& p : m_points) {
        QJsonObject pt;
        pt["x"] = p.x();
        pt["y"] = p.y();
        pts.append(pt);
    }
    obj["points"] = pts;
    return obj;
}

std::unique_ptr<SchematicPrimitive> PolylinePrimitive::clone() const {
    return std::make_unique<PolylinePrimitive>(m_points);
}

// --- RectPrimitive ---

RectPrimitive::RectPrimitive(const QRectF& rect, bool filled)
    : m_rect(rect), m_filled(filled) {}

void RectPrimitive::paint(QPainter* painter, const QPen& pen, const QBrush& brush) const {
    painter->setPen(pen);
    painter->setBrush(m_filled ? brush : Qt::NoBrush);
    painter->drawRect(m_rect);
}

QRectF RectPrimitive::boundingRect() const {
    return m_rect.adjusted(-1, -1, 1, 1);
}

QJsonObject RectPrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["x"] = m_rect.x();
    obj["y"] = m_rect.y();
    obj["w"] = m_rect.width();
    obj["h"] = m_rect.height();
    obj["filled"] = m_filled;
    return obj;
}

std::unique_ptr<SchematicPrimitive> RectPrimitive::clone() const {
    return std::make_unique<RectPrimitive>(m_rect, m_filled);
}

// --- TextPrimitive ---

TextPrimitive::TextPrimitive(const QString& text, const QPointF& pos, int fontSize)
    : m_text(text), m_pos(pos), m_fontSize(fontSize) {}

void TextPrimitive::paint(QPainter* painter, const QPen& pen, const QBrush&) const {
    painter->setPen(pen);
    QFont font = painter->font();
    font.setPointSize(m_fontSize);
    painter->setFont(font);
    painter->drawText(m_pos, m_text);
}

QRectF TextPrimitive::boundingRect() const {
    QFont font;
    font.setPointSize(m_fontSize);
    QFontMetricsF fm(font);
    QRectF textRect = fm.boundingRect(m_text);
    // Move the rectangle so that its baseline is at m_pos
    // In fm.boundingRect(), the baseline is at y=0.
    textRect.translate(m_pos);
    return textRect.adjusted(-2, -2, 2, 2);
}

QJsonObject TextPrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["text"] = m_text;
    obj["x"] = m_pos.x();
    obj["y"] = m_pos.y();
    obj["size"] = m_fontSize;
    return obj;
}

std::unique_ptr<SchematicPrimitive> TextPrimitive::clone() const {
    return std::make_unique<TextPrimitive>(m_text, m_pos, m_fontSize);
}

// --- PinPrimitive ---

PinPrimitive::PinPrimitive(const QPointF& pos, const QString& name, PinDirection dir, qreal length)
    : m_pos(pos), m_name(name), m_direction(dir), m_length(length) {}

QPointF PinPrimitive::connectionPoint() const {
    switch (m_direction) {
        case Left:  return m_pos + QPointF(-m_length, 0);
        case Right: return m_pos + QPointF(m_length, 0);
        case Up:    return m_pos + QPointF(0, -m_length);
        case Down:  return m_pos + QPointF(0, m_length);
    }
    return m_pos;
}

void PinPrimitive::paint(QPainter* painter, const QPen& pen, const QBrush&) const {
    painter->setPen(pen);
    
    QPointF end = connectionPoint();
    painter->drawLine(m_pos, end);
    
    // Draw connection dot at end
    painter->setBrush(pen.color());
    painter->drawEllipse(end, 2.5, 2.5);
    
    // Draw pin name
    if (!m_name.isEmpty()) {
        QFont font = painter->font();
        font.setPointSize(8);
        painter->setFont(font);
        
        QPointF textPos = m_pos;
        switch (m_direction) {
            case Left:  textPos += QPointF(3, -3); break;
            case Right: textPos += QPointF(-20, -3); break;
            case Up:    textPos += QPointF(3, 10); break;
            case Down:  textPos += QPointF(3, -3); break;
        }
        painter->drawText(textPos, m_name);
    }
}

QRectF PinPrimitive::boundingRect() const {
    QPointF end = connectionPoint();
    qreal minX = qMin(m_pos.x(), end.x()) - 5;
    qreal minY = qMin(m_pos.y(), end.y()) - 15;
    qreal maxX = qMax(m_pos.x(), end.x()) + 25;
    qreal maxY = qMax(m_pos.y(), end.y()) + 5;
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

QJsonObject PinPrimitive::toJson() const {
    QJsonObject obj;
    obj["type"] = typeName();
    obj["x"] = m_pos.x();
    obj["y"] = m_pos.y();
    obj["name"] = m_name;
    obj["dir"] = static_cast<int>(m_direction);
    obj["len"] = m_length;
    return obj;
}

std::unique_ptr<SchematicPrimitive> PinPrimitive::clone() const {
    return std::make_unique<PinPrimitive>(m_pos, m_name, m_direction, m_length);
}

// --- Factory function ---

std::unique_ptr<SchematicPrimitive> SchematicPrimitive::fromJson(const QJsonObject& json) {
    QString type = json["type"].toString();
    
    if (type == "line") {
        return std::make_unique<LinePrimitive>(
            QPointF(json["x1"].toDouble(), json["y1"].toDouble()),
            QPointF(json["x2"].toDouble(), json["y2"].toDouble())
        );
    } else if (type == "circle") {
        return std::make_unique<CirclePrimitive>(
            QPointF(json["cx"].toDouble(), json["cy"].toDouble()),
            json["r"].toDouble(),
            json["filled"].toBool()
        );
    } else if (type == "arc") {
        return std::make_unique<ArcPrimitive>(
            QRectF(json["x"].toDouble(), json["y"].toDouble(),
                   json["w"].toDouble(), json["h"].toDouble()),
            json["start"].toInt(),
            json["span"].toInt()
        );
    } else if (type == "polygon") {
        QList<QPointF> points;
        QJsonArray pts = json["points"].toArray();
        for (const QJsonValue& v : pts) {
            QJsonObject pt = v.toObject();
            points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
        }
        return std::make_unique<PolygonPrimitive>(points, json["filled"].toBool());
    } else if (type == "polyline") {
        QList<QPointF> points;
        QJsonArray pts = json["points"].toArray();
        for (const QJsonValue& v : pts) {
            QJsonObject pt = v.toObject();
            points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
        }
        return std::make_unique<PolylinePrimitive>(points);
    } else if (type == "rect") {
        return std::make_unique<RectPrimitive>(
            QRectF(json["x"].toDouble(), json["y"].toDouble(),
                   json["w"].toDouble(), json["h"].toDouble()),
            json["filled"].toBool()
        );
    } else if (type == "text") {
        return std::make_unique<TextPrimitive>(
            json["text"].toString(),
            QPointF(json["x"].toDouble(), json["y"].toDouble()),
            json["size"].toInt()
        );
    } else if (type == "pin") {
        return std::make_unique<PinPrimitive>(
            QPointF(json["x"].toDouble(), json["y"].toDouble()),
            json["name"].toString(),
            static_cast<PinPrimitive::PinDirection>(json["dir"].toInt()),
            json["len"].toDouble()
        );
    }
    
    return nullptr;
}
