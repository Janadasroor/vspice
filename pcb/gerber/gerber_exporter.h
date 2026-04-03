#ifndef GERBER_EXPORTER_H
#define GERBER_EXPORTER_H

#include <QString>
#include <QList>
#include <QGraphicsScene>
#include <QMap>

struct GerberExportSettings {
    QString outputDirectory;
    bool useApertureMacros = true;
    int decimalPlaces = 4;
    bool plotBoardEdge = true;
};

/**
 * @brief Logic for generating RS-274X Gerber files from a PCB scene
 */
class GerberExporter {
public:
    static bool exportLayer(QGraphicsScene* scene, int layerId, const QString& filePath, const GerberExportSettings& settings);
    static bool generateDrillFile(QGraphicsScene* scene, const QString& filePath);

private:
    static QString formatCoord(double val, int decimals);
};

#endif // GERBER_EXPORTER_H
