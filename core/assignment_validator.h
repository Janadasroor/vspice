#ifndef ASSIGNMENT_VALIDATOR_H
#define ASSIGNMENT_VALIDATOR_H

#include <QString>
#include <QList>
#include "../symbols/models/symbol_definition.h"

namespace Flux {
namespace Core {

/**
 * @brief Global roles and validation logic for Symbol properties.
 */
class AssignmentValidator {
public:
    struct ValidationResult {
        bool valid;
        QString message;
        enum Severity { Info, Warning, Error } severity;
    };

    /**
     * @brief Get the "Role" of a symbol based on its properties (e.g. "IC", "Passive").
     */
    static QString identifySymbolRole(const Model::SymbolDefinition& symbol);

private:
    static int countPins(const Model::SymbolDefinition& symbol);
};

} // namespace Core
} // namespace Flux

#endif // ASSIGNMENT_VALIDATOR_H
