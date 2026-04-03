#ifndef FOOTPRINT_PRIMITIVE_MODEL_H
#define FOOTPRINT_PRIMITIVE_MODEL_H

#include <QString>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QJsonObject>
#include <QJsonArray>
#include <QRectF>

namespace Flux {
namespace Model {

/**
 * @brief Pure data model for a single drawing primitive in a footprint.
 */
struct FootprintPrimitive {
    enum Type {
        Line,       // Silkscreen line
        Arc,        // Silkscreen arc
        Rect,       // Silkscreen rectangle
        Circle,     // Silkscreen circle
        Polygon,    // Silkscreen or copper polygon
        Text,       // Text label
        Pad,        // Copper pad (SMD or Through-hole)
        Via,        // Via hole
        Dimension   // Persistent measurement line
    };

    enum Layer {
        Top_Silkscreen,
        Top_Courtyard,
        Top_Fabrication,
        Top_Copper,      // Primarily for pads
        Bottom_Copper,
        Bottom_Silkscreen,
        Top_SolderMask,
        Bottom_SolderMask,
        Top_SolderPaste,
        Bottom_SolderPaste,
        Top_Adhesive,
        Bottom_Adhesive,
        Bottom_Courtyard,
        Bottom_Fabrication,
        Inner_Copper_1,
        Inner_Copper_2,
        Inner_Copper_3,
        Inner_Copper_4
    };
    
    Type type;
    Layer layer = Top_Silkscreen;
    QJsonObject data;  // Type-specific parameters
    
    // Convenience constructors
    static FootprintPrimitive createLine(QPointF p1, QPointF p2, qreal width = 0.1);
    static FootprintPrimitive createRect(QRectF rect, bool filled = false, qreal width = 0.1);
    static FootprintPrimitive createCircle(QPointF center, qreal radius, bool filled = false, qreal width = 0.1);
    static FootprintPrimitive createArc(QPointF center, qreal radius, qreal startAngle, qreal spanAngle, qreal width = 0.1);
    static FootprintPrimitive createText(const QString& text, QPointF pos, qreal height = 1.0);
    static FootprintPrimitive createPad(QPointF pos, const QString& number, const QString& shape = "Rect", QSizeF size = QSizeF(1.5, 1.5), qreal cornerRadius = 0.0);
    static FootprintPrimitive createPolygonPad(const QList<QPointF>& points, const QString& number);
    
    QJsonObject toJson() const;
    static FootprintPrimitive fromJson(const QJsonObject& json);
    
    void move(qreal dx, qreal dy);
};

} // namespace Model
} // namespace Flux

#endif // FOOTPRINT_PRIMITIVE_MODEL_H
