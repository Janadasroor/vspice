#ifndef SCHEMATIC_PAGE_MODEL_H
#define SCHEMATIC_PAGE_MODEL_H

#include <QList>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "wire_model.h"
#include "schematic_component_model.h"

namespace Flux {
namespace Model {

/**
 * @brief Represents a single page in a Schematic.
 */
class SchematicPageModel {
public:
    SchematicPageModel() : m_name("Page 1") {}
    ~SchematicPageModel() { clear(); }

    void clear() {
        qDeleteAll(m_wires); m_wires.clear();
        qDeleteAll(m_components); m_components.clear();
    }

    QString name() const { return m_name; }
    void setName(const QString& n) { m_name = n; }

    const QList<WireModel*>& wires() const { return m_wires; }
    void addWire(WireModel* w) { m_wires.append(w); }

    const QList<SchematicComponentModel*>& components() const { return m_components; }
    void addComponent(SchematicComponentModel* c) { m_components.append(c); }

    struct Label {
        QString text;
        QPointF pos;
        QUuid id;
    };
    const QList<Label>& labels() const { return m_labels; }
    void addLabel(const Label& l) { m_labels.append(l); }

    // Serialization
    QJsonObject toJson() const {
        QJsonObject root;
        root["name"] = m_name;
        QJsonArray itemsArray;
        for (auto* w : m_wires) { QJsonObject j = w->toJson(); j["type"] = "Wire"; itemsArray.append(j); }
        for (auto* c : m_components) { QJsonObject j = c->toJson(); j["type"] = "Component"; itemsArray.append(j); }
        
        QJsonArray labelsArray;
        for (const auto& l : m_labels) {
            QJsonObject j;
            j["text"] = l.text;
            j["x"] = l.pos.x();
            j["y"] = l.pos.y();
            j["id"] = l.id.toString();
            labelsArray.append(j);
        }
        root["labels"] = labelsArray;
        
        root["items"] = itemsArray;
        return root;
    }

    void fromJson(const QJsonObject& root) {
        clear();
        m_name = root["name"].toString();
        
        m_labels.clear();
        QJsonArray labelsArray = root["labels"].toArray();
        for (const QJsonValue& val : labelsArray) {
            QJsonObject obj = val.toObject();
            m_labels.append({obj["text"].toString(), QPointF(obj["x"].toDouble(), obj["y"].toDouble()), QUuid::fromString(obj["id"].toString())});
        }

        QJsonArray itemsArray = root["items"].toArray();
        for (const QJsonValue& val : itemsArray) {
            QJsonObject obj = val.toObject();
            QString type = obj["type"].toString();
            if (type == "Wire") {
                WireModel* w = new WireModel();
                w->fromJson(obj);
                m_wires.append(w);
            } else if (type == "Component") {
                SchematicComponentModel* c = new SchematicComponentModel();
                c->fromJson(obj);
                m_components.append(c);
            }
        }
    }

private:
    QString m_name;
    QList<WireModel*> m_wires;
    QList<SchematicComponentModel*> m_components;
    QList<Label> m_labels;
};

} // namespace Model
} // namespace Flux

#endif // SCHEMATIC_PAGE_MODEL_H
