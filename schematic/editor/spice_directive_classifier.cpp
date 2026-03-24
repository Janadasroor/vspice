#include "spice_directive_classifier.h"

#include <QRegularExpression>
#include <QSet>

namespace {

const QSet<QString> kSimulationCards = {
    ".tran",
    ".ac",
    ".op",
    ".dc"
};

} // namespace

QString SpiceDirectiveClassifier::firstDirectiveCard(const QString& commandText) {
    const QStringList lines = commandText.split('\n');
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        const QString token = line.section(QRegularExpression("\\s+"), 0, 0).trimmed();
        return token.toLower();
    }
    return QString();
}

SpiceDirectiveClassification SpiceDirectiveClassifier::classify(const QString& commandText) {
    SpiceDirectiveClassification out;
    out.firstCard = firstDirectiveCard(commandText);

    if (out.firstCard.isEmpty() || kSimulationCards.contains(out.firstCard)) {
        out.target = SpiceDirectiveEditTarget::SimulationSetup;
        return out;
    }

    if (out.firstCard == ".mean") {
        out.target = SpiceDirectiveEditTarget::MeanDialog;
        return out;
    }

    out.target = SpiceDirectiveEditTarget::GenericDirective;
    return out;
}
