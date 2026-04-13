#ifndef JFET_PROPERTIES_DIALOG_H
#define JFET_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QPointer>

class QLineEdit;
class SchematicItem;

class JfetPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit JfetPropertiesDialog(SchematicItem* item, QWidget* parent = nullptr);

    QString modelName() const;
    QMap<QString, QString> paramExpressions() const;

private Q_SLOTS:
    void updateCommandPreview();
    void applyChanges();
    void autoMatchModel();

private:
    void setupUI();
    void loadValues();
    void fillFromModel(const QString& modelName);
    bool isPChannel() const;

    QPointer<SchematicItem> m_item;

    QLineEdit* m_modelNameEdit = nullptr;
    QLineEdit* m_betaEdit = nullptr;
    QLineEdit* m_vtoEdit = nullptr;
    QLineEdit* m_lambdaEdit = nullptr;
    QLineEdit* m_rdEdit = nullptr;
    QLineEdit* m_rsEdit = nullptr;
    QLineEdit* m_cgsEdit = nullptr;
    QLineEdit* m_cgdEdit = nullptr;
    QLineEdit* m_isEdit = nullptr;
    QLineEdit* m_commandPreview = nullptr;
};

#endif // JFET_PROPERTIES_DIALOG_H
