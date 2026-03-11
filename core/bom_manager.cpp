#include "bom_manager.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <algorithm>

QList<BOMEntry> BOMManager::generateFromECO(const ECOPackage& pkg) {
    QList<BOMEntry> bom;

    for (const auto& comp : pkg.components) {
        bool found = false;
        for (auto& entry : bom) {
            if (entry.value == comp.value && 
                entry.footprint == comp.footprint &&
                entry.manufacturer == comp.manufacturer &&
                entry.mpn == comp.mpn) {
                entry.references.append(comp.reference);
                entry.quantity++;
                found = true;
                break;
            }
        }

        if (!found) {
            BOMEntry newEntry;
            newEntry.value = comp.value;
            newEntry.footprint = comp.footprint;
            newEntry.manufacturer = comp.manufacturer;
            newEntry.mpn = comp.mpn;
            newEntry.references << comp.reference;
            newEntry.quantity = 1;
            bom.append(newEntry);
        }
    }

    // Sort by reference (e.g. C before R)
    std::sort(bom.begin(), bom.end(), [](const BOMEntry& a, const BOMEntry& b) {
        if (a.references.isEmpty() || b.references.isEmpty()) return false;
        return a.references.first() < b.references.first();
    });

    return bom;
}

bool BOMManager::exportCSV(const QString& filePath, const QList<BOMEntry>& bom) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out << "Item,Quantity,Reference,Value,Footprint,Manufacturer,MPN,Description\n";

    int item = 1;
    for (const auto& entry : bom) {
        QString refs = entry.references.join(", ");
        out << QString("%1,%2,\"%3\",\"%4\",\"%5\",\"%6\",\"%7\",\"%8\"\n")
               .arg(item++)
               .arg(entry.quantity)
               .arg(refs)
               .arg(entry.value)
               .arg(entry.footprint)
               .arg(entry.manufacturer)
               .arg(entry.mpn)
               .arg(entry.description);
    }

    return true;
}

bool BOMManager::exportHTML(const QString& filePath, const QList<BOMEntry>& bom) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out << "<html><head><title>BOM Report</title>";
    out << "<style>body{font-family:sans-serif;padding:20px;} table{width:100%; border-collapse:collapse;} th,td{border:1px solid #ccc; padding:8px; text-align:left;} th{background:#eee;}</style>";
    out << "</head><body>";
    out << "<h1>Bill of Materials</h1>";
    out << "<table><tr><th>Item</th><th>Qty</th><th>References</th><th>Value</th><th>Footprint</th></tr>";

    int item = 1;
    for (const auto& entry : bom) {
        out << QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
               .arg(item++)
               .arg(entry.quantity)
               .arg(entry.references.join(", "))
               .arg(entry.value)
               .arg(entry.footprint);
    }

    out << "</table></body></html>";
    return true;
}
