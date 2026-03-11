#include "assignment_validator.h"
#include <QSet>

namespace Flux {
namespace Core {

using namespace Flux::Model;

QString AssignmentValidator::identifySymbolRole(const Model::SymbolDefinition& symbol) {
    const QString name = symbol.name().toLower();
    const QString cat = symbol.category().toLower();
    const QString prefix = symbol.referencePrefix().toUpper();
    const int pins = countPins(symbol);

    if (cat.contains("integrated circuits") || cat.contains("ic") || prefix.startsWith("U")) {
        return "IC";
    }

    // Fallback heuristic: many multi-pin non-power symbols are most often ICs.
    if (!symbol.isPowerSymbol() && pins >= 8) {
        return "IC";
    }

    if (cat.contains("passives") || name.contains("resistor") || name.contains("capacitor") || 
        name.contains("inductor") || prefix == "R" || prefix == "C" || prefix == "L") {
        return "Passive";
    }

    if (cat.contains("semiconductors") || name.contains("diode") || name.contains("transistor") || 
        prefix == "D" || prefix == "Q") {
        return "Semiconductor";
    }

    if (symbol.isPowerSymbol() || prefix == "#PWR" || name.contains("gnd") || name.contains("vcc")) {
        return "Power";
    }

    return "Generic";
}

int AssignmentValidator::countPins(const Model::SymbolDefinition& symbol) {
    int count = 0;
    for (const auto& prim : symbol.primitives()) {
        if (prim.type == SymbolPrimitive::Pin) {
            count++;
        }
    }
    return count;
}

} // namespace Core
} // namespace Flux
