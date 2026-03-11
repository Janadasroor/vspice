#include "symbol_definition.h"
#include "../symbol_library.h"
#include <QJsonDocument>
#include <QLineF>

namespace Flux {
namespace Model {

namespace {

QString oppositeOrientation(const QString& orientation) {
    if (orientation == "Left") return "Right";
    if (orientation == "Right") return "Left";
    if (orientation == "Up") return "Down";
    if (orientation == "Down") return "Up";
    return orientation;
}

bool isCardinalOrientation(const QString& orientation) {
    return orientation == "Left" || orientation == "Right" ||
           orientation == "Up" || orientation == "Down";
}

bool primitiveBodyBounds(const SymbolPrimitive& prim, QRectF& out) {
    switch (prim.type) {
    case SymbolPrimitive::Line: {
        const QPointF p1(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
        const QPointF p2(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
        out = QRectF(p1, p2).normalized();
        return true;
    }
    case SymbolPrimitive::Rect:
    case SymbolPrimitive::Arc:
    case SymbolPrimitive::Image: {
        const qreal x = prim.data["x"].toDouble();
        const qreal y = prim.data["y"].toDouble();
        const qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
        const qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
        out = QRectF(x, y, w, h).normalized();
        return true;
    }
    case SymbolPrimitive::Circle: {
        const qreal cx = prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble();
        const qreal cy = prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble();
        const qreal r = prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble();
        out = QRectF(cx - r, cy - r, r * 2.0, r * 2.0);
        return true;
    }
    case SymbolPrimitive::Polygon: {
        const QJsonArray pts = prim.data["points"].toArray();
        if (pts.isEmpty()) return false;
        qreal minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (const auto& v : pts) {
            const QJsonObject o = v.toObject();
            const qreal x = o["x"].toDouble();
            const qreal y = o["y"].toDouble();
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
        out = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
        return true;
    }
    case SymbolPrimitive::Bezier: {
        qreal minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
        for (int i = 1; i <= 4; ++i) {
            const qreal x = prim.data[QString("x%1").arg(i)].toDouble();
            const qreal y = prim.data[QString("y%1").arg(i)].toDouble();
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
        out = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
        return true;
    }
    case SymbolPrimitive::Text: {
        const qreal x = prim.data["x"].toDouble();
        const qreal y = prim.data["y"].toDouble();
        out = QRectF(x, y, 0.0, 0.0);
        return true;
    }
    default:
        break;
    }
    return false;
}

QString expectedOrientationTowardsBody(const QPointF& pinPos, const QPointF& bodyCenter) {
    const QPointF d = bodyCenter - pinPos;
    if (qAbs(d.x()) >= qAbs(d.y())) {
        return d.x() >= 0.0 ? "Right" : "Left";
    }
    return d.y() >= 0.0 ? "Down" : "Up";
}

QPointF pinEndPoint(const QPointF& pinPos, qreal length, const QString& orientation) {
    if (orientation == "Left")  return QPointF(pinPos.x() - length, pinPos.y());
    if (orientation == "Right") return QPointF(pinPos.x() + length, pinPos.y());
    if (orientation == "Up")    return QPointF(pinPos.x(), pinPos.y() - length);
    if (orientation == "Down")  return QPointF(pinPos.x(), pinPos.y() + length);
    return pinPos;
}

void normalizeLegacyPinOrientations(QList<SymbolPrimitive>& primitives) {
    QRectF bodyBounds;
    bool haveBodyBounds = false;
    for (const SymbolPrimitive& prim : primitives) {
        if (prim.type == SymbolPrimitive::Pin) continue;
        QRectF b;
        if (!primitiveBodyBounds(prim, b)) continue;
        bodyBounds = haveBodyBounds ? bodyBounds.united(b) : b;
        haveBodyBounds = true;
    }
    if (!haveBodyBounds) return;

    const QPointF center = bodyBounds.center();
    for (SymbolPrimitive& prim : primitives) {
        if (prim.type != SymbolPrimitive::Pin) continue;
        const qreal len = prim.data.value("length").toDouble(15.0);
        if (len <= 0.0) continue;

        const QString orientation = prim.data.value("orientation").toString("Right");
        if (!isCardinalOrientation(orientation)) continue;

        const QPointF pinPos(prim.data["x"].toDouble(), prim.data["y"].toDouble());
        const QString expected = expectedOrientationTowardsBody(pinPos, center);
        if (!isCardinalOrientation(expected) || expected == orientation) continue;

        const QPointF currentEnd = pinEndPoint(pinPos, len, orientation);
        const QPointF expectedEnd = pinEndPoint(pinPos, len, expected);
        const qreal currentDist = QLineF(currentEnd, center).length();
        const qreal expectedDist = QLineF(expectedEnd, center).length();

        // Normalize only when the alternative clearly points more toward the symbol body.
        if (expectedDist + 1e-6 < currentDist) {
            prim.data["orientation"] = expected;
        }
    }
}

} // namespace

// ============ SymbolDefinition ============

SymbolDefinition::SymbolDefinition() 
    : m_referencePrefix("U"), m_referencePos(0, 0), m_namePos(0, 0) {
}

SymbolDefinition::SymbolDefinition(const QString& name)
    : m_name(name), m_referencePrefix("U"), m_referencePos(0, 0), m_namePos(0, 0) {
}

void SymbolDefinition::addPrimitive(const SymbolPrimitive& prim) {
    m_primitives.append(prim);
}

void SymbolDefinition::insertPrimitive(int index, const SymbolPrimitive& prim) {
    if (index >= 0 && index <= m_primitives.size()) {
        m_primitives.insert(index, prim);
    } else {
        m_primitives.append(prim);
    }
}

void SymbolDefinition::removePrimitive(int index) {
    if (index >= 0 && index < m_primitives.size()) {
        m_primitives.removeAt(index);
    }
}

void SymbolDefinition::clearPrimitives() {
    m_primitives.clear();
}

QList<SymbolPrimitive> SymbolDefinition::effectivePrimitives() const {
    if (!isDerived()) return m_primitives;

    QList<SymbolPrimitive> result;
    // 1. Get parent primitives (recursive)
    // We cast the return from SymbolLibraryManager since it might still return the old type
    // but in reality we will update SymbolLibraryManager shortly.
    SymbolDefinition* parent = dynamic_cast<SymbolDefinition*>(SymbolLibraryManager::instance().findSymbol(m_parentName, m_parentLibrary));
    if (parent) {
        result = parent->effectivePrimitives();
    }

    // 2. Add local overrides/additions
    result.append(m_primitives);
    return result;
}

QList<QPointF> SymbolDefinition::connectionPoints() const {
    QList<QPointF> points;
    for (const SymbolPrimitive& prim : m_primitives) {
        if (prim.type == SymbolPrimitive::Pin) {
            qreal x = prim.data["x"].toDouble();
            qreal y = prim.data["y"].toDouble();
            points.append(QPointF(x, y));
        }
    }
    return points;
}

QRectF SymbolDefinition::boundingRect() const {
    if (m_primitives.isEmpty()) return QRectF(-20, -20, 40, 40);
    
    qreal minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    
    for (const SymbolPrimitive& prim : m_primitives) {
        switch (prim.type) {
            case SymbolPrimitive::Line:
                minX = qMin(minX, qMin(prim.data["x1"].toDouble(), prim.data["x2"].toDouble()));
                maxX = qMax(maxX, qMax(prim.data["x1"].toDouble(), prim.data["x2"].toDouble()));
                minY = qMin(minY, qMin(prim.data["y1"].toDouble(), prim.data["y2"].toDouble()));
                maxY = qMax(maxY, qMax(prim.data["y1"].toDouble(), prim.data["y2"].toDouble()));
                break;
            case SymbolPrimitive::Rect:
                minX = qMin(minX, prim.data["x"].toDouble());
                maxX = qMax(maxX, prim.data["x"].toDouble() + prim.data["w"].toDouble());
                minY = qMin(minY, prim.data["y"].toDouble());
                maxY = qMax(maxY, prim.data["y"].toDouble() + prim.data["h"].toDouble());
                break;
            case SymbolPrimitive::Circle: {
                qreal cx = prim.data["cx"].toDouble();
                qreal cy = prim.data["cy"].toDouble();
                qreal r = prim.data["r"].toDouble();
                minX = qMin(minX, cx - r);
                maxX = qMax(maxX, cx + r);
                minY = qMin(minY, cy - r);
                maxY = qMax(maxY, cy + r);
                break;
            }
            case SymbolPrimitive::Pin: {
                qreal x = prim.data["x"].toDouble();
                qreal y = prim.data["y"].toDouble();
                qreal len = prim.data.contains("length")
                    ? prim.data["length"].toDouble()
                    : 15.0; // legacy default when field is absent
                if (len < 0) len = 15.0;
                QString orient = prim.data["orientation"].toString();
                // Convention: orientation string describes the direction the line extends FROM the connection point.
                qreal ex = x, ey = y;
                if      (orient == "Left")  ex = x - len;
                else if (orient == "Up")    ey = y - len;
                else if (orient == "Down")  ey = y + len;
                else                        ex = x + len; // default Right
                minX = qMin(minX, qMin(x, ex) - 3);
                maxX = qMax(maxX, qMax(x, ex) + 3);
                minY = qMin(minY, qMin(y, ey) - 3);
                maxY = qMax(maxY, qMax(y, ey) + 3);
                break;
            }
            case SymbolPrimitive::Arc: {
                minX = qMin(minX, prim.data["x"].toDouble());
                maxX = qMax(maxX, prim.data["x"].toDouble() + prim.data["w"].toDouble());
                minY = qMin(minY, prim.data["y"].toDouble());
                maxY = qMax(maxY, prim.data["y"].toDouble() + prim.data["h"].toDouble());
                break;
            }
            case SymbolPrimitive::Polygon: {
                 QJsonArray points = prim.data["points"].toArray();
                 for(const auto& val : points) {
                     QJsonObject pt = val.toObject();
                     qreal x = pt["x"].toDouble();
                     qreal y = pt["y"].toDouble();
                     minX = qMin(minX, x);
                     maxX = qMax(maxX, x);
                     minY = qMin(minY, y);
                     maxY = qMax(maxY, y);
                 }
                 break;
            }
            case SymbolPrimitive::Bezier: {
                for (int i = 1; i <= 4; ++i) {
                    qreal x = prim.data[QString("x%1").arg(i)].toDouble();
                    qreal y = prim.data[QString("y%1").arg(i)].toDouble();
                    minX = qMin(minX, x);
                    maxX = qMax(maxX, x);
                    minY = qMin(minY, y);
                    maxY = qMax(maxY, y);
                }
                break;
            }
            case SymbolPrimitive::Image: {
                qreal x = prim.data["x"].toDouble();
                qreal y = prim.data["y"].toDouble();
                qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
                qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
                minX = qMin(minX, x);
                maxX = qMax(maxX, x + w);
                minY = qMin(minY, y);
                maxY = qMax(maxY, y + h);
                break;
            }
            default:
                break;
        }
    }
    
    return QRectF(minX, minY, maxX - minX, maxY - minY).adjusted(-5, -5, 5, 5);
}

QJsonObject SymbolDefinition::toJson() const {
    QJsonObject json;
    json["name"] = m_name;
    json["description"] = m_description;
    json["category"] = m_category;
    json["datasheet"] = m_datasheet;
    json["referencePrefix"] = m_referencePrefix;
    json["parentName"] = m_parentName;
    json["parentLibrary"] = m_parentLibrary;
    json["defaultValue"] = m_defaultValue;
    json["defaultFootprint"] = m_defaultFootprint;
    json["spiceModelName"] = m_spiceModelName;
    json["unitCount"] = m_unitCount;
    json["unitsInterchangeable"] = m_unitsInterchangeable;
    json["isPowerSymbol"] = m_isPowerSymbol;
    json["footprintFilters"] = QJsonArray::fromStringList(m_footprintFilters);
    
    QJsonObject spiceMap;
    for (auto it = m_spiceNodeMapping.begin(); it != m_spiceNodeMapping.end(); ++it) {
        spiceMap[QString::number(it.key())] = it.value();
    }
    json["spiceNodeMapping"] = spiceMap;

    QJsonObject fields;
    for (auto it = m_customFields.begin(); it != m_customFields.end(); ++it) {
        fields[it.key()] = it.value();
    }
    json["customFields"] = fields;
    
    QJsonObject rPos; rPos["x"] = m_referencePos.x(); rPos["y"] = m_referencePos.y();
    json["referencePos"] = rPos;
    
    QJsonObject nPos; nPos["x"] = m_namePos.x(); nPos["y"] = m_namePos.y();
    json["namePos"] = nPos;
    
    QJsonArray primArray;
    for (const SymbolPrimitive& prim : m_primitives) {
        primArray.append(prim.toJson());
    }
    json["primitives"] = primArray;
    
    return json;
}

SymbolDefinition SymbolDefinition::fromJson(const QJsonObject& json) {
    SymbolDefinition def;
    def.m_name = json["name"].toString();
    def.m_description = json["description"].toString();
    def.m_category = json["category"].toString();
    def.m_datasheet = json["datasheet"].toString();
    def.m_referencePrefix = json["referencePrefix"].toString("U");
    def.m_parentName = json["parentName"].toString();
    def.m_parentLibrary = json["parentLibrary"].toString();
    def.m_defaultValue = json["defaultValue"].toString();
    def.m_defaultFootprint = json["defaultFootprint"].toString();
    def.m_spiceModelName = json["spiceModelName"].toString();
    def.m_unitCount = json["unitCount"].toInt(1);
    def.m_unitsInterchangeable = json["unitsInterchangeable"].toBool(true);
    def.m_isPowerSymbol = json["isPowerSymbol"].toBool(false);
    
    if (json.contains("spiceNodeMapping")) {
        QJsonObject smap = json["spiceNodeMapping"].toObject();
        for (auto it = smap.begin(); it != smap.end(); ++it) {
            def.m_spiceNodeMapping[it.key().toInt()] = it.value().toString();
        }
    }

    if (json.contains("customFields")) {
        QJsonObject fields = json["customFields"].toObject();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            def.m_customFields[it.key()] = it.value().toString();
        }
    }
    
    QJsonArray filters = json["footprintFilters"].toArray();
    for (const auto& f : filters) def.m_footprintFilters.append(f.toString());
    
    if (json.contains("referencePos")) {
        QJsonObject rPos = json["referencePos"].toObject();
        def.m_referencePos = QPointF(rPos["x"].toDouble(), rPos["y"].toDouble());
    }
    if (json.contains("namePos")) {
        QJsonObject nPos = json["namePos"].toObject();
        def.m_namePos = QPointF(nPos["x"].toDouble(), nPos["y"].toDouble());
    }
    
    QJsonArray primArray = json["primitives"].toArray();
    for (const QJsonValue& val : primArray) {
        def.m_primitives.append(SymbolPrimitive::fromJson(val.toObject()));
    }

    normalizeLegacyPinOrientations(def.m_primitives);
    
    return def;
}

bool SymbolDefinition::isValid() const {
    return !m_name.isEmpty() && !m_primitives.isEmpty();
}

SymbolDefinition SymbolDefinition::clone() const {
    SymbolDefinition copy;
    copy.m_name = m_name;
    copy.m_description = m_description;
    copy.m_category = m_category;
    copy.m_datasheet = m_datasheet;
    copy.m_referencePrefix = m_referencePrefix;
    copy.m_parentName = m_parentName;
    copy.m_parentLibrary = m_parentLibrary;
    copy.m_defaultValue = m_defaultValue;
    copy.m_defaultFootprint = m_defaultFootprint;
    copy.m_spiceModelName = m_spiceModelName;
    copy.m_spiceNodeMapping = m_spiceNodeMapping;
    copy.m_footprintFilters = m_footprintFilters;
    copy.m_unitCount = m_unitCount;
    copy.m_unitsInterchangeable = m_unitsInterchangeable;
    copy.m_isPowerSymbol = m_isPowerSymbol;
    copy.m_referencePos = m_referencePos;
    copy.m_namePos = m_namePos;
    copy.m_customFields = m_customFields;
    copy.m_primitives = m_primitives;
    return copy;
}

} // namespace Model
} // namespace Flux
