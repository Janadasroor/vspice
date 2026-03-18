#ifndef SWITCH_PROPERTIES_DIALOG_H
#define SWITCH_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QLineEdit;
class SwitchItem;

class SwitchPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    SwitchPropertiesDialog(SwitchItem* item, QWidget* parent = nullptr);

private:
    void applyChanges();
    void updateEnabledState();

    QPointer<SwitchItem> m_item;

    QCheckBox* m_useModel = nullptr;
    QLineEdit* m_modelName = nullptr;
    QLineEdit* m_ron = nullptr;
    QLineEdit* m_roff = nullptr;
    QLineEdit* m_vt = nullptr;
    QLineEdit* m_vh = nullptr;
    QComboBox* m_state = nullptr;
};

#endif // SWITCH_PROPERTIES_DIALOG_H
