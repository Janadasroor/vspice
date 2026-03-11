#ifndef BOM_DIALOG_H
#define BOM_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include "../core/bom_manager.h"

class BOMDialog : public QDialog {
    Q_OBJECT

public:
    explicit BOMDialog(const ECOPackage& pkg, QWidget* parent = nullptr);
    ~BOMDialog();

private slots:
    void onExportCSV();
    void onExportHTML();

private:
    void setupUI();
    void populateTable();

    QList<BOMEntry> m_bom;
    QTableWidget* m_table;
};

#endif // BOM_DIALOG_H
