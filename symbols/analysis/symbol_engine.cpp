#include "symbol_engine.h"
#include <QMap>
#include <QSet>
#include <QLineF>

namespace Flux {
namespace Analysis {

QList<SymbolViolation> SymbolEngine::checkSymbol(const Model::SymbolDefinition& symbol) {
    QList<SymbolViolation> violations;

    // 1. Symbol-level metadata checks
    checkMetadata(symbol, violations);

    // 2. Connectivity & Pins
    checkConnectivity(symbol, violations);

    // 3. Physical & Drawing rules
    checkPhysicalRules(symbol, violations);

    // 4. Multi-unit consistency
    checkUnitConsistency(symbol, violations);

    return violations;
}

void SymbolEngine::checkMetadata(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations) {
    if (symbol.referencePrefix().isEmpty()) {
        violations.append({SymbolViolation::Warning, "Reference prefix is empty (e.g. 'U', 'R').", -1});
    }
    if (symbol.name().isEmpty()) {
        violations.append({SymbolViolation::Error, "Symbol name is empty.", -1});
    }

    if (symbol.primitives().isEmpty() && !symbol.isDerived()) {
        violations.append({SymbolViolation::Critical, "Symbol contains no primitives.", -1});
    }
}

void SymbolEngine::checkConnectivity(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations) {
    const auto& prims = symbol.primitives();
    QMap<int, int> pinNumbers; // Number -> Count
    QList<QPair<QPointF, int>> pinPositions; // pos, index
    QMap<int, QString> unitPinTypes; // pinNumber -> electricalType (for multi-unit consistency)
    
    for (int i = 0; i < prims.size(); ++i) {
        const auto& prim = prims[i];
        if (prim.type == Model::SymbolPrimitive::Pin) {
            int num = prim.data.value("number").toInt();
            QString name = prim.data.value("name").toString();
            QString type = prim.data.value("electricalType").toString("Passive");
            QPointF pos(prim.data.value("x").toDouble(), prim.data.value("y").toDouble());

            // Duplicate Pin Number (within same unit or shared)
            // Note: Different units *can* have the same pin number if they represent 
            // the same physical pin but in different parts (e.g. op-amp).
            // However, pins within the SAME unit must be unique.
            if (type != "Power") {
                // Heuristic: Flag duplicate numbers if they appear multiple times in the SAME unit
                // or if they overlap in different units without being part of a multi-unit setup.
                // For now, let's stick to the previous simple logic but refine with unit context.
                if (pinNumbers.contains(num) && symbol.unitCount() == 1) {
                    violations.append({SymbolViolation::Error, QString("Duplicate Pin Number '%1' detected.").arg(num), i, pos});
                }
            }
            pinNumbers[num]++;

            // Missing Name
            if (name.trimmed().isEmpty()) {
                violations.append({SymbolViolation::Warning, QString("Pin %1 has no name.").arg(num), i, pos});
            }

            // Overlapping Pins
            for (const auto& pair : pinPositions) {
                if (QLineF(pos, pair.first).length() < 0.1) {
                    violations.append({SymbolViolation::Error, 
                        QString("Pin %1 overlaps another pin at (%2, %3)")
                        .arg(QString::number(num))
                        .arg(QString::number(pos.x()))
                        .arg(QString::number(pos.y())), i, pos});
                }
            }
            pinPositions.append({pos, i});
        }
    }
}

void SymbolEngine::checkPhysicalRules(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations) {
    // Check for weird coordinate ranges or massive symbols
    QRectF bounds = symbol.boundingRect();
    if (bounds.width() > 1000 || bounds.height() > 1000) {
        violations.append({SymbolViolation::Warning, "Symbol dimensions are extremely large (>1000 units). Check scaling.", -1});
    }

    // Check for pins being too close but not overlapping (future enhancement)
}

void SymbolEngine::checkUnitConsistency(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations) {
    if (symbol.unitCount() <= 1) return;

    const auto& prims = symbol.primitives();
    QMap<int, QString> unitPinTypes; // pinNumber -> electricalType

    for (int i = 0; i < prims.size(); ++i) {
        const auto& prim = prims[i];
        if (prim.type == Model::SymbolPrimitive::Pin) {
            int num = prim.data.value("number").toInt();
            QString type = prim.data.value("electricalType").toString("Passive");

            if (unitPinTypes.contains(num) && unitPinTypes[num] != type) {
                violations.append({SymbolViolation::Error, 
                    QString("Pin %1 has inconsistent electrical types across units (%2 vs %3)")
                    .arg(QString::number(num), type, unitPinTypes[num]), i});
            }
            unitPinTypes[num] = type;
        }
    }
}

} // namespace Analysis
} // namespace Flux
