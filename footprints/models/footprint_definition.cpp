#include "footprint_definition.h"
#include <algorithm>

namespace Flux {
namespace Model {

// Footprint3DModel Implementation

QJsonObject Footprint3DModel::toJson() const {
    QJsonObject json;
    json["filename"] = filename;
    json["opacity"] = double(opacity);
    json["visible"] = visible;
    
    QJsonObject off;
    off["x"] = offset.x(); off["y"] = offset.y(); off["z"] = offset.z();
    json["offset"] = off;
    
    QJsonObject rot;
    rot["x"] = rotation.x(); rot["y"] = rotation.y(); rot["z"] = rotation.z();
    json["rotation"] = rot;
    
    QJsonObject scl;
    scl["x"] = scale.x(); scl["y"] = scale.y(); scl["z"] = scale.z();
    json["scale"] = scl;
    
    return json;
}

Footprint3DModel Footprint3DModel::fromJson(const QJsonObject& json) {
    Footprint3DModel model;
    model.filename = json["filename"].toString();
    model.opacity = float(std::clamp(json["opacity"].toDouble(1.0), 0.0, 1.0));
    model.visible = json["visible"].toBool(true);
    
    QJsonObject off = json["offset"].toObject();
    model.offset = QVector3D(off["x"].toDouble(), off["y"].toDouble(), off["z"].toDouble());
    
    QJsonObject rot = json["rotation"].toObject();
    model.rotation = QVector3D(rot["x"].toDouble(), rot["y"].toDouble(), rot["z"].toDouble());
    
    QJsonObject scl = json["scale"].toObject();
    if (scl.isEmpty()) model.scale = QVector3D(1,1,1);
    else model.scale = QVector3D(scl["x"].toDouble(), scl["y"].toDouble(), scl["z"].toDouble());
    
    return model;
}


// FootprintDefinition Implementation

FootprintDefinition::FootprintDefinition() {
    m_classification = "Unspecified";
}

FootprintDefinition::FootprintDefinition(const QString& name) 
    : m_name(name) {
    m_classification = "Unspecified";
}

void FootprintDefinition::setModel3D(const Footprint3DModel& model) {
    m_model3D = model;
    if (m_models3D.isEmpty()) {
        m_models3D.append(model);
    } else {
        m_models3D[0] = model;
    }
}

void FootprintDefinition::setModels3D(const QList<Footprint3DModel>& models) {
    m_models3D = models;
    if (!m_models3D.isEmpty()) {
        m_model3D = m_models3D.first();
    } else {
        m_model3D = Footprint3DModel();
    }
}

void FootprintDefinition::addModel3D(const Footprint3DModel& model) {
    if (m_models3D.isEmpty()) {
        m_model3D = model;
    }
    m_models3D.append(model);
}

void FootprintDefinition::addPrimitive(const FootprintPrimitive& prim) {
    m_primitives.append(prim);
}

void FootprintDefinition::removePrimitive(int index) {
    if (index >= 0 && index < m_primitives.size()) {
        m_primitives.removeAt(index);
    }
}

void FootprintDefinition::clearPrimitives() {
    m_primitives.clear();
}

QList<QPointF> FootprintDefinition::padPositions() const {
    QList<QPointF> points;
    for (const auto& prim : m_primitives) {
        if (prim.type == FootprintPrimitive::Pad) {
            points.append(QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble()));
        }
    }
    return points;
}

QRectF FootprintDefinition::boundingRect() const {
    if (m_primitives.isEmpty()) return QRectF();
    
    QRectF rect;
    bool first = true;
    
    for (const auto& prim : m_primitives) {
        QRectF primRect;
        if (prim.type == FootprintPrimitive::Line) {
            primRect = QRectF(prim.data["x1"].toDouble(), prim.data["y1"].toDouble(), 0, 0);
            primRect = primRect.united(QRectF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble(), 0, 0));
            qreal w = prim.data["width"].toDouble();
            primRect.adjust(-w/2, -w/2, w/2, w/2);
        } else if (prim.type == FootprintPrimitive::Rect) {
            primRect = QRectF(prim.data["x"].toDouble(), prim.data["y"].toDouble(), 
                              prim.data["width"].toDouble(), prim.data["height"].toDouble());
        } else if (prim.type == FootprintPrimitive::Circle || prim.type == FootprintPrimitive::Arc) {
            qreal r = prim.data["radius"].toDouble();
            primRect = QRectF(prim.data["cx"].toDouble() - r, prim.data["cy"].toDouble() - r, r*2, r*2);
        } else if (prim.type == FootprintPrimitive::Pad) {
            const QString shape = prim.data["shape"].toString();
            if (shape == "Custom") {
                const QJsonArray pts = prim.data["points"].toArray();
                if (!pts.isEmpty()) {
                    QJsonObject first = pts.first().toObject();
                    primRect = QRectF(first["x"].toDouble(), first["y"].toDouble(), 0.0, 0.0);
                    for (const QJsonValue& v : pts) {
                        const QJsonObject o = v.toObject();
                        primRect = primRect.united(QRectF(o["x"].toDouble(), o["y"].toDouble(), 0.0, 0.0));
                    }
                }
            }
            if (primRect.isNull() || !primRect.isValid()) {
                qreal w = prim.data["width"].toDouble();
                qreal h = prim.data["height"].toDouble();
                primRect = QRectF(prim.data["x"].toDouble() - w/2, prim.data["y"].toDouble() - h/2, w, h);
            }
        } else if (prim.type == FootprintPrimitive::Text) {
            // Approximation for text
            qreal h = prim.data["height"].toDouble();
            primRect = QRectF(prim.data["x"].toDouble(), prim.data["y"].toDouble(), h * 5, h);
        } else if (prim.type == FootprintPrimitive::Dimension) {
            primRect = QRectF(prim.data["x1"].toDouble(), prim.data["y1"].toDouble(), 0, 0);
            primRect = primRect.united(QRectF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble(), 0, 0));
            primRect.adjust(-0.5, -0.5, 0.5, 0.5);
        }

        if (first) {
            rect = primRect;
            first = false;
        } else {
            rect = rect.united(primRect);
        }
    }
    
    return rect;
}

QJsonObject FootprintDefinition::toJson() const {
    QJsonObject json;
    json["name"] = m_name;
    json["description"] = m_description;
    json["category"] = m_category;
    json["classification"] = m_classification;
    json["excludeFromBOM"] = m_excludeFromBOM;
    json["excludeFromPosFiles"] = m_excludeFromPosFiles;
    json["dnp"] = m_dnp;
    json["isNetTie"] = m_isNetTie;
    QJsonArray keywordsArray;
    for (const QString& keyword : m_keywords) {
        if (!keyword.trimmed().isEmpty()) keywordsArray.append(keyword.trimmed());
    }
    json["keywords"] = keywordsArray;
    json["model3D"] = m_model3D.toJson();
    QJsonArray models3DArray;
    for (const Footprint3DModel& model : m_models3D) {
        models3DArray.append(model.toJson());
    }
    json["models3D"] = models3DArray;
    
    QJsonArray primsArray;
    for (const auto& prim : m_primitives) {
        primsArray.append(prim.toJson());
    }
    json["primitives"] = primsArray;
    
    return json;
}

FootprintDefinition FootprintDefinition::fromJson(const QJsonObject& json) {
    FootprintDefinition def;
    def.setName(json["name"].toString());
    def.setDescription(json["description"].toString());
    def.setCategory(json["category"].toString());
    def.setClassification(json["classification"].toString("Unspecified"));
    def.setExcludeFromBOM(json["excludeFromBOM"].toBool(false));
    def.setExcludeFromPosFiles(json["excludeFromPosFiles"].toBool(false));
    def.setDnp(json["dnp"].toBool(false));
    def.setIsNetTie(json["isNetTie"].toBool(false));
    QStringList keywords;
    const QJsonArray keywordsArray = json["keywords"].toArray();
    for (const QJsonValue& value : keywordsArray) {
        const QString keyword = value.toString().trimmed();
        if (!keyword.isEmpty()) keywords.append(keyword);
    }
    def.setKeywords(keywords);
    
    QList<Footprint3DModel> models3D;
    const QJsonArray models3DArray = json["models3D"].toArray();
    for (const QJsonValue& value : models3DArray) {
        if (!value.isObject()) continue;
        models3D.append(Footprint3DModel::fromJson(value.toObject()));
    }
    if (!models3D.isEmpty()) {
        def.setModels3D(models3D);
    } else if (json.contains("model3D")) {
        def.setModel3D(Footprint3DModel::fromJson(json["model3D"].toObject()));
    }
    
    QJsonArray primsArray = json["primitives"].toArray();
    for (const auto& val : primsArray) {
        def.addPrimitive(FootprintPrimitive::fromJson(val.toObject()));
    }
    
    return def;
}

bool FootprintDefinition::isValid() const {
    return !m_name.isEmpty();
}

FootprintDefinition FootprintDefinition::clone() const {
    FootprintDefinition copy;
    copy.m_name = m_name;
    copy.m_description = m_description;
    copy.m_category = m_category;
    copy.m_classification = m_classification;
    copy.m_excludeFromBOM = m_excludeFromBOM;
    copy.m_excludeFromPosFiles = m_excludeFromPosFiles;
    copy.m_dnp = m_dnp;
    copy.m_keywords = m_keywords;
    copy.m_isNetTie = m_isNetTie;
    copy.m_model3D = m_model3D;
    copy.m_models3D = m_models3D;
    copy.m_primitives = m_primitives;
    return copy;
}

} // namespace Model
} // namespace Flux
