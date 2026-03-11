#include "schematic_item_factory.h"
#include <QDebug>
#include "generic_component_item.h"
#include "symbol_library.h"

using Flux::Model::SymbolDefinition;

SchematicItemFactory& SchematicItemFactory::instance() {
    static SchematicItemFactory instance;
    return instance;
}

void SchematicItemFactory::registerItemType(const QString& typeName, CreatorFunction creator) {
    if (m_creators.contains(typeName)) {
        qWarning() << "Schematic item type" << typeName << "is already registered. Overwriting.";
    }
    m_creators[typeName] = creator;
    qDebug() << "Registered schematic item type:" << typeName;
}

SchematicItem* SchematicItemFactory::createItem(const QString& typeName, QPointF pos,
                                               const QJsonObject& properties,
                                               QGraphicsItem* parent) {
    qDebug() << "SchematicItemFactory: Creating item of type:" << typeName;
    auto it = m_creators.find(typeName);
    SchematicItem* item = nullptr;

    if (it != m_creators.end()) {
        item = it.value()(pos, properties, parent);
        qDebug() << "SchematicItemFactory: Created built-in item:" << typeName;
    } else {
        // Fallback: Check if it's a dynamic symbol from libraries
        SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol(typeName);
        if (def) {
             item = new GenericComponentItem(*def, parent);
             item->setPos(pos);
             qDebug() << "SchematicItemFactory: Created dynamic library item:" << typeName;
        } else {
            qWarning() << "SchematicItemFactory Error: Unknown item type:" << typeName;
            return nullptr;
        }
    }
    if (item) {
        // Apply common properties from JSON
        if (properties.contains("name")) {
            item->setName(properties["name"].toString());
        }
        if (properties.contains("value")) {
            item->setValue(properties["value"].toString());
        }
        if (properties.contains("id")) {
            item->setId(QUuid(properties["id"].toString()));
        }
        
        // Auto-assign reference designator if not already set
        if (properties.contains("reference")) {
            item->setReference(properties["reference"].toString());
        } else {
            // Use generic "?" for new items to avoid cross-sheet conflicts
            item->setReference(item->referencePrefix() + "?");
        }
        
        // Force update to rebuild primitives with the new reference
        item->update();
    }
    return item;
}

QStringList SchematicItemFactory::registeredTypes() const {
    return m_creators.keys();
}

bool SchematicItemFactory::isTypeRegistered(const QString& typeName) const {
    return m_creators.contains(typeName);
}

QString SchematicItemFactory::nextReference(const QString& prefix) {
    int& counter = m_referenceCounters[prefix];
    counter++;
    return prefix + QString::number(counter);
}

int SchematicItemFactory::getCounter(const QString& prefix) const {
    return m_referenceCounters.value(prefix, 0);
}

void SchematicItemFactory::resetCounter(const QString& prefix) {
    m_referenceCounters[prefix] = 0;
}

void SchematicItemFactory::resetAllCounters() {
    m_referenceCounters.clear();
}
