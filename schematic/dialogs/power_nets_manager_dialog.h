#ifndef POWER_NETS_MANAGER_DIALOG_H
#define POWER_NETS_MANAGER_DIALOG_H

#include <QDialog>
#include <QStringList>
#include <QVector>

class QTableWidget;
class QPushButton;

struct PowerNetUsageRow {
    QString netName;
    int symbolCount = 0;
    QStringList files;
};

class PowerNetsManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PowerNetsManagerDialog(QWidget* parent = nullptr);
    void setRows(const QVector<PowerNetUsageRow>& rows);

signals:
    void renameRequested(const QString& oldName, const QString& newName);
    void refreshRequested();

private slots:
    void onRenameClicked();
    void onRefreshClicked();

private:
    QString selectedNetName() const;

    QTableWidget* m_table = nullptr;
    QPushButton* m_renameButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
};

#endif // POWER_NETS_MANAGER_DIALOG_H
