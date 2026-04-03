#ifndef PADITEM_H
#define PADITEM_H

#include "pcb_item.h"
#include "../models/pad_model.h"
#include <QBrush>
#include <QPen>
#include <QColor>

class PadItem : public PCBItem {
public:
    PadItem(QPointF pos = QPointF(), double diameter = 1.0, QGraphicsItem *parent = nullptr);
    PadItem(Flux::Model::PadModel* model, QGraphicsItem *parent = nullptr);
    ~PadItem();

    // Data Management
    Flux::Model::PadModel* model() const { return m_model; }
    void setModel(Flux::Model::PadModel* model);
    void setOwned(bool owned) { m_ownsModel = owned; }

    // PCBItem interface
    QString itemTypeName() const override { return "Pad"; }
    ItemType itemType() const override { return PadType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    PCBItem* clone() const override;

    // Delegate properties to the model
    double diameter() const { return m_model->size().width(); }
    void setDiameter(double diameter);
    
    QSizeF size() const { return m_model->size(); }
    void setSize(const QSizeF& size);
    
    QString padShape() const { return m_model->shape(); }
    void setPadShape(const QString& shape); // "Round", "Rect", "Oblong"

    double drillSize() const { return m_model->drillSize(); }
    void setDrillSize(double size);

    void setLayer(int layer) override;

    bool maskExpansionOverrideEnabled() const { return m_model->maskExpansionOverride(); }
    void setMaskExpansionOverrideEnabled(bool enabled) { m_model->setMaskExpansionOverride(enabled); update(); }
    double maskExpansion() const { return m_model->maskExpansion(); }
    void setMaskExpansion(double value) { m_model->setMaskExpansion(value); update(); }

    bool pasteExpansionOverrideEnabled() const { return m_model->pasteExpansionOverride(); }
    void setPasteExpansionOverrideEnabled(bool enabled) { m_model->setPasteExpansionOverride(enabled); update(); }
    double pasteExpansion() const { return m_model->pasteExpansion(); }
    void setPasteExpansion(double value) { m_model->setPasteExpansion(value); update(); }

    void updateFlags() override {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemIsMovable, false);
    }

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    Flux::Model::PadModel* m_model;
    bool m_ownsModel;
    QBrush m_brush;
    QPen m_pen;
};

#endif // PADITEM_H
