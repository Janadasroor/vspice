#ifndef BOARD_MODEL_H
#define BOARD_MODEL_H

#include <QList>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include "trace_model.h"
#include "via_model.h"
#include "pad_model.h"
#include "component_model.h"
#include "copper_pour_model.h"

namespace Flux {
namespace Model {

/**
 * @brief The BoardModel is the "Source of Truth" for an entire PCB.
 * It contains all data models (Traces, Vias, Pads, Components, Pours) 
 * and is completely independent of the Qt Graphics Scene.
 */
class BoardModel {
public:
    BoardModel() 
        : m_version(1)
        , m_name("Untitled Board")
        , m_createdAt(QDateTime::currentDateTime())
        , m_modifiedAt(QDateTime::currentDateTime())
    {}

    ~BoardModel() {
        clear();
    }

    void clear() {
        qDeleteAll(m_traces); m_traces.clear();
        qDeleteAll(m_vias); m_vias.clear();
        qDeleteAll(m_pads); m_pads.clear();
        qDeleteAll(m_components); m_components.clear();
        qDeleteAll(m_copperPours); m_copperPours.clear();
    }

    // Metadata
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    
    int version() const { return m_version; }
    QDateTime createdAt() const { return m_createdAt; }
    QDateTime modifiedAt() const { return m_modifiedAt; }
    void updateModified() { m_modifiedAt = QDateTime::currentDateTime(); }

    // Model Accessors
    const QList<TraceModel*>& traces() const { return m_traces; }
    void addTrace(TraceModel* t) { m_traces.append(t); }

    const QList<ViaModel*>& vias() const { return m_vias; }
    void addVia(ViaModel* v) { m_vias.append(v); }

    const QList<PadModel*>& pads() const { return m_pads; }
    void addPad(PadModel* p) { m_pads.append(p); }

    const QList<ComponentModel*>& components() const { return m_components; }
    void addComponent(ComponentModel* c) { m_components.append(c); }

    const QList<CopperPourModel*>& copperPours() const { return m_copperPours; }
    void addCopperPour(CopperPourModel* cp) { m_copperPours.append(cp); }

    // Serialization (Headless)
    QJsonObject toJson() const {
        QJsonObject root;
        
        QJsonObject metadata;
        metadata["version"] = m_version;
        metadata["name"] = m_name;
        metadata["createdAt"] = m_createdAt.toString(Qt::ISODate);
        metadata["modifiedAt"] = m_modifiedAt.toString(Qt::ISODate);
        root["metadata"] = metadata;

        QJsonArray itemsArray;
        for (auto* t : m_traces) { QJsonObject j = t->toJson(); j["type"] = "Trace"; itemsArray.append(j); }
        for (auto* v : m_vias) { QJsonObject j = v->toJson(); j["type"] = "Via"; itemsArray.append(j); }
        for (auto* p : m_pads) { QJsonObject j = p->toJson(); j["type"] = "Pad"; itemsArray.append(j); }
        for (auto* c : m_components) { QJsonObject j = c->toJson(); j["type"] = "Component"; itemsArray.append(j); }
        for (auto* cp : m_copperPours) { QJsonObject j = cp->toJson(); j["type"] = "CopperPour"; itemsArray.append(j); }
        
        root["items"] = itemsArray;
        return root;
    }

    void fromJson(const QJsonObject& root) {
        clear();
        
        QJsonObject metadata = root["metadata"].toObject();
        m_version = metadata["version"].toInt(1);
        m_name = metadata["name"].toString("Untitled Board");
        m_createdAt = QDateTime::fromString(metadata["createdAt"].toString(), Qt::ISODate);
        m_modifiedAt = QDateTime::fromString(metadata["modifiedAt"].toString(), Qt::ISODate);

        QJsonArray itemsArray = root["items"].toArray();
        for (const QJsonValue& val : itemsArray) {
            QJsonObject obj = val.toObject();
            QString type = obj["type"].toString();
            
            if (type == "Trace") {
                TraceModel* t = new TraceModel();
                t->fromJson(obj);
                m_traces.append(t);
            } else if (type == "Via") {
                ViaModel* v = new ViaModel();
                v->fromJson(obj);
                m_vias.append(v);
            } else if (type == "Pad") {
                PadModel* p = new PadModel();
                p->fromJson(obj);
                m_pads.append(p);
            } else if (type == "Component") {
                ComponentModel* c = new ComponentModel();
                c->fromJson(obj);
                m_components.append(c);
            } else if (type == "CopperPour") {
                CopperPourModel* cp = new CopperPourModel();
                cp->fromJson(obj);
                m_copperPours.append(cp);
            }
        }
    }

private:
    int m_version;
    QString m_name;
    QDateTime m_createdAt;
    QDateTime m_modifiedAt;

    QList<TraceModel*> m_traces;
    QList<ViaModel*> m_vias;
    QList<PadModel*> m_pads;
    QList<ComponentModel*> m_components;
    QList<CopperPourModel*> m_copperPours;
};

} // namespace Model
} // namespace Flux

#endif // BOARD_MODEL_H
