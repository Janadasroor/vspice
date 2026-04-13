#ifndef BJT_PROPERTIES_DIALOG_H
#define BJT_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QMap>
#include <QPointer>

class QLineEdit;
class QComboBox;
class QPushButton;
class SchematicItem;

class BjtPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit BjtPropertiesDialog(SchematicItem* item, QWidget* parent = nullptr);

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
    bool isPnpSelected() const;
    bool isPnp() const;

    QPointer<SchematicItem> m_item;

    QLineEdit* m_modelNameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QPushButton* m_pickModelButton = nullptr;
    QLineEdit* m_isEdit = nullptr;
    QLineEdit* m_bfEdit = nullptr;
    QLineEdit* m_vafEdit = nullptr;
    QLineEdit* m_cjeEdit = nullptr;
    QLineEdit* m_cjcEdit = nullptr;
    QLineEdit* m_tfEdit = nullptr;
    QLineEdit* m_trEdit = nullptr;
    QLineEdit* m_footprintEdit = nullptr;
    QLineEdit* m_commandPreview = nullptr;
};

#endif // BJT_PROPERTIES_DIALOG_H
