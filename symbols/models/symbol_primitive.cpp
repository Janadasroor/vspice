#include "symbol_primitive.h"
#include <QJsonDocument>
#include <QDate>

namespace Flux {
namespace Model {

// ============ SymbolPrimitive ============

namespace {

int normalizeArcAngle16(int angle) {
    // Accept degrees for legacy call sites and convert to Qt's 1/16 degree unit.
    return (qAbs(angle) <= 360) ? angle * 16 : angle;
}

int readArcAngle16(const QJsonObject& data, const char* primaryKey, const char* aliasKey, int defaultValue16) {
    if (data.contains(primaryKey)) return normalizeArcAngle16(data.value(primaryKey).toInt(defaultValue16));
    if (data.contains(aliasKey)) return normalizeArcAngle16(data.value(aliasKey).toInt(defaultValue16));
    return defaultValue16;
}

void writeArcAngles(QJsonObject& data, int start16, int span16) {
    data["startAngle"] = start16;
    data["spanAngle"] = span16;
    data["start"] = start16;
    data["span"] = span16;
}

} // namespace

SymbolPrimitive SymbolPrimitive::createLine(QPointF p1, QPointF p2) {
    SymbolPrimitive prim;
    prim.type = Line;
    prim.data["x1"] = p1.x();
    prim.data["y1"] = p1.y();
    prim.data["x2"] = p2.x();
    prim.data["y2"] = p2.y();
    return prim;
}

SymbolPrimitive SymbolPrimitive::createRect(QRectF rect, bool filled) {
    SymbolPrimitive prim;
    prim.type = Rect;
    prim.data["x"] = rect.x();
    prim.data["y"] = rect.y();
    prim.data["w"] = rect.width();
    prim.data["h"] = rect.height();
    prim.data["filled"] = filled;
    return prim;
}

SymbolPrimitive SymbolPrimitive::createCircle(QPointF center, qreal radius, bool filled) {
    SymbolPrimitive prim;
    prim.type = Circle;
    prim.data["cx"] = center.x();
    prim.data["cy"] = center.y();
    prim.data["r"] = radius;
    prim.data["filled"] = filled;
    return prim;
}

SymbolPrimitive SymbolPrimitive::createArc(QRectF rect, int startAngle, int spanAngle) {
    SymbolPrimitive prim;
    prim.type = Arc;
    prim.data["x"] = rect.x();
    prim.data["y"] = rect.y();
    prim.data["w"] = rect.width();
    prim.data["h"] = rect.height();
    const int sa16 = normalizeArcAngle16(startAngle);
    const int sp16 = normalizeArcAngle16(spanAngle);
    // Keep both modern and legacy keys for compatibility.
    writeArcAngles(prim.data, sa16, sp16);
    return prim;
}

SymbolPrimitive SymbolPrimitive::createText(const QString& text, QPointF pos, int fontSize, QColor color) {
    SymbolPrimitive prim;
    prim.type = Text;
    prim.data["text"] = text;
    prim.data["x"] = pos.x();
    prim.data["y"] = pos.y();
    prim.data["fontSize"] = fontSize;
    prim.data["color"] = color.name();
    return prim;
}

SymbolPrimitive SymbolPrimitive::createPin(QPointF pos, int number, const QString& name, const QString& orientation, qreal length) {
    SymbolPrimitive prim;
    prim.type = Pin;
    prim.data["x"] = pos.x();
    prim.data["y"] = pos.y();
    prim.data["number"] = number;
    prim.data["name"] = name;
    prim.data["orientation"] = orientation;
    prim.data["electricalType"] = "Passive"; // Default
    prim.data["length"] = length;
    prim.data["pinShape"] = "Line"; // Standard line
    prim.data["stackedNumbers"] = QString("");
    prim.data["swapGroup"] = 0;
    prim.data["pinModes"] = QJsonArray();
    prim.data["nameSize"] = 7.0;
    prim.data["numSize"] = 7.0;
    prim.data["visible"] = true;
    prim.data["jumperGroup"] = 0;
    return prim;
}

SymbolPrimitive SymbolPrimitive::createPolygon(const QList<QPointF>& points, bool filled) {
    SymbolPrimitive prim;
    prim.type = Polygon;
    QJsonArray arr;
    for (const QPointF& pt : points) {
        QJsonObject ptObj;
        ptObj["x"] = pt.x();
        ptObj["y"] = pt.y();
        arr.append(ptObj);
    }
    prim.data["points"] = arr;
    prim.data["filled"] = filled;
    return prim;
}

SymbolPrimitive SymbolPrimitive::createBezier(QPointF p1, QPointF c1, QPointF c2, QPointF p2) {
    SymbolPrimitive prim;
    prim.type = Bezier;
    prim.data["x1"] = p1.x(); prim.data["y1"] = p1.y();
    prim.data["x2"] = c1.x(); prim.data["y2"] = c1.y();
    prim.data["x3"] = c2.x(); prim.data["y3"] = c2.y();
    prim.data["x4"] = p2.x(); prim.data["y4"] = p2.y();
    return prim;
}

SymbolPrimitive SymbolPrimitive::createImage(const QString& base64Data, QRectF rect) {
    SymbolPrimitive prim;
    prim.type = Image;
    prim.data["image"] = base64Data;
    prim.data["x"] = rect.x();
    prim.data["y"] = rect.y();
    prim.data["w"] = rect.width();
    prim.data["h"] = rect.height();
    return prim;
}

QJsonObject SymbolPrimitive::toJson() const {
    QJsonObject json;
    
    static const QStringList typeNames = {"line", "arc", "rect", "circle", "polygon", "text", "pin", "bezier", "image"};
    json["type"] = typeNames.value(static_cast<int>(type), "unknown");
    json["unit"] = m_unit;
    json["bodyStyle"] = m_bodyStyle;
    
    // Merge data fields into json
    for (auto it = data.begin(); it != data.end(); ++it) {
        json[it.key()] = it.value();
    }
    
    return json;
}

SymbolPrimitive SymbolPrimitive::fromJson(const QJsonObject& json) {
    SymbolPrimitive prim;
    
    QString typeName = json["type"].toString().toLower();
    static const QMap<QString, Type> typeMap = {
        {"line", Line}, {"arc", Arc}, {"rect", Rect}, {"circle", Circle},
        {"polygon", Polygon}, {"text", Text}, {"pin", Pin}, {"bezier", Bezier}, {"image", Image}
    };
    prim.type = typeMap.value(typeName, Line);
    prim.m_unit = json["unit"].toInt(0);
    prim.m_bodyStyle = json["bodyStyle"].toInt(0);
    
    // Copy all fields except "type" to data
    prim.data = json;
    prim.data.remove("type");
    prim.data.remove("unit");
    prim.data.remove("bodyStyle");
    
    return prim;
}

void SymbolPrimitive::rotateCW(QPointF center) {
    auto rot = [&](qreal x, qreal y) {
        qreal dx = x - center.x();
        qreal dy = y - center.y();
        return QPointF(center.x() - dy, center.y() + dx);
    };

    switch (type) {
    case Line: {
        QPointF p1 = rot(data["x1"].toDouble(), data["y1"].toDouble());
        QPointF p2 = rot(data["x2"].toDouble(), data["y2"].toDouble());
        data["x1"] = p1.x(); data["y1"] = p1.y();
        data["x2"] = p2.x(); data["y2"] = p2.y();
        break;
    }
    case Rect:
    case Arc: {
        qreal x = data["x"].toDouble();
        qreal y = data["y"].toDouble();
        qreal w = data.contains("width")  ? data["width"].toDouble() : data["w"].toDouble();
        qreal h = data.contains("height") ? data["height"].toDouble() : data["h"].toDouble();
        QPointF p = rot(x, y);
        data["x"] = p.x() - h; data["y"] = p.y();
        data["width"] = h; data["w"] = h;
        data["height"] = w; data["h"] = w;
        if (type == Arc) {
            const int sa = readArcAngle16(data, "startAngle", "start", 0);
            const int sp = readArcAngle16(data, "spanAngle", "span", 180 * 16);
            // Visual clockwise rotation in scene coordinates.
            writeArcAngles(data, sa - 90 * 16, sp);
        }
        break;
    }
    case Circle: {
        qreal cx = data.contains("centerX") ? data["centerX"].toDouble() : data["cx"].toDouble();
        qreal cy = data.contains("centerY") ? data["centerY"].toDouble() : data["cy"].toDouble();
        QPointF p = rot(cx, cy);
        data["centerX"] = p.x(); data["cx"] = p.x();
        data["centerY"] = p.y(); data["cy"] = p.y();
        break;
    }
    case Polygon: {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            QPointF p = rot(o["x"].toDouble(), o["y"].toDouble());
            o["x"] = p.x(); o["y"] = p.y();
            newPts.append(o);
        }
        data["points"] = newPts;
        break;
    }
    case Pin: {
        QPointF p = rot(data["x"].toDouble(), data["y"].toDouble());
        data["x"] = p.x(); data["y"] = p.y();
        const QString o = data["orientation"].toString();
        if      (o == "Right") data["orientation"] = "Down";
        else if (o == "Down")  data["orientation"] = "Left";
        else if (o == "Left")  data["orientation"] = "Up";
        else                   data["orientation"] = "Right";
        break;
    }
    case Text: {
        QPointF p = rot(data["x"].toDouble(), data["y"].toDouble());
        data["x"] = p.x(); data["y"] = p.y();
        break;
    }
    case Bezier: {
        QPointF p1 = rot(data["x1"].toDouble(), data["y1"].toDouble());
        QPointF p2 = rot(data["x2"].toDouble(), data["y2"].toDouble());
        QPointF p3 = rot(data["x3"].toDouble(), data["y3"].toDouble());
        QPointF p4 = rot(data["x4"].toDouble(), data["y4"].toDouble());
        data["x1"] = p1.x(); data["y1"] = p1.y();
        data["x2"] = p2.x(); data["y2"] = p2.y();
        data["x3"] = p3.x(); data["y3"] = p3.y();
        data["x4"] = p4.x(); data["y4"] = p4.y();
        break;
    }
    case Image: {
        qreal x = data["x"].toDouble();
        qreal y = data["y"].toDouble();
        qreal w = data.contains("width") ? data["width"].toDouble() : data["w"].toDouble();
        qreal h = data.contains("height") ? data["height"].toDouble() : data["h"].toDouble();
        QPointF p = rot(x, y);
        data["x"] = p.x() - h; data["y"] = p.y();
        data["width"] = h; data["w"] = h;
        data["height"] = w; data["h"] = w;
        break;
    }
    default: break;
    }
}

void SymbolPrimitive::rotateCCW(QPointF center) {
    auto rot = [&](qreal x, qreal y) {
        qreal dx = x - center.x();
        qreal dy = y - center.y();
        return QPointF(center.x() + dy, center.y() - dx);
    };

    switch (type) {
    case Line: {
        QPointF p1 = rot(data["x1"].toDouble(), data["y1"].toDouble());
        QPointF p2 = rot(data["x2"].toDouble(), data["y2"].toDouble());
        data["x1"] = p1.x(); data["y1"] = p1.y();
        data["x2"] = p2.x(); data["y2"] = p2.y();
        break;
    }
    case Rect:
    case Arc: {
        qreal x = data["x"].toDouble();
        qreal y = data["y"].toDouble();
        qreal w = data.contains("width")  ? data["width"].toDouble() : data["w"].toDouble();
        qreal h = data.contains("height") ? data["height"].toDouble() : data["h"].toDouble();
        QPointF p = rot(x, y);
        data["x"] = p.x(); data["y"] = p.y() - w;
        data["width"] = h; data["w"] = h;
        data["height"] = w; data["h"] = w;
        if (type == Arc) {
            const int sa = readArcAngle16(data, "startAngle", "start", 0);
            const int sp = readArcAngle16(data, "spanAngle", "span", 180 * 16);
            // Visual counter-clockwise rotation in scene coordinates.
            writeArcAngles(data, sa + 90 * 16, sp);
        }
        break;
    }
    case Circle: {
        qreal cx = data.contains("centerX") ? data["centerX"].toDouble() : data["cx"].toDouble();
        qreal cy = data.contains("centerY") ? data["centerY"].toDouble() : data["cy"].toDouble();
        QPointF p = rot(cx, cy);
        data["centerX"] = p.x(); data["cx"] = p.x();
        data["centerY"] = p.y(); data["cy"] = p.y();
        break;
    }
    case Polygon: {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            QPointF p = rot(o["x"].toDouble(), o["y"].toDouble());
            o["x"] = p.x(); o["y"] = p.y();
            newPts.append(o);
        }
        data["points"] = newPts;
        break;
    }
    case Pin: {
        QPointF p = rot(data["x"].toDouble(), data["y"].toDouble());
        data["x"] = p.x(); data["y"] = p.y();
        const QString o = data["orientation"].toString();
        if      (o == "Right") data["orientation"] = "Up";
        else if (o == "Up")    data["orientation"] = "Left";
        else if (o == "Left")  data["orientation"] = "Down";
        else                   data["orientation"] = "Right";
        break;
    }
    case Text: {
        QPointF p = rot(data["x"].toDouble(), data["y"].toDouble());
        data["x"] = p.x(); data["y"] = p.y();
        break;
    }
    case Bezier: {
        QPointF p1 = rot(data["x1"].toDouble(), data["y1"].toDouble());
        QPointF p2 = rot(data["x2"].toDouble(), data["y2"].toDouble());
        QPointF p3 = rot(data["x3"].toDouble(), data["y3"].toDouble());
        QPointF p4 = rot(data["x4"].toDouble(), data["y4"].toDouble());
        data["x1"] = p1.x(); data["y1"] = p1.y();
        data["x2"] = p2.x(); data["y2"] = p2.y();
        data["x3"] = p3.x(); data["y3"] = p3.y();
        data["x4"] = p4.x(); data["y4"] = p4.y();
        break;
    }
    case Image: {
        qreal x = data["x"].toDouble();
        qreal y = data["y"].toDouble();
        qreal w = data.contains("width") ? data["width"].toDouble() : data["w"].toDouble();
        qreal h = data.contains("height") ? data["height"].toDouble() : data["h"].toDouble();
        QPointF p = rot(x, y);
        data["x"] = p.x(); data["y"] = p.y() - w;
        data["width"] = h; data["w"] = h;
        data["height"] = w; data["h"] = w;
        break;
    }
    default: break;
    }
}

void SymbolPrimitive::flipH(QPointF center) {
    auto flipX = [&](qreal x) { return 2.0 * center.x() - x; };

    switch (type) {
    case Line:
        data["x1"] = flipX(data["x1"].toDouble());
        data["x2"] = flipX(data["x2"].toDouble());
        break;
    case Rect:
    case Arc: {
        qreal w = data.contains("width") ? data["width"].toDouble() : data["w"].toDouble();
        data["x"] = flipX(data["x"].toDouble()) - w;
        if (type == Arc) {
            const int sa = readArcAngle16(data, "startAngle", "start", 0);
            const int sp = readArcAngle16(data, "spanAngle", "span", 180 * 16);
            // Mirror across Y-axis: theta' = 180 - theta, span direction flips.
            writeArcAngles(data, 180 * 16 - sa, -sp);
        }
        break;
    }
    case Circle:
        if (data.contains("centerX")) data["centerX"] = flipX(data["centerX"].toDouble());
        if (data.contains("cx"))      data["cx"]      = flipX(data["cx"].toDouble());
        break;
    case Polygon: {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            o["x"] = flipX(o["x"].toDouble());
            newPts.append(o);
        }
        data["points"] = newPts;
        break;
    }
    case Pin: {
        data["x"] = flipX(data["x"].toDouble());
        const QString o = data["orientation"].toString();
        if      (o == "Right") data["orientation"] = "Left";
        else if (o == "Left")  data["orientation"] = "Right";
        break;
    }
    case Text:
        data["x"] = flipX(data["x"].toDouble());
        break;
    case Bezier:
        data["x1"] = flipX(data["x1"].toDouble());
        data["x2"] = flipX(data["x2"].toDouble());
        data["x3"] = flipX(data["x3"].toDouble());
        data["x4"] = flipX(data["x4"].toDouble());
        break;
    case Image: {
        qreal w = data.contains("width") ? data["width"].toDouble() : data["w"].toDouble();
        data["x"] = flipX(data["x"].toDouble()) - w;
        break;
    }
    default: break;
    }
}

void SymbolPrimitive::flipV(QPointF center) {
    auto flipY = [&](qreal y) { return 2.0 * center.y() - y; };

    switch (type) {
    case Line:
        data["y1"] = flipY(data["y1"].toDouble());
        data["y2"] = flipY(data["y2"].toDouble());
        break;
    case Rect:
    case Arc: {
        qreal h = data.contains("height") ? data["height"].toDouble() : data["h"].toDouble();
        data["y"] = flipY(data["y"].toDouble()) - h;
        if (type == Arc) {
            const int sa = readArcAngle16(data, "startAngle", "start", 0);
            const int sp = readArcAngle16(data, "spanAngle", "span", 180 * 16);
            // Mirror across X-axis: theta' = -theta, span direction flips.
            writeArcAngles(data, -sa, -sp);
        }
        break;
    }
    case Circle:
        if (data.contains("centerY")) data["centerY"] = flipY(data["centerY"].toDouble());
        if (data.contains("cy"))      data["cy"]      = flipY(data["cy"].toDouble());
        break;
    case Polygon: {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            o["y"] = flipY(o["y"].toDouble());
            newPts.append(o);
        }
        data["points"] = newPts;
        break;
    }
    case Pin: {
        data["y"] = flipY(data["y"].toDouble());
        const QString o = data["orientation"].toString();
        if      (o == "Up")   data["orientation"] = "Down";
        else if (o == "Down") data["orientation"] = "Up";
        break;
    }
    case Text:
        data["y"] = flipY(data["y"].toDouble());
        break;
    case Bezier:
        data["y1"] = flipY(data["y1"].toDouble());
        data["y2"] = flipY(data["y2"].toDouble());
        data["y3"] = flipY(data["y3"].toDouble());
        data["y4"] = flipY(data["y4"].toDouble());
        break;
    case Image: {
        qreal h = data.contains("height") ? data["height"].toDouble() : data["h"].toDouble();
        data["y"] = flipY(data["y"].toDouble()) - h;
        break;
    }
    default: break;
    }
}

void SymbolPrimitive::move(qreal dx, qreal dy) {
    switch (type) {
    case Line:
        data["x1"] = data["x1"].toDouble() + dx;
        data["y1"] = data["y1"].toDouble() + dy;
        data["x2"] = data["x2"].toDouble() + dx;
        data["y2"] = data["y2"].toDouble() + dy;
        break;
    case Rect:
    case Arc:
    case Text:
    case Pin:
    case Image:
        data["x"] = data["x"].toDouble() + dx;
        data["y"] = data["y"].toDouble() + dy;
        break;
    case Circle:
        if (data.contains("centerX")) data["centerX"] = data["centerX"].toDouble() + dx;
        if (data.contains("cx"))      data["cx"]      = data["cx"].toDouble() + dx;
        if (data.contains("centerY")) data["centerY"] = data["centerY"].toDouble() + dy;
        if (data.contains("cy"))      data["cy"]      = data["cy"].toDouble() + dy;
        break;
    case Polygon: {
        QJsonArray pts = data["points"].toArray();
        QJsonArray newPts;
        for (auto v : pts) {
            QJsonObject o = v.toObject();
            o["x"] = o["x"].toDouble() + dx;
            o["y"] = o["y"].toDouble() + dy;
            newPts.append(o);
        }
        data["points"] = newPts;
        break;
    }
    case Bezier:
        data["x1"] = data["x1"].toDouble() + dx;
        data["y1"] = data["y1"].toDouble() + dy;
        data["x2"] = data["x2"].toDouble() + dx;
        data["y2"] = data["y2"].toDouble() + dy;
        data["x3"] = data["x3"].toDouble() + dx;
        data["y3"] = data["y3"].toDouble() + dy;
        data["x4"] = data["x4"].toDouble() + dx;
        data["y4"] = data["y4"].toDouble() + dy;
        break;
    default: break;
    }
}

} // namespace Model
} // namespace Flux
