#include "schematic_connectivity.h"
#include "wire_item.h"
#include "bus_item.h"
#include "netlist_engine.h"
#include "../items/schematic_item.h"
#include "net_manager.h"
#include <QGraphicsItem>
#include <QLineF>
#include <QUuid>
#include <algorithm>

using Flux::Analysis::NetlistEngine;

namespace {
QString exportRefForItem(const SchematicItem* item) {
    if (!item) return "UNKNOWN";
    const QString ref = item->reference().trimmed();
    if (!ref.isEmpty()) return ref;
    return QString("%1_%2").arg(item->itemTypeName(), item->id().toString(QUuid::WithoutBraces));
}

bool isConductiveOnlyItem(const SchematicItem* item) {
    if (!item) return true;
    return item->itemType() == SchematicItem::WireType ||
           item->itemType() == SchematicItem::JunctionType ||
           item->itemType() == SchematicItem::BusType ||
           item->itemType() == SchematicItem::NoConnectType ||
           item->itemTypeName() == "BusEntry";
}
} // namespace

void SchematicConnectivity::updateVisualConnections(QGraphicsScene* scene) {
    if (!scene) return;

    QList<WireItem*> allWires;
    QList<BusItem*> allBuses;
    QList<SchematicItem*> conductiveItems;
    for (auto* item : scene->items()) {
        if (WireItem* wire = dynamic_cast<WireItem*>(item)) {
            allWires.append(wire);
            conductiveItems.append(wire);
            wire->clearJunctions();
            wire->clearJumpOvers();
        } else if (BusItem* bus = dynamic_cast<BusItem*>(item)) {
            allBuses.append(bus);
            conductiveItems.append(bus);
            bus->clearJunctions();
        }
    }

    QMap<QString, int> connectionCounts; 

    // Count endpoints across all conductive segments (wire + bus).
    for (SchematicItem* item : conductiveItems) {
        const QList<QPointF> pts = item->connectionPoints();
        if (pts.size() < 2) continue;
        connectionCounts[NetlistEngine::pointKey(pts.first())]++;
        connectionCounts[NetlistEngine::pointKey(pts.last())]++;
    }

    auto addJunction = [](SchematicItem* item, const QPointF& p) {
        if (WireItem* w = dynamic_cast<WireItem*>(item)) w->addJunction(p);
        else if (BusItem* b = dynamic_cast<BusItem*>(item)) b->addJunction(p);
    };

    // Detect Junctions: 
    // A dot appears ONLY if:
    // 1. Three or more endpoints meet at the same location.
    // 2. An endpoint of one wire touches the middle of a segment of another wire (T-Junction).
    for (SchematicItem* item : conductiveItems) {
        const QList<QPointF> wPts = item->connectionPoints();
        if (wPts.size() < 2) continue;
        
        // Rule 1: 3+ way connections at endpoints
        if (connectionCounts[NetlistEngine::pointKey(wPts.first())] >= 3) addJunction(item, wPts.first());
        if (connectionCounts[NetlistEngine::pointKey(wPts.last())] >= 3) addJunction(item, wPts.last());

        // Rule 2: T-Junctions (Endpoint of 'other' on segment of 'item')
        for (int i = 0; i < wPts.size() - 1; ++i) {
            QLineF segment(wPts[i], wPts[i+1]);
            for (SchematicItem* other : conductiveItems) {
                if (item == other) continue;
                
                const QList<QPointF> otherPts = other->connectionPoints();
                if (otherPts.size() < 2) continue;
                
                QPointF ends[2] = {otherPts.first(), otherPts.last()};
                for (const QPointF& end : ends) {
                    QPointF p1 = wPts[i];
                    QPointF p2 = wPts[i+1];
                    QPointF vec = p2 - p1;
                    qreal lenSq = vec.x()*vec.x() + vec.y()*vec.y();
                    if (lenSq > 0) {
                        qreal u = ((end.x() - p1.x()) * vec.x() + (end.y() - p1.y()) * vec.y()) / lenSq;
                        // Tolerance check: must be strictly inside the segment (not at endpoints)
                        if (u > 0.01 && u < 0.99) { 
                            QPointF proj = p1 + u * vec;
                            if (QLineF(end, proj).length() < 1.0) {
                                addJunction(item, proj); // Dot on the segment
                                addJunction(other, end); // Dot on the connecting endpoint
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Add jump-overs
    for (WireItem* wire : allWires) {
        detectJumpOvers(wire, allWires);
    }
}

QList<SchematicConnectivityNet> SchematicConnectivity::buildConnectivity(QGraphicsScene* scene, NetManager* netManager) {
    QList<SchematicConnectivityNet> out;
    if (!scene) return out;

    NetManager localManager;
    NetManager* mgr = netManager ? netManager : &localManager;
    mgr->updateNets(scene);

    QStringList netNames = mgr->netNames();
    std::sort(netNames.begin(), netNames.end(), [](const QString& a, const QString& b) {
        return a.toLower() < b.toLower();
    });

    for (const QString& netName : netNames) {
        SchematicConnectivityNet net;
        net.name = netName;
        QList<NetConnection> conns = mgr->getConnections(netName);

        for (const NetConnection& conn : conns) {
            SchematicItem* item = conn.item;
            if (!item || isConductiveOnlyItem(item)) {
                continue;
            }

            SchematicConnectivityPin pin;
            pin.componentRef = exportRefForItem(item);
            pin.pinName = conn.pinName.isEmpty() ? "1" : conn.pinName;

            bool exists = false;
            for (const auto& p : net.pins) {
                if (p.componentRef == pin.componentRef && p.pinName == pin.pinName) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                net.pins.append(pin);
            }
        }

        out.append(net);
    }

    return out;
}

void SchematicConnectivity::detectJumpOvers(WireItem* wire, const QList<WireItem*>& allWires) {
    // Jump-overs removed per user request (wires now cross directly without semicircles)
    Q_UNUSED(wire) Q_UNUSED(allWires)
}
