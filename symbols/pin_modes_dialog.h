#ifndef PIN_MODES_DIALOG_H
#define PIN_MODES_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QJsonArray>
#include <QJsonObject>

class PinModesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PinModesDialog(const QJsonArray& modes, QWidget* parent = nullptr);
    QJsonArray results() const;

private:
    void addModeRow(const QJsonObject& data = QJsonObject());
    QTableWidget* m_table;
};

#endif // PIN_MODES_DIALOG_H
