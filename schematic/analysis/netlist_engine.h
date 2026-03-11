#ifndef NETLIST_ENGINE_H
#define NETLIST_ENGINE_H

#include <QList>
#include <QMap>
#include <QString>
#include <QPointF>
#include <QSet>
#include <QUuid>
#include "../models/schematic_page_model.h"

namespace Flux {
namespace Analysis {

/**
 * @brief Robust Disjoint Set Union (DSU) for grouping physical points into nets.
 */
class NetUnionSet {
public:
    QString find(const QString& p) {
        if (!parent.contains(p)) return p;
        if (parent[p] == p) return p;
        return parent[p] = find(parent[p]);
    }

    void unite(const QString& p1, const QString& p2) {
        QString root1 = find(p1);
        QString root2 = find(p2);
        if (root1 != root2) {
            parent[root1] = root2;
        }
    }

    void ensure(const QString& p) {
        if (!parent.contains(p)) parent[p] = p;
    }

private:
    QMap<QString, QString> parent;
};

/**
 * @brief Headless engine for generating netlists from Schematic Models.
 * Provides high-reliability connectivity resolving using DSU.
 */
class NetlistEngine {
public:
    struct PinConnection {
        QString componentRef;
        QString pinName;
        QString pinNumber;
        QUuid componentId;
    };

    struct Net {
        QString name;
        QList<PinConnection> connections;
        QSet<QString> labels;
    };

    /**
     * @brief Generate a full netlist from a schematic page with high reliability.
     */
    static QMap<QString, Net> generateNetlist(const Model::SchematicPageModel* page) {
        NetUnionSet dsu;

        // 1. Process all wires to establish physical connectivity
        for (auto* wire : page->wires()) {
            const auto& points = wire->points();
            if (points.isEmpty()) continue;
            
            QString firstKey = pointKey(points.first());
            dsu.ensure(firstKey);
            
            for (int i = 1; i < points.size(); ++i) {
                QString currentKey = pointKey(points[i]);
                dsu.ensure(currentKey);
                dsu.unite(firstKey, currentKey);
            }
        }

        // 2. Identify and group all connected pins
        QMap<QString, Net> netsByRoot;
        
        for (auto* comp : page->components()) {
            for (auto* pin : comp->pins()) {
                QPointF globalPinPos = comp->pos() + pin->pos;
                QString key = pointKey(globalPinPos);
                QString root = dsu.find(key);

                if (!netsByRoot.contains(root)) {
                    netsByRoot[root].name = QString("NET_%1").arg(root.replace(":", "_"));
                }
                
                netsByRoot[root].connections.append({
                    comp->reference(),
                    pin->name,
                    pin->number,
                    comp->id()
                });
            }
        }

        // 3. Resolve Net Names (Prefer labels over generated names)
        for (const auto& l : page->labels()) {
            QString key = pointKey(l.pos);
            QString root = dsu.find(key);
            if (netsByRoot.contains(root)) {
                netsByRoot[root].name = l.text;
                netsByRoot[root].labels.insert(l.text);
            } else {
                // Label not touching any wire — still record it as a potential net
                netsByRoot[root].name = l.text;
                netsByRoot[root].labels.insert(l.text);
            }
        }
        
        return netsByRoot;
    }

    /**
     * @brief Precise key for physical connection points (snapped to 1mm/1mil grid)
     */
    static QString pointKey(const QPointF& p) {
        // Round to nearest integer to avoid floating point precision issues
        return QString("%1:%2").arg(qRound(p.x())).arg(qRound(p.y()));
    }
};

} // namespace Analysis
} // namespace Flux

#endif // NETLIST_ENGINE_H
