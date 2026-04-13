#ifndef SYMBOL_PRIMITIVE_ITEM_H
#define SYMBOL_PRIMITIVE_ITEM_H

#include <QGraphicsItem>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include "../models/symbol_primitive.h"

namespace Flux {
namespace Item {

/**
 * @brief Base class for visual items representing symbol primitives.
 */
class SymbolPrimitiveItem : public QGraphicsItem {
public:
    explicit SymbolPrimitiveItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    virtual ~SymbolPrimitiveItem() = default;

    const Model::SymbolPrimitive& model() const { return m_model; }
    virtual void setModel(const Model::SymbolPrimitive& model) { 
        m_model = model; 
        update(); 
    }

    // index in SymbolDefinition::primitives()
    int primitiveIndex() const { return data(1).toInt(); }
    void setPrimitiveIndex(int index) { setData(1, index); }

protected:
    void paintSelectionBorder(QPainter* painter, const QStyleOptionGraphicsItem* option) const;
    
    // Suppresses default QGraphicsItem selection rectangle
    void prepareOption(QStyleOptionGraphicsItem* option) const {
        option->state &= ~QStyle::State_Selected;
    }

protected:
    Model::SymbolPrimitive m_model;
};

/**
 * @brief Visual item for a line in a symbol.
 */
class SymbolLineItem : public SymbolPrimitiveItem {
public:
    explicit SymbolLineItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/**
 * @brief Visual item for a rectangle in a symbol.
 */
class SymbolRectItem : public SymbolPrimitiveItem {
public:
    explicit SymbolRectItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/**
 * @brief Visual item for a circle in a symbol.
 */
class SymbolCircleItem : public SymbolPrimitiveItem {
public:
    explicit SymbolCircleItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/**
 * @brief Visual item for an arc in a symbol.
 */
class SymbolArcItem : public SymbolPrimitiveItem {
public:
    explicit SymbolArcItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/**
 * @brief Visual item for a polygon in a symbol.
 */
class SymbolPolygonItem : public SymbolPrimitiveItem {
public:
    explicit SymbolPolygonItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/**
 * @brief Visual item for a text label in a symbol.
 */
class SymbolTextItem : public SymbolPrimitiveItem {
public:
    explicit SymbolTextItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    void setSymbolContext(const QString& name, const QString& ref, const QString& val);
    void setKeepUpright(bool keep) { m_keepUpright = keep; update(); }
    bool keepUpright() const { return m_keepUpright; }
    void syncUprightTransform();

private:
    QString m_symbolName;
    QString m_symbolRef;
    QString m_symbolVal;
    bool m_keepUpright = false;
};

/**
 * @brief Visual item for a connection pin in a symbol.
 */
class SymbolPinItem : public SymbolPrimitiveItem {
public:
    explicit SymbolPinItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    void drawPinShape(QPainter* painter, const QPointF& endPt, const QString& orientation, const QString& shape, const QColor& color);
};

/**
 * @brief Visual item for a Bezier curve in a symbol.
 */
class SymbolBezierItem : public SymbolPrimitiveItem {
public:
    explicit SymbolBezierItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
};

/**
 * @brief Visual item for a bitmap image in a symbol.
 */
class SymbolImageItem : public SymbolPrimitiveItem {
public:
    explicit SymbolImageItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    QPixmap m_pixmap;
    bool m_pixmapLoaded = false;
};

} // namespace Item
} // namespace Flux

#endif // SYMBOL_PRIMITIVE_ITEM_H
