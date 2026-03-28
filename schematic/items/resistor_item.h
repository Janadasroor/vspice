#ifndef RESISTORITEM_H
#define RESISTORITEM_H

#include "schematic_item.h"
#include "schematic_primitives.h"
#include "../models/schematic_component_model.h"
#include <QBrush>
#include <QPen>
#include <QColor>
#include <memory>
#include <vector>

class ResistorItem : public SchematicItem {
public:
    enum ResistorStyle { US, IEC };
    ResistorItem(QPointF pos = QPointF(), QString value = "10k", ResistorStyle style = US, QGraphicsItem *parent = nullptr);
    ResistorItem(Flux::Model::SchematicComponentModel* model, QGraphicsItem *parent = nullptr);
    ~ResistorItem();

    // Data Management
    Flux::Model::SchematicComponentModel* model() const { return m_model; }
    void setModel(Flux::Model::SchematicComponentModel* model);
    void setOwned(bool owned) { m_ownsModel = owned; }

    // SchematicItem interface
    QString itemTypeName() const override { return "Resistor"; }
    ItemType itemType() const override { return SchematicItem::ResistorType; }
    QString referencePrefix() const override { return "R"; }
    void rebuildPrimitives() override { buildPrimitives(); }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    
    // Connectivity
    QList<QPointF> connectionPoints() const override;
    QList<PinElectricalType> pinElectricalTypes() const override { return { PassivePin, PassivePin }; }

    // Properties
    QString value() const override { return m_model->value(); }
    void setValue(const QString& value) override;
    void setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents) override;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    void buildPrimitives();

    Flux::Model::SchematicComponentModel* m_model;
    bool m_ownsModel;
    ResistorStyle m_style;
    QPen m_pen;
    QBrush m_brush;
    std::vector<std::unique_ptr<SchematicPrimitive>> m_primitives;
};

#endif // RESISTORITEM_H
