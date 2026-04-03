#ifndef VIA_MODEL_H
#define VIA_MODEL_H

#include <QUuid>
#include <QPointF>
#include <QString>
#include <QJsonObject>
#include <algorithm>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a PCB Via.
 */
class ViaModel {
public:
    ViaModel() 
        : m_id(QUuid::createUuid())
        , m_diameter(0.8)
        , m_drillSize(0.3)
        , m_startLayer(0) // TopCopper
        , m_endLayer(31)  // BottomCopper (assuming 32 layers max for now or specific enum)
        , m_microvia(false)
        , m_maskExpansionOverride(false)
        , m_maskExpansion(0.0)
        , m_pasteExpansionOverride(false)
        , m_pasteExpansion(0.0)
    {}

    // Data Accessors
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QPointF pos() const { return m_pos; }
    void setPos(const QPointF& p) { m_pos = p; }

    double diameter() const { return m_diameter; }
    void setDiameter(double d) { m_diameter = d; }

    double drillSize() const { return m_drillSize; }
    void setDrillSize(double s) { m_drillSize = s; }

    int startLayer() const { return m_startLayer; }
    void setStartLayer(int l) { m_startLayer = l; }

    int endLayer() const { return m_endLayer; }
    void setEndLayer(int l) { m_endLayer = l; }

    bool isMicrovia() const { return m_microvia; }
    void setMicrovia(bool m) { m_microvia = m; }

    bool maskExpansionOverride() const { return m_maskExpansionOverride; }
    void setMaskExpansionOverride(bool o) { m_maskExpansionOverride = o; }

    double maskExpansion() const { return m_maskExpansion; }
    void setMaskExpansion(double e) { m_maskExpansion = e; }

    bool pasteExpansionOverride() const { return m_pasteExpansionOverride; }
    void setPasteExpansionOverride(bool o) { m_pasteExpansionOverride = o; }

    double pasteExpansion() const { return m_pasteExpansion; }
    void setPasteExpansion(double e) { m_pasteExpansion = e; }

    QString netName() const { return m_netName; }
    void setNetName(const QString& name) { m_netName = name; }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = m_id.toString();
        json["x"] = m_pos.x();
        json["y"] = m_pos.y();
        json["diameter"] = m_diameter;
        json["drillSize"] = m_drillSize;
        json["startLayer"] = m_startLayer;
        json["endLayer"] = m_endLayer;
        json["isMicrovia"] = m_microvia;
        json["maskExpansionOverride"] = m_maskExpansionOverride;
        json["maskExpansion"] = m_maskExpansion;
        json["pasteExpansionOverride"] = m_pasteExpansionOverride;
        json["pasteExpansion"] = m_pasteExpansion;
        json["netName"] = m_netName;
        return json;
    }

    void fromJson(const QJsonObject& json) {
        m_id = QUuid(json["id"].toString());
        m_pos = QPointF(json["x"].toDouble(), json["y"].toDouble());
        m_diameter = json["diameter"].toDouble();
        m_drillSize = json["drillSize"].toDouble();
        m_startLayer = json["startLayer"].toInt();
        m_endLayer = json["endLayer"].toInt();
        m_microvia = json["isMicrovia"].toBool();
        m_maskExpansionOverride = json["maskExpansionOverride"].toBool();
        m_maskExpansion = json["maskExpansion"].toDouble();
        m_pasteExpansionOverride = json["pasteExpansionOverride"].toBool();
        m_pasteExpansion = json["pasteExpansion"].toDouble();
        m_netName = json["netName"].toString();
    }

private:
    QUuid m_id;
    QPointF m_pos;
    double m_diameter;
    double m_drillSize;
    int m_startLayer;
    int m_endLayer;
    bool m_microvia;
    bool m_maskExpansionOverride;
    double m_maskExpansion;
    bool m_pasteExpansionOverride;
    double m_pasteExpansion;
    QString m_netName;
};

} // namespace Model
} // namespace Flux

#endif // VIA_MODEL_H
