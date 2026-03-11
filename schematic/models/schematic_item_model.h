#ifndef SCHEMATIC_ITEM_MODEL_H
#define SCHEMATIC_ITEM_MODEL_H

#include <QUuid>
#include <QString>
#include <QPointF>
#include <QJsonObject>

namespace Flux {
namespace Model {

/**
 * @brief Base data model for all Schematic Items.
 */
class SchematicItemModel {
public:
    SchematicItemModel() 
        : m_id(QUuid::createUuid())
        , m_pos(0, 0)
        , m_rotation(0)
        , m_isMirroredX(false)
        , m_isMirroredY(false)
        , m_unit(1)
    {}

    virtual ~SchematicItemModel() = default;

    // Common properties
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QPointF pos() const { return m_pos; }
    void setPos(const QPointF& p) { m_pos = p; }

    double rotation() const { return m_rotation; }
    void setRotation(double r) { m_rotation = r; }

    bool isMirroredX() const { return m_isMirroredX; }
    void setMirroredX(bool m) { m_isMirroredX = m; }

    bool isMirroredY() const { return m_isMirroredY; }
    void setMirroredY(bool m) { m_isMirroredY = m; }

    int unit() const { return m_unit; }
    void setUnit(int u) { m_unit = u; }

    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }

    QString value() const { return m_value; }
    void setValue(const QString& v) { m_value = v; }

    QString reference() const { return m_reference; }
    void setReference(const QString& r) { m_reference = r; }

    // Serialization
    virtual QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = m_id.toString();
        json["x"] = m_pos.x();
        json["y"] = m_pos.y();
        json["rotation"] = m_rotation;
        json["mirroredX"] = m_isMirroredX;
        json["mirroredY"] = m_isMirroredY;
        json["unit"] = m_unit;
        json["name"] = m_name;
        json["value"] = m_value;
        json["reference"] = m_reference;
        return json;
    }

    virtual void fromJson(const QJsonObject& json) {
        m_id = QUuid(json["id"].toString());
        m_pos = QPointF(json["x"].toDouble(), json["y"].toDouble());
        m_rotation = json["rotation"].toDouble();
        m_isMirroredX = json["mirroredX"].toBool();
        m_isMirroredY = json["mirroredY"].toBool();
        m_unit = json["unit"].toInt(1);
        m_name = json["name"].toString();
        m_value = json["value"].toString();
        m_reference = json["reference"].toString();
    }

protected:
    QUuid m_id;
    QPointF m_pos;
    double m_rotation;
    bool m_isMirroredX;
    bool m_isMirroredY;
    int m_unit;
    QString m_name;
    QString m_value;
    QString m_reference;
};

} // namespace Model
} // namespace Flux

#endif // SCHEMATIC_ITEM_MODEL_H
