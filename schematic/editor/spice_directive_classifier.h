#ifndef SPICE_DIRECTIVE_CLASSIFIER_H
#define SPICE_DIRECTIVE_CLASSIFIER_H

#include <QString>

enum class SpiceDirectiveEditTarget {
    SimulationSetup,
    MeanDialog,
    GenericDirective
};

struct SpiceDirectiveClassification {
    QString firstCard;
    SpiceDirectiveEditTarget target = SpiceDirectiveEditTarget::GenericDirective;
};

class SpiceDirectiveClassifier {
public:
    static QString firstDirectiveCard(const QString& commandText);
    static SpiceDirectiveClassification classify(const QString& commandText);
};

#endif // SPICE_DIRECTIVE_CLASSIFIER_H
