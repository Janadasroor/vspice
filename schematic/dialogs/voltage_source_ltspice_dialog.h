#ifndef VOLTAGE_SOURCE_LTSPICE_DIALOG_H
#define VOLTAGE_SOURCE_LTSPICE_DIALOG_H

#include <QDialog>
#include <QRadioButton>
#include <QLineEdit>
#include <QLabel>
#include <QStackedWidget>
#include <QCheckBox>
#include <QUndoStack>
#include <QGraphicsScene>
#include "../items/voltage_source_item.h"

class VoltageSourceLTSpiceDialog : public QDialog {
    Q_OBJECT

public:
    VoltageSourceLTSpiceDialog(VoltageSourceItem* item, QUndoStack* undoStack = nullptr, QGraphicsScene* scene = nullptr, QWidget* parent = nullptr);

private slots:
    void onFunctionChanged();
    void onAccepted();
    void onPwlBrowse();

private:
    void setupUi();
    void loadFromItem();
    void saveToItem();

    VoltageSourceItem* m_item;
    QUndoStack* m_undoStack;
    QGraphicsScene* m_scene;

    // Functions
    QRadioButton* m_noneRadio;
    QRadioButton* m_pulseRadio;
    QRadioButton* m_sineRadio;
    QRadioButton* m_expRadio;
    QRadioButton* m_sffmRadio;
    QRadioButton* m_pwlRadio;
    QRadioButton* m_pwlFileRadio;

    QStackedWidget* m_paramStack;

    // Pulse Params
    QLineEdit* m_pulseV1;
    QLineEdit* m_pulseV2;
    QLineEdit* m_pulseTd;
    QLineEdit* m_pulseTr;
    QLineEdit* m_pulseTf;
    QLineEdit* m_pulseTon;
    QLineEdit* m_pulseTperiod;
    QLineEdit* m_pulseNcycles;

    // Sine Params
    QLineEdit* m_sineVoffset;
    QLineEdit* m_sineVamp;
    QLineEdit* m_sineFreq;
    QLineEdit* m_sineTd;
    QLineEdit* m_sineTheta;
    QLineEdit* m_sinePhi;
    QLineEdit* m_sineNcycles;

    // EXP Params
    QLineEdit* m_expV1;
    QLineEdit* m_expV2;
    QLineEdit* m_expTd1;
    QLineEdit* m_expTau1;
    QLineEdit* m_expTd2;
    QLineEdit* m_expTau2;

    // SFFM Params
    QLineEdit* m_sffmVoff;
    QLineEdit* m_sffmVamp;
    QLineEdit* m_sffmFcar;
    QLineEdit* m_sffmMdi;
    QLineEdit* m_sffmFsig;

    // PWL Params
    QLineEdit* m_pwlPoints;

    // PWL File Params
    QLineEdit* m_pwlFile;

    // DC Value
    QLineEdit* m_dcValue;
    QCheckBox* m_dcVisible;

    // Small signal AC
    QLineEdit* m_acAmplitude;
    QLineEdit* m_acPhase;
    QCheckBox* m_acVisible;

    // Parasitic Properties
    QLineEdit* m_seriesRes;
    QLineEdit* m_parallelCap;
    QCheckBox* m_parasiticVisible;

    QCheckBox* m_functionVisible;
};

#endif // VOLTAGE_SOURCE_LTSPICE_DIALOG_H
