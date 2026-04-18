#include "schematic_shape_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>

SchematicShapeItem::SchematicShapeItem(ShapeType type, QPointF start, QPointF end, QGraphicsItem* parent)
    : SchematicItem(parent), m_shapeType(type), m_start(start), m_end(end)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsMovable);
    m_brush = Qt::NoBrush;
    
    // Set default pen based on theme if possible, otherwise black
    QColor penColor = Qt::black;
    if (ThemeManager::theme()) {
        penColor = ThemeManager::theme()->schematicLine();
    }
    m_pen = QPen(penColor, 2);
}

QString SchematicShapeItem::itemTypeName() const {
    switch (m_shapeType) {
        case Rectangle: return "Rectangle";
        case Circle: return "Circle";
        case Line: return "Line";
        case Polygon: return "Polygon";
        case Bezier: return "Bezier";
    }
    return "Unknown";
}

QJsonObject SchematicShapeItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["rotation"] = rotation();
    json["isMirroredX"] = isMirroredX();
    json["isMirroredY"] = isMirroredY();
    json["start_x"] = m_start.x();
    json["start_y"] = m_start.y();
    json["end_x"] = m_end.x();
    json["end_y"] = m_end.y();
    
    if (m_shapeType == Polygon || m_shapeType == Bezier) {
        QJsonArray pts;
        for (const auto& p : m_points) {
            QJsonObject pt; pt["x"] = p.x(); pt["y"] = p.y();
            pts.append(pt);
        }
        json["points"] = pts;
    }

    json["stroke_color"] = m_pen.color().name();
    json["stroke_width"] = m_pen.widthF();
    if (m_brush.style() != Qt::NoBrush) {
        json["fill_color"] = m_brush.color().name();
    }
    return json;
}

bool SchematicShapeItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) setId(QUuid::fromString(json["id"].toString()));
    setRotation(json["rotation"].toDouble(0.0));
    setMirroredX(json["isMirroredX"].toBool(false));
    setMirroredY(json["isMirroredY"].toBool(false));
    
    if (json.contains("start_x")) m_start.setX(json["start_x"].toDouble());
    if (json.contains("start_y")) m_start.setY(json["start_y"].toDouble());
    if (json.contains("end_x")) m_end.setX(json["end_x"].toDouble());
    if (json.contains("end_y")) m_end.setY(json["end_y"].toDouble());
    
    if (json.contains("points")) {
        m_points.clear();
        QJsonArray pts = json["points"].toArray();
        for (const auto& v : pts) {
            QJsonObject pt = v.toObject();
            m_points << QPointF(pt["x"].toDouble(), pt["y"].toDouble());
        }
    }

    if (json.contains("stroke_color")) {
        m_pen.setColor(QColor(json["stroke_color"].toString()));
    }
    if (json.contains("stroke_width")) {
        m_pen.setWidthF(json["stroke_width"].toDouble());
    }
    if (json.contains("fill_color")) {
        m_brush.setColor(QColor(json["fill_color"].toString()));
        m_brush.setStyle(Qt::SolidPattern);
    } else {
        m_brush = Qt::NoBrush;
    }
    
    QString typeStr = json["type"].toString();
    if (typeStr == "Rectangle") m_shapeType = Rectangle;
    else if (typeStr == "Circle") m_shapeType = Circle;
    else if (typeStr == "Line") m_shapeType = Line;
    else if (typeStr == "Polygon") m_shapeType = Polygon;
    else if (typeStr == "Bezier") m_shapeType = Bezier;
    
    return true;
}

SchematicItem* SchematicShapeItem::clone() const {
    auto* item = new SchematicShapeItem(m_shapeType, m_start, m_end);
    item->setPoints(m_points);
    item->setPen(m_pen);
    item->setBrush(m_brush);
    item->setRotation(rotation());
    item->setMirroredX(isMirroredX());
    item->setMirroredY(isMirroredY());
    return item;
}

void SchematicShapeItem::setStartPoint(QPointF p) {
    prepareGeometryChange();
    m_start = p;
    update();
}

void SchematicShapeItem::setEndPoint(QPointF p) {
    prepareGeometryChange();
    m_end = p;
    update();
}

void SchematicShapeItem::setPoints(const QList<QPointF>& points) {
    prepareGeometryChange();
    m_points = points;
    update();
}

void SchematicShapeItem::setShapeType(ShapeType type) {
    prepareGeometryChange();
    m_shapeType = type;
    update();
}

void SchematicShapeItem::setPen(const QPen& pen) {
    m_pen = pen;
    update();
}

void SchematicShapeItem::setBrush(const QBrush& brush) {
    m_brush = brush;
    update();
}

void SchematicShapeItem::setPreviewOpen(bool open) {
    if (m_previewOpen == open) return;
    m_previewOpen = open;
    update();
}

QRectF SchematicShapeItem::boundingRect() const {
    if ((m_shapeType == Polygon || m_shapeType == Bezier) && !m_points.isEmpty()) {
        QRectF r(m_points.first(), QSizeF(0,0));
        for (const auto& p : m_points) {
            r = r.united(QRectF(p, QSizeF(0,0)));
        }
        return r.normalized().adjusted(-5, -5, 5, 5);
    }
    QRectF rect(m_start, m_end);
    return rect.normalized().adjusted(-5, -5, 5, 5);
}

void SchematicShapeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget)
    
    // Create a modified option to hide the default Qt selection box
    QStyleOptionGraphicsItem opt = *option;
    opt.state &= ~QStyle::State_Selected;

    painter->setPen(m_pen);
    painter->setBrush(m_brush);
    
    if (m_shapeType == Rectangle) {
        painter->drawRect(QRectF(m_start, m_end));
    } else if (m_shapeType == Circle) {
        painter->drawEllipse(QRectF(m_start, m_end));
    } else if (m_shapeType == Line) {
        painter->drawLine(m_start, m_end);
    } else if (m_shapeType == Polygon && !m_points.isEmpty()) {
        if (m_previewOpen) {
            painter->drawPolyline(QPolygonF() << m_points);
        } else {
            painter->drawPolygon(QPolygonF() << m_points);
        }
    } else if (m_shapeType == Bezier && m_points.size() == 4) {
        QPainterPath path;
        path.moveTo(m_points[0]);
        path.cubicTo(m_points[1], m_points[2], m_points[3]);
        painter->drawPath(path);
    }
}
