#ifndef GENERIC_COMPONENT_ITEM_H
#define GENERIC_COMPONENT_ITEM_H

#include "schematic_item.h"
#include "../../symbols/models/symbol_definition.h"
#include "../../symbols/items/symbol_primitive_item.h"
#include <QObject>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;
using namespace Flux::Item;

class GenericComponentItem : public SchematicItem {
    Q_OBJECT
public:
    GenericComponentItem(const SymbolDefinition& symbol, QGraphicsItem* parent = nullptr);
    virtual ~GenericComponentItem();

    // SchematicItem interface
    QString itemTypeName() const override { return m_symbol.name(); }
    ItemType itemType() const override { 
        return m_symbol.isPowerSymbol() ? PowerType : CustomType; 
    }
    
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    
    virtual QString referencePrefix() const override { return m_symbol.referencePrefix(); }
    
    virtual void rebuildPrimitives() override;
    virtual SchematicItem* clone() const override;

    SymbolDefinition symbol() const { return m_symbol; }
    
    virtual QList<QPointF> connectionPoints() const override;
    virtual QList<PinElectricalType> pinElectricalTypes() const override;
    
    virtual QRectF boundingRect() const override;
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // Pin Mode Overrides (Per-instance)
    void setPinMode(int pinNumber, int modeIndex);
    int getPinMode(int pinNumber) const;

protected:
    QList<SymbolPrimitive> resolvedPrimitives() const;

    SymbolDefinition m_symbol;
    QMap<int, int> m_pinModeOverrides; // pinNumber -> modeIndex (-1 = default)
    QList<SymbolPrimitiveItem*> m_primitiveItems;
};

#endif // GENERIC_COMPONENT_ITEM_H
