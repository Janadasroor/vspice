#ifndef SIGNAL_INTEGRITY_ENGINE_H
#define SIGNAL_INTEGRITY_ENGINE_H

#include <QString>
#include <QPointF>
#include <vector>
#include "../layers/pcb_layer.h"

class QGraphicsScene;

namespace Flux {
namespace Analysis {

/**
 * @brief Logic for real-time Signal Integrity (SI) calculations.
 * Supports characteristic impedance (Z0) and crosstalk coupling estimation.
 */
class SignalIntegrityEngine {
public:
    struct SIRadarResults {
        double characteristicImpedance; // Ohms (Z0)
        double propagationDelay;        // ps/mm
        double crosstalkCoupling;       // Percentage (0.0 to 1.0)
        QString nearestVictimNet;
        bool valid = false;
    };

    /**
     * @brief Calculate characteristic impedance for a microstrip or stripline.
     * @param width Trace width in mm
     * @param h Dielectric height to reference plane in mm
     * @param er Dielectric constant
     * @param t Copper thickness in mm (e.g. 0.035 for 1oz)
     */
    static double calculateCharacteristicImpedance(double width, double h, double er, double t);

    /**
     * @brief Estimate crosstalk coupling between two parallel traces.
     * @param s Separation (edge-to-edge) in mm
     * @param w Trace width in mm
     * @param h Dielectric height in mm
     * @param length Parallel length in mm
     */
    static double estimateCrosstalk(double s, double w, double h, double length);

    /**
     * @brief Perform a comprehensive real-time SI check for a point during routing.
     */
    static SIRadarResults analyzeLive(QGraphicsScene* scene, const QPointF& pos, double width, int layerId, const PCBLayerManager::BoardStackup& stackup, const QString& currentNet);
};

} // namespace Analysis
} // namespace Flux

#endif // SIGNAL_INTEGRITY_ENGINE_H
