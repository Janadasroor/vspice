#ifndef SPICE_STEP_DIALOG_H
#define SPICE_STEP_DIALOG_H

#include <QDialog>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QLabel;
class QStackedWidget;
class QWidget;
class QComboBox;

class SpiceStepDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpiceStepDialog(const QString& initialCommand, QWidget* parent = nullptr);

    QString commandText() const;

private slots:
    void updateUiState();
    void updatePreview();
    void applyCommandText();
    void browseStepFile();
    void applyPreset(const QString& presetId);
    void onDimensionCountChanged();
    void onEditingLevelChanged();

private:
    enum class TargetKind {
        Parameter,
        Temperature,
        Source,
        ModelParameter
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
    QString validationMessage() const;
    QString buildSingleLevelCommand() const;
    bool parseSingleLevelCommand(const QString& text);
    void loadLevelIntoUi(int levelIndex);
    void syncCurrentLevelFromUi();
    int currentLevelIndex() const;

    QComboBox* m_dimensionCountCombo = nullptr;
    QComboBox* m_editLevelCombo = nullptr;
    QComboBox* m_targetKindCombo = nullptr;
    QStackedWidget* m_targetStack = nullptr;
    QLineEdit* m_paramNameEdit = nullptr;
    QLineEdit* m_sourceNameEdit = nullptr;
    QLineEdit* m_modelTypeEdit = nullptr;
    QLineEdit* m_modelNameEdit = nullptr;
    QLineEdit* m_modelParamEdit = nullptr;
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
    QLabel* m_validationLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QStringList m_levelCommands;
    bool m_syncingCommand = false;
};

#endif // SPICE_STEP_DIALOG_H
