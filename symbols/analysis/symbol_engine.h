#ifndef SYMBOL_ENGINE_H
#define SYMBOL_ENGINE_H

#include <QString>
#include <QList>
#include <QPointF>
#include "../models/symbol_definition.h"

namespace Flux {
namespace Analysis {

/**
 * @brief Represents a single violation found by the Symbol Rule Checker (SRC).
 */
struct SymbolViolation {
    enum Severity {
        Info,
        Warning,
        Error,
        Critical
    };

    Severity severity;
    QString message;
    int primitiveIndex; // -1 for symbol-level issues
    QPointF location;   // Relevant coordinate if applicable

    QString severityString() const {
        switch (severity) {
            case Info: return "Info";
            case Warning: return "Warning";
            case Error: return "Error";
            case Critical: return "Critical";
            default: return "Unknown";
        }
    }
};

/**
 * @brief Static engine for performing Symbol Rule Checks (SRC).
 * This class operates exclusively on models and is fully testable 
 * without a GUI context.
 */
class SymbolEngine {
public:
    /**
     * @brief Performs a full suite of rule checks on a symbol definition.
     * @param symbol The symbol definition to validate.
     * @return A list of violations found.
     */
    static QList<SymbolViolation> checkSymbol(const Model::SymbolDefinition& symbol);

private:
    // Individual rule implementations
    static void checkMetadata(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations);
    static void checkConnectivity(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations);
    static void checkPhysicalRules(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations);
    static void checkUnitConsistency(const Model::SymbolDefinition& symbol, QList<SymbolViolation>& violations);
};

} // namespace Analysis
} // namespace Flux

#endif // SYMBOL_ENGINE_H
