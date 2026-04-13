#ifndef COMPONENT_LABEL_PROPERTIES_DIALOG_H
#define COMPONENT_LABEL_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QPointer>

class QLineEdit;
class GenericComponentItem;

class ComponentLabelPropertiesDialog : public QDialog {
    Q_OBJECT

public:
    enum LabelType { Reference, Value };
    explicit ComponentLabelPropertiesDialog(GenericComponentItem* component, LabelType labelType, QWidget* parent = nullptr);

private Q_SLOTS:
    void applyChanges();
    void updateCommandPreview();

private:
    void setupUI();
    void loadValues();
    QString detectModel(const QString& text) const;

    QPointer<GenericComponentItem> m_component;
    LabelType m_labelType;

    QLineEdit* m_textEdit = nullptr;
    QLineEdit* m_commandPreview = nullptr;
};

#endif // COMPONENT_LABEL_PROPERTIES_DIALOG_H
