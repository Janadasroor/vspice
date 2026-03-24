#ifndef MESFET_PROPERTIES_DIALOG_H
#define MESFET_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QPointer>

class QLineEdit;
class SchematicItem;

class MesfetPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit MesfetPropertiesDialog(SchematicItem* item, QWidget* parent = nullptr);

    QString modelName() const;

private slots:
    void updatePreview();
    void applyChanges();

private:
    QPointer<SchematicItem> m_item;
    QLineEdit* m_modelNameEdit = nullptr;
    QLineEdit* m_previewEdit = nullptr;
};

#endif // MESFET_PROPERTIES_DIALOG_H
