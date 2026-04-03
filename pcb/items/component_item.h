#ifndef COMPONENTITEM_H
#define COMPONENTITEM_H

#include "pcb_item.h"
#include "../models/component_model.h"
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QBrush>
#include <QPen>
#include <QColor>

class ComponentItem : public PCBItem {
public:
    ComponentItem(QPointF pos = QPointF(), QString type = "IC", QGraphicsItem *parent = nullptr);
    ComponentItem(Flux::Model::ComponentModel* model, QGraphicsItem *parent = nullptr);
    ~ComponentItem();

    // Data Management
    Flux::Model::ComponentModel* model() const { return m_model; }
    void setModel(Flux::Model::ComponentModel* model);
    void setOwned(bool owned) { m_ownsModel = owned; }

    // PCBItem interface
    QString itemTypeName() const override { return "Component"; }
    ItemType itemType() const override { return ComponentType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    PCBItem* clone() const override;

    // Delegate properties to the model
    QString componentType() const { return m_model->componentType(); }
    void setComponentType(const QString& type);
    void setName(const QString& name);
    void setValue(const QString& value);
    QString value() const { return m_model->value(); }

    QSizeF size() const { return m_model->size(); }
    void setSize(const QSizeF& size);

    void setLayer(int layer) override;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    void createBody();
    void createLabel();
    void createPads();
    void updateBody();
    void updatePads();

    Flux::Model::ComponentModel* m_model;
    bool m_ownsModel;
    QGraphicsRectItem* m_body;
    QGraphicsSimpleTextItem* m_label;
    QList<QGraphicsItem*> m_footprintItems;
};

#endif // COMPONENTITEM_H
