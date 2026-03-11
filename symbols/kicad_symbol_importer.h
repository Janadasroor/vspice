#ifndef KICAD_SYMBOL_IMPORTER_H
#define KICAD_SYMBOL_IMPORTER_H

#include <QString>
#include <QList>
#include "models/symbol_definition.h"

using Flux::Model::SymbolDefinition;

/**
 * @brief Utility to import and convert KiCad symbols (.kicad_sym) to Viora EDA format
 */
class KicadSymbolImporter {
public:
    struct ImportResult {
        SymbolDefinition symbol;
        QString detectedFootprint;
        QString footprintSource;
    };

    /**
     * @brief Parses a KiCad symbol library file and returns names of all symbols found.
     */
    static QStringList getSymbolNames(const QString& filePath);

    /**
     * @brief Imports a specific symbol from a KiCad library file.
     */
    static SymbolDefinition importSymbol(const QString& filePath, const QString& symbolName);
    static ImportResult importSymbolDetailed(const QString& filePath, const QString& symbolName);

private:
    static QString extractSExpr(const QString& content, const QString& key, int& from);
    static QList<QString> splitSExpr(const QString& s);
};

#endif // KICAD_SYMBOL_IMPORTER_H
