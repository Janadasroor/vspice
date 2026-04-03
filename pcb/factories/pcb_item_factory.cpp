#include "pcb_item_factory.h"
#include <QDebug>

PCBItemFactory& PCBItemFactory::instance() {
    static PCBItemFactory instance;
    return instance;
}

void PCBItemFactory::registerItemType(const QString& typeName, CreatorFunction creator) {
    if (m_creators.contains(typeName)) {
        qWarning() << "Item type" << typeName << "is already registered. Overwriting.";
    }
    m_creators[typeName] = creator;
    qDebug() << "Registered PCB item type:" << typeName;
}

PCBItem* PCBItemFactory::createItem(const QString& typeName, QPointF pos,
                                   const QJsonObject& properties,
                                   QGraphicsItem* parent) {
    auto it = m_creators.find(typeName);
    if (it == m_creators.end()) {
        qWarning() << "Unknown PCB item type:" << typeName;
        return nullptr;
    }

    PCBItem* item = it.value()(pos, properties, parent);
    if (item) {
        // Apply common properties from JSON
        if (properties.contains("name")) {
            item->setName(properties["name"].toString());
        }
        if (properties.contains("layer")) {
            item->setLayer(properties["layer"].toInt());
        }
        if (properties.contains("id")) {
            item->setId(QUuid(properties["id"].toString()));
        }
    }
    return item;
}

QStringList PCBItemFactory::registeredTypes() const {
    return m_creators.keys();
}

bool PCBItemFactory::isTypeRegistered(const QString& typeName) const {
    return m_creators.contains(typeName);
}
