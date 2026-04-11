#ifndef PAD_MODEL_H
#define PAD_MODEL_H

#include <QUuid>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QJsonObject>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a PCB Pad.
 */
class PadModel {
public:
    PadModel() 
        : m_id(QUuid::createUuid())
        , m_size(1.0, 1.0)
        , m_rotation(0.0)
        , m_drillSize(0.0)
        , m_shape("Round")
        , m_layer(0)
        , m_maskExpansionOverride(false)
        , m_maskExpansion(0.0)
        , m_pasteExpansionOverride(false)
        , m_pasteExpansion(0.0)
        , m_number()
    {}

    // Data Accessors
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QPointF pos() const { return m_pos; }
    void setPos(const QPointF& p) { m_pos = p; }

    QSizeF size() const { return m_size; }
    void setSize(const QSizeF& s) { m_size = s; }

    double rotation() const { return m_rotation; }
    void setRotation(double r) { m_rotation = r; }

    double drillSize() const { return m_drillSize; }
    void setDrillSize(double s) { m_drillSize = s; }

    QString shape() const { return m_shape; }
    void setShape(const QString& s) { m_shape = s; }

    int layer() const { return m_layer; }
    void setLayer(int l) { m_layer = l; }

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

    QString number() const { return m_number; }
    void setNumber(const QString& number) { m_number = number; }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = m_id.toString();
        json["x"] = m_pos.x();
        json["y"] = m_pos.y();
        json["width"] = m_size.width();
        json["height"] = m_size.height();
        json["rotation"] = m_rotation;
        json["drillSize"] = m_drillSize;
        json["shape"] = m_shape;
        json["layer"] = m_layer;
        json["maskExpansionOverride"] = m_maskExpansionOverride;
        json["maskExpansion"] = m_maskExpansion;
        json["pasteExpansionOverride"] = m_pasteExpansionOverride;
        json["pasteExpansion"] = m_pasteExpansion;
        json["netName"] = m_netName;
        json["number"] = m_number;
        return json;
    }

    void fromJson(const QJsonObject& json) {
        m_id = QUuid(json["id"].toString());
        m_pos = QPointF(json["x"].toDouble(), json["y"].toDouble());
        m_size = QSizeF(json["width"].toDouble(), json["height"].toDouble());
        m_rotation = json["rotation"].toDouble();
        m_drillSize = json["drillSize"].toDouble();
        m_shape = json["shape"].toString();
        m_layer = json["layer"].toInt();
        m_maskExpansionOverride = json["maskExpansionOverride"].toBool();
        m_maskExpansion = json["maskExpansion"].toDouble();
        m_pasteExpansionOverride = json["pasteExpansionOverride"].toBool();
        m_pasteExpansion = json["pasteExpansion"].toDouble();
        m_netName = json["netName"].toString();
        m_number = json["number"].toString();
    }

private:
    QUuid m_id;
    QPointF m_pos;
    QSizeF m_size;
    double m_rotation;
    double m_drillSize;
    QString m_shape;
    int m_layer;
    bool m_maskExpansionOverride;
    double m_maskExpansion;
    bool m_pasteExpansionOverride;
    double m_pasteExpansion;
    QString m_netName;
    QString m_number;
};

} // namespace Model
} // namespace Flux

#endif // PAD_MODEL_H
