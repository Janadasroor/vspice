#ifndef LED_PROPERTIES_DIALOG_H
#define LED_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGraphicsScene;
class SchematicItem;

class LedPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    LedPropertiesDialog(SchematicItem* item, QGraphicsScene* scene, QWidget* parent = nullptr);

private:
    void applyChanges();
    QString detectColor(const QString& value) const;

    QPointer<SchematicItem> m_item;
    QPointer<QGraphicsScene> m_scene;

    QComboBox* m_colorCombo = nullptr;
    QCheckBox* m_blinkCheck = nullptr;
    QDoubleSpinBox* m_blinkHz = nullptr;
    QDoubleSpinBox* m_threshold = nullptr;
};

#endif // LED_PROPERTIES_DIALOG_H
