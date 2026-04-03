#ifndef FOOTPRINT_DEFINITION_MODEL_H
#define FOOTPRINT_DEFINITION_MODEL_H

#include "footprint_primitive.h"
#include <QString>
#include <QList>
#include <QPointF>
#include <QJsonObject>
#include <QJsonArray>
#include <QRectF>
#include <QStringList>
#include <QVector3D>

namespace Flux {
namespace Model {

/**
 * @brief 3D Model settings for a footprint.
 */
struct Footprint3DModel {
    QString filename; // Path to .obj, .step, or empty for procedural
    QVector3D offset;
    QVector3D rotation;
    QVector3D scale;
    float opacity = 1.0f;
    bool visible = true;
    
    Footprint3DModel() : scale(1,1,1) {}
    
    QJsonObject toJson() const;
    static Footprint3DModel fromJson(const QJsonObject& json);
};

/**
 * @brief Complete pure data definition of a PCB footprint.
 */
class FootprintDefinition {
public:
    FootprintDefinition();
    FootprintDefinition(const QString& name);
    
    // Basic properties
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    
    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }
    
    QString category() const { return m_category; }
    void setCategory(const QString& cat) { m_category = cat; }

    // Attributes & metadata
    QString classification() const { return m_classification; }
    void setClassification(const QString& classification) { m_classification = classification; }

    bool excludeFromBOM() const { return m_excludeFromBOM; }
    void setExcludeFromBOM(bool exclude) { m_excludeFromBOM = exclude; }

    bool excludeFromPosFiles() const { return m_excludeFromPosFiles; }
    void setExcludeFromPosFiles(bool exclude) { m_excludeFromPosFiles = exclude; }

    bool dnp() const { return m_dnp; }
    void setDnp(bool dnp) { m_dnp = dnp; }

    QStringList keywords() const { return m_keywords; }
    void setKeywords(const QStringList& keywords) { m_keywords = keywords; }

    bool isNetTie() const { return m_isNetTie; }
    void setIsNetTie(bool isNetTie) { m_isNetTie = isNetTie; }
    
    // 3D Model
    Footprint3DModel model3D() const { return m_model3D; }
    void setModel3D(const Footprint3DModel& model);
    QList<Footprint3DModel> models3D() const { return m_models3D; }
    void setModels3D(const QList<Footprint3DModel>& models);
    void addModel3D(const Footprint3DModel& model);

    // Primitives
    QList<FootprintPrimitive>& primitives() { return m_primitives; }
    const QList<FootprintPrimitive>& primitives() const { return m_primitives; }
    void addPrimitive(const FootprintPrimitive& prim);
    void removePrimitive(int index);
    void clearPrimitives();
    
    // Connection points (derived from Pad primitives)
    QList<QPointF> padPositions() const;
    
    // Bounding rect
    QRectF boundingRect() const;
    
    // Serialization
    QJsonObject toJson() const;
    static FootprintDefinition fromJson(const QJsonObject& json);
    
    // Validation
    bool isValid() const;
    
    // Clone
    FootprintDefinition clone() const;

private:
    QString m_name;
    QString m_description;
    QString m_category;
    QString m_classification;
    bool m_excludeFromBOM = false;
    bool m_excludeFromPosFiles = false;
    bool m_dnp = false;
    QStringList m_keywords;
    bool m_isNetTie = false;
    Footprint3DModel m_model3D;
    QList<Footprint3DModel> m_models3D;
    QList<FootprintPrimitive> m_primitives;
};

} // namespace Model
} // namespace Flux

#endif // FOOTPRINT_DEFINITION_MODEL_H
