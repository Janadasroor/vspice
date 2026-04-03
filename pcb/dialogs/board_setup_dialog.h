#ifndef BOARD_SETUP_DIALOG_H
#define BOARD_SETUP_DIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include "../layers/pcb_layer.h"

class BoardSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit BoardSetupDialog(QWidget* parent = nullptr);
    ~BoardSetupDialog();

private slots:
    void onLayerCountChanged(int count);
    void onApply();
    void updateTable();
    void updateNetClassTable();
    void updateNetAssignmentTable();
    void updateClearanceRulesTable();
    void onAddNetClass();
    void onRemoveNetClass();
    void onAddClearanceRule();
    void onRemoveClearanceRule();
    void validateClearanceRulesUI();
    void onImpedanceInputsChanged();

private:
    void setupUI();
    QWidget* createStackupTab();
    QWidget* createDesignRulesTab();
    void updateImpedanceCalculatorDefaults();
    void recomputeImpedance();

    QTabWidget* m_tabs;
    QTableWidget* m_stackupTable;
    QTableWidget* m_netClassTable;
    QTableWidget* m_netAssignmentTable;
    QTableWidget* m_clearanceRulesTable;
    QSpinBox* m_layerCountSpin;
    QComboBox* m_surfaceFinishCombo;
    QLabel* m_totalThicknessLabel;
    QDoubleSpinBox* m_solderMaskExpansionSpin;
    QDoubleSpinBox* m_pasteExpansionSpin;
    QDoubleSpinBox* m_targetImpedanceSpin;
    QDoubleSpinBox* m_calcErSpin;
    QDoubleSpinBox* m_calcDielectricHeightSpin;
    QDoubleSpinBox* m_calcCopperThicknessSpin;
    QLabel* m_calcSuggestedWidthLabel;
    QLabel* m_calcModeLabel;
    
    PCBLayerManager::BoardStackup m_currentStackup;
};

#endif // BOARD_SETUP_DIALOG_H
