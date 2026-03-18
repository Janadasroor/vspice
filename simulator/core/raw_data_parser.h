#ifndef RAW_DATA_PARSER_H
#define RAW_DATA_PARSER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include "sim_results.h"

struct RawData {
    QStringList varNames;
    int numVariables = 0;
    int numPoints = 0;
    QVector<double> x;
    QVector<QVector<double>> y;

    SimResults toSimResults() const;
};

class RawDataParser {
public:
    static bool loadRawAscii(const QString& path, RawData* out, QString* error = nullptr);
};

#endif // RAW_DATA_PARSER_H
