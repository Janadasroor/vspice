#ifndef COMPONENT_MODEL_H
#define COMPONENT_MODEL_H

#include <QUuid>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include "pad_model.h"

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a PCB Component.
 * Manages its own properties and a collection of child PadModels.
 */
class ComponentModel {
public:
    ComponentModel() 
        : m_id(QUuid::createUuid())
        , m_pos(0, 0)
        , m_size(10, 6)
        , m_rotation(0.0)
        , m_layer(0)
        , m_componentType("IC")
    {}

    ~ComponentModel() {
        qDeleteAll(m_pads);
    }

    // Data Accessors
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QPointF pos() const { return m_pos; }
    void setPos(const QPointF& p) { m_pos = p; }

    QSizeF size() const { return m_size; }
    void setSize(const QSizeF& s) { m_size = s; }

    double rotation() const { return m_rotation; }
    void setRotation(double r) { m_rotation = r; }

    int layer() const { return m_layer; }
    void setLayer(int l) { m_layer = l; }

    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }

    QString componentType() const { return m_componentType; }
    void setComponentType(const QString& t) { m_componentType = t; }

    QString value() const { return m_value; }
    void setValue(const QString& v) { m_value = v; }

    const QList<PadModel*>& pads() const { return m_pads; }
    void addPad(PadModel* pad) { m_pads.append(pad); }
    void clearPads() { qDeleteAll(m_pads); m_pads.clear(); }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject json;
        json["id"] = m_id.toString();
        json["name"] = m_name;
        json["x"] = m_pos.x();
        json["y"] = m_pos.y();
        json["width"] = m_size.width();
        json["height"] = m_size.height();
        json["rotation"] = m_rotation;
        json["layer"] = m_layer;
        json["componentType"] = m_componentType;
        json["value"] = m_value;

        QJsonArray padsArray;
        for (const auto* pad : m_pads) {
            padsArray.append(pad->toJson());
        }
        json["pads"] = padsArray;

        return json;
    }

    void fromJson(const QJsonObject& json) {
        m_id = QUuid(json["id"].toString());
        m_name = json["name"].toString();
        m_pos = QPointF(json["x"].toDouble(), json["y"].toDouble());
        m_size = QSizeF(json["width"].toDouble(), json["height"].toDouble());
        m_rotation = json["rotation"].toDouble();
        m_layer = json["layer"].toInt();
        m_componentType = json["componentType"].toString();
        m_value = json["value"].toString();

        clearPads();
        QJsonArray padsArray = json["pads"].toArray();
        for (const QJsonValue& padValue : padsArray) {
            PadModel* pad = new PadModel();
            pad->fromJson(padValue.toObject());
            m_pads.append(pad);
        }
    }

private:
    QUuid m_id;
    QString m_name;
    QPointF m_pos;
    QSizeF m_size;
    double m_rotation;
    int m_layer;
    QString m_componentType;
    QString m_value;
    QList<PadModel*> m_pads;
};

} // namespace Model
} // namespace Flux

#endif // COMPONENT_MODEL_H
