#ifndef SMART_PROPERTIES_DIALOG_H
#define SMART_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include <functional>
#include <QUndoStack>
#include <QGraphicsScene>
#include "../items/schematic_item.h"

struct PropertyField {
    enum Type {
        Text,
        Integer,
        Double,
        Boolean,
        Choice,
        EngineeringValue // Special for 10k, 4.7u etc
    };

    QString name;
    QString label;
    Type type;
    QVariant defaultValue;
    QStringList choices; // For Choice type
    QString unit;
    QString tooltip;
    std::function<QString(const QVariant&)> validator; // Returns error message if invalid
};

struct PropertyTab {
    QString title;
    QList<PropertyField> fields;
};

class SmartPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    SmartPropertiesDialog(const QList<SchematicItem*>& items, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent = nullptr);
    virtual ~SmartPropertiesDialog() = default;

    void addTab(const PropertyTab& tab);
    
    QVariant getPropertyValue(const QString& name) const;
    void setPropertyValue(const QString& name, const QVariant& value);

    void setTabVisible(int index, bool visible);

public slots:
    void accept() override;
    void reject() override;
    virtual void onApply();
    virtual void applyPreview();
    virtual void onFieldChanged();

protected:
    void setupUi();
    void createFieldWidget(const PropertyField& field, QFormLayout* layout);
    bool validateAll();
    void revertToOriginal();

    QList<SchematicItem*> m_items;
    QMap<QUuid, QJsonObject> m_originalStates;
    QUndoStack* m_undoStack;
    QGraphicsScene* m_scene;
    QTabWidget* m_tabWidget;
    QMap<QString, QWidget*> m_widgets;
    QMap<QString, QLabel*> m_errorLabels;
    QList<PropertyTab> m_tabs;
};

#endif // SMART_PROPERTIES_DIALOG_H
