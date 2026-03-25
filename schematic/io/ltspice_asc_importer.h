#ifndef LTSPICE_ASC_IMPORTER_H
#define LTSPICE_ASC_IMPORTER_H

#include <QString>

class QGraphicsScene;

class LtspiceAscImporter {
public:
    static bool importFile(QGraphicsScene* scene,
                           const QString& filePath,
                           QString& pageSize,
                           QString* errorOut = nullptr);
};

#endif // LTSPICE_ASC_IMPORTER_H
