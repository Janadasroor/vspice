#ifndef SCHEMATICPRIMITIVES_H
#define SCHEMATICPRIMITIVES_H

#include <QPointF>
#include <QRectF>
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <memory>

// Forward declaration
class SchematicPrimitive;

// Base class for all vector primitives
class SchematicPrimitive {
public:
    virtual ~SchematicPrimitive() = default;
    
    virtual void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const = 0;
    virtual QRectF boundingRect() const = 0;
    virtual QJsonObject toJson() const = 0;
    virtual std::unique_ptr<SchematicPrimitive> clone() const = 0;
    virtual QString typeName() const = 0;
    
    static std::unique_ptr<SchematicPrimitive> fromJson(const QJsonObject& json);
};

// Line primitive
class LinePrimitive : public SchematicPrimitive {
public:
    LinePrimitive(const QPointF& start = QPointF(), const QPointF& end = QPointF());
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "line"; }
    
    QPointF start() const { return m_start; }
    QPointF end() const { return m_end; }
    void setStart(const QPointF& p) { m_start = p; }
    void setEnd(const QPointF& p) { m_end = p; }
    
private:
    QPointF m_start;
    QPointF m_end;
};

// Circle primitive
class CirclePrimitive : public SchematicPrimitive {
public:
    CirclePrimitive(const QPointF& center = QPointF(), qreal radius = 5.0, bool filled = false);
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "circle"; }
    
    QPointF center() const { return m_center; }
    qreal radius() const { return m_radius; }
    bool isFilled() const { return m_filled; }
    
private:
    QPointF m_center;
    qreal m_radius;
    bool m_filled;
};

// Arc primitive
class ArcPrimitive : public SchematicPrimitive {
public:
    ArcPrimitive(const QRectF& rect = QRectF(), int startAngle = 0, int spanAngle = 90);
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "arc"; }
    
    QRectF rect() const { return m_rect; }
    int startAngle() const { return m_startAngle; }
    int spanAngle() const { return m_spanAngle; }
    
private:
    QRectF m_rect;
    int m_startAngle; // In 1/16th of a degree
    int m_spanAngle;
};

// Polygon primitive
class PolygonPrimitive : public SchematicPrimitive {
public:
    PolygonPrimitive(const QList<QPointF>& points = QList<QPointF>(), bool filled = false);
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "polygon"; }
    
    QList<QPointF> points() const { return m_points; }
    bool isFilled() const { return m_filled; }
    void addPoint(const QPointF& p) { m_points.append(p); }
    
private:
    QList<QPointF> m_points;
    bool m_filled;
};

// Polyline primitive (open path, not closed like polygon)
class PolylinePrimitive : public SchematicPrimitive {
public:
    PolylinePrimitive(const QList<QPointF>& points = QList<QPointF>());
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "polyline"; }
    
    QList<QPointF> points() const { return m_points; }
    void addPoint(const QPointF& p) { m_points.append(p); }
    
private:
    QList<QPointF> m_points;
};

// Rectangle primitive
class RectPrimitive : public SchematicPrimitive {
public:
    RectPrimitive(const QRectF& rect = QRectF(), bool filled = false);
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "rect"; }
    
    QRectF rect() const { return m_rect; }
    bool isFilled() const { return m_filled; }
    
private:
    QRectF m_rect;
    bool m_filled;
};

// Text primitive
class TextPrimitive : public SchematicPrimitive {
public:
    TextPrimitive(const QString& text = QString(), const QPointF& pos = QPointF(), int fontSize = 10);
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "text"; }
    
    QString text() const { return m_text; }
    QPointF position() const { return m_pos; }
    int fontSize() const { return m_fontSize; }
    void setText(const QString& t) { m_text = t; }
    
private:
    QString m_text;
    QPointF m_pos;
    int m_fontSize;
};

// Pin primitive (connection point)
class PinPrimitive : public SchematicPrimitive {
public:
    enum PinDirection { Left, Right, Up, Down };
    
    PinPrimitive(const QPointF& pos = QPointF(), const QString& name = QString(), 
                 PinDirection dir = Right, qreal length = 20.0);
    
    void paint(QPainter* painter, const QPen& pen, const QBrush& brush) const override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;
    std::unique_ptr<SchematicPrimitive> clone() const override;
    QString typeName() const override { return "pin"; }
    
    QPointF connectionPoint() const;
    QString name() const { return m_name; }
    PinDirection direction() const { return m_direction; }
    
private:
    QPointF m_pos;
    QString m_name;
    PinDirection m_direction;
    qreal m_length;
};

#endif // SCHEMATICPRIMITIVES_H
