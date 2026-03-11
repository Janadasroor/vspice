#ifndef PIN_TABLE_DIALOG_H
#define PIN_TABLE_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QMap>
#include <QVariant>
#include "models/symbol_definition.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

/**
 * @brief Dialog for batch editing pins in a table view
 */
class PinTableDialog : public QDialog {
    Q_OBJECT
public:
    explicit PinTableDialog(const QList<SymbolPrimitive>& pins, QWidget* parent = nullptr);

    QList<QMap<QString, QVariant>> results() const;

private slots:
    void validate();
    void onImportCSV();
    void onExportCSV();

private:
    QTableWidget* m_table;
    QList<SymbolPrimitive> m_pins;
};

#endif // PIN_TABLE_DIALOG_H
