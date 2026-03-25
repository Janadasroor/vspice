#ifndef PASSIVE_MODEL_PROPERTIES_DIALOG_H
#define PASSIVE_MODEL_PROPERTIES_DIALOG_H

#include <QDialog>

class QLineEdit;
class QCheckBox;
class SchematicItem;

class PassiveModelPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    enum class Kind {
        Resistor,
        Capacitor
    };

    explicit PassiveModelPropertiesDialog(SchematicItem* item, Kind kind, QWidget* parent = nullptr);

    QString reference() const;
    QString valueText() const;
    QString spiceModel() const;
    bool excludeFromSimulation() const;
    bool excludeFromPcb() const;

private slots:
    void pickModel();

private:
    SchematicItem* m_item;
    Kind m_kind;
    QLineEdit* m_referenceEdit = nullptr;
    QLineEdit* m_valueEdit = nullptr;
    QLineEdit* m_spiceModelEdit = nullptr;
    QCheckBox* m_excludeSimCheck = nullptr;
    QCheckBox* m_excludePcbCheck = nullptr;
};

#endif // PASSIVE_MODEL_PROPERTIES_DIALOG_H
