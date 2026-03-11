#ifndef SCHEMATIC_WIRE_UTILS_H
#define SCHEMATIC_WIRE_UTILS_H

#include <QGraphicsScene>
#include <QUndoStack>
#include <QList>

class SchematicItem;
class WireItem;

/**
 * @brief Utilities for wire manipulation in the schematic
 */
class SchematicWireUtils {
public:
    /**
     * @brief Automatically split wires that are crossed by a component's pins.
     * Matches LTSpice/KiCad behavior where dropping a component on a wire "breaks" the wire.
     * @param excludeWires Wires to skip (e.g. wires already attached to moved component pins).
     */
    static void splitWiresByComponent(
        SchematicItem* component,
        QGraphicsScene* scene,
        QUndoStack* undoStack,
        const QList<WireItem*>& excludeWires = {}
    );

    /**
     * @brief Adjusts wire points to maintain 90-degree bends when an endpoint moves.
     * @param points The current points of the wire.
     * @param index The index of the point being moved (usually 0 or size-1).
     * @param newPos The new scene position for that point.
     * @return The updated list of points.
     */
    static QList<QPointF> maintainOrthogonality(const QList<QPointF>& points, int index, const QPointF& newPos);
};

#endif // SCHEMATIC_WIRE_UTILS_H
