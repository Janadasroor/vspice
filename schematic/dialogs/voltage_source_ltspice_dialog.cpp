#include "voltage_source_ltspice_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QFileDialog>
#include "../editor/schematic_commands.h"

VoltageSourceLTSpiceDialog::VoltageSourceLTSpiceDialog(VoltageSourceItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_item(item), m_undoStack(undoStack), m_scene(scene) {
    
    setWindowTitle("Independent Voltage Source - " + item->reference());
    setupUi();
    loadFromItem();
}

void VoltageSourceLTSpiceDialog::setupUi() {
    auto* mainLayout = new QHBoxLayout(this);

    // --- Left Column: Functions ---
    auto* leftCol = new QVBoxLayout();
    auto* groupFunctions = new QGroupBox("Functions");
    auto* functionsLayout = new QVBoxLayout(groupFunctions);

    m_noneRadio = new QRadioButton("(none)");
    m_pulseRadio = new QRadioButton("PULSE(V1 V2 Tdelay Trise Tfall Ton Period Ncycles)");
    m_sineRadio = new QRadioButton("SINE(Voffset Vamp Freq Td Theta Phi Ncycles)");
    m_expRadio = new QRadioButton("EXP(V1 V2 Td1 Tau1 Td2 Tau2)");
    m_sffmRadio = new QRadioButton("SFFM(Voff Vamp Fcar MDI Fsig)");
    m_pwlRadio = new QRadioButton("PWL(t1 v1 t2 v2...)");
    m_pwlFileRadio = new QRadioButton("PWL FILE:");

    functionsLayout->addWidget(m_noneRadio);
    functionsLayout->addWidget(m_pulseRadio);
    functionsLayout->addWidget(m_sineRadio);
    functionsLayout->addWidget(m_expRadio);
    functionsLayout->addWidget(m_sffmRadio);
    functionsLayout->addWidget(m_pwlRadio);
    
    auto* pwlFileLayout = new QHBoxLayout();
    pwlFileLayout->addWidget(m_pwlFileRadio);
    m_pwlFile = new QLineEdit();
    pwlFileLayout->addWidget(m_pwlFile);
    auto* browseBtn = new QPushButton("Browse");
    pwlFileLayout->addWidget(browseBtn);
    functionsLayout->addLayout(pwlFileLayout);
    
    connect(browseBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onPwlBrowse);

    // Function Parameters (Stacked)
    m_paramStack = new QStackedWidget();
    
    // 0: None
    m_paramStack->addWidget(new QWidget());

    // 1: Pulse
    auto* pulsePage = new QWidget();
    auto* pulseLayout = new QGridLayout(pulsePage);
    pulseLayout->addWidget(new QLabel("Vinitial[V]:"), 0, 0); m_pulseV1 = new QLineEdit(); pulseLayout->addWidget(m_pulseV1, 0, 1);
    pulseLayout->addWidget(new QLabel("Von[V]:"), 1, 0);     m_pulseV2 = new QLineEdit(); pulseLayout->addWidget(m_pulseV2, 1, 1);
    pulseLayout->addWidget(new QLabel("Tdelay[s]:"), 2, 0); m_pulseTd = new QLineEdit(); pulseLayout->addWidget(m_pulseTd, 2, 1);
    pulseLayout->addWidget(new QLabel("Trise[s]:"), 3, 0);  m_pulseTr = new QLineEdit(); pulseLayout->addWidget(m_pulseTr, 3, 1);
    pulseLayout->addWidget(new QLabel("Tfall[s]:"), 4, 0);  m_pulseTf = new QLineEdit(); pulseLayout->addWidget(m_pulseTf, 4, 1);
    pulseLayout->addWidget(new QLabel("Ton[s]:"), 5, 0);    m_pulseTon = new QLineEdit(); pulseLayout->addWidget(m_pulseTon, 5, 1);
    pulseLayout->addWidget(new QLabel("Tperiod[s]:"), 6, 0); m_pulseTperiod = new QLineEdit(); pulseLayout->addWidget(m_pulseTperiod, 6, 1);
    pulseLayout->addWidget(new QLabel("Ncycles:"), 7, 0);   m_pulseNcycles = new QLineEdit(); pulseLayout->addWidget(m_pulseNcycles, 7, 1);
    m_paramStack->addWidget(pulsePage);

    // 2: Sine
    auto* sinePage = new QWidget();
    auto* sineLayout = new QGridLayout(sinePage);
    sineLayout->addWidget(new QLabel("Voffset[V]:"), 0, 0); m_sineVoffset = new QLineEdit(); sineLayout->addWidget(m_sineVoffset, 0, 1);
    sineLayout->addWidget(new QLabel("Vamp[V]:"), 1, 0);    m_sineVamp = new QLineEdit(); sineLayout->addWidget(m_sineVamp, 1, 1);
    sineLayout->addWidget(new QLabel("Freq[Hz]:"), 2, 0);   m_sineFreq = new QLineEdit(); sineLayout->addWidget(m_sineFreq, 2, 1);
    sineLayout->addWidget(new QLabel("Td[s]:"), 3, 0);     m_sineTd = new QLineEdit(); sineLayout->addWidget(m_sineTd, 3, 1);
    sineLayout->addWidget(new QLabel("Theta[1/s]:"), 4, 0); m_sineTheta = new QLineEdit(); sineLayout->addWidget(m_sineTheta, 4, 1);
    sineLayout->addWidget(new QLabel("Phi[deg]:"), 5, 0);   m_sinePhi = new QLineEdit(); sineLayout->addWidget(m_sinePhi, 5, 1);
    sineLayout->addWidget(new QLabel("Ncycles:"), 6, 0);    m_sineNcycles = new QLineEdit(); sineLayout->addWidget(m_sineNcycles, 6, 1);
    m_paramStack->addWidget(sinePage);

    // 3: EXP
    auto* expPage = new QWidget();
    auto* expLayout = new QGridLayout(expPage);
    expLayout->addWidget(new QLabel("V1[V]:"), 0, 0);   m_expV1 = new QLineEdit(); expLayout->addWidget(m_expV1, 0, 1);
    expLayout->addWidget(new QLabel("V2[V]:"), 1, 0);   m_expV2 = new QLineEdit(); expLayout->addWidget(m_expV2, 1, 1);
    expLayout->addWidget(new QLabel("Td1[s]:"), 2, 0);  m_expTd1 = new QLineEdit(); expLayout->addWidget(m_expTd1, 2, 1);
    expLayout->addWidget(new QLabel("Tau1[s]:"), 3, 0); m_expTau1 = new QLineEdit(); expLayout->addWidget(m_expTau1, 3, 1);
    expLayout->addWidget(new QLabel("Td2[s]:"), 4, 0);  m_expTd2 = new QLineEdit(); expLayout->addWidget(m_expTd2, 4, 1);
    expLayout->addWidget(new QLabel("Tau2[s]:"), 5, 0); m_expTau2 = new QLineEdit(); expLayout->addWidget(m_expTau2, 5, 1);
    m_paramStack->addWidget(expPage);

    // 4: SFFM
    auto* sffmPage = new QWidget();
    auto* sffmLayout = new QGridLayout(sffmPage);
    sffmLayout->addWidget(new QLabel("Voff[V]:"), 0, 0);  m_sffmVoff = new QLineEdit(); sffmLayout->addWidget(m_sffmVoff, 0, 1);
    sffmLayout->addWidget(new QLabel("Vamp[V]:"), 1, 0);  m_sffmVamp = new QLineEdit(); sffmLayout->addWidget(m_sffmVamp, 1, 1);
    sffmLayout->addWidget(new QLabel("Fcar[Hz]:"), 2, 0); m_sffmFcar = new QLineEdit(); sffmLayout->addWidget(m_sffmFcar, 2, 1);
    sffmLayout->addWidget(new QLabel("MDI:"), 3, 0);      m_sffmMdi = new QLineEdit(); sffmLayout->addWidget(m_sffmMdi, 3, 1);
    sffmLayout->addWidget(new QLabel("Fsig[Hz]:"), 4, 0); m_sffmFsig = new QLineEdit(); sffmLayout->addWidget(m_sffmFsig, 4, 1);
    m_paramStack->addWidget(sffmPage);

    // 5: PWL
    auto* pwlPage = new QWidget();
    auto* pwlLayout = new QVBoxLayout(pwlPage);
    pwlLayout->addWidget(new QLabel("Values(t1 v1 t2 v2...):"));
    m_pwlPoints = new QLineEdit();
    pwlLayout->addWidget(m_pwlPoints);
    m_paramStack->addWidget(pwlPage);

    functionsLayout->addWidget(m_paramStack);
    
    m_functionVisible = new QCheckBox("Make this information visible on schematic:");
    functionsLayout->addWidget(m_functionVisible);
    
    leftCol->addWidget(groupFunctions);

    // --- Right Column: DC, AC, Parasitics ---
    auto* rightCol = new QVBoxLayout();
    
    // DC Value
    auto* groupDc = new QGroupBox("DC Value");
    auto* dcLayout = new QGridLayout(groupDc);
    dcLayout->addWidget(new QLabel("DC value:"), 0, 0);
    m_dcValue = new QLineEdit();
    dcLayout->addWidget(m_dcValue, 0, 1);
    m_dcVisible = new QCheckBox("Make this information visible on schematic:");
    dcLayout->addWidget(m_dcVisible, 1, 0, 1, 2);
    rightCol->addWidget(groupDc);

    // AC Analysis
    auto* groupAc = new QGroupBox("Small signal AC analysis(.AC)");
    auto* acLayout = new QGridLayout(groupAc);
    acLayout->addWidget(new QLabel("AC Amplitude:"), 0, 0); m_acAmplitude = new QLineEdit(); acLayout->addWidget(m_acAmplitude, 0, 1);
    acLayout->addWidget(new QLabel("AC Phase:"), 1, 0);     m_acPhase = new QLineEdit(); acLayout->addWidget(m_acPhase, 1, 1);
    m_acVisible = new QCheckBox("Make this information visible on schematic:");
    acLayout->addWidget(m_acVisible, 2, 0, 1, 2);
    rightCol->addWidget(groupAc);

    // Parasitics
    auto* groupPara = new QGroupBox("Parasitic Properties");
    auto* paraLayout = new QGridLayout(groupPara);
    paraLayout->addWidget(new QLabel("Series Resistance[Ω]:"), 0, 0); m_seriesRes = new QLineEdit(); paraLayout->addWidget(m_seriesRes, 0, 1);
    paraLayout->addWidget(new QLabel("Parallel Capacitance[F]:"), 1, 0); m_parallelCap = new QLineEdit(); paraLayout->addWidget(m_parallelCap, 1, 1);
    m_parasiticVisible = new QCheckBox("Make this information visible on schematic:");
    paraLayout->addWidget(m_parasiticVisible, 2, 0, 1, 2);
    rightCol->addWidget(groupPara);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    auto* cancelBtn = new QPushButton("Cancel");
    auto* okBtn = new QPushButton("OK");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    rightCol->addLayout(btnLayout);

    mainLayout->addLayout(leftCol);
    mainLayout->addLayout(rightCol);

    connect(m_noneRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pulseRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_sineRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_expRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_sffmRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pwlRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);
    connect(m_pwlFileRadio, &QRadioButton::toggled, this, &VoltageSourceLTSpiceDialog::onFunctionChanged);

    connect(okBtn, &QPushButton::clicked, this, &VoltageSourceLTSpiceDialog::onAccepted);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void VoltageSourceLTSpiceDialog::onFunctionChanged() {
    if (m_noneRadio->isChecked()) m_paramStack->setCurrentIndex(0);
    else if (m_pulseRadio->isChecked()) m_paramStack->setCurrentIndex(1);
    else if (m_sineRadio->isChecked()) m_paramStack->setCurrentIndex(2);
    else if (m_expRadio->isChecked()) m_paramStack->setCurrentIndex(3);
    else if (m_sffmRadio->isChecked()) m_paramStack->setCurrentIndex(4);
    else if (m_pwlRadio->isChecked()) m_paramStack->setCurrentIndex(5);
    else if (m_pwlFileRadio->isChecked()) m_paramStack->setCurrentIndex(0); // PWL File uses the HLayout line edit
}

void VoltageSourceLTSpiceDialog::onPwlBrowse() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select PWL File", "", "Data Files (*.txt *.csv *.dat);;All Files (*)");
    if (!fileName.isEmpty()) {
        m_pwlFile->setText(fileName);
        m_pwlFileRadio->setChecked(true);
    }
}

void VoltageSourceLTSpiceDialog::loadFromItem() {
    // Functions
    m_noneRadio->setChecked(m_item->sourceType() == VoltageSourceItem::DC);
    m_pulseRadio->setChecked(m_item->sourceType() == VoltageSourceItem::Pulse);
    m_sineRadio->setChecked(m_item->sourceType() == VoltageSourceItem::Sine);
    m_expRadio->setChecked(m_item->sourceType() == VoltageSourceItem::EXP);
    m_sffmRadio->setChecked(m_item->sourceType() == VoltageSourceItem::SFFM);
    m_pwlRadio->setChecked(m_item->sourceType() == VoltageSourceItem::PWL);
    m_pwlFileRadio->setChecked(m_item->sourceType() == VoltageSourceItem::PWLFile);
    
    onFunctionChanged();

    // Pulse
    m_pulseV1->setText(m_item->pulseV1());
    m_pulseV2->setText(m_item->pulseV2());
    m_pulseTd->setText(m_item->pulseDelay());
    m_pulseTr->setText(m_item->pulseRise());
    m_pulseTf->setText(m_item->pulseFall());
    m_pulseTon->setText(m_item->pulseWidth());
    m_pulseTperiod->setText(m_item->pulsePeriod());
    m_pulseNcycles->setText(m_item->pulseNcycles());

    // Sine
    m_sineVoffset->setText(m_item->sineOffset());
    m_sineVamp->setText(m_item->sineAmplitude());
    m_sineFreq->setText(m_item->sineFrequency());
    m_sineTd->setText(m_item->sineDelay());
    m_sineTheta->setText(m_item->sineTheta());
    m_sinePhi->setText(m_item->sinePhi());
    m_sineNcycles->setText(m_item->sineNcycles());

    // EXP
    m_expV1->setText(m_item->expV1());
    m_expV2->setText(m_item->expV2());
    m_expTd1->setText(m_item->expTd1());
    m_expTau1->setText(m_item->expTau1());
    m_expTd2->setText(m_item->expTd2());
    m_expTau2->setText(m_item->expTau2());

    // SFFM
    m_sffmVoff->setText(m_item->sffmOff());
    m_sffmVamp->setText(m_item->sffmAmplit());
    m_sffmFcar->setText(m_item->sffmCarrier());
    m_sffmMdi->setText(m_item->sffmModIndex());
    m_sffmFsig->setText(m_item->sffmSignalFreq());

    // PWL
    m_pwlPoints->setText(m_item->pwlPoints());
    m_pwlFile->setText(m_item->pwlFile());

    // Right Col
    m_dcValue->setText(m_item->dcVoltage());
    m_acAmplitude->setText(m_item->acAmplitude());
    m_acPhase->setText(m_item->acPhase());
    m_seriesRes->setText(m_item->seriesResistance());
    m_parallelCap->setText(m_item->parallelCapacitance());

    // Visibility
    m_functionVisible->setChecked(m_item->isFunctionVisible());
    m_dcVisible->setChecked(m_item->isDcVisible());
    m_acVisible->setChecked(m_item->isAcVisible());
    m_parasiticVisible->setChecked(m_item->isParasiticVisible());
}

void VoltageSourceLTSpiceDialog::onAccepted() {
    saveToItem();
    accept();
}

void VoltageSourceLTSpiceDialog::saveToItem() {
    if (m_undoStack) {
        m_undoStack->beginMacro("Update Voltage Source (LTspice Dialog)");
    }

    // Capture state
    VoltageSourceItem::SourceType type = VoltageSourceItem::DC;
    if (m_pulseRadio->isChecked()) type = VoltageSourceItem::Pulse;
    else if (m_sineRadio->isChecked()) type = VoltageSourceItem::Sine;
    else if (m_expRadio->isChecked()) type = VoltageSourceItem::EXP;
    else if (m_sffmRadio->isChecked()) type = VoltageSourceItem::SFFM;
    else if (m_pwlRadio->isChecked()) type = VoltageSourceItem::PWL;
    else if (m_pwlFileRadio->isChecked()) type = VoltageSourceItem::PWLFile;

    m_item->setSourceType(type);
    
    // Pulse
    m_item->setPulseV1(m_pulseV1->text());
    m_item->setPulseV2(m_pulseV2->text());
    m_item->setPulseDelay(m_pulseTd->text());
    m_item->setPulseRise(m_pulseTr->text());
    m_item->setPulseFall(m_pulseTf->text());
    m_item->setPulseWidth(m_pulseTon->text());
    m_item->setPulsePeriod(m_pulseTperiod->text());
    m_item->setPulseNcycles(m_pulseNcycles->text());

    // Sine
    m_item->setSineOffset(m_sineVoffset->text());
    m_item->setSineAmplitude(m_sineVamp->text());
    m_item->setSineFrequency(m_sineFreq->text());
    m_item->setSineDelay(m_sineTd->text());
    m_item->setSineTheta(m_sineTheta->text());
    m_item->setSinePhi(m_sinePhi->text());
    m_item->setSineNcycles(m_sineNcycles->text());

    // EXP
    m_item->setExpV1(m_expV1->text());
    m_item->setExpV2(m_expV2->text());
    m_item->setExpTd1(m_expTd1->text());
    m_item->setExpTau1(m_expTau1->text());
    m_item->setExpTd2(m_expTd2->text());
    m_item->setExpTau2(m_expTau2->text());

    // SFFM
    m_item->setSffmOff(m_sffmVoff->text());
    m_item->setSffmAmplit(m_sffmVamp->text());
    m_item->setSffmCarrier(m_sffmFcar->text());
    m_item->setSffmModIndex(m_sffmMdi->text());
    m_item->setSffmSignalFreq(m_sffmFsig->text());

    // PWL
    m_item->setPwlPoints(m_pwlPoints->text());
    m_item->setPwlFile(m_pwlFile->text());

    // Right Col
    m_item->setDcVoltage(m_dcValue->text());
    m_item->setAcAmplitude(m_acAmplitude->text());
    m_item->setAcPhase(m_acPhase->text());
    m_item->setSeriesResistance(m_seriesRes->text());
    m_item->setParallelCapacitance(m_parallelCap->text());

    // Visibility
    m_item->setFunctionVisible(m_functionVisible->isChecked());
    m_item->setDcVisible(m_dcVisible->isChecked());
    m_item->setAcVisible(m_acVisible->isChecked());
    m_item->setParasiticVisible(m_parasiticVisible->isChecked());

    if (m_undoStack) {
        m_undoStack->endMacro();
    }
    
    m_item->update();
}
