#ifndef SCHEMATIC_SHAPE_ITEM_H
#define SCHEMATIC_SHAPE_ITEM_H

#include "schematic_item.h"
#include <QPen>
#include <QBrush>

class SchematicShapeItem : public SchematicItem {
public:
    enum ShapeType { Rectangle, Circle, Line, Polygon, Bezier };

    SchematicShapeItem(ShapeType type, QPointF start = QPointF(), QPointF end = QPointF(), QGraphicsItem* parent = nullptr);
    virtual ~SchematicShapeItem() = default;

    QString itemTypeName() const override;
    ItemType itemType() const override { return CustomType; } 
    
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    
    SchematicItem* clone() const override;
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setStartPoint(QPointF p);
    void setEndPoint(QPointF p);
    
    QPointF startPoint() const { return m_start; }
    QPointF endPoint() const { return m_end; }
    QPen pen() const { return m_pen; }
    QBrush brush() const { return m_brush; }
    
    void setPoints(const QList<QPointF>& points);
    QList<QPointF> points() const { return m_points; }
    
    void setShapeType(ShapeType type);
    ShapeType shapeType() const { return m_shapeType; }
    
    void setPen(const QPen& pen);
    void setBrush(const QBrush& brush);
    void setPreviewOpen(bool open);
    bool isPreviewOpen() const { return m_previewOpen; }

private:
    ShapeType m_shapeType;
    QPointF m_start;
    QPointF m_end;
    QList<QPointF> m_points;
    QPen m_pen;
    QBrush m_brush;
    bool m_previewOpen = false;
};

#endif // SCHEMATIC_SHAPE_ITEM_H
