#ifndef TRACE_MODEL_H
#define TRACE_MODEL_H

#include <QUuid>
#include <QPointF>
#include <QString>
#include <QJsonObject>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a PCB Trace.
 * This class contains only data and mathematical logic, 
 * making it testable without a GUI.
 */
class TraceModel {
public:
    TraceModel() : m_id(QUuid::createUuid()), m_width(0.2), m_layer(0) {}

    // Data Accessors
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QPointF start() const { return m_start; }
    void setStart(const QPointF& p) { m_start = p; }

    QPointF end() const { return m_end; }
    void setEnd(const QPointF& p) { m_end = p; }

    double width() const { return m_width; }
    void setWidth(double w) { m_width = w; }

    int layer() const { return m_layer; }
    void setLayer(int l) { m_layer = l; }

    QString netName() const { return m_netName; }
    void setNetName(const QString& name) { m_netName = name; }

    // Pure Logic: Math/Geometry that doesn't need a UI
    double length() const {
        double dx = m_end.x() - m_start.x();
        double dy = m_end.y() - m_start.y();
        return std::sqrt(dx*dx + dy*dy);
    }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = m_id.toString();
        json["startX"] = m_start.x();
        json["startY"] = m_start.y();
        json["endX"] = m_end.x();
        json["endY"] = m_end.y();
        json["width"] = m_width;
        json["layer"] = m_layer;
        json["netName"] = m_netName;
        return json;
    }

    void fromJson(const QJsonObject& json) {
        m_id = QUuid(json["id"].toString());
        m_start = QPointF(json["startX"].toDouble(), json["startY"].toDouble());
        m_end = QPointF(json["endX"].toDouble(), json["endY"].toDouble());
        m_width = json["width"].toDouble();
        m_layer = json["layer"].toInt();
        m_netName = json["netName"].toString();
    }

private:
    QUuid m_id;
    QPointF m_start;
    QPointF m_end;
    double m_width;
    int m_layer;
    QString m_netName;
};

} // namespace Model
} // namespace Flux

#endif // TRACE_MODEL_H
