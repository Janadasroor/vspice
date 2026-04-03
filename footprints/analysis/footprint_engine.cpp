#include "footprint_engine.h"
#include <QMap>
#include <QSet>
#include <QLineF>

namespace Flux {
namespace Analysis {

QList<FootprintViolation> FootprintEngine::checkFootprint(const Model::FootprintDefinition& footprint) {
    QList<FootprintViolation> violations;

    checkMetadata(footprint, violations);
    checkPads(footprint, violations);
    checkPhysicalRules(footprint, violations);

    return violations;
}

void FootprintEngine::checkMetadata(const Model::FootprintDefinition& footprint, QList<FootprintViolation>& violations) {
    if (footprint.name().isEmpty()) {
        violations.append({FootprintViolation::Error, "Footprint name is empty.", -1});
    }
    if (footprint.description().isEmpty()) {
        violations.append({FootprintViolation::Info, "Footprint has no description.", -1});
    }
}

void FootprintEngine::checkPads(const Model::FootprintDefinition& footprint, QList<FootprintViolation>& violations) {
    const auto& prims = footprint.primitives();
    QMap<QString, int> padNumbers;
    QList<QPair<QPointF, int>> padPositions;

    for (int i = 0; i < prims.size(); ++i) {
        const auto& prim = prims[i];
        if (prim.type == Model::FootprintPrimitive::Pad) {
            QString num = prim.data.value("number").toString();
            QPointF pos(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());

            if (num.isEmpty()) {
                violations.append({FootprintViolation::Warning, "Pad has no number.", i, pos});
            } else {
                if (padNumbers.contains(num)) {
                    violations.append({FootprintViolation::Warning, QString("Duplicate Pad Number '%1' detected.").arg(num), i, pos});
                }
                padNumbers[num]++;
            }

            for (const auto& pair : padPositions) {
                if (QLineF(pos, pair.first).length() < 0.1) {
                    violations.append({FootprintViolation::Error, QString("Pad %1 overlaps another pad.").arg(num), i, pos});
                }
            }
            padPositions.append({pos, i});
        }
    }

    if (padNumbers.isEmpty()) {
        violations.append({FootprintViolation::Warning, "Footprint has no pads.", -1});
    }
}

void FootprintEngine::checkPhysicalRules(const Model::FootprintDefinition& footprint, QList<FootprintViolation>& violations) {
    QRectF bounds = footprint.boundingRect();
    if (bounds.width() > 200 || bounds.height() > 200) {
        violations.append({FootprintViolation::Warning, "Footprint is unusually large (>200mm).", -1});
    }
}

} // namespace Analysis
} // namespace Flux
