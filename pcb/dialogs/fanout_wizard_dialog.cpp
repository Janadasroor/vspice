#include "fanout_wizard_dialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QGroupBox>
#include "theme_manager.h"

FanoutWizardDialog::FanoutWizardDialog(ComponentItem* component, QWidget* parent)
    : QDialog(parent), m_component(component) {
    setWindowTitle(QString("Fan-out Wizard: %1").arg(component->model()->name()));
    resize(400, 450);
    setupUI();
}

void FanoutWizardDialog::setupUI() {
    PCBTheme* theme = ThemeManager::theme();
    QString accent = theme ? theme->accentColor().name() : "#007acc";

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    QLabel* titleLabel = new QLabel("Configure Component Fan-out");
    titleLabel->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(accent));
    mainLayout->addWidget(titleLabel);

    QGroupBox* routingGroup = new QGroupBox("Routing Options", this);
    QFormLayout* routingLayout = new QFormLayout(routingGroup);
    routingLayout->setContentsMargins(15, 15, 15, 15);
    routingLayout->setSpacing(10);

    m_directionCombo = new QComboBox(this);
    m_directionCombo->addItem("Outward", FanoutOptions::Outward);
    m_directionCombo->addItem("Inward", FanoutOptions::Inward);
    m_directionCombo->addItem("Alternating", FanoutOptions::Alternating);
    m_directionCombo->setCurrentIndex(0);
    routingLayout->addRow("Direction:", m_directionCombo);

    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(0.05, 2.0);
    m_widthSpin->setSingleStep(0.05);
    m_widthSpin->setValue(0.25);
    m_widthSpin->setSuffix(" mm");
    routingLayout->addRow("Trace Width:", m_widthSpin);

    m_lengthSpin = new QDoubleSpinBox(this);
    m_lengthSpin->setRange(0.5, 10.0);
    m_lengthSpin->setSingleStep(0.25);
    m_lengthSpin->setValue(1.27);
    m_lengthSpin->setSuffix(" mm");
    routingLayout->addRow("Escape Length:", m_lengthSpin);

    mainLayout->addWidget(routingGroup);

    QGroupBox* viaGroup = new QGroupBox("Via Options", this);
    QFormLayout* viaLayout = new QFormLayout(viaGroup);
    viaLayout->setContentsMargins(15, 15, 15, 15);
    viaLayout->setSpacing(10);

    m_viaDiaSpin = new QDoubleSpinBox(this);
    m_viaDiaSpin->setRange(0.2, 5.0);
    m_viaDiaSpin->setSingleStep(0.1);
    m_viaDiaSpin->setValue(0.6);
    m_viaDiaSpin->setSuffix(" mm");
    viaLayout->addRow("Diameter:", m_viaDiaSpin);

    m_viaDrillSpin = new QDoubleSpinBox(this);
    m_viaDrillSpin->setRange(0.1, 3.0);
    m_viaDrillSpin->setSingleStep(0.05);
    m_viaDrillSpin->setValue(0.3);
    m_viaDrillSpin->setSuffix(" mm");
    viaLayout->addRow("Drill:", m_viaDrillSpin);

    mainLayout->addWidget(viaGroup);

    m_powerOnlyCheck = new QCheckBox("Only fan-out power nets (VCC, GND, etc.)", this);
    m_unconnectedCheck = new QCheckBox("Include unconnected pins", this);
    m_unconnectedCheck->setChecked(false);
    
    mainLayout->addWidget(m_powerOnlyCheck);
    mainLayout->addWidget(m_unconnectedCheck);

    mainLayout->addStretch();

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
    
    // Premium button style
    buttons->button(QDialogButtonBox::Ok)->setText("Generate Fan-out");
    buttons->button(QDialogButtonBox::Ok)->setStyleSheet(QString("background-color: %1; color: white; font-weight: bold; padding: 8px 20px;").arg(accent));
}

FanoutWizardDialog::FanoutOptions FanoutWizardDialog::options() const {
    FanoutOptions opts;
    opts.direction = static_cast<FanoutOptions::Direction>(m_directionCombo->currentData().toInt());
    opts.traceWidth = m_widthSpin->value();
    opts.viaDiameter = m_viaDiaSpin->value();
    opts.viaDrill = m_viaDrillSpin->value();
    opts.shortTraceLength = m_lengthSpin->value();
    opts.onlyPowerNets = m_powerOnlyCheck->isChecked();
    opts.includeUnconnected = m_unconnectedCheck->isChecked();
    return opts;
}
