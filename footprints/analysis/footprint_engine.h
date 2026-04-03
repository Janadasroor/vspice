#ifndef FOOTPRINT_ENGINE_H
#define FOOTPRINT_ENGINE_H

#include <QString>
#include <QList>
#include <QPointF>
#include "../models/footprint_definition.h"

namespace Flux {
namespace Analysis {

/**
 * @brief Represents a single violation found by the Footprint Rule Checker (FRC).
 */
struct FootprintViolation {
    enum Severity {
        Info,
        Warning,
        Error,
        Critical
    };

    Severity severity;
    QString message;
    int primitiveIndex; // -1 for footprint-level issues
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
 * @brief Static engine for performing Footprint Rule Checks (FRC).
 */
class FootprintEngine {
public:
    /**
     * @brief Performs a full suite of rule checks on a footprint definition.
     * @param footprint The footprint definition to validate.
     * @return A list of violations found.
     */
    static QList<FootprintViolation> checkFootprint(const Model::FootprintDefinition& footprint);

private:
    static void checkMetadata(const Model::FootprintDefinition& footprint, QList<FootprintViolation>& violations);
    static void checkPads(const Model::FootprintDefinition& footprint, QList<FootprintViolation>& violations);
    static void checkPhysicalRules(const Model::FootprintDefinition& footprint, QList<FootprintViolation>& violations);
};

} // namespace Analysis
} // namespace Flux

#endif // FOOTPRINT_ENGINE_H
