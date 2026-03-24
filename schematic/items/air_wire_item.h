#ifndef AIR_WIRE_ITEM_H
#define AIR_WIRE_ITEM_H

#include <QGraphicsLineItem>
#include <QPen>
#include <QString>

/**
 * @brief Visual-only ratsnest line showing connectivity between two pins.
 *
 * AirWireItem is a lightweight QGraphicsLineItem (NOT a SchematicItem) so it
 * is never serialized to .flxsch files. It draws a thin dashed line between
 * two scene-coordinate endpoints, representing an unrouted net connection.
 *
 * Users see these air wires as a guide and can manually route real wires
 * over them. Air wires disappear on save/reload.
 */
class AirWireItem : public QGraphicsLineItem {
public:
    AirWireItem(const QPointF& from, const QPointF& to,
                const QString& netName = QString(),
                QGraphicsItem* parent = nullptr);

    /// Update both endpoints (e.g. when components are moved)
    void updateEndpoints(const QPointF& from, const QPointF& to);

    /// Net name for tooltip display
    QString netName() const { return m_netName; }
    void setNetName(const QString& name);

private:
    void setupPen();
    QString m_netName;
};

#endif // AIR_WIRE_ITEM_H
