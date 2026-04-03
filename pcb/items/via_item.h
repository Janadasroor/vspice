#ifndef VIAITEM_H
#define VIAITEM_H

#include "pcb_item.h"
#include "../models/via_model.h"
#include <QBrush>
#include <QPen>
#include <QColor>

class ViaItem : public PCBItem {
public:
    ViaItem(QPointF pos = QPointF(), double diameter = 0.8, QGraphicsItem *parent = nullptr);
    ViaItem(Flux::Model::ViaModel* model, QGraphicsItem *parent = nullptr);
    ~ViaItem();

    // Data Management
    Flux::Model::ViaModel* model() const { return m_model; }
    void setModel(Flux::Model::ViaModel* model);
    void setOwned(bool owned) { m_ownsModel = owned; }

    // PCBItem interface
    QString itemTypeName() const override { return "Via"; }
    ItemType itemType() const override { return ViaType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    PCBItem* clone() const override;

    // Delegate properties to the model
    double diameter() const { return m_model->diameter(); }
    void setDiameter(double diameter);

    double drillSize() const { return m_model->drillSize(); }
    void setDrillSize(double size);

    void setLayer(int layer) override;

    int startLayer() const { return m_model->startLayer(); }
    void setStartLayer(int layer);

    int endLayer() const { return m_model->endLayer(); }
    void setEndLayer(int layer);

    bool spansLayer(int layerId) const;
    QString viaType() const;
    bool isMicrovia() const { return m_model->isMicrovia(); }
    void setMicrovia(bool microvia);
    bool maskExpansionOverrideEnabled() const { return m_model->maskExpansionOverride(); }
    void setMaskExpansionOverrideEnabled(bool enabled) { m_model->setMaskExpansionOverride(enabled); update(); }
    double maskExpansion() const { return m_model->maskExpansion(); }
    void setMaskExpansion(double value) { m_model->setMaskExpansion(value); update(); }
    bool pasteExpansionOverrideEnabled() const { return m_model->pasteExpansionOverride(); }
    void setPasteExpansionOverrideEnabled(bool enabled) { m_model->setPasteExpansionOverride(enabled); update(); }
    double pasteExpansion() const { return m_model->pasteExpansion(); }
    void setPasteExpansion(double value) { m_model->setPasteExpansion(value); update(); }

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    Flux::Model::ViaModel* m_model;
    bool m_ownsModel;
    QBrush m_brush;
    QPen m_pen;
};

#endif // VIAITEM_H
