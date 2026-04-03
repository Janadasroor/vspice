#ifndef PCB_SYNC_DIALOG_H
#define PCB_SYNC_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include "../../core/eco_types.h"

/**
 * @brief Dialog to review and confirm schematic to PCB synchronization
 */
class PCBSyncDialog : public QDialog {
    Q_OBJECT

public:
    explicit PCBSyncDialog(const ECOPackage& pkg, QWidget* parent = nullptr);
    ~PCBSyncDialog();

    ECOPackage package() const { return m_package; }

private:
    void setupUI();
    void populateTable();
    bool validatePackage();

    ECOPackage m_package;
    QTableWidget* m_table;
    QLabel* m_summaryLabel;
    QPushButton* m_applyBtn;
    QPushButton* m_cancelBtn;
};

#endif // PCB_SYNC_DIALOG_H
