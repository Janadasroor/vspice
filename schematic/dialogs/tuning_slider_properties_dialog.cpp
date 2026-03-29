#include "tuning_slider_properties_dialog.h"
#include "../items/tuning_slider_symbol_item.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>

TuningSliderPropertiesDialog::TuningSliderPropertiesDialog(TuningSliderSymbolItem* item, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Slider Properties");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    QFormLayout* form = new QFormLayout();

    m_refEdit = new QLineEdit(item->reference());
    
    m_minSpin = new QDoubleSpinBox();
    m_minSpin->setRange(-1e15, 1e15);
    m_minSpin->setValue(item->minValue());

    m_maxSpin = new QDoubleSpinBox();
    m_maxSpin->setRange(-1e15, 1e15);
    m_maxSpin->setValue(item->maxValue());

    m_currentSpin = new QDoubleSpinBox();
    m_currentSpin->setRange(-1e15, 1e15);
    m_currentSpin->setValue(item->currentValue());

    form->addRow("Parameter Name:", m_refEdit);
    form->addRow("Min Value:", m_minSpin);
    form->addRow("Max Value:", m_maxSpin);
    form->addRow("Current Value:", m_currentSpin);

    mainLayout->addLayout(form);

    auto* note = new QLabel("Use this parameter in other components as {NAME}.");
    note->setStyleSheet("color: #888; font-style: italic;");
    mainLayout->addWidget(note);

    QDialogButtonBox* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bbox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(bbox);
}
