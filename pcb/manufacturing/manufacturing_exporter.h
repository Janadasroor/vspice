#ifndef MANUFACTURING_EXPORTER_H
#define MANUFACTURING_EXPORTER_H

#include <QString>

class QGraphicsScene;

class ManufacturingExporter {
public:
    static bool exportIPC2581(QGraphicsScene* scene, const QString& filePath, QString* error = nullptr);
    static bool exportODBppPackage(QGraphicsScene* scene, const QString& outputDirectory, QString* error = nullptr);
};

#endif // MANUFACTURING_EXPORTER_H
