#ifndef VOLTAGE_SOURCE_CUSTOM_WAVEFORM_DIALOG_H
#define VOLTAGE_SOURCE_CUSTOM_WAVEFORM_DIALOG_H

#include <QDialog>
#include <QVector>
#include <QPointF>

class QCheckBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class WaveformDrawWidget;

class VoltageSourceCustomWaveformDialog : public QDialog {
    Q_OBJECT

public:
    explicit VoltageSourceCustomWaveformDialog(QWidget* parent = nullptr);
    void setDefaultSavePath(const QString& dirPath, const QString& baseName);

    QString pwlPoints() const { return m_pwlPoints; }
    bool repeatEnabled() const { return m_repeatEnabled; }
    bool saveToFileEnabled() const { return m_saveToFileEnabled; }
    QString pwlFilePath() const { return m_pwlFilePath; }

private Q_SLOTS:
    void onClear();
    void onAccepted();
    void onApplySine();
    void onApplySquare();
    void onApplyTriangle();
    void onApplySawtooth();
    void onApplyPulse();
    void onApplyMirrorV();
    void onApplySmooth();
    void onApplyNoise();
    void onApplyInvert();
    void onApplyFormula();
    void onApplyReverse();
    void onApplyScaleTime();
    void onApplyShiftTime();

private:
    void setupUi();
    QString buildPwlPoints() const;

    WaveformDrawWidget* m_drawWidget;
    QLineEdit* m_formulaEdit;
    QLineEdit* m_periodEdit;
    QLineEdit* m_amplitudeEdit;
    QLineEdit* m_offsetEdit;
    QSpinBox* m_samplesSpin;
    QCheckBox* m_repeatCheck;
    QCheckBox* m_saveToFileCheck;
    QLineEdit* m_filePathEdit;
    QPushButton* m_browseBtn;
    QPushButton* m_clearBtn;

    QString m_pwlPoints;
    bool m_repeatEnabled;
    bool m_saveToFileEnabled;
    QString m_pwlFilePath;
    QString m_defaultDir;
    QString m_defaultBaseName;
};

#endif // VOLTAGE_SOURCE_CUSTOM_WAVEFORM_DIALOG_H
