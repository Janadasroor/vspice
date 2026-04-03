#include "via_stitching_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include "../layers/pcb_layer.h"
#include <algorithm>

ViaStitchingDialog::ViaStitchingDialog(QWidget* parent)
    : QDialog(parent) {
    setupUI();
}

void ViaStitchingDialog::setupUI() {
    setWindowTitle("Via Stitching Settings");
    resize(380, 340);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout();

    m_spacingX = new QDoubleSpinBox();
    m_spacingX->setRange(0.5, 20.0);
    m_spacingX->setValue(2.54);
    m_spacingX->setSuffix(" mm");
    form->addRow("Grid Spacing X:", m_spacingX);

    m_spacingY = new QDoubleSpinBox();
    m_spacingY->setRange(0.5, 20.0);
    m_spacingY->setValue(2.54);
    m_spacingY->setSuffix(" mm");
    form->addRow("Grid Spacing Y:", m_spacingY);

    m_viaDiam = new QDoubleSpinBox();
    m_viaDiam->setRange(0.2, 5.0);
    m_viaDiam->setValue(0.6);
    m_viaDiam->setSuffix(" mm");
    form->addRow("Via Diameter:", m_viaDiam);

    m_viaDrill = new QDoubleSpinBox();
    m_viaDrill->setRange(0.1, 4.0);
    m_viaDrill->setValue(0.3);
    m_viaDrill->setSuffix(" mm");
    form->addRow("Via Drill Size:", m_viaDrill);

    m_netEdit = new QLineEdit("GND");
    form->addRow("Net Name:", m_netEdit);

    m_startLayerCombo = new QComboBox();
    m_endLayerCombo = new QComboBox();
    QList<PCBLayer*> copper = PCBLayerManager::instance().copperLayers();
    std::sort(copper.begin(), copper.end(), [](PCBLayer* a, PCBLayer* b) { return a->id() < b->id(); });
    for (PCBLayer* layer : copper) {
        if (!layer) continue;
        m_startLayerCombo->addItem(layer->name(), layer->id());
        m_endLayerCombo->addItem(layer->name(), layer->id());
    }
    int topIdx = m_startLayerCombo->findData(PCBLayerManager::TopCopper);
    int botIdx = m_endLayerCombo->findData(PCBLayerManager::BottomCopper);
    if (topIdx >= 0) m_startLayerCombo->setCurrentIndex(topIdx);
    if (botIdx >= 0) m_endLayerCombo->setCurrentIndex(botIdx);
    form->addRow("Start Layer:", m_startLayerCombo);
    form->addRow("End Layer:", m_endLayerCombo);

    m_microviaCheck = new QCheckBox("Microvia mode (adjacent layers)");
    form->addRow("", m_microviaCheck);

    connect(m_microviaCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_viaDiam->setValue(std::min(m_viaDiam->value(), 0.35));
            m_viaDrill->setValue(std::min(m_viaDrill->value(), 0.15));
        }
    });

    mainLayout->addLayout(form);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton("Generate Vias");
    QPushButton* cancelBtn = new QPushButton("Cancel");
    
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}
