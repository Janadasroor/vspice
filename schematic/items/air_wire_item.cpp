#include "air_wire_item.h"
#include <QCursor>

AirWireItem::AirWireItem(const QPointF& from, const QPointF& to,
                         const QString& netName, QGraphicsItem* parent)
    : QGraphicsLineItem(QLineF(from, to), parent)
    , m_netName(netName)
{
    setupPen();

    // Tag so serialization and selection filters can skip this item
    setData(0, "airwire");

    // Non-interactive: user cannot select or move air wires
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    setAcceptHoverEvents(true);

    // Z-value below components but above grid
    setZValue(-5);

    if (!netName.isEmpty()) {
        setToolTip(QString("Air Wire: %1").arg(netName));
    }
}

void AirWireItem::updateEndpoints(const QPointF& from, const QPointF& to) {
    setLine(QLineF(from, to));
}

void AirWireItem::setNetName(const QString& name) {
    m_netName = name;
    if (!name.isEmpty()) {
        setToolTip(QString("Air Wire: %1").arg(name));
    }
}

void AirWireItem::setupPen() {
    QPen airPen;
    airPen.setColor(QColor(0, 180, 220, 160));  // Semi-transparent cyan
    airPen.setWidthF(0.8);
    airPen.setStyle(Qt::DashLine);
    airPen.setDashPattern({6, 4});
    airPen.setCosmetic(true);  // Constant screen-space width regardless of zoom
    setPen(airPen);
}
