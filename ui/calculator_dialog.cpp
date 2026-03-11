#include "calculator_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QtMath>
#include <QDebug>
#include <QLabel>

CalculatorDialog::CalculatorDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Viora EDA Engineering Calculator");
    resize(700, 680);
    setAttribute(Qt::WA_DeleteOnClose);
    setupUI();
    applyStyle();
}

CalculatorDialog::~CalculatorDialog() {}

void CalculatorDialog::applyStyle() {
    setStyleSheet(R"(
        QDialog { background: #1a1a1e; color: #e0e0e0; font-family: 'Inter', 'Segoe UI', sans-serif; }
        QTabWidget::pane { border: 1px solid #333; border-radius: 6px; background: #1e1e24; }
        QTabBar::tab { background: #252530; color: #888; padding: 10px 20px;
                       border-radius: 4px 4px 0 0; margin-right: 2px; font-size: 12px; font-weight: 600; }
        QTabBar::tab:selected { background: #6366f1; color: white; border-bottom: 2px solid white; }
        QTabBar::tab:hover:!selected { background: #2e2e3a; color: #ccc; }
        
        QLabel { color: #c0c0c0; font-size: 13px; }
        
        /* Engineering Hand-written style for results */
        QLabel#ResultBox { 
            background: #0d1117; 
            border: 1px solid #333; 
            border-left: 4px solid #6366f1;
            border-radius: 4px;
            color: #7ee787; 
            font-family: 'Consolas', 'Courier New', monospace; 
            font-size: 15px;
            padding: 15px; 
            min-height: 100px; 
            line-height: 1.6;
        }
        
        QLabel#SectionHeader { 
            color: #6366f1; 
            font-size: 11px; 
            font-weight: 800;
            letter-spacing: 1.5px; 
            padding-top: 15px; 
            border-bottom: 1px solid #333;
            margin-bottom: 8px;
            text-transform: uppercase;
        }
        
        QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox {
            background: #252530; border: 1px solid #3a3a4a; border-radius: 5px;
            color: #e0e0e0; padding: 6px 10px; font-size: 13px; min-height: 30px; }
        QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus, QSpinBox:focus { border-color: #6366f1; background: #2a2a35; }
        
        QPushButton { background: #6366f1; color: white; border: none; border-radius: 6px;
                      padding: 10px 20px; font-size: 13px; font-weight: 700; }
        QPushButton:hover { background: #7577f5; }
        QPushButton:pressed { background: #4f51d0; }
    )");
}

QLabel* CalculatorDialog::makeHeader(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setObjectName("SectionHeader");
    return l;
}

QLabel* CalculatorDialog::makeResultBox(const QString& text) {
    QLabel* l = new QLabel(text);
    l->setObjectName("ResultBox");
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

void CalculatorDialog::setupUI() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 15);
    root->setSpacing(15);

    QLabel* hdr = new QLabel("ENGINEERING CALCULATOR");
    hdr->setStyleSheet("font-size: 18px; font-weight: 900; color: #ffffff; letter-spacing: 2px;");
    root->addWidget(hdr);

    QTabWidget* tabs = new QTabWidget;
    tabs->addTab(createTraceWidthTab(), "Trace Width");
    tabs->addTab(createImpedanceTab(), "Impedance");
    tabs->addTab(createVoltageTab(), "Voltage");
    tabs->addTab(createViaTab(), "Via");
    tabs->addTab(createUnitConverterTab(), "Units");
    tabs->addTab(createSMDTab(), "SMD Code");
    tabs->addTab(createOhmsLawTab(), "Ohm's Law");
    
    root->addWidget(tabs, 1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(box, 0, Qt::AlignRight);
}

// ─────────────────────────────────────────────────────────────────────
// TAB 1 — Trace Width
// ─────────────────────────────────────────────────────────────────────
QWidget* CalculatorDialog::createTraceWidthTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15); vl->setSpacing(10);
    vl->addWidget(makeHeader("IPC-2152 Trace Current Capacity"));

    QFormLayout* f = new QFormLayout; f->setSpacing(10); f->setLabelAlignment(Qt::AlignRight);
    m_traceI = new QDoubleSpinBox; m_traceI->setRange(0.01, 100); m_traceI->setValue(1.0); m_traceI->setSuffix(" A");
    m_traceOz = new QDoubleSpinBox; m_traceOz->setRange(0.5, 6); m_traceOz->setValue(1.0); m_traceOz->setSuffix(" oz/ft²");
    m_traceDT = new QDoubleSpinBox; m_traceDT->setRange(1, 100); m_traceDT->setValue(10); m_traceDT->setSuffix(" °C");
    m_traceTa = new QDoubleSpinBox; m_traceTa->setRange(0, 85); m_traceTa->setValue(25); m_traceTa->setSuffix(" °C");
    m_traceLayer = new QComboBox; m_traceLayer->addItems({"External (k=0.048)", "Internal (k=0.024)"});
    
    f->addRow("Current (I):", m_traceI);
    f->addRow("Copper Weight:", m_traceOz);
    f->addRow("Temp Rise (ΔT):", m_traceDT);
    f->addRow("Ambient (Ta):", m_traceTa);
    f->addRow("Layer Type:", m_traceLayer);
    vl->addLayout(f);

    m_traceRes = makeResultBox();
    vl->addWidget(m_traceRes);
    vl->addStretch();

    auto update = [this](){ calcTraceWidth(); };
    connect(m_traceI, &QDoubleSpinBox::valueChanged, update);
    connect(m_traceOz, &QDoubleSpinBox::valueChanged, update);
    connect(m_traceDT, &QDoubleSpinBox::valueChanged, update);
    connect(m_traceTa, &QDoubleSpinBox::valueChanged, update);
    connect(m_traceLayer, &QComboBox::currentIndexChanged, update);
    
    calcTraceWidth();
    return w;
}

void CalculatorDialog::calcTraceWidth() {
    double I = m_traceI->value();
    double T = m_traceDT->value();
    double oz = m_traceOz->value();
    double Ta = m_traceTa->value();
    double k = (m_traceLayer->currentIndex() == 0) ? 0.048 : 0.024;
    
    double thickMils = oz * 1.378;
    double A = std::pow(I / (k * std::pow(T, 0.44)), 1.0/0.725);
    double wMils = A / thickMils;
    double wMm = wMils * 0.0254;
    
    double rho = 1.72e-8;
    double area_m2 = (thickMils * 0.0254e-3) * (wMils * 0.0254e-3);
    double Rpm = rho / area_m2 * 1000; 

    QString eq = QString("Required Width (W)\n");
    eq += QString("──────────────────────────────\n");
    eq += QString("= %1 mils (%2 mm)\n").arg(wMils, 0, 'f', 2).arg(wMm, 0, 'f', 3);
    eq += QString("Res:  %1 mΩ/m\n").arg(Rpm, 0, 'f', 1);
    eq += QString("Temp: %1 °C (Ta + ΔT)").arg(Ta + T, 0, 'f', 1);
    m_traceRes->setText(eq);
}

// ─────────────────────────────────────────────────────────────────────
// TAB 2 — Impedance
// ─────────────────────────────────────────────────────────────────────
QWidget* CalculatorDialog::createImpedanceTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15);
    vl->addWidget(makeHeader("Transmission Line Impedance"));

    m_impMode = new QComboBox;
    m_impMode->addItems({"Microstrip (External)", "Stripline (Internal)", "Diff Microstrip", "Diff Stripline"});
    vl->addWidget(m_impMode);

    QFormLayout* f = new QFormLayout;
    m_impW = new QDoubleSpinBox; m_impW->setRange(0.01, 10); m_impW->setValue(0.2); m_impW->setSuffix(" mm");
    m_impH = new QDoubleSpinBox; m_impH->setRange(0.01, 5); m_impH->setValue(0.2); m_impH->setSuffix(" mm");
    m_impT = new QDoubleSpinBox; m_impT->setRange(0.001, 1); m_impT->setValue(0.035); m_impT->setSuffix(" mm");
    m_impEr = new QDoubleSpinBox; m_impEr->setRange(1, 20); m_impEr->setValue(4.5);
    m_impG = new QDoubleSpinBox; m_impG->setRange(0.01, 5); m_impG->setValue(0.2); m_impG->setSuffix(" mm");
    
    f->addRow("Width (W):", m_impW);
    f->addRow("Height (H):", m_impH);
    f->addRow("Thick (T):", m_impT);
    f->addRow("Perm (εr):", m_impEr);
    f->addRow("Gap (S):", m_impG);
    vl->addLayout(f);

    m_impRes = makeResultBox();
    vl->addWidget(m_impRes);
    vl->addStretch();

    auto update = [this](){ calcImpedance(); };
    connect(m_impMode, &QComboBox::currentIndexChanged, update);
    connect(m_impW, &QDoubleSpinBox::valueChanged, update);
    connect(m_impH, &QDoubleSpinBox::valueChanged, update);
    connect(m_impT, &QDoubleSpinBox::valueChanged, update);
    connect(m_impEr, &QDoubleSpinBox::valueChanged, update);
    connect(m_impG, &QDoubleSpinBox::valueChanged, update);
    
    calcImpedance();
    return w;
}

void CalculatorDialog::calcImpedance() {
    double W = m_impW->value(), H = m_impH->value(), T = m_impT->value(), er = m_impEr->value(), S = m_impG->value();
    int mode = m_impMode->currentIndex();
    double Z0 = 0, Zdiff = 0;

    if (mode == 0) { // Microstrip
        double Weff = W + (T / M_PI) * std::log(1 + 4 * M_E * H / (T * std::pow(1.0 / std::tanh(std::sqrt(6.517 * W / H)), 2)));
        double erEff = (er + 1) / 2.0 + (er - 1) / 2.0 * std::pow(1 + 12 * H / Weff, -0.5);
        if (Weff / H <= 1) Z0 = (60.0 / std::sqrt(erEff)) * std::log(8 * H / Weff + Weff / (4 * H));
        else               Z0 = (120.0 * M_PI) / (std::sqrt(erEff) * (Weff / H + 1.393 + 0.667 * std::log(Weff / H + 1.444)));
    } else if (mode == 1) { // Stripline
        double b = 2 * H;
        double Weff = W + (T / M_PI) * (1 + std::log(4 * M_E / (1.0 / std::tanh(std::sqrt(6.517 * W / b)))));
        Z0 = (60.0 / std::sqrt(er)) * std::log(4 * b / (0.67 * M_PI * (0.8 * Weff + T)));
    } else if (mode == 2) { // Diff Microstrip
        double erEff = (er + 1) / 2.0 + (er - 1) / 2.0 * std::pow(1 + 12 * H / W, -0.5);
        if (W / H <= 1) Z0 = (60.0 / std::sqrt(erEff)) * std::log(8 * H / W + W / (4 * H));
        else            Z0 = (120.0 * M_PI) / (std::sqrt(erEff) * (W / H + 1.393 + 0.667 * std::log(W / H + 1.444)));
        Zdiff = 2 * Z0 * (1 - 0.347 * std::exp(-2.9 * S / H));
    } else { // Diff Stripline
        double b = 2 * H;
        Z0 = (60.0 / std::sqrt(er)) * std::log(4 * b / (0.67 * M_PI * (0.8 * W + T)));
        Zdiff = 2 * Z0 * (1 - 0.347 * std::exp(-2.9 * S / b));
    }

    QString eq = QString("Characteristic Impedance (Z0)\n");
    eq += QString("──────────────────────────────\n");
    eq += QString("= %1 Ω\n").arg(Z0, 0, 'f', 1);
    if (mode >= 2) {
        eq += QString("Diff Zdiff: %1 Ω").arg(Zdiff, 0, 'f', 1);
    }
    m_impRes->setText(eq);
}

// ─────────────────────────────────────────────────────────────────────
// TAB 5 — Units
// ─────────────────────────────────────────────────────────────────────
QWidget* CalculatorDialog::createUnitConverterTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15);
    vl->addWidget(makeHeader("Length Converter"));
    
    QHBoxLayout* row = new QHBoxLayout;
    m_unitVal = new QDoubleSpinBox; m_unitVal->setRange(-1e9, 1e9); m_unitVal->setValue(1.0); m_unitVal->setDecimals(4);
    m_unitFrom = new QComboBox; m_unitFrom->addItems({"mm", "mils", "inches", "µm"});
    m_unitTo = new QComboBox; m_unitTo->addItems({"mils", "mm", "inches", "µm"});
    
    row->addWidget(m_unitVal, 2); row->addWidget(m_unitFrom, 1);
    row->addWidget(new QLabel("→"), 0); row->addWidget(m_unitTo, 1);
    vl->addLayout(row);
    
    m_unitRes = makeResultBox();
    vl->addWidget(m_unitRes);
    
    auto update = [this](){ calcUnits(); };
    connect(m_unitVal, &QDoubleSpinBox::valueChanged, update);
    connect(m_unitFrom, &QComboBox::currentIndexChanged, update);
    connect(m_unitTo, &QComboBox::currentIndexChanged, update);
    
    calcUnits();
    vl->addStretch();
    return w;
}

void CalculatorDialog::calcUnits() {
    double v = m_unitVal->value();
    auto toMm = [](double x, const QString& u) -> double {
        if (u == "mils") return x * 0.0254; 
        if (u == "inches") return x * 25.4;
        if (u == "µm") return x * 0.001; 
        return x;
    };
    auto fromMm = [](double x, const QString& u) -> double {
        if (u == "mils") return x / 0.0254; 
        if (u == "inches") return x / 25.4;
        if (u == "µm") return x * 1000; 
        return x;
    };
    
    double r = fromMm(toMm(v, m_unitFrom->currentText()), m_unitTo->currentText());
    
    QString eq = QString("%1 %2\n").arg(v, 0, 'g', 6).arg(m_unitFrom->currentText());
    eq += QString("──────────────────────────────\n");
    eq += QString("= %1 %2").arg(r, 0, 'g', 6).arg(m_unitTo->currentText());
    m_unitRes->setText(eq);
}

// ─────────────────────────────────────────────────────────────────────
// Other Tabs
// ─────────────────────────────────────────────────────────────────────
QWidget* CalculatorDialog::createVoltageTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15);
    vl->addWidget(makeHeader("Voltage Divider Solver"));

    QFormLayout* f = new QFormLayout;
    m_divVin = new QDoubleSpinBox; m_divVin->setRange(0, 1000); m_divVin->setValue(5.0);
    m_divR1 = new QDoubleSpinBox; m_divR1->setRange(0.1, 1e9); m_divR1->setValue(10000); m_divR1->setSuffix(" Ω");
    m_divR2 = new QDoubleSpinBox; m_divR2->setRange(0.1, 1e9); m_divR2->setValue(10000); m_divR2->setSuffix(" Ω");
    
    f->addRow("Vin:", m_divVin);
    f->addRow("Resistor R1:", m_divR1);
    f->addRow("Resistor R2:", m_divR2);
    vl->addLayout(f);

    m_divRes = makeResultBox();
    vl->addWidget(m_divRes);
    
    auto update = [this](){
        double vout = m_divVin->value() * (m_divR2->value() / (m_divR1->value() + m_divR2->value()));
        double i = m_divVin->value() / (m_divR1->value() + m_divR2->value());
        
        QString eq = QString("Vout = Vin * (R2 / (R1 + R2))\n");
        eq += QString("──────────────────────────────\n");
        eq += QString("= %1 V\n").arg(vout, 0, 'f', 3);
        eq += QString("Current: %1 mA").arg(i * 1000, 0, 'f', 2);
        m_divRes->setText(eq);
    };
    connect(m_divVin, &QDoubleSpinBox::valueChanged, update);
    connect(m_divR1, &QDoubleSpinBox::valueChanged, update);
    connect(m_divR2, &QDoubleSpinBox::valueChanged, update);
    
    update();
    vl->addStretch();
    return w;
}

QWidget* CalculatorDialog::createViaTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15);
    vl->addWidget(makeHeader("Via Current Capacity (IPC-2152)"));

    QFormLayout* f = new QFormLayout;
    m_viaDrill = new QDoubleSpinBox; m_viaDrill->setRange(0.1, 5); m_viaDrill->setValue(0.3); m_viaDrill->setSuffix(" mm");
    m_viaPlating = new QDoubleSpinBox; m_viaPlating->setRange(0.005, 0.1); m_viaPlating->setValue(0.025); m_viaPlating->setSuffix(" mm");
    m_viaLength = new QDoubleSpinBox; m_viaLength->setRange(0.1, 10); m_viaLength->setValue(1.6); m_viaLength->setSuffix(" mm");
    m_viaDT = new QDoubleSpinBox; m_viaDT->setRange(1, 100); m_viaDT->setValue(10); m_viaDT->setSuffix(" °C");
    
    f->addRow("Drill (d):", m_viaDrill);
    f->addRow("Plating (t):", m_viaPlating);
    f->addRow("Length (L):", m_viaLength);
    f->addRow("Temp Rise:", m_viaDT);
    vl->addLayout(f);

    m_viaRes = makeResultBox();
    vl->addWidget(m_viaRes);
    
    auto update = [this](){
        double d = m_viaDrill->value();
        double t = m_viaPlating->value();
        double dt = m_viaDT->value();
        double area = M_PI * ((d/2.0)*(d/2.0) - (d/2.0-t)*(d/2.0-t));
        double I = 0.048 * std::pow(dt, 0.44) * std::pow(area * 1550, 0.725);
        
        QString eq = QString("Current Limit (Imax)\n");
        eq += QString("──────────────────────────────\n");
        eq += QString("= %1 A\n").arg(I, 0, 'f', 2);
        eq += QString("Area: %1 mm²").arg(area, 0, 'f', 4);
        m_viaRes->setText(eq);
    };
    connect(m_viaDrill, &QDoubleSpinBox::valueChanged, update);
    connect(m_viaPlating, &QDoubleSpinBox::valueChanged, update);
    connect(m_viaLength, &QDoubleSpinBox::valueChanged, update);
    connect(m_viaDT, &QDoubleSpinBox::valueChanged, update);
    
    update();
    vl->addStretch();
    return w;
}

QWidget* CalculatorDialog::createSMDTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15);
    vl->addWidget(makeHeader("SMD Resistor Decoder"));

    QHBoxLayout* row = new QHBoxLayout;
    m_smdInput = new QLineEdit; m_smdInput->setPlaceholderText("Code (e.g. 103, 4R7)");
    QPushButton* btn = new QPushButton("DECODE");
    row->addWidget(m_smdInput, 1); row->addWidget(btn);
    vl->addLayout(row);

    m_smdRes = makeResultBox("Enter code above.");
    vl->addWidget(m_smdRes);
    
    auto decode = [this](){
        QString code = m_smdInput->text().trimmed().toUpper();
        if (code.isEmpty()) return;
        double val = 0; bool ok = false;
        if (code.length() == 3 && code.at(0).isDigit()) {
            val = code.left(2).toInt() * std::pow(10, code.right(1).toInt()); ok = true;
        } else if (code.contains('R')) {
            val = code.replace('R', '.').toDouble(&ok);
        }
        if (ok) m_smdRes->setText(QString("Value\n──────────────────────────────\n= %1 Ω").arg(val));
        else m_smdRes->setText("Invalid Code");
    };
    connect(btn, &QPushButton::clicked, decode);
    vl->addStretch();
    return w;
}

QWidget* CalculatorDialog::createOhmsLawTab() {
    QWidget* w = new QWidget; QVBoxLayout* vl = new QVBoxLayout(w); vl->setContentsMargins(15, 15, 15, 15);
    vl->addWidget(makeHeader("Ohm's Law (V = I * R)"));
    
    QFormLayout* f = new QFormLayout;
    m_ohmV = new QLineEdit; m_ohmI = new QLineEdit; m_ohmR = new QLineEdit; m_ohmP = new QLineEdit;
    f->addRow("Voltage (V):", m_ohmV);
    f->addRow("Current (I):", m_ohmI);
    f->addRow("Resistance (R):", m_ohmR);
    f->addRow("Power (P):", m_ohmP);
    vl->addLayout(f);

    QPushButton* solve = new QPushButton("SOLVE");
    vl->addWidget(solve);
    m_ohmRes = makeResultBox("Leave one field empty to solve.");
    vl->addWidget(m_ohmRes);

    connect(solve, &QPushButton::clicked, [this](){
        double v = m_ohmV->text().toDouble(), i = m_ohmI->text().toDouble(), r = m_ohmR->text().toDouble();
        if (m_ohmV->text().isEmpty()) v = i * r;
        else if (m_ohmI->text().isEmpty()) i = v / r;
        else if (m_ohmR->text().isEmpty()) r = v / i;
        m_ohmV->setText(QString::number(v)); m_ohmI->setText(QString::number(i)); m_ohmR->setText(QString::number(r));
        m_ohmRes->setText(QString("Solved.\n──────────────────────────────\nP = %1 W").arg(v * i));
    });
    vl->addStretch();
    return w;
}
