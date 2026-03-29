#ifndef TUNING_SLIDER_PROPERTIES_DIALOG_H
#define TUNING_SLIDER_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>

class TuningSliderSymbolItem;

class TuningSliderPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TuningSliderPropertiesDialog(TuningSliderSymbolItem* item, QWidget* parent = nullptr);

    QString reference() const { return m_refEdit->text(); }
    double minValue() const { return m_minSpin->value(); }
    double maxValue() const { return m_maxSpin->value(); }
    double currentValue() const { return m_currentSpin->value(); }

private:
    QLineEdit* m_refEdit;
    QDoubleSpinBox* m_minSpin;
    QDoubleSpinBox* m_maxSpin;
    QDoubleSpinBox* m_currentSpin;
};

#endif // TUNING_SLIDER_PROPERTIES_DIALOG_H
