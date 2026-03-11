#ifndef BOM_MANAGER_H
#define BOM_MANAGER_H

#include <QString>
#include <QList>
#include <QMap>
#include <QVector>
#include "../core/eco_types.h"

struct BOMEntry {
    QString value;
    QString footprint;
    QString manufacturer;
    QString mpn;
    QString description;
    QStringList references;
    int quantity;

    bool operator==(const BOMEntry& other) const {
        return value == other.value && 
               footprint == other.footprint && 
               mpn == other.mpn;
    }
};

/**
 * @brief Logic for generating and exporting Bill of Materials
 */
class BOMManager {
public:
    static QList<BOMEntry> generateFromECO(const ECOPackage& pkg);
    
    static bool exportCSV(const QString& filePath, const QList<BOMEntry>& bom);
    static bool exportHTML(const QString& filePath, const QList<BOMEntry>& bom);
};

#endif // BOM_MANAGER_H
