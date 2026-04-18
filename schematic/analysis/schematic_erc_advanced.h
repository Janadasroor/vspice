#ifndef SCHEMATIC_ERC_ADVANCED_H
#define SCHEMATIC_ERC_ADVANCED_H

#include <QList>
#include <QString>
#include <QMap>
#include <QSet>
#include "../items/schematic_item.h"
#include "net_manager.h"
#include "design_rule.h"

using namespace Flux::Core;

/**
 * @brief Advanced ERC (Electrical Rules Check) engine
 * 
 * Provides comprehensive electrical rule checking beyond the basic
 * pin connection matrix.
 */
class SchematicERCAdvanced {
public:
    /**
     * @brief Run all advanced ERC checks on a scene
     * @param scene The schematic scene to check
     * @param netManager Net connectivity information
     * @param rules Design rules to apply
     * @return List of violations found
     */
    static QList<DesignRuleViolation> runAllChecks(
        QGraphicsScene* scene,
        NetManager* netManager,
        const DesignRuleSet* rules
    );

    // Individual check functions
    static QList<DesignRuleViolation> checkUnconnectedPins(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkOutputConflicts(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkMissingPowerPins(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkFloatingInputs(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkDuplicateDesignators(
        QGraphicsScene* scene,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkMissingValues(
        QGraphicsScene* scene,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkMissingFootprints(
        QGraphicsScene* scene,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkBusWidthMismatch(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkDifferentVoltageDomains(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkMissingDecouplingCaps(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    static QList<DesignRuleViolation> checkUnconnectedNets(
        QGraphicsScene* scene,
        NetManager* netManager,
        DesignRule* rule
    );

    // Helper functions
    static QList<SchematicItem*> getComponentsByType(
        QGraphicsScene* scene, 
        const QString& typeName
    );

    static QList<SchematicItem*> getComponentsConnectedToNet(
        NetManager* netManager,
        const QString& netName
    );

    static bool hasDecouplingCapacitor(
        NetManager* netManager,
        SchematicItem* ic,
        double minCapacitance = 0.1e-6
    );

    static QString getNetVoltageDomain(const QString& netName);
};

#endif // SCHEMATIC_ERC_ADVANCED_H
