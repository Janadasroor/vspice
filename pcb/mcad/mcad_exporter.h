#ifndef MCAD_EXPORTER_H
#define MCAD_EXPORTER_H

#include <QString>

class QGraphicsScene;

class MCADExporter {
public:
    static bool exportSTEPWireframe(QGraphicsScene* scene, const QString& filePath, QString* error = nullptr);
    static bool exportIGESWireframe(QGraphicsScene* scene, const QString& filePath, QString* error = nullptr);
    static bool exportVRMLAssembly(QGraphicsScene* scene, const QString& filePath, QString* error = nullptr);
};

#endif // MCAD_EXPORTER_H
