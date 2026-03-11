#ifndef ERC_ENGINE_H
#define ERC_ENGINE_H

#include <QList>
#include <QString>
#include <QPointF>
#include "schematic_page_model.h"

namespace Flux {
namespace Analysis {

/**
 * @brief Represents a single ERC violation (Electronic Rules Check).
 */
struct ERCViolation {
    enum Severity { Error, Warning, Info };
    enum Type { 
        UnconnectedPin,      // Pin with no wire attached
        FloatingWire,       // Wire with no connections at ends
        NetNamingConflict,  // Multiple labels on same net
        ShortCircuit,       // Different power nodes connected
        MissingValue,       // Component without a value (e.g., R with no ohms)
        InvalidReference    // Duplicate reference (e.g., two R1's)
    };

    Type type;
    Severity severity;
    QString message;
    QPointF location;
    QUuid itemId;
};

/**
 * @brief Headless engine for Electronic Rule Checks.
 */
class ERCEngine {
public:
    struct CheckResult {
        QList<ERCViolation> violations;
        int errorCount = 0;
        int warningCount = 0;
    };

    /**
     * @brief Run basic ERC on a schematic page.
     */
    static CheckResult runPageCheck(const Model::SchematicPageModel* page) {
        CheckResult result;

        // 1. Check for missing values or references
        checkBasics(page, result);

        // 2. Check for unconnected pins
        checkUnconnectedPins(page, result);

        return result;
    }

private:
    static void checkBasics(const Model::SchematicPageModel* page, CheckResult& result) {
        for (auto* comp : page->components()) {
            if (comp->value().isEmpty()) {
                result.violations.append({
                    ERCViolation::MissingValue,
                    ERCViolation::Warning,
                    QString("Component %1 has no value set.").arg(comp->reference()),
                    comp->pos(),
                    comp->id()
                });
                result.warningCount++;
            }
        }
    }

    static void checkUnconnectedPins(const Model::SchematicPageModel* page, CheckResult& result) {
        // Collect all wire endpoints
        QSet<QString> connectionPoints;
        for (auto* wire : page->wires()) {
            for (const QPointF& p : wire->points()) {
                connectionPoints.insert(pointKey(p));
            }
        }

        for (auto* comp : page->components()) {
            for (auto* pin : comp->pins()) {
                // Pin position is local, need to map to page (simplification: assume no rotation for test)
                QPointF globalPinPos = comp->pos() + pin->pos;
                if (!connectionPoints.contains(pointKey(globalPinPos))) {
                    result.violations.append({
                        ERCViolation::UnconnectedPin,
                        ERCViolation::Warning,
                        QString("Pin %1 of %2 is not connected.").arg(pin->name).arg(comp->reference()),
                        globalPinPos,
                        comp->id()
                    });
                    result.warningCount++;
                }
            }
        }
    }

    static QString pointKey(const QPointF& p) {
        return QString("%1:%2").arg(std::round(p.x()), 0, 'f', 0).arg(std::round(p.y()), 0, 'f', 0);
    }
};

} // namespace Analysis
} // namespace Flux

#endif // ERC_ENGINE_H
