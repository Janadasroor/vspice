#ifndef SYMBOL_DEFINITION_MODEL_H
#define SYMBOL_DEFINITION_MODEL_H

#include "symbol_primitive.h"
#include <QString>
#include <QStringList>
#include <QList>
#include <QPointF>
#include <QMap>
#include <QJsonObject>
#include <QRectF>

namespace Flux {
namespace Model {

/**
 * @brief Complete pure data definition of a schematic symbol.
 */
class SymbolDefinition {
public:
    SymbolDefinition();
    SymbolDefinition(const QString& name);
    
    // Basic properties
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    
    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }
    
    QString category() const { return m_category; }
    void setCategory(const QString& cat) { m_category = cat; }

    QString symbolId() const { return m_symbolId; }
    void setSymbolId(const QString& id) { m_symbolId = id; }

    QStringList aliases() const { return m_aliases; }
    void setAliases(const QStringList& aliases) { m_aliases = aliases; }

    QString datasheet() const { return m_datasheet; }
    void setDatasheet(const QString& ds) { m_datasheet = ds; }
    
    QString referencePrefix() const { return m_referencePrefix; }
    void setReferencePrefix(const QString& prefix) { m_referencePrefix = prefix; }

    QString parentName() const { return m_parentName; }
    void setParentName(const QString& name) { m_parentName = name; }

    QString parentLibrary() const { return m_parentLibrary; }
    void setParentLibrary(const QString& lib) { m_parentLibrary = lib; }

    bool isDerived() const { return !m_parentName.isEmpty(); }

    QString defaultValue() const { return m_defaultValue; }
    void setDefaultValue(const QString& value) { m_defaultValue = value; }

    QString defaultFootprint() const { return m_defaultFootprint; }
    void setDefaultFootprint(const QString& fp) { m_defaultFootprint = fp; }

    QString spiceModelName() const { return m_spiceModelName; }
    void setSpiceModelName(const QString& model) { m_spiceModelName = model; }

    QMap<int, QString> spiceNodeMapping() const { return m_spiceNodeMapping; }
    void setSpiceNodeMapping(const QMap<int, QString>& mapping) { m_spiceNodeMapping = mapping; }

    // Model linkage (LTspice-style)
    QString modelSource() const { return m_modelSource; } // "cmp", "sub", "lib", "builtin", "project"
    void setModelSource(const QString& source) { m_modelSource = source; }
    QString modelPath() const { return m_modelPath; } // relative to library root
    void setModelPath(const QString& path) { m_modelPath = path; }
    QString modelName() const { return m_modelName; } // .model or .subckt name
    void setModelName(const QString& name) { m_modelName = name; }

    QStringList footprintFilters() const { return m_footprintFilters; }
    void setFootprintFilters(const QStringList& filters) { m_footprintFilters = filters; }

    int unitCount() const { return m_unitCount; }
    void setUnitCount(int count) { m_unitCount = qMax(1, count); }

    bool unitsInterchangeable() const { return m_unitsInterchangeable; }
    void setUnitsInterchangeable(bool interchangeable) { m_unitsInterchangeable = interchangeable; }

    bool isPowerSymbol() const { return m_isPowerSymbol; }
    void setIsPowerSymbol(bool power) { m_isPowerSymbol = power; }

    bool isStub() const { return m_isStub; }
    void setStub(bool stub) { m_isStub = stub; }

    QString libraryPath() const { return m_libraryPath; }
    void setLibraryPath(const QString& path) { m_libraryPath = path; }

    // Custom Fields
    QMap<QString, QString> customFields() const { return m_customFields; }
    void setCustomFields(const QMap<QString, QString>& fields) { m_customFields = fields; }
    void setCustomField(const QString& name, const QString& value) { m_customFields[name] = value; }
    QString customField(const QString& name) const { return m_customFields.value(name); }

    QPointF referencePos() const { return m_referencePos; }
    void setReferencePos(QPointF pos) { m_referencePos = pos; }

    QPointF namePos() const { return m_namePos; }
    void setNamePos(QPointF pos) { m_namePos = pos; }
    
    // Primitives
    QList<SymbolPrimitive>& primitives() { return m_primitives; }
    const QList<SymbolPrimitive>& primitives() const { return m_primitives; }
    QList<SymbolPrimitive> effectivePrimitives() const;
    void addPrimitive(const SymbolPrimitive& prim);
    void insertPrimitive(int index, const SymbolPrimitive& prim);
    void removePrimitive(int index);
    void clearPrimitives();
    
    // Connection points (derived from Pin primitives)
    QList<QPointF> connectionPoints() const;
    const SymbolPrimitive* pinPrimitive(const QString& identifier) const;
    QString pinSignalDomain(const QString& identifier) const;
    QString pinSignalDirection(const QString& identifier) const;
    
    // Bounding rect
    QRectF boundingRect() const;
    
    // Serialization
    QJsonObject toJson() const;
    static SymbolDefinition fromJson(const QJsonObject& json);
    
    // Validation
    bool isValid() const;
    
    // Clone
    SymbolDefinition clone() const;

private:
    QString m_name;
    QString m_description;
    QString m_category;
    QString m_symbolId;
    QStringList m_aliases;
    QString m_datasheet;
    QString m_referencePrefix;
    QString m_parentName;
    QString m_parentLibrary;
    QString m_defaultValue;
    QString m_defaultFootprint;
    QString m_spiceModelName;
    QMap<int, QString> m_spiceNodeMapping;
    QString m_modelSource;
    QString m_modelPath;
    QString m_modelName;
    QStringList m_footprintFilters;
    int m_unitCount = 1;
    bool m_unitsInterchangeable = true;
    bool m_isPowerSymbol = false;
    bool m_isStub = false;
    QString m_libraryPath;
    QPointF m_referencePos;
    QPointF m_namePos;
    QMap<QString, QString> m_customFields;
    QList<SymbolPrimitive> m_primitives;
};

} // namespace Model
} // namespace Flux

#endif // SYMBOL_DEFINITION_MODEL_H
