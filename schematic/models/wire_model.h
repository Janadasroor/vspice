#ifndef WIRE_MODEL_H
#define WIRE_MODEL_H

#include "schematic_item_model.h"
#include <QVector>
#include <QList>
#include <QJsonArray>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a Schematic Wire.
 */
class WireModel : public SchematicItemModel {
public:
    enum WireType { SignalWire, PowerWire };

    struct JumpOver {
        QPointF position;
        double radius;
        bool isVertical;

        QJsonObject toJson() const {
            QJsonObject json;
            json["x"] = position.x();
            json["y"] = position.y();
            json["radius"] = radius;
            json["isVertical"] = isVertical;
            return json;
        }

        void fromJson(const QJsonObject& json) {
            position = QPointF(json["x"].toDouble(), json["y"].toDouble());
            radius = json["radius"].toDouble();
            isVertical = json["isVertical"].toBool();
        }
    };

    WireModel() : m_netName("No Net"), m_wireType(SignalWire) {}

    QVector<QPointF> points() const { return m_points; }
    void setPoints(const QVector<QPointF>& p) { m_points = p; }
    void addPoint(const QPointF& p) { m_points.append(p); }

    QString netName() const { return m_netName; }
    void setNetName(const QString& n) { m_netName = n; }

    WireType wireType() const { return m_wireType; }
    void setWireType(WireType t) { m_wireType = t; }

    QList<QPointF> junctions() const { return m_junctions; }
    void setJunctions(const QList<QPointF>& j) { m_junctions = j; }
    void addJunction(const QPointF& j) { if (!m_junctions.contains(j)) m_junctions.append(j); }

    QList<JumpOver> jumpOvers() const { return m_jumpOvers; }
    void setJumpOvers(const QList<JumpOver>& jo) { m_jumpOvers = jo; }

    // Logic: Calculate length
    double length() const {
        double len = 0;
        for (int i = 0; i < m_points.size() - 1; ++i) {
            len += QLineF(m_points[i], m_points[i+1]).length();
        }
        return len;
    }

    // Serialization
    QJsonObject toJson() const override {
        QJsonObject json = SchematicItemModel::toJson();
        json["netName"] = m_netName;
        json["wireType"] = static_cast<int>(m_wireType);

        QJsonArray pointsArray;
        for (const QPointF& p : m_points) {
            QJsonObject pt; pt["x"] = p.x(); pt["y"] = p.y();
            pointsArray.append(pt);
        }
        json["points"] = pointsArray;

        QJsonArray junctionsArray;
        for (const QPointF& j : m_junctions) {
            QJsonObject pt; pt["x"] = j.x(); pt["y"] = j.y();
            junctionsArray.append(pt);
        }
        json["junctions"] = junctionsArray;

        QJsonArray jumpOversArray;
        for (const auto& jo : m_jumpOvers) {
            jumpOversArray.append(jo.toJson());
        }
        json["jumpOvers"] = jumpOversArray;

        return json;
    }

    void fromJson(const QJsonObject& json) override {
        SchematicItemModel::fromJson(json);
        m_netName = json["netName"].toString();
        m_wireType = static_cast<WireType>(json["wireType"].toInt(SignalWire));

        m_points.clear();
        QJsonArray pointsArray = json["points"].toArray();
        for (const QJsonValue& val : pointsArray) {
            QJsonObject pt = val.toObject();
            m_points.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
        }

        m_junctions.clear();
        QJsonArray junctionsArray = json["junctions"].toArray();
        for (const QJsonValue& val : junctionsArray) {
            QJsonObject pt = val.toObject();
            m_junctions.append(QPointF(pt["x"].toDouble(), pt["y"].toDouble()));
        }

        m_jumpOvers.clear();
        QJsonArray jumpOversArray = json["jumpOvers"].toArray();
        for (const QJsonValue& val : jumpOversArray) {
            JumpOver jo; jo.fromJson(val.toObject());
            m_jumpOvers.append(jo);
        }
    }

private:
    QVector<QPointF> m_points;
    QString m_netName;
    WireType m_wireType;
    QList<QPointF> m_junctions;
    QList<JumpOver> m_jumpOvers;
};

} // namespace Model
} // namespace Flux

#endif // WIRE_MODEL_H
