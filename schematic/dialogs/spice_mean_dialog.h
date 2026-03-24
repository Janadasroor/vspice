#ifndef SPICE_MEAN_DIALOG_H
#define SPICE_MEAN_DIALOG_H

#include <QDialog>
#include <QRegularExpression>
#include <QString>

class QComboBox;
class QLineEdit;

class SpiceMeanDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpiceMeanDialog(const QString& initialCommand, QWidget* parent = nullptr);

    QString commandText() const;

private slots:
    void updatePreview();
    void applyCommandText();

private:
    static QRegularExpression meanRegex();

    QComboBox* m_modeCombo = nullptr;
    QLineEdit* m_signalEdit = nullptr;
    QLineEdit* m_fromEdit = nullptr;
    QLineEdit* m_toEdit = nullptr;
    QLineEdit* m_commandEdit = nullptr;
    bool m_syncingCommand = false;
};

#endif // SPICE_MEAN_DIALOG_H
