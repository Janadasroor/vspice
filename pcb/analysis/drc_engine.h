#ifndef DRC_ENGINE_H
#define DRC_ENGINE_H

#include <QList>
#include <QString>
#include <QPointF>
#include "../models/board_model.h"
#include "../drc/pcb_drc.h" // Reuse DRCViolation and DRCRules for compatibility

namespace Flux {
namespace Analysis {

/**
 * @brief Headless Design Rule Check (DRC) Engine.
 * Operates exclusively on BoardModel (POCO) to allow background processing
 * and automated testing without a GUI.
 */
class DRCEngine {
public:
    struct CheckResult {
        QList<DRCViolation> violations;
        int errorCount = 0;
        int warningCount = 0;
    };

    /**
     * @brief Run a full DRC check on a board model.
     */
    static CheckResult runFullCheck(const Model::BoardModel* board, const DRCRules& rules) {
        CheckResult result;
        
        // 1. Check Trace Widths
        checkTraceWidths(board, rules, result);
        
        // 2. Check Clearances (Simplistic implementation for now)
        checkClearances(board, rules, result);
        
        // 3. Check Via/Drill sizes
        checkDrillSizes(board, rules, result);

        return result;
    }

private:
    static void checkTraceWidths(const Model::BoardModel* board, const DRCRules& rules, CheckResult& result) {
        for (auto* trace : board->traces()) {
            if (trace->width() < rules.minTraceWidth() - 0.0001) {
                DRCViolation v(
                    DRCViolation::MinTraceWidth,
                    DRCViolation::Error,
                    QString("Trace %1 width %2mm is below minimum %3mm")
                        .arg(trace->id().toString())
                        .arg(trace->width())
                        .arg(rules.minTraceWidth()),
                    trace->start(),
                    trace->id().toString()
                );
                result.violations.append(v);
                result.errorCount++;
            }
        }
    }

    static void checkDrillSizes(const Model::BoardModel* board, const DRCRules& rules, CheckResult& result) {
        for (auto* via : board->vias()) {
            if (via->drillSize() < rules.minDrillSize() - 0.0001) {
                DRCViolation v(
                    DRCViolation::MinDrillSize,
                    DRCViolation::Error,
                    QString("Via %1 drill %2mm is below minimum %3mm")
                        .arg(via->id().toString())
                        .arg(via->drillSize())
                        .arg(rules.minDrillSize()),
                    via->pos(),
                    via->id().toString()
                );
                result.violations.append(v);
                result.errorCount++;
            }
        }
    }

    static void checkClearances(const Model::BoardModel* board, const DRCRules& rules, CheckResult& result) {
        // Example: Trace to Via clearance (N^2 complexity, but headless so okay for small boards)
        // In a real implementation, we'd use a Spatial Hash or Quadtree here.
        for (auto* trace : board->traces()) {
            for (auto* via : board->vias()) {
                // Skip if same net
                if (!trace->netName().isEmpty() && trace->netName() == via->netName()) continue;
                
                // Simple distance check from trace endpoints (not full segment yet)
                double d1 = QLineF(trace->start(), via->pos()).length();
                double d2 = QLineF(trace->end(), via->pos()).length();
                
                double minDistance = (trace->width() / 2.0) + (via->diameter() / 2.0) + rules.minClearance();
                
                if (d1 < minDistance || d2 < minDistance) {
                    DRCViolation v(
                        DRCViolation::ClearanceViolation,
                        DRCViolation::Error,
                        QString("Clearance violation between Trace %1 and Via %2")
                            .arg(trace->id().toString())
                            .arg(via->id().toString()),
                        via->pos(),
                        trace->id().toString(),
                        via->id().toString()
                    );
                    result.violations.append(v);
                    result.errorCount++;
                }
            }
        }
    }
};

} // namespace Analysis
} // namespace Flux

#endif // DRC_ENGINE_H
