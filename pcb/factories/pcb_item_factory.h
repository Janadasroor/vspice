#ifndef PCBITEMFACTORY_H
#define PCBITEMFACTORY_H

#include "pcb_item.h"
#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QPointF>
#include <functional>

class PCBItemFactory {
public:
    using CreatorFunction = std::function<PCBItem*(QPointF pos, const QJsonObject& properties, QGraphicsItem* parent)>;

    static PCBItemFactory& instance();

    // Register a new item type
    void registerItemType(const QString& typeName, CreatorFunction creator);

    // Create item by type name
    PCBItem* createItem(const QString& typeName, QPointF pos = QPointF(),
                       const QJsonObject& properties = QJsonObject(),
                       QGraphicsItem* parent = nullptr);

    // Get all registered types
    QStringList registeredTypes() const;

    // Check if type is registered
    bool isTypeRegistered(const QString& typeName) const;

private:
    PCBItemFactory() = default;
    ~PCBItemFactory() = default;
    PCBItemFactory(const PCBItemFactory&) = delete;
    PCBItemFactory& operator=(const PCBItemFactory&) = delete;

    QMap<QString, CreatorFunction> m_creators;
};

#endif // PCBITEMFACTORY_H
