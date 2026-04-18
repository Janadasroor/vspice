#ifndef SIM_SCHEMATIC_BRIDGE_H
#define SIM_SCHEMATIC_BRIDGE_H

#include "../core/sim_netlist.h"
#include <QGraphicsScene>
#include "net_manager.h"

/**
 * @brief Bridge between VioraEDA Schematic Scene and Pure C++ SimNetlist.
 */
class SimSchematicBridge {
public:
    struct DiagnosticTarget {
        enum class Type {
            None,
            Net,
            Component
        };
        Type type = Type::None;
        QString id;
    };

    static SimNetlist buildNetlist(QGraphicsScene* scene, NetManager* netManager);
    static DiagnosticTarget extractDiagnosticTarget(const QString& message);

private:
    static double parseValue(const QString& val);
};

#endif // SIM_SCHEMATIC_BRIDGE_H
