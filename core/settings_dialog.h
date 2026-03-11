#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QTextEdit>
#include <QLineEdit>

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog();

private slots:
    void onAccept();

private:
    void setupUI();
    void loadSettings();

    // General
    QCheckBox* m_autoSaveCheck;
    QSpinBox* m_autoSaveSpin;
    QComboBox* m_themeCombo;
    QCheckBox* m_snapGridCheck;
    QCheckBox* m_autoFocusCrossProbeCheck;
    QCheckBox* m_realtimeWireUpdateCheck;

    // Simulator
    QComboBox* m_solverCombo;
    QComboBox* m_integrationCombo;
    QDoubleSpinBox* m_reltolSpin;
    QDoubleSpinBox* m_abstolSpin;
    QDoubleSpinBox* m_vntolSpin;
    QDoubleSpinBox* m_gminSpin;
    QSpinBox* m_maxIterSpin;
    
    // AI
    QLineEdit* m_geminiKeyEdit;
    
    // Libraries
    QTextEdit* m_symbolPathsEdit;
    QTextEdit* m_modelPathsEdit;

    QListWidget* m_navMenu;
    QStackedWidget* m_pagesStack;
};

#endif // SETTINGS_DIALOG_H
