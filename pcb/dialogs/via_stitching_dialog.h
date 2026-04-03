#ifndef VIA_STITCHING_DIALOG_H
#define VIA_STITCHING_DIALOG_H

#include <QDialog>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>

class ViaStitchingDialog : public QDialog {
    Q_OBJECT

public:
    explicit ViaStitchingDialog(QWidget* parent = nullptr);

    double gridSpacingX() const { return m_spacingX->value(); }
    double gridSpacingY() const { return m_spacingY->value(); }
    double viaDiameter() const { return m_viaDiam->value(); }
    double viaDrill() const { return m_viaDrill->value(); }
    QString netName() const { return m_netEdit->text(); }
    int startLayer() const { return m_startLayerCombo->currentData().toInt(); }
    int endLayer() const { return m_endLayerCombo->currentData().toInt(); }
    bool microviaMode() const { return m_microviaCheck->isChecked(); }

private:
    void setupUI();

    QDoubleSpinBox *m_spacingX, *m_spacingY;
    QDoubleSpinBox *m_viaDiam, *m_viaDrill;
    QLineEdit *m_netEdit;
    QComboBox *m_startLayerCombo, *m_endLayerCombo;
    QCheckBox *m_microviaCheck;
};

#endif // VIA_STITCHING_DIALOG_H
