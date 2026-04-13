#ifndef MOS_PROPERTIES_DIALOG_H
#define MOS_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QPointer>

class QLineEdit;
class QComboBox;
class QPushButton;
class SchematicItem;

class MosPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit MosPropertiesDialog(SchematicItem* item, QWidget* parent = nullptr);

    QString modelName() const;
    QString footprint() const;
    QMap<QString, QString> paramExpressions() const;
    QString newSymbolName() const;

private Q_SLOTS:
    void updateCommandPreview();
    void applyChanges();
    void pickFootprint();
    void autoMatchModel();

private:
    void setupUI();
    void loadValues();
    void fillFromModel(const QString& modelName);
    bool isPmosSelected() const;
    bool isPmos() const;

    QPointer<SchematicItem> m_item;

    QLineEdit* m_modelNameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QPushButton* m_pickModelButton = nullptr;
    QLineEdit* m_vtoEdit = nullptr;
    QLineEdit* m_kpEdit = nullptr;
    QLineEdit* m_lambdaEdit = nullptr;
    QLineEdit* m_rdEdit = nullptr;
    QLineEdit* m_rsEdit = nullptr;
    QLineEdit* m_cgsoEdit = nullptr;
    QLineEdit* m_cgdoEdit = nullptr;
    QLineEdit* m_footprintEdit = nullptr;
    QLineEdit* m_commandPreview = nullptr;
};

#endif // MOS_PROPERTIES_DIALOG_H
