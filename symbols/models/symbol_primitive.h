#ifndef SYMBOL_PRIMITIVE_MODEL_H
#define SYMBOL_PRIMITIVE_MODEL_H

#include <QString>
#include <QList>
#include <QPointF>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>
#include <QRectF>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a single drawing primitive in a symbol.
 */
struct SymbolPrimitive {
    enum Type {
        Line,       // Two-point line
        Arc,        // Arc with center, radius, start/span angles
        Rect,       // Rectangle
        Circle,     // Circle with center and radius
        Polygon,    // Multi-point polygon
        Text,       // Text label
        Pin,        // Connection pin with number and name
        Bezier,     // Cubic Bezier curve (4 points)
        Image       // Bitmap image
    };
    
    Type type;
    QJsonObject data;  // Type-specific parameters
    
    int unit() const { return m_unit; }
    void setUnit(int u) { m_unit = u; }

    int bodyStyle() const { return m_bodyStyle; }
    void setBodyStyle(int s) { m_bodyStyle = s; }

    // Convenience constructors
    static SymbolPrimitive createLine(QPointF p1, QPointF p2);
    static SymbolPrimitive createRect(QRectF rect, bool filled = false);
    static SymbolPrimitive createCircle(QPointF center, qreal radius, bool filled = false);
    static SymbolPrimitive createArc(QRectF rect, int startAngle, int spanAngle);
    static SymbolPrimitive createText(const QString& text, QPointF pos, int fontSize = 10, QColor color = QColor(Qt::white));
    static SymbolPrimitive createPin(QPointF pos, int number, const QString& name = QString(), const QString& orientation = "Right", qreal length = 20.0);
    static SymbolPrimitive createPolygon(const QList<QPointF>& points, bool filled = false);
    static SymbolPrimitive createBezier(QPointF p1, QPointF c1, QPointF c2, QPointF p2);
    static SymbolPrimitive createImage(const QString& base64Data, QRectF rect);
    
    QJsonObject toJson() const;
    static SymbolPrimitive fromJson(const QJsonObject& json);

    void rotateCW(QPointF center);
    void rotateCCW(QPointF center);
    void flipH(QPointF center);
    void flipV(QPointF center);
    void move(qreal dx, qreal dy);

private:
    int m_unit = 0; // 0 = shared, 1..N = specific unit
    int m_bodyStyle = 0; // 0 = shared, 1 = standard, 2 = alternate
};

} // namespace Model
} // namespace Flux

#endif // SYMBOL_PRIMITIVE_MODEL_H
