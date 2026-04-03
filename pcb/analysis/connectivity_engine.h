#ifndef CONNECTIVITY_ENGINE_H
#define CONNECTIVITY_ENGINE_H

#include <QList>
#include <QMap>
#include <QString>
#include <QUuid>
#include <QPointF>
#include <QSet>
#include "../models/trace_model.h"
#include "../models/via_model.h"
#include "../models/pad_model.h"

namespace Flux {
namespace Analysis {

/**
 * @brief Headless engine for analyzing PCB connectivity.
 * This engine operates solely on Data Models, allowing it to run in background threads.
 */
class ConnectivityEngine {
public:
    struct PointConnection {
        QPointF point;
        QUuid itemId;
        QString netName;
    };

    /**
     * @brief Find all items belonging to a specific net.
     */
    static QMap<QString, QList<QUuid>> groupItemsByNet(
        const QList<Model::TraceModel*>& traces,
        const QList<Model::ViaModel*>& vias,
        const QList<Model::PadModel*>& pads
    ) {
        QMap<QString, QList<QUuid>> netMap;

        for (auto* t : traces) if (!t->netName().isEmpty()) netMap[t->netName()].append(t->id());
        for (auto* v : vias) if (!v->netName().isEmpty()) netMap[v->netName()].append(v->id());
        for (auto* p : pads) if (!p->netName().isEmpty()) netMap[p->netName()].append(p->id());

        return netMap;
    }

    /**
     * @brief Find "Floating" items (items with no net assigned).
     */
    static QList<QUuid> findFloatingItems(
        const QList<Model::TraceModel*>& traces,
        const QList<Model::ViaModel*>& vias,
        const QList<Model::PadModel*>& pads
    ) {
        QList<QUuid> floating;
        for (auto* t : traces) if (t->netName().isEmpty()) floating.append(t->id());
        for (auto* v : vias) if (v->netName().isEmpty()) floating.append(v->id());
        for (auto* p : pads) if (p->netName().isEmpty()) floating.append(p->id());
        return floating;
    }

    /**
     * @brief Check if two models are physically touching.
     * Pure geometric logic - no Qt Graphics system involved.
     */
    static bool areTouching(const Model::TraceModel* t, const Model::PadModel* p) {
        // Simple distance check from endpoints to pad center
        double d1 = QLineF(t->start(), p->pos()).length();
        double d2 = QLineF(t->end(), p->pos()).length();
        double threshold = (t->width() + p->size().width()) / 2.0;
        return (d1 < threshold || d2 < threshold);
    }
    
    // ... more complex algorithms (MST, etc.) can be added here
};

} // namespace Analysis
} // namespace Flux

#endif // CONNECTIVITY_ENGINE_H
