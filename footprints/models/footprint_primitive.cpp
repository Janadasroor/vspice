#include "footprint_primitive.h"
#include <algorithm>

namespace Flux {
namespace Model {

// FootprintPrimitive Implementation

FootprintPrimitive FootprintPrimitive::createLine(QPointF p1, QPointF p2, qreal width) {
    FootprintPrimitive p;
    p.type = Line;
    p.data["x1"] = p1.x();
    p.data["y1"] = p1.y();
    p.data["x2"] = p2.x();
    p.data["y2"] = p2.y();
    p.data["width"] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createRect(QRectF rect, bool filled, qreal width) {
    FootprintPrimitive p;
    p.type = Rect;
    p.data["x"] = rect.x();
    p.data["y"] = rect.y();
    p.data["width"] = rect.width();
    p.data["height"] = rect.height();
    p.data["filled"] = filled;
    p.data["lineWidth"] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createCircle(QPointF center, qreal radius, bool filled, qreal width) {
    FootprintPrimitive p;
    p.type = Circle;
    p.data["cx"] = center.x();
    p.data["cy"] = center.y();
    p.data["radius"] = radius;
    p.data["filled"] = filled;
    p.data["lineWidth"] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createArc(QPointF center, qreal radius, qreal startAngle, qreal spanAngle, qreal width) {
    FootprintPrimitive p;
    p.type = Arc;
    p.data["cx"] = center.x();
    p.data["cy"] = center.y();
    p.data["radius"] = radius;
    p.data["startAngle"] = startAngle;
    p.data["spanAngle"] = spanAngle;
    p.data["width"] = width;
    return p;
}

FootprintPrimitive FootprintPrimitive::createText(const QString& text, QPointF pos, qreal height) {
    FootprintPrimitive p;
    p.type = Text;
    p.data["text"] = text;
    p.data["x"] = pos.x();
    p.data["y"] = pos.y();
    p.data["height"] = height;
    return p;
}

FootprintPrimitive FootprintPrimitive::createPad(QPointF pos, const QString& number, const QString& shape, QSizeF size, qreal cornerRadius) {
    FootprintPrimitive p;
    p.type = Pad;
    p.data["number"] = number;
    p.data["x"] = pos.x();
    p.data["y"] = pos.y();
    p.data["shape"] = shape;
    p.data["width"] = size.width();
    p.data["height"] = size.height();
    p.data["rotation"] = 0.0;
    p.data["drill_size"] = 0.0;
    p.data["pad_type"] = "SMD";
    p.data["corner_radius"] = cornerRadius;
    p.data["trapezoid_delta_x"] = 0.0;
    p.data["net_clearance_override_enabled"] = false;
    p.data["net_clearance"] = 0.2;
    p.data["thermal_relief_enabled"] = true;
    p.data["thermal_spoke_width"] = 0.3;
    p.data["thermal_relief_gap"] = 0.25;
    p.data["thermal_spoke_count"] = 4;
    p.data["thermal_spoke_angle_deg"] = 0.0;
    p.data["jumper_group"] = 0;
    p.data["net_tie_group"] = 0;
    p.data["solder_mask_expansion"] = 0.05;
    p.data["paste_mask_expansion"] = 0.0;
    p.data["plated"] = true;
    return p;
}

FootprintPrimitive FootprintPrimitive::createPolygonPad(const QList<QPointF>& points, const QString& number) {
    FootprintPrimitive p;
    p.type = Pad;
    p.layer = Top_Copper;
    p.data["number"] = number;
    p.data["shape"] = "Custom";
    
    QJsonArray pts;
    for (const auto& pt : points) {
        QJsonObject ptObj;
        ptObj["x"] = pt.x();
        ptObj["y"] = pt.y();
        pts.append(ptObj);
    }
    p.data["points"] = pts;
    
    // Default pad properties
    p.data["rotation"] = 0.0;
    p.data["drill_size"] = 0.0;
    p.data["pad_type"] = "SMD";
    p.data["net_clearance_override_enabled"] = false;
    p.data["net_clearance"] = 0.2;
    p.data["thermal_relief_enabled"] = true;
    p.data["thermal_spoke_width"] = 0.3;
    p.data["thermal_relief_gap"] = 0.25;
    p.data["thermal_spoke_count"] = 4;
    p.data["thermal_spoke_angle_deg"] = 0.0;
    p.data["jumper_group"] = 0;
    p.data["net_tie_group"] = 0;
    p.data["solder_mask_expansion"] = 0.05;
    p.data["paste_mask_expansion"] = 0.0;
    p.data["plated"] = true;
    
    // Anchor point (centroid of points for rotation/selection)
    if (!points.isEmpty()) {
        qreal sumX = 0, sumY = 0;
        for (const auto& pt : points) { sumX += pt.x(); sumY += pt.y(); }
        p.data["x"] = sumX / points.size();
        p.data["y"] = sumY / points.size();

        QRectF bounds(points.first(), QSizeF(0, 0));
        for (const auto& pt : points) bounds = bounds.united(QRectF(pt, QSizeF(0, 0)));
        p.data["width"] = bounds.width();
        p.data["height"] = bounds.height();
    } else {
        p.data["x"] = 0.0;
        p.data["y"] = 0.0;
        p.data["width"] = 0.0;
        p.data["height"] = 0.0;
    }
    
    return p;
}

QJsonObject FootprintPrimitive::toJson() const {
    QJsonObject json;
    json["type"] = QString::number(type);
    json["layer"] = static_cast<int>(layer);
    json["data"] = data;
    return json;
}

FootprintPrimitive FootprintPrimitive::fromJson(const QJsonObject& json) {
    FootprintPrimitive p;
    QString typeStr = json["type"].toString();
    bool isInt;
    int typeInt = typeStr.toInt(&isInt);
    
    if (isInt) {
        p.type = static_cast<Type>(typeInt);
    } else {
        if (typeStr == "Line") p.type = Line;
        else if (typeStr == "Arc") p.type = Arc;
        else if (typeStr == "Rect") p.type = Rect;
        else if (typeStr == "Circle") p.type = Circle;
        else if (typeStr == "Polygon") p.type = Polygon;
        else if (typeStr == "Text") p.type = Text;
        else if (typeStr == "Pad") p.type = Pad;
        else if (typeStr == "Via") p.type = Via;
        else if (typeStr == "Dimension") p.type = Dimension;
        else p.type = Line;
    }

    if (json.contains("layer")) {
        p.layer = static_cast<Layer>(json["layer"].toInt());
    }
    
    if (json.contains("data") && json["data"].isObject()) {
        p.data = json["data"].toObject();
    } else {
        p.data = json;
        p.data.remove("type");
    }
    
    return p;
}

void FootprintPrimitive::move(qreal dx, qreal dy) {
    if (type == Line) {
        data["x1"] = data["x1"].toDouble() + dx;
        data["y1"] = data["y1"].toDouble() + dy;
        data["x2"] = data["x2"].toDouble() + dx;
        data["y2"] = data["y2"].toDouble() + dy;
    } else if (type == Circle || type == Arc) {
        data["cx"] = data["cx"].toDouble() + dx;
        data["cy"] = data["cy"].toDouble() + dy;
    } else if (type == Pad && data["shape"].toString() == "Custom") {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            o["x"] = o["x"].toDouble() + dx;
            o["y"] = o["y"].toDouble() + dy;
            newPts.append(o);
        }
        data["points"] = newPts;
        // Also move anchor
        data["x"] = data["x"].toDouble() + dx;
        data["y"] = data["y"].toDouble() + dy;
    } else {
        data["x"] = data["x"].toDouble() + dx;
        data["y"] = data["y"].toDouble() + dy;
    }
}

} // namespace Model
} // namespace Flux
