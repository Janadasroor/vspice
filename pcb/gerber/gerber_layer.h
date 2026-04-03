#ifndef GERBER_LAYER_H
#define GERBER_LAYER_H

#include <QString>
#include <QList>
#include <QPointF>
#include <QPainterPath>
#include <QMap>
#include <QColor>

/**
 * @brief Represents a Gerber aperture (tool)
 */
struct GerberAperture {
    enum Type { Circle, Rectangle, Obround, Polygon, Macro };
    Type type;
    QList<double> params;
};

/**
 * @brief Represents a single drawing command in a Gerber file
 */
struct GerberPrimitive {
    enum Type { Line, Arc, Flash };
    Type type;
    int apertureId;
    QPainterPath path;
    QPointF center; // For arcs and flashes
};

/**
 * @brief Data structure for a parsed Gerber layer
 */
class GerberLayer {
public:
    GerberLayer(const QString& name = "") : m_name(name), m_color(Qt::white), m_visible(true) {}

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QColor color() const { return m_color; }
    void setColor(const QColor& color) { m_color = color; }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    void addPrimitive(const GerberPrimitive& prim) { m_primitives.append(prim); }
    const QList<GerberPrimitive>& primitives() const { return m_primitives; }

    void setAperture(int id, const GerberAperture& ap) { m_apertures[id] = ap; }
    GerberAperture getAperture(int id) const { return m_apertures.value(id); }

private:
    QString m_name;
    QColor m_color;
    bool m_visible;
    QList<GerberPrimitive> m_primitives;
    QMap<int, GerberAperture> m_apertures;
};

#endif // GERBER_LAYER_H
