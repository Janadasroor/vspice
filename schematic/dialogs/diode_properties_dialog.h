#ifndef DIODE_PROPERTIES_DIALOG_H
#define DIODE_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QPointer>
#include <QMap>

class QLineEdit;
class QLabel;
class SchematicItem;

class DiodePropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit DiodePropertiesDialog(SchematicItem* item, QWidget* parent = nullptr);

    QString modelName() const;
    QMap<QString, QString> paramExpressions() const;
    QString newSymbolName() const { return m_newSymbolName; }

private Q_SLOTS:
    void updateCommandPreview();
    void applyChanges();
    void autoMatchModel();

private:
    void setupUI();
    void loadValues();
    void fillFromModel(const QString& modelName);
    QString detectDiodeType() const;

    QPointer<SchematicItem> m_item;
    QString m_diodeType;
    QString m_newSymbolName; // set when symbol needs switching

    QLineEdit* m_modelNameEdit = nullptr;

    // Forward params
    QLineEdit* m_isEdit = nullptr;
    QLineEdit* m_nEdit = nullptr;
    QLineEdit* m_rsEdit = nullptr;

    // Capacitance params
    QLineEdit* m_vjEdit = nullptr;
    QLineEdit* m_cjoEdit = nullptr;
    QLineEdit* m_mEdit = nullptr;

    // Advanced params
    QLineEdit* m_ttEdit = nullptr;
    QLineEdit* m_bvEdit = nullptr;
    QLineEdit* m_ibvEdit = nullptr;

    // Preview
    QLineEdit* m_commandPreview = nullptr;
};

#endif // DIODE_PROPERTIES_DIALOG_H
