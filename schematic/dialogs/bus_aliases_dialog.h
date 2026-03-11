#ifndef BUS_ALIASES_DIALOG_H
#define BUS_ALIASES_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QStringList>

class QTableWidget;

class BusAliasesDialog : public QDialog {
    Q_OBJECT
public:
    explicit BusAliasesDialog(const QMap<QString, QList<QString>>& aliases, QWidget* parent = nullptr);

    QMap<QString, QList<QString>> aliases() const { return m_aliases; }

private slots:
    void onAddRow();
    void onRemoveRow();
    void onAccept();

private:
    void populate();

    QTableWidget* m_table = nullptr;
    QMap<QString, QList<QString>> m_aliases;
};

#endif // BUS_ALIASES_DIALOG_H
