#ifndef COMPONENT_PROPERTIES_DIALOG_H
#define COMPONENT_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QTabWidget>
#include <QLabel>
#include <QCheckBox>
#include "../items/schematic_item.h"

class ComponentPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    explicit ComponentPropertiesDialog(const QList<SchematicItem*>& items, QWidget* parent = nullptr);
    ~ComponentPropertiesDialog();

    // Results
    QString reference() const;
    QString value() const;
    QString fileName() const;
    bool excludeFromSim() const;

private slots:
    void onEditSpiceMapping();
    void onAccept();

private:
    void setupUI();
    void loadFromItem();

    QList<SchematicItem*> m_items;

    // UI Widgets
    QLineEdit* m_refEdit;
    QLineEdit* m_valEdit;
    QLineEdit* m_nameEdit;
    
    QLineEdit* m_fileEdit;
    
    QCheckBox* m_excludeSimCheck;

    // SPICE
    QPushButton* m_spiceMapperBtn;
    QLabel* m_spiceInfoLabel;
    
    // Appearance
    QComboBox* m_rotationCombo;
    QPushButton* m_mirrorBtn;
    
    QPushButton* m_okBtn;
    QPushButton* m_cancelBtn;
};

#endif // COMPONENT_PROPERTIES_DIALOG_H
