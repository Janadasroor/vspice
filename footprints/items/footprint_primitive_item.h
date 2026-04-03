#ifndef FOOTPRINT_PRIMITIVE_ITEM_H
#define FOOTPRINT_PRIMITIVE_ITEM_H

#include <QGraphicsItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include "../models/footprint_primitive.h"

namespace Flux {
namespace Item {

/**
 * @brief Base class for visual items representing footprint primitives.
 */
class FootprintPrimitiveItem : public QGraphicsItem {
public:
    explicit FootprintPrimitiveItem(const Model::FootprintPrimitive& model, QGraphicsItem* parent = nullptr);
    virtual ~FootprintPrimitiveItem() = default;

    const Model::FootprintPrimitive& model() const { return m_model; }
    virtual void setModel(const Model::FootprintPrimitive& model) { 
        m_model = model; 
        update(); 
    }

    int primitiveIndex() const { return data(1).toInt(); }
    void setPrimitiveIndex(int index) { setData(1, index); }

protected:
    void paintSelectionBorder(QPainter* painter, const QStyleOptionGraphicsItem* option) const;
    void prepareOption(QStyleOptionGraphicsItem* option) const {
        option->state &= ~QStyle::State_Selected;
    }

protected:
    Model::FootprintPrimitive m_model;
};

class FootprintLineItem : public FootprintPrimitiveItem {
public:
    using FootprintPrimitiveItem::FootprintPrimitiveItem;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

class FootprintRectItem : public FootprintPrimitiveItem {
public:
    using FootprintPrimitiveItem::FootprintPrimitiveItem;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

class FootprintCircleItem : public FootprintPrimitiveItem {
public:
    using FootprintPrimitiveItem::FootprintPrimitiveItem;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

class FootprintArcItem : public FootprintPrimitiveItem {
public:
    using FootprintPrimitiveItem::FootprintPrimitiveItem;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

class FootprintPadItem : public FootprintPrimitiveItem {
public:
    using FootprintPrimitiveItem::FootprintPrimitiveItem;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

class FootprintTextItem : public FootprintPrimitiveItem {
public:
    using FootprintPrimitiveItem::FootprintPrimitiveItem;
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

} // namespace Item
} // namespace Flux

#endif // FOOTPRINT_PRIMITIVE_ITEM_H
