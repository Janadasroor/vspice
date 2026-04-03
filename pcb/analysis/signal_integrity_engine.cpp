#include "signal_integrity_engine.h"
#include "../items/trace_item.h"
#include <QGraphicsScene>
#include <cmath>
#include <algorithm>

namespace Flux {
namespace Analysis {

double SignalIntegrityEngine::calculateCharacteristicImpedance(double width, double h, double er, double t) {
    if (h <= 0 || width <= 0 || er <= 1.0) return 0.0;

    // --- IPC-2141 Surface Microstrip Formula ---
    // Accurate for 0.1 < w/h < 3.0 and 1 < er < 15
    
    // Effective dielectric constant approximation
    double e_eff = (er + 1.0) / 2.0 + ((er - 1.0) / 2.0) * std::pow(1.0 + 12.0 * h / width, -0.5);
    
    // Characteristic Impedance (Z0)
    double z0 = (60.0 / std::sqrt(e_eff)) * std::log(8.0 * h / width + 0.25 * width / h);
    
    // Adjust for thickness 't' using IPC-2141 empirical correction
    if (t > 0) {
        double w_eff = width + (t / M_PI) * (1.0 + std::log(2.0 * h / t));
        z0 = (60.0 / std::sqrt(e_eff)) * std::log(8.0 * h / w_eff + 0.25 * w_eff / h);
    }

    return z0;
}

double SignalIntegrityEngine::estimateCrosstalk(double s, double w, double h, double length) {
    if (s <= 0 || h <= 0) return 1.0; // Total coupling if touching

    // --- Empirical Crosstalk Coefficient (NEXT/FEXT approximation) ---
    // K = 1 / (1 + (s/h)^2)
    // This is a simplified model for peak coupling.
    
    double couplingCoeff = 1.0 / (1.0 + std::pow(s / h, 2.0));
    
    // Factor in parallel length (crosstalk increases with length up to saturation)
    // Critical length Lc = tr * v / 2
    // For now, we return the coupling coefficient which is the worst-case.
    
    return couplingCoeff;
}

SignalIntegrityEngine::SIRadarResults SignalIntegrityEngine::analyzeLive(QGraphicsScene* scene, const QPointF& pos, double width, int layerId, const PCBLayerManager::BoardStackup& stackup, const QString& currentNet) {
    SIRadarResults results;
    
    // 1. Identify physical parameters for this layer
    double h = 0.2;  // Default 0.2mm dielectric
    double er = 4.2; // Default FR4
    double t = 0.035; // Default 1oz copper
    
    bool found = false;
    for (int i = 0; i < stackup.stack.size(); ++i) {
        if (stackup.stack[i].layerId == layerId) {
            // Find dielectric BELOW this copper layer (Microstrip reference)
            // Note: Stackup usually alternating Copper/Dielectric
            for (int j = i + 1; j < stackup.stack.size(); ++j) {
                if (stackup.stack[j].type != "Copper") {
                    h = stackup.stack[j].thickness;
                    er = stackup.stack[j].dielectricConstant;
                    break;
                }
            }
            t = stackup.stack[i].thickness;
            found = true;
            break;
        }
    }
    
    // 2. Calculate Z0 and Delay
    results.characteristicImpedance = calculateCharacteristicImpedance(width, h, er, t);
    
    double e_eff = (er + 1.0) / 2.0 + ((er - 1.0) / 2.0) * std::pow(1.0 + 12.0 * h / width, -0.5);
    results.propagationDelay = (1000.0 / 299.79) * std::sqrt(e_eff); // ps/mm
    
    // 3. Crosstalk Radar (Nearest Neighbor search)
    if (scene) {
        double searchRadius = 2.0; // mm
        QList<QGraphicsItem*> near = scene->items(QRectF(pos.x() - searchRadius, pos.y() - searchRadius, searchRadius*2, searchRadius*2));
        
        double minSep = 1e9;
        QString victimNet;
        
        for (auto* item : near) {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                if (trace->layer() != layerId || trace->netName() == currentNet || trace->netName() == "No Net") continue;
                
                // Simple distance from point to line segment
                QLineF line(trace->mapToScene(trace->startPoint()), trace->mapToScene(trace->endPoint()));
                
                // Calculate distance from pos to line
                // ... (using standard point-to-line-segment distance)
                double dist = 1e9;
                double l2 = std::pow(line.dx(), 2) + std::pow(line.dy(), 2);
                if (l2 == 0) dist = QLineF(pos, line.p1()).length();
                else {
                    double t_proj = ((pos.x() - line.x1()) * line.dx() + (pos.y() - line.y1()) * line.dy()) / l2;
                    t_proj = std::max(0.0, std::min(1.0, t_proj));
                    dist = QLineF(pos, QPointF(line.x1() + t_proj * line.dx(), line.y1() + t_proj * line.dy())).length();
                }

                // Edge-to-edge separation
                double sep = dist - (width / 2.0) - (trace->width() / 2.0);
                if (sep < minSep) {
                    minSep = sep;
                    victimNet = trace->netName();
                }
            }
        }
        
        if (minSep < searchRadius) {
            results.crosstalkCoupling = estimateCrosstalk(std::max(0.01, minSep), width, h, 10.0);
            results.nearestVictimNet = victimNet;
        }
    }

    results.valid = true;
    return results;
}

} // namespace Analysis
} // namespace Flux
