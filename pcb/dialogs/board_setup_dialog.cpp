#include "board_setup_dialog.h"
#include "../../core/net_class.h"
#include "flux/core/net_manager.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QDoubleSpinBox>
#include <QMessageBox>
#include <QSignalBlocker>
#include <cmath>

namespace {
double microstripImpedance(double widthMm, double hMm, double er, double copperTMm) {
    const double w = std::max(0.001, widthMm);
    const double t = std::max(0.0, copperTMm);
    const double h = std::max(0.001, hMm);
    const double u = (w + t) / h; // simple thickness correction
    const double eeff = (er + 1.0) * 0.5 + (er - 1.0) * 0.5 / std::sqrt(1.0 + 12.0 / u);
    if (u <= 1.0) {
        return (60.0 / std::sqrt(eeff)) * std::log(8.0 / u + 0.25 * u);
    }
    constexpr double kPi = 3.14159265358979323846;
    return (120.0 * kPi) / (std::sqrt(eeff) * (u + 1.393 + 0.667 * std::log(u + 1.444)));
}

double solveMicrostripWidthForImpedance(double targetZ, double hMm, double er, double copperTMm) {
    double lo = 0.01;
    double hi = 20.0;
    for (int i = 0; i < 64; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double z = microstripImpedance(mid, hMm, er, copperTMm);
        if (z > targetZ) lo = mid; // Need wider trace to lower impedance.
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}
}

BoardSetupDialog::BoardSetupDialog(QWidget* parent)
    : QDialog(parent) {
    m_currentStackup = PCBLayerManager::instance().stackup();
    setupUI();
    updateImpedanceCalculatorDefaults();
    updateTable();
}

BoardSetupDialog::~BoardSetupDialog() {}

void BoardSetupDialog::setupUI() {
    setWindowTitle("Board Setup");
    resize(900, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    m_tabs = new QTabWidget();
    m_tabs->addTab(createStackupTab(), "Board Stackup");
    m_tabs->addTab(createDesignRulesTab(), "Design Rules");
    
    mainLayout->addWidget(m_tabs);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* applyBtn = new QPushButton("Apply");
    QPushButton* cancelBtn = new QPushButton("Cancel");
    
    connect(applyBtn, &QPushButton::clicked, this, &BoardSetupDialog::onApply);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addStretch();
    btnLayout->addWidget(applyBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);
}

QWidget* BoardSetupDialog::createStackupTab() {
    QWidget* w = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);

    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel("Copper Layers:"));
    m_layerCountSpin = new QSpinBox();
    m_layerCountSpin->setRange(2, 32);
    m_layerCountSpin->setSingleStep(2);
    m_layerCountSpin->setValue(PCBLayerManager::instance().copperLayerCount());
    connect(m_layerCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BoardSetupDialog::onLayerCountChanged);
    topRow->addWidget(m_layerCountSpin);
    topRow->addSpacing(20);
    topRow->addWidget(new QLabel("Surface Finish:"));
    m_surfaceFinishCombo = new QComboBox();
    m_surfaceFinishCombo->addItems({"ENIG", "HASL", "Lead-free HASL", "OSP", "Immersion Silver", "Immersion Tin"});
    const int idx = m_surfaceFinishCombo->findText(m_currentStackup.surfaceFinish);
    if (idx >= 0) m_surfaceFinishCombo->setCurrentIndex(idx);
    topRow->addWidget(m_surfaceFinishCombo);
    topRow->addSpacing(20);
    m_totalThicknessLabel = new QLabel();
    topRow->addWidget(m_totalThicknessLabel);
    topRow->addStretch();
    
    layout->addLayout(topRow);

    QHBoxLayout* maskPasteRow = new QHBoxLayout();
    maskPasteRow->addWidget(new QLabel("Board Mask Expansion:"));
    m_solderMaskExpansionSpin = new QDoubleSpinBox();
    m_solderMaskExpansionSpin->setRange(-1.0, 1.0);
    m_solderMaskExpansionSpin->setDecimals(3);
    m_solderMaskExpansionSpin->setSingleStep(0.01);
    m_solderMaskExpansionSpin->setSuffix(" mm");
    m_solderMaskExpansionSpin->setValue(m_currentStackup.solderMaskExpansion);
    maskPasteRow->addWidget(m_solderMaskExpansionSpin);
    maskPasteRow->addSpacing(20);
    maskPasteRow->addWidget(new QLabel("Board Paste Expansion:"));
    m_pasteExpansionSpin = new QDoubleSpinBox();
    m_pasteExpansionSpin->setRange(-1.0, 1.0);
    m_pasteExpansionSpin->setDecimals(3);
    m_pasteExpansionSpin->setSingleStep(0.01);
    m_pasteExpansionSpin->setSuffix(" mm");
    m_pasteExpansionSpin->setValue(m_currentStackup.pasteExpansion);
    maskPasteRow->addWidget(m_pasteExpansionSpin);
    maskPasteRow->addStretch();
    layout->addLayout(maskPasteRow);

    m_stackupTable = new QTableWidget();
    m_stackupTable->setColumnCount(6);
    m_stackupTable->setHorizontalHeaderLabels({"Layer Name", "Type", "Material", "Thickness (mm)", "Er", "Cu Weight (oz)"});
    m_stackupTable->horizontalHeader()->setStretchLastSection(true);
    m_stackupTable->verticalHeader()->setVisible(false);
    
    layout->addWidget(m_stackupTable);

    layout->addSpacing(12);
    QLabel* impHeader = new QLabel("Impedance Calculator");
    impHeader->setStyleSheet("font-weight: bold; color: #3b82f6;");
    layout->addWidget(impHeader);

    QGridLayout* impGrid = new QGridLayout();
    impGrid->addWidget(new QLabel("Mode:"), 0, 0);
    m_calcModeLabel = new QLabel("Top Microstrip");
    impGrid->addWidget(m_calcModeLabel, 0, 1);

    impGrid->addWidget(new QLabel("Target Z (Ohm):"), 1, 0);
    m_targetImpedanceSpin = new QDoubleSpinBox();
    m_targetImpedanceSpin->setRange(20.0, 200.0);
    m_targetImpedanceSpin->setValue(50.0);
    m_targetImpedanceSpin->setDecimals(2);
    impGrid->addWidget(m_targetImpedanceSpin, 1, 1);

    impGrid->addWidget(new QLabel("Dielectric Er:"), 2, 0);
    m_calcErSpin = new QDoubleSpinBox();
    m_calcErSpin->setRange(1.0, 20.0);
    m_calcErSpin->setValue(4.2);
    m_calcErSpin->setDecimals(3);
    impGrid->addWidget(m_calcErSpin, 2, 1);

    impGrid->addWidget(new QLabel("Dielectric H (mm):"), 3, 0);
    m_calcDielectricHeightSpin = new QDoubleSpinBox();
    m_calcDielectricHeightSpin->setRange(0.01, 10.0);
    m_calcDielectricHeightSpin->setValue(0.18);
    m_calcDielectricHeightSpin->setDecimals(4);
    impGrid->addWidget(m_calcDielectricHeightSpin, 3, 1);

    impGrid->addWidget(new QLabel("Copper T (mm):"), 4, 0);
    m_calcCopperThicknessSpin = new QDoubleSpinBox();
    m_calcCopperThicknessSpin->setRange(0.005, 0.3);
    m_calcCopperThicknessSpin->setValue(0.035);
    m_calcCopperThicknessSpin->setDecimals(4);
    impGrid->addWidget(m_calcCopperThicknessSpin, 4, 1);

    m_calcSuggestedWidthLabel = new QLabel("Suggested Width: -");
    m_calcSuggestedWidthLabel->setStyleSheet("font-weight: bold;");
    impGrid->addWidget(m_calcSuggestedWidthLabel, 5, 0, 1, 2);
    impGrid->setColumnStretch(2, 1);
    layout->addLayout(impGrid);

    connect(m_targetImpedanceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BoardSetupDialog::onImpedanceInputsChanged);
    connect(m_calcErSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BoardSetupDialog::onImpedanceInputsChanged);
    connect(m_calcDielectricHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BoardSetupDialog::onImpedanceInputsChanged);
    connect(m_calcCopperThicknessSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BoardSetupDialog::onImpedanceInputsChanged);

    return w;
}

void BoardSetupDialog::onLayerCountChanged(int count) {
    const QString previousFinish = m_currentStackup.surfaceFinish;
    const double previousMaskExpansion = m_currentStackup.solderMaskExpansion;
    const double previousPasteExpansion = m_currentStackup.pasteExpansion;
    PCBLayerManager::instance().updateStackupFromLayerCount(count); // Temp preview
    m_currentStackup = PCBLayerManager::instance().stackup();
    m_currentStackup.surfaceFinish = previousFinish.isEmpty() ? m_currentStackup.surfaceFinish : previousFinish;
    m_currentStackup.solderMaskExpansion = previousMaskExpansion;
    m_currentStackup.pasteExpansion = previousPasteExpansion;
    if (m_surfaceFinishCombo) {
        int idx = m_surfaceFinishCombo->findText(m_currentStackup.surfaceFinish);
        if (idx >= 0) m_surfaceFinishCombo->setCurrentIndex(idx);
    }
    if (m_solderMaskExpansionSpin) m_solderMaskExpansionSpin->setValue(m_currentStackup.solderMaskExpansion);
    if (m_pasteExpansionSpin) m_pasteExpansionSpin->setValue(m_currentStackup.pasteExpansion);
    updateImpedanceCalculatorDefaults();
    updateTable();
}

void BoardSetupDialog::updateTable() {
    m_stackupTable->setRowCount(m_currentStackup.stack.size());
    double totalThickness = 0.0;
    
    for (int i = 0; i < m_currentStackup.stack.size(); ++i) {
        const auto& layer = m_currentStackup.stack[i];
        
        m_stackupTable->setItem(i, 0, new QTableWidgetItem(layer.name));
        auto* typeCombo = new QComboBox();
        typeCombo->addItems({"Copper", "Dielectric", "Core", "Prepreg", "Soldermask", "Paste", "Silkscreen"});
        int tIdx = typeCombo->findText(layer.type);
        if (tIdx < 0) tIdx = 0;
        typeCombo->setCurrentIndex(tIdx);
        m_stackupTable->setCellWidget(i, 1, typeCombo);

        auto* materialCombo = new QComboBox();
        materialCombo->setEditable(true);
        materialCombo->addItems({"Copper", "FR-4", "Epoxy", "Rogers 4350B", "Polyimide"});
        int mIdx = materialCombo->findText(layer.material);
        if (mIdx >= 0) materialCombo->setCurrentIndex(mIdx);
        else materialCombo->setCurrentText(layer.material);
        m_stackupTable->setCellWidget(i, 2, materialCombo);
        
        QDoubleSpinBox* thickSpin = new QDoubleSpinBox();
        thickSpin->setRange(0.01, 10.0);
        thickSpin->setValue(layer.thickness);
        thickSpin->setSuffix(" mm");
        m_stackupTable->setCellWidget(i, 3, thickSpin);
        totalThickness += layer.thickness;
        
        QDoubleSpinBox* erSpin = new QDoubleSpinBox();
        erSpin->setRange(1.0, 20.0);
        erSpin->setValue(layer.dielectricConstant);
        m_stackupTable->setCellWidget(i, 4, erSpin);

        QDoubleSpinBox* cuWeightSpin = new QDoubleSpinBox();
        cuWeightSpin->setRange(0.0, 10.0);
        cuWeightSpin->setSingleStep(0.25);
        cuWeightSpin->setDecimals(2);
        cuWeightSpin->setValue(layer.copperWeightOz);
        cuWeightSpin->setSuffix(" oz");
        cuWeightSpin->setEnabled(layer.type == "Copper");
        m_stackupTable->setCellWidget(i, 5, cuWeightSpin);

        connect(typeCombo, &QComboBox::currentTextChanged, this, [cuWeightSpin](const QString& type) {
            cuWeightSpin->setEnabled(type == "Copper");
            if (type != "Copper") cuWeightSpin->setValue(0.0);
        });
    }
    
    m_stackupTable->resizeColumnsToContents();
    m_currentStackup.finishThickness = totalThickness;
    if (m_totalThicknessLabel) {
        m_totalThicknessLabel->setText(QString("Total: %1 mm").arg(totalThickness, 0, 'f', 3));
    }
    recomputeImpedance();
}

void BoardSetupDialog::onApply() {
    // Collect values from table
    for (int i = 0; i < m_currentStackup.stack.size(); ++i) {
        auto& layer = m_currentStackup.stack[i];
        layer.name = m_stackupTable->item(i, 0)->text();
        if (auto* typeCombo = qobject_cast<QComboBox*>(m_stackupTable->cellWidget(i, 1))) {
            layer.type = typeCombo->currentText();
        }
        if (auto* materialCombo = qobject_cast<QComboBox*>(m_stackupTable->cellWidget(i, 2))) {
            layer.material = materialCombo->currentText();
        }
        
        if (auto* thickSpin = qobject_cast<QDoubleSpinBox*>(m_stackupTable->cellWidget(i, 3))) {
            layer.thickness = thickSpin->value();
        }
        if (auto* erSpin = qobject_cast<QDoubleSpinBox*>(m_stackupTable->cellWidget(i, 4))) {
            layer.dielectricConstant = erSpin->value();
        }
        if (auto* cuWeightSpin = qobject_cast<QDoubleSpinBox*>(m_stackupTable->cellWidget(i, 5))) {
            layer.copperWeightOz = cuWeightSpin->value();
        }
    }
    if (m_surfaceFinishCombo) {
        m_currentStackup.surfaceFinish = m_surfaceFinishCombo->currentText();
    }
    if (m_solderMaskExpansionSpin) {
        m_currentStackup.solderMaskExpansion = m_solderMaskExpansionSpin->value();
    }
    if (m_pasteExpansionSpin) {
        m_currentStackup.pasteExpansion = m_pasteExpansionSpin->value();
    }
    double totalThickness = 0.0;
    for (const auto& layer : m_currentStackup.stack) totalThickness += layer.thickness;
    m_currentStackup.finishThickness = totalThickness;
    
    PCBLayerManager::instance().setCopperLayerCount(m_layerCountSpin->value());
    PCBLayerManager::instance().setStackup(m_currentStackup);
    
    // Save Net Classes
    for (int i = 0; i < m_netClassTable->rowCount(); ++i) {
        if (!m_netClassTable->item(i, 0)) continue;
        QString name = m_netClassTable->item(i, 0)->text();
        
        auto* wSB = qobject_cast<QDoubleSpinBox*>(m_netClassTable->cellWidget(i, 1));
        auto* cSB = qobject_cast<QDoubleSpinBox*>(m_netClassTable->cellWidget(i, 2));
        auto* dSB = qobject_cast<QDoubleSpinBox*>(m_netClassTable->cellWidget(i, 3));
        auto* hSB = qobject_cast<QDoubleSpinBox*>(m_netClassTable->cellWidget(i, 4));
        
        if (!wSB || !cSB || !dSB || !hSB) continue;

        double width = wSB->value();
        double clearance = cSB->value();
        double viaDiam = dSB->value();
        double viaDrill = hSB->value();
        
        NetClass nc(name, width, clearance, viaDiam, viaDrill);
        NetClassManager::instance().addClass(nc);
    }
    
    // Save Net Assignments
    for (int i = 0; i < m_netAssignmentTable->rowCount(); ++i) {
        QString netName = m_netAssignmentTable->item(i, 0)->text();
        if (auto* combo = qobject_cast<QComboBox*>(m_netAssignmentTable->cellWidget(i, 1))) {
            NetClassManager::instance().assignNetToClass(netName, combo->currentText());
        }
    }

    // Save custom clearance rules (Net/Class A, Net/Class B, Clearance)
    QList<ClearanceRule> rules;
    QMap<QString, double> seenRuleClearance;
    QStringList duplicateConflicts;
    for (int i = 0; i < m_clearanceRulesTable->rowCount(); ++i) {
        if (!m_clearanceRulesTable->item(i, 0) || !m_clearanceRulesTable->item(i, 1)) continue;
        QString lhs = m_clearanceRulesTable->item(i, 0)->text().trimmed();
        QString rhs = m_clearanceRulesTable->item(i, 1)->text().trimmed();
        auto* cSB = qobject_cast<QDoubleSpinBox*>(m_clearanceRulesTable->cellWidget(i, 2));
        if (!cSB || lhs.isEmpty() || rhs.isEmpty()) continue;

        // Canonicalize ANY and build order-independent key for duplicate detection.
        if (lhs.compare("ANY", Qt::CaseInsensitive) == 0) lhs = "ANY";
        if (rhs.compare("ANY", Qt::CaseInsensitive) == 0) rhs = "ANY";
        const QString k1 = (lhs < rhs) ? lhs : rhs;
        const QString k2 = (lhs < rhs) ? rhs : lhs;
        const QString key = k1 + "|" + k2;
        const double clearance = cSB->value();
        if (seenRuleClearance.contains(key)) {
            const double prev = seenRuleClearance.value(key);
            if (!qFuzzyCompare(prev + 1.0, clearance + 1.0)) {
                duplicateConflicts.append(
                    QString("Rule '%1 ↔ %2' has conflicting clearances: %3mm and %4mm")
                        .arg(k1).arg(k2).arg(prev).arg(clearance));
            }
            // Same pair repeated; keep first and skip duplicates.
            continue;
        }
        seenRuleClearance.insert(key, clearance);

        ClearanceRule r;
        r.lhs = lhs;
        r.rhs = rhs;
        r.clearance = clearance;
        rules.append(r);
    }

    if (!duplicateConflicts.isEmpty()) {
        QMessageBox::warning(
            this,
            "Invalid Clearance Rules",
            QString("Please resolve conflicting duplicate rules before Apply:\n\n%1")
                .arg(duplicateConflicts.join("\n")));
        return;
    }

    NetClassManager::instance().setClearanceRules(rules);

    accept();
}

QWidget* BoardSetupDialog::createDesignRulesTab() {
    QWidget* w = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(w);

    QLabel* classLabel = new QLabel("Net Classes");
    classLabel->setStyleSheet("font-weight: bold; color: #3b82f6;");
    layout->addWidget(classLabel);

    m_netClassTable = new QTableWidget();
    m_netClassTable->setColumnCount(5);
    m_netClassTable->setHorizontalHeaderLabels({"Class Name", "Width (mm)", "Clearance", "Via Diam", "Via Drill"});
    m_netClassTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_netClassTable);

    QHBoxLayout* classBtns = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton("Add Class");
    QPushButton* removeBtn = new QPushButton("Remove Class");
    connect(addBtn, &QPushButton::clicked, this, &BoardSetupDialog::onAddNetClass);
    connect(removeBtn, &QPushButton::clicked, this, &BoardSetupDialog::onRemoveNetClass);
    classBtns->addWidget(addBtn);
    classBtns->addWidget(removeBtn);
    classBtns->addStretch();
    layout->addLayout(classBtns);

    layout->addSpacing(20);

    QLabel* assignLabel = new QLabel("Net Assignments");
    assignLabel->setStyleSheet("font-weight: bold; color: #3b82f6;");
    layout->addWidget(assignLabel);

    m_netAssignmentTable = new QTableWidget();
    m_netAssignmentTable->setColumnCount(2);
    m_netAssignmentTable->setHorizontalHeaderLabels({"Net Name", "Net Class"});
    m_netAssignmentTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_netAssignmentTable);

    layout->addSpacing(16);
    QLabel* clearanceRulesLabel = new QLabel("Custom Clearance Rules");
    clearanceRulesLabel->setStyleSheet("font-weight: bold; color: #3b82f6;");
    layout->addWidget(clearanceRulesLabel);

    m_clearanceRulesTable = new QTableWidget();
    m_clearanceRulesTable->setColumnCount(3);
    m_clearanceRulesTable->setHorizontalHeaderLabels({"Net/Class A", "Net/Class B", "Clearance (mm)"});
    m_clearanceRulesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_clearanceRulesTable);
    connect(m_clearanceRulesTable, &QTableWidget::itemChanged, this, &BoardSetupDialog::validateClearanceRulesUI);

    QHBoxLayout* ruleBtns = new QHBoxLayout();
    QPushButton* addRuleBtn = new QPushButton("Add Rule");
    QPushButton* removeRuleBtn = new QPushButton("Remove Rule");
    connect(addRuleBtn, &QPushButton::clicked, this, &BoardSetupDialog::onAddClearanceRule);
    connect(removeRuleBtn, &QPushButton::clicked, this, &BoardSetupDialog::onRemoveClearanceRule);
    ruleBtns->addWidget(addRuleBtn);
    ruleBtns->addWidget(removeRuleBtn);
    ruleBtns->addStretch();
    layout->addLayout(ruleBtns);

    updateNetClassTable();
    updateNetAssignmentTable();
    updateClearanceRulesTable();
    validateClearanceRulesUI();

    return w;
}

void BoardSetupDialog::updateNetClassTable() {
    QList<NetClass> classes = NetClassManager::instance().classes();
    m_netClassTable->setRowCount(classes.size());
    
    for (int i = 0; i < classes.size(); ++i) {
        const auto& nc = classes[i];
        m_netClassTable->setItem(i, 0, new QTableWidgetItem(nc.name));
        if (nc.name == "Default") m_netClassTable->item(i, 0)->setFlags(m_netClassTable->item(i, 0)->flags() & ~Qt::ItemIsEditable);

        auto addSpin = [&](int col, double val) {
            QDoubleSpinBox* sb = new QDoubleSpinBox();
            sb->setRange(0.01, 10.0);
            sb->setSingleStep(0.05);
            sb->setValue(val);
            m_netClassTable->setCellWidget(i, col, sb);
        };

        addSpin(1, nc.traceWidth);
        addSpin(2, nc.clearance);
        addSpin(3, nc.viaDiameter);
        addSpin(4, nc.viaDrill);
    }
}

void BoardSetupDialog::updateNetAssignmentTable() {
    // Get all net names from the active board (via ratsnest manager)
    QStringList nets = PCBRatsnestManager::instance().netNames();
    nets.sort();
    
    m_netAssignmentTable->setRowCount(nets.size()); 
    for (int i = 0; i < nets.size(); ++i) {
        QString netName = nets[i];
        m_netAssignmentTable->setItem(i, 0, new QTableWidgetItem(netName));
        m_netAssignmentTable->item(i, 0)->setFlags(m_netAssignmentTable->item(i, 0)->flags() & ~Qt::ItemIsEditable);
        
        QComboBox* classCombo = new QComboBox();
        QStringList classes;
        for (const auto& nc : NetClassManager::instance().classes()) {
            classes.append(nc.name);
        }
        classCombo->addItems(classes);
        
        QString currentClass = NetClassManager::instance().getClassName(netName);
        classCombo->setCurrentText(currentClass);
        
        m_netAssignmentTable->setCellWidget(i, 1, classCombo);
    }
}

void BoardSetupDialog::onAddNetClass() {
    int row = m_netClassTable->rowCount();
    m_netClassTable->insertRow(row);
    m_netClassTable->setItem(row, 0, new QTableWidgetItem("NewClass"));
    
    auto addSpin = [&](int col, double val) {
        QDoubleSpinBox* sb = new QDoubleSpinBox();
        sb->setRange(0.01, 10.0);
        sb->setValue(val);
        m_netClassTable->setCellWidget(row, col, sb);
    };
    addSpin(1, 0.25);
    addSpin(2, 0.2);
    addSpin(3, 0.6);
    addSpin(4, 0.3);
}

void BoardSetupDialog::onRemoveNetClass() {
    int row = m_netClassTable->currentRow();
    if (row >= 0) {
        QString name = m_netClassTable->item(row, 0)->text();
        if (name != "Default") {
            m_netClassTable->removeRow(row);
        }
    }
}

void BoardSetupDialog::updateClearanceRulesTable() {
    const QList<ClearanceRule> rules = NetClassManager::instance().clearanceRules();
    m_clearanceRulesTable->setRowCount(rules.size());

    for (int i = 0; i < rules.size(); ++i) {
        const ClearanceRule& r = rules[i];
        m_clearanceRulesTable->setItem(i, 0, new QTableWidgetItem(r.lhs));
        m_clearanceRulesTable->setItem(i, 1, new QTableWidgetItem(r.rhs));

        QDoubleSpinBox* sb = new QDoubleSpinBox();
        sb->setRange(0.01, 50.0);
        sb->setSingleStep(0.05);
        sb->setValue(r.clearance);
        sb->setSuffix(" mm");
        m_clearanceRulesTable->setCellWidget(i, 2, sb);
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BoardSetupDialog::validateClearanceRulesUI);
    }
}

void BoardSetupDialog::onAddClearanceRule() {
    int row = m_clearanceRulesTable->rowCount();
    m_clearanceRulesTable->insertRow(row);
    m_clearanceRulesTable->setItem(row, 0, new QTableWidgetItem("ANY"));
    m_clearanceRulesTable->setItem(row, 1, new QTableWidgetItem("ANY"));

    QDoubleSpinBox* sb = new QDoubleSpinBox();
    sb->setRange(0.01, 50.0);
    sb->setSingleStep(0.05);
    sb->setValue(0.2);
    sb->setSuffix(" mm");
    m_clearanceRulesTable->setCellWidget(row, 2, sb);
    connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BoardSetupDialog::validateClearanceRulesUI);
    validateClearanceRulesUI();
}

void BoardSetupDialog::onRemoveClearanceRule() {
    int row = m_clearanceRulesTable->currentRow();
    if (row >= 0) {
        m_clearanceRulesTable->removeRow(row);
    }
    validateClearanceRulesUI();
}

void BoardSetupDialog::validateClearanceRulesUI() {
    if (!m_clearanceRulesTable) return;
    QSignalBlocker blocker(m_clearanceRulesTable);

    // Reset row visuals.
    for (int i = 0; i < m_clearanceRulesTable->rowCount(); ++i) {
        for (int col = 0; col < 2; ++col) {
            if (QTableWidgetItem* it = m_clearanceRulesTable->item(i, col)) {
                it->setBackground(QBrush(Qt::NoBrush));
                it->setToolTip(QString());
            }
        }
        if (auto* sb = qobject_cast<QDoubleSpinBox*>(m_clearanceRulesTable->cellWidget(i, 2))) {
            sb->setStyleSheet("");
            sb->setToolTip(QString());
        }
    }

    // Detect conflicting duplicate rules.
    struct Entry { int row; double clearance; };
    QMap<QString, QList<Entry>> byKey;

    for (int i = 0; i < m_clearanceRulesTable->rowCount(); ++i) {
        if (!m_clearanceRulesTable->item(i, 0) || !m_clearanceRulesTable->item(i, 1)) continue;
        QString lhs = m_clearanceRulesTable->item(i, 0)->text().trimmed();
        QString rhs = m_clearanceRulesTable->item(i, 1)->text().trimmed();
        auto* cSB = qobject_cast<QDoubleSpinBox*>(m_clearanceRulesTable->cellWidget(i, 2));
        if (!cSB || lhs.isEmpty() || rhs.isEmpty()) continue;

        if (lhs.compare("ANY", Qt::CaseInsensitive) == 0) lhs = "ANY";
        if (rhs.compare("ANY", Qt::CaseInsensitive) == 0) rhs = "ANY";
        const QString k1 = (lhs < rhs) ? lhs : rhs;
        const QString k2 = (lhs < rhs) ? rhs : lhs;
        const QString key = k1 + "|" + k2;
        byKey[key].append({i, cSB->value()});
    }

    const QColor warnBg(110, 45, 45);
    for (auto it = byKey.begin(); it != byKey.end(); ++it) {
        const QList<Entry>& entries = it.value();
        if (entries.size() < 2) continue;

        bool conflict = false;
        const double base = entries.first().clearance;
        for (const Entry& e : entries) {
            if (!qFuzzyCompare(base + 1.0, e.clearance + 1.0)) {
                conflict = true;
                break;
            }
        }
        if (!conflict) continue;

        QString keyLabel = it.key();
        keyLabel.replace("|", " ↔ ");
        const QString tip = QString("Conflicting duplicate rule for '%1'").arg(keyLabel);
        for (const Entry& e : entries) {
            for (int col = 0; col < 2; ++col) {
                if (QTableWidgetItem* cell = m_clearanceRulesTable->item(e.row, col)) {
                    cell->setBackground(warnBg);
                    cell->setToolTip(tip);
                }
            }
            if (auto* sb = qobject_cast<QDoubleSpinBox*>(m_clearanceRulesTable->cellWidget(e.row, 2))) {
                sb->setStyleSheet("QDoubleSpinBox { border: 1px solid #ef4444; }");
                sb->setToolTip(tip);
            }
        }
    }
}

void BoardSetupDialog::updateImpedanceCalculatorDefaults() {
    if (!m_calcErSpin || !m_calcDielectricHeightSpin || !m_calcCopperThicknessSpin) return;
    if (m_currentStackup.stack.isEmpty()) return;

    // Find top copper and nearest dielectric below it.
    int topCuIndex = -1;
    for (int i = 0; i < m_currentStackup.stack.size(); ++i) {
        const auto& l = m_currentStackup.stack[i];
        if (l.layerId == PCBLayerManager::TopCopper && l.type.compare("Copper", Qt::CaseInsensitive) == 0) {
            topCuIndex = i;
            break;
        }
    }
    if (topCuIndex < 0) return;

    const auto& topCu = m_currentStackup.stack[topCuIndex];
    m_calcCopperThicknessSpin->setValue(topCu.thickness);

    for (int i = topCuIndex + 1; i < m_currentStackup.stack.size(); ++i) {
        const auto& l = m_currentStackup.stack[i];
        const QString type = l.type.toLower();
        if (type == "dielectric" || type == "core" || type == "prepreg") {
            m_calcDielectricHeightSpin->setValue(l.thickness);
            m_calcErSpin->setValue(l.dielectricConstant > 0.0 ? l.dielectricConstant : 4.2);
            break;
        }
    }
    recomputeImpedance();
}

void BoardSetupDialog::onImpedanceInputsChanged() {
    recomputeImpedance();
}

void BoardSetupDialog::recomputeImpedance() {
    if (!m_calcSuggestedWidthLabel || !m_targetImpedanceSpin || !m_calcDielectricHeightSpin || !m_calcErSpin) return;
    const double zTarget = m_targetImpedanceSpin->value();
    const double h = m_calcDielectricHeightSpin->value();
    const double er = m_calcErSpin->value();

    const double t = m_calcCopperThicknessSpin ? m_calcCopperThicknessSpin->value() : 0.035;
    const double width = solveMicrostripWidthForImpedance(zTarget, h, er, t);
    const double checkZ = microstripImpedance(width, h, er, t);
    m_calcSuggestedWidthLabel->setText(
        QString("Suggested Width: %1 mm  (calc Z0≈%2 Ohm)")
            .arg(width, 0, 'f', 3)
            .arg(checkZ, 0, 'f', 2));
}
