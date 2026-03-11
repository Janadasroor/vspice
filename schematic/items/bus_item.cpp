#include "bus_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineF>

BusItem::BusItem(QPointF start, QPointF end, QGraphicsItem *parent)
    : SchematicItem(parent) {
    if (!start.isNull() || !end.isNull()) {
        m_points << start << end;
    }
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setZValue(1); // Standard layer for buses
    updatePen();
}

void BusItem::updatePen() {
    QColor color = Qt::blue;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->schematicBus();
        if (color == Qt::transparent) color = Qt::blue; // Fallback
    }
    m_pen = QPen(color, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void BusItem::setStartPoint(const QPointF& point) {
    if (m_points.isEmpty()) m_points << point << point;
    else m_points[0] = point;
    prepareGeometryChange();
    update();
}

void BusItem::setEndPoint(const QPointF& point) {
    if (m_points.isEmpty()) m_points << point << point;
    else m_points.last() = point;
    prepareGeometryChange();
    update();
}

void BusItem::addSegment(const QPointF& point) {
    m_points << point;
    prepareGeometryChange();
    update();
}

void BusItem::setPoints(const QList<QPointF>& points) {
    m_points = points;
    prepareGeometryChange();
    update();
}

void BusItem::addJunction(const QPointF& point) {
    for (const QPointF& existing : m_junctions) {
        if (QLineF(existing, point).length() < 0.75) return;
    }
    m_junctions.append(point);
    update();
}

QRectF BusItem::boundingRect() const {
    if (m_points.isEmpty()) return QRectF();
    
    qreal minX = m_points[0].x();
    qreal minY = m_points[0].y();
    qreal maxX = minX;
    qreal maxY = minY;
    
    for (const QPointF& p : m_points) {
        minX = qMin(minX, p.x());
        minY = qMin(minY, p.y());
        maxX = qMax(maxX, p.x());
        maxY = qMax(maxY, p.y());
    }
    
    qreal w = m_pen.widthF() + 2;
    return QRectF(minX, minY, maxX - minX, maxY - minY).adjusted(-w, -w, w, w);
}

void BusItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    if (m_points.size() < 2) return;
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPen p = m_pen;
    if (isSelected()) {
        p.setColor(p.color().lighter(150));
        p.setStyle(Qt::DashLine);
    }
    
    // Draw highlight glow if enabled
    if (m_isHighlighted) {
        QPen highlightPen(QColor(255, 215, 0, 100), m_pen.widthF() + 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(highlightPen);
        for (int i = 0; i < m_points.size() - 1; ++i) {
            painter->drawLine(m_points[i], m_points[i+1]);
        }
    }

    painter->setPen(p);
    for (int i = 0; i < m_points.size() - 1; ++i) {
        painter->drawLine(m_points[i], m_points[i+1]);
    }

    // Draw bus junction dots (used for bus-to-bus and bus-to-wire T-connections)
    if (!m_junctions.isEmpty()) {
        QColor dotColor = ThemeManager::theme() ? ThemeManager::theme()->wireJunction() : QColor(230, 230, 230);
        painter->setPen(QPen(dotColor, 1.5));
        painter->setBrush(QBrush(dotColor));
        for (const QPointF& junction : m_junctions) {
            painter->drawEllipse(junction, 3.2, 3.2);
        }
    }
    
    drawConnectionPointHighlights(painter);
}

QJsonObject BusItem::toJson() const {
    QJsonObject json;
    json["type"] = "Bus";
    json["id"] = m_id.toString();
    
    // Save visual properties
    json["color"] = m_pen.color().name();
    json["width"] = m_pen.widthF();
    json["lineStyle"] = static_cast<int>(m_pen.style());

    QJsonArray pointsArray;
    for (const QPointF& p : m_points) {
        QJsonObject pt;
        pt["x"] = p.x();
        pt["y"] = p.y();
        pointsArray.append(pt);
    }
    json["points"] = pointsArray;

    QJsonArray junctionsArray;
    for (const QPointF& junction : m_junctions) {
        QJsonObject jo;
        jo["x"] = junction.x();
        jo["y"] = junction.y();
        junctionsArray.append(jo);
    }
    json["junctions"] = junctionsArray;
    
    return json;
}

bool BusItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    
    // Restore visual properties
    updatePen(); // Set defaults first
    if (json.contains("color")) {
        QColor color(json["color"].toString());
        qreal width = json["width"].toDouble(m_pen.widthF());
        Qt::PenStyle style = static_cast<Qt::PenStyle>(json["lineStyle"].toInt(Qt::SolidLine));
        m_pen = QPen(color, width, style, Qt::RoundCap, Qt::RoundJoin);
    }

    m_points.clear();
    QJsonArray pointsArray = json["points"].toArray();
    for (int i = 0; i < pointsArray.size(); ++i) {
        QJsonObject pt = pointsArray[i].toObject();
        m_points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
    }

    m_junctions.clear();
    const QJsonArray junctionsArray = json["junctions"].toArray();
    for (const QJsonValue& jv : junctionsArray) {
        const QJsonObject jo = jv.toObject();
        m_junctions.append(QPointF(jo["x"].toDouble(), jo["y"].toDouble()));
    }
    
    updatePen();
    return true;
}

SchematicItem* BusItem::clone() const {
    BusItem* item = new BusItem();
    item->setPoints(m_points);
    return item;
}
