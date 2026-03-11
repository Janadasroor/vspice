#ifndef SCHEMATICITEMFACTORY_H
#define SCHEMATICITEMFACTORY_H

#include "schematic_item.h"
#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QPointF>
#include <functional>

class SchematicItemFactory {
public:
    using CreatorFunction = std::function<SchematicItem*(QPointF pos, const QJsonObject& properties, QGraphicsItem* parent)>;

    static SchematicItemFactory& instance();

    // Register a new item type
    void registerItemType(const QString& typeName, CreatorFunction creator);

    // Create item by type name (auto-assigns reference designator)
    SchematicItem* createItem(const QString& typeName, QPointF pos = QPointF(),
                              const QJsonObject& properties = QJsonObject(),
                              QGraphicsItem* parent = nullptr);

    // Get all registered types
    QStringList registeredTypes() const;

    // Check if type is registered
    bool isTypeRegistered(const QString& typeName) const;
    
    // Reference designator management
    QString nextReference(const QString& prefix);
    int getCounter(const QString& prefix) const;
    void resetCounter(const QString& prefix);
    void resetAllCounters();

private:
    SchematicItemFactory() = default;
    ~SchematicItemFactory() = default;
    SchematicItemFactory(const SchematicItemFactory&) = delete;
    SchematicItemFactory& operator=(const SchematicItemFactory&) = delete;

    QMap<QString, CreatorFunction> m_creators;
    QMap<QString, int> m_referenceCounters;  // Tracks R1, R2, C1, C2, etc.
};

#endif // SCHEMATICITEMFACTORY_H
