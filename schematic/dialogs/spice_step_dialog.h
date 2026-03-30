#ifndef SPICE_STEP_DIALOG_H
#define SPICE_STEP_DIALOG_H

#include <QDialog>
#include <QRegularExpression>
#include <QString>

class QComboBox;
class QLineEdit;
class QLabel;
class QStackedWidget;

class SpiceStepDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpiceStepDialog(const QString& initialCommand, QWidget* parent = nullptr);

    QString commandText() const;

private slots:
    void updateUiState();
    void updatePreview();
    void applyCommandText();

private:
    enum class TargetKind {
        Parameter,
        Temperature,
        Custom
    };

    enum class SweepMode {
        LinearRange,
        List,
        Decade,
        Octave,
        File
    };

    static QString quotedFilePath(const QString& path);

    QString targetPrefix() const;
    TargetKind currentTargetKind() const;
    SweepMode currentSweepMode() const;

    QComboBox* m_targetKindCombo = nullptr;
    QLabel* m_targetLabel = nullptr;
    QLineEdit* m_targetEdit = nullptr;
    QComboBox* m_sweepModeCombo = nullptr;
    QStackedWidget* m_modeStack = nullptr;

    QLineEdit* m_linearStartEdit = nullptr;
    QLineEdit* m_linearStopEdit = nullptr;
    QLineEdit* m_linearStepEdit = nullptr;

    QLineEdit* m_listValuesEdit = nullptr;

    QLineEdit* m_logPointsEdit = nullptr;
    QLineEdit* m_logStartEdit = nullptr;
    QLineEdit* m_logStopEdit = nullptr;

    QLineEdit* m_octPointsEdit = nullptr;
    QLineEdit* m_octStartEdit = nullptr;
    QLineEdit* m_octStopEdit = nullptr;

    QLineEdit* m_filePathEdit = nullptr;

    QLineEdit* m_commandEdit = nullptr;
    bool m_syncingCommand = false;
};

#endif // SPICE_STEP_DIALOG_H
