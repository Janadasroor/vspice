#ifndef LTSPICE_SYMBOL_IMPORTER_H
#define LTSPICE_SYMBOL_IMPORTER_H

#include <QString>
#include <QList>
#include "models/symbol_definition.h"

using Flux::Model::SymbolDefinition;

/**
 * @brief Utility to import and convert LTspice symbols (.asy) to Viora EDA format
 */
class LtspiceSymbolImporter {
public:
    struct ImportResult {
        SymbolDefinition symbol;
        bool success = false;
        QString errorMessage;
    };

    /**
     * @brief Imports an LTspice symbol from an .asy file.
     */
    static SymbolDefinition importSymbol(const QString& filePath);
    static ImportResult importSymbolDetailed(const QString& filePath);

private:
    static QPointF parsePoint(const QString& x, const QString& y);
    static QPointF parsePoint(const QString& x, const QString& y, bool applyScale);
    static qreal scale(qreal val);
    static QPointF scale(QPointF p);
};

#endif // LTSPICE_SYMBOL_IMPORTER_H
