#ifndef KICAD_FOOTPRINT_IMPORTER_H
#define KICAD_FOOTPRINT_IMPORTER_H

#include <QString>
#include <QStringList>
#include "models/footprint_definition.h"

using Flux::Model::FootprintDefinition;

class KicadFootprintImporter {
public:
    struct ImportReport {
        FootprintDefinition footprint;
        int padCount = 0;
        int lineCount = 0;
        int rectCount = 0;
        int circleCount = 0;
        int arcCount = 0;
        int textCount = 0;
        int unsupportedCount = 0;
        QStringList unsupportedKinds;
    };

    static QStringList getFootprintNames(const QString& filePath);
    static FootprintDefinition importFootprint(const QString& filePath, const QString& footprintName = QString());
    static ImportReport importFootprintDetailed(const QString& filePath, const QString& footprintName = QString());
};

#endif // KICAD_FOOTPRINT_IMPORTER_H
