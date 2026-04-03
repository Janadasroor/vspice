#ifndef COPPER_POUR_MODEL_H
#define COPPER_POUR_MODEL_H

#include <QUuid>
#include <QPolygonF>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a PCB Copper Pour.
 * Stores the boundary polygon and all pour parameters (clearance, thermal relief, etc.).
 */
class CopperPourModel {
public:
    enum PourType { SolidPour, HatchPour };

    CopperPourModel()
        : m_id(QUuid::createUuid())
        , m_layer(0)
        , m_clearance(0.2)
        , m_minWidth(0.15)
        , m_filled(true)
        , m_pourType(SolidPour)
        , m_hatchWidth(0.15)
        , m_priority(0)
        , m_removeIslands(true)
        , m_useThermalReliefs(true)
        , m_thermalSpokeWidth(0.3)
        , m_thermalSpokeCount(4)
        , m_thermalSpokeAngleDeg(45.0)
    {}

    // Data Accessors
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QPolygonF polygon() const { return m_polygon; }
    void setPolygon(const QPolygonF& p) { m_polygon = p; }

    int layer() const { return m_layer; }
    void setLayer(int l) { m_layer = l; }

    QString netName() const { return m_netName; }
    void setNetName(const QString& n) { m_netName = n; }

    double clearance() const { return m_clearance; }
    void setClearance(double c) { m_clearance = c; }

    double minWidth() const { return m_minWidth; }
    void setMinWidth(double w) { m_minWidth = w; }

    bool filled() const { return m_filled; }
    void setFilled(bool filled) { m_filled = filled; }

    PourType pourType() const { return m_pourType; }
    void setPourType(PourType t) { m_pourType = t; }

    double hatchWidth() const { return m_hatchWidth; }
    void setHatchWidth(double w) { m_hatchWidth = w; }

    int priority() const { return m_priority; }
    void setPriority(int p) { m_priority = p; }

    bool removeIslands() const { return m_removeIslands; }
    void setRemoveIslands(bool r) { m_removeIslands = r; }

    bool useThermalReliefs() const { return m_useThermalReliefs; }
    void setUseThermalReliefs(bool u) { m_useThermalReliefs = u; }

    double thermalSpokeWidth() const { return m_thermalSpokeWidth; }
    void setThermalSpokeWidth(double w) { m_thermalSpokeWidth = w; }

    int thermalSpokeCount() const { return m_thermalSpokeCount; }
    void setThermalSpokeCount(int c) { m_thermalSpokeCount = c; }

    double thermalSpokeAngleDeg() const { return m_thermalSpokeAngleDeg; }
    void setThermalSpokeAngleDeg(double d) { m_thermalSpokeAngleDeg = d; }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = m_id.toString();
        json["layer"] = m_layer;
        json["netName"] = m_netName;
        json["clearance"] = m_clearance;
        json["minWidth"] = m_minWidth;
        json["filled"] = m_filled;
        json["pourType"] = static_cast<int>(m_pourType);
        json["hatchWidth"] = m_hatchWidth;
        json["priority"] = m_priority;
        json["removeIslands"] = m_removeIslands;
        json["useThermalReliefs"] = m_useThermalReliefs;
        json["thermalSpokeWidth"] = m_thermalSpokeWidth;
        json["thermalSpokeCount"] = m_thermalSpokeCount;
        json["thermalSpokeAngleDeg"] = m_thermalSpokeAngleDeg;

        QJsonArray polyArray;
        for (const QPointF& p : m_polygon) {
            QJsonObject pt;
            pt["x"] = p.x();
            pt["y"] = p.y();
            polyArray.append(pt);
        }
        json["polygon"] = polyArray;

        return json;
    }

    void fromJson(const QJsonObject& json) {
        m_id = QUuid(json["id"].toString());
        m_layer = json["layer"].toInt();
        m_netName = json["netName"].toString();
        m_clearance = json["clearance"].toDouble();
        m_minWidth = json["minWidth"].toDouble();
        m_filled = json.contains("filled") ? json["filled"].toBool() : true;
        m_pourType = static_cast<PourType>(json["pourType"].toInt());
        m_hatchWidth = json["hatchWidth"].toDouble();
        m_priority = json["priority"].toInt();
        m_removeIslands = json["removeIslands"].toBool();
        m_useThermalReliefs = json["useThermalReliefs"].toBool();
        m_thermalSpokeWidth = json["thermalSpokeWidth"].toDouble();
        m_thermalSpokeCount = json["thermalSpokeCount"].toInt();
        m_thermalSpokeAngleDeg = json["thermalSpokeAngleDeg"].toDouble();

        m_polygon.clear();
        QJsonArray polyArray = json["polygon"].toArray();
        for (const QJsonValue& val : polyArray) {
            QJsonObject pt = val.toObject();
            m_polygon.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
        }
    }

private:
    QUuid m_id;
    int m_layer;
    QString m_netName;
    QPolygonF m_polygon;
    double m_clearance;
    double m_minWidth;
    bool m_filled;
    PourType m_pourType;
    double m_hatchWidth;
    int m_priority;
    bool m_removeIslands;
    bool m_useThermalReliefs;
    double m_thermalSpokeWidth;
    int m_thermalSpokeCount;
    double m_thermalSpokeAngleDeg;
};

} // namespace Model
} // namespace Flux

#endif // COPPER_POUR_MODEL_H
