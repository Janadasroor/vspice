#include "netlist_import_dialog.h"
#include "../../footprints/footprint_library.h"
#include "sync_manager.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QFontDatabase>
#include <QStackedWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>

NetlistImportDialog::NetlistImportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Import Netlist to PCB");
    setMinimumSize(900, 650);
    resize(950, 700);

    setupUI();
    applyTheme();
    updateButtonStates();
}

NetlistImportDialog::~NetlistImportDialog() = default;

ECOPackage NetlistImportDialog::resultECOPackage() const {
    return m_resultECO;
}

// ============================================================================
// UI Setup
// ============================================================================

void NetlistImportDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Header
    m_stepLabel = new QLabel("Step 1 of 4: Select Netlist Source");
    m_stepLabel->setStyleSheet("font-size: 16px; font-weight: bold; padding: 8px;");
    mainLayout->addWidget(m_stepLabel);

    // Stacked widget for wizard steps
    m_stackedWidget = new QStackedWidget();
    m_stackedWidget->addWidget(createStepSource());
    m_stackedWidget->addWidget(createStepComponents());
    m_stackedWidget->addWidget(createStepNets());
    m_stackedWidget->addWidget(createStepValidation());
    mainLayout->addWidget(m_stackedWidget, 1);

    // Button bar
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_backBtn = new QPushButton("← Back");
    m_nextBtn = new QPushButton("Next →");
    m_cancelBtn = new QPushButton("Cancel");
    m_importBtn = new QPushButton("📥 Import to PCB");
    m_importBtn->setObjectName("primaryButton");

    buttonLayout->addWidget(m_backBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_nextBtn);
    buttonLayout->addWidget(m_importBtn);

    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_backBtn, &QPushButton::clicked, this, &NetlistImportDialog::onBack);
    connect(m_nextBtn, &QPushButton::clicked, this, &NetlistImportDialog::onNext);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_importBtn, &QPushButton::clicked, this, &NetlistImportDialog::onImport);
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NetlistImportDialog::onSourceChanged);
}

QWidget* NetlistImportDialog::createStepSource() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    QGroupBox* sourceGroup = new QGroupBox("Netlist Source");
    QGridLayout* grid = new QGridLayout(sourceGroup);

    grid->addWidget(new QLabel("Source:"), 0, 0);
    m_sourceCombo = new QComboBox();
    m_sourceCombo->addItem("📁 Load from File");
    m_sourceCombo->addItem("📐 From Open Schematic");
    grid->addWidget(m_sourceCombo, 0, 1);

    grid->addWidget(new QLabel("File:"), 1, 0);
    m_filePathEdit = new QLineEdit();
    m_filePathEdit->setPlaceholderText("Select a netlist file (.json, .net, .cir, .sp)");
    grid->addWidget(m_filePathEdit, 1, 1);

    m_browseBtn = new QPushButton("Browse...");
    grid->addWidget(m_browseBtn, 1, 2);
    connect(m_browseBtn, &QPushButton::clicked, this, &NetlistImportDialog::onBrowseFile);

    grid->addWidget(new QLabel("Format:"), 2, 0);
    m_formatCombo = new QComboBox();
    m_formatCombo->addItem("Auto-Detect");
    m_formatCombo->addItem("FluxJSON (VioSpice)");
    m_formatCombo->addItem("Protel Netlist");
    m_formatCombo->addItem("SPICE Circuit");
    grid->addWidget(m_formatCombo, 2, 1);

    m_sourceSummaryLabel = new QLabel("Select a netlist file to import.");
    m_sourceSummaryLabel->setWordWrap(true);
    m_sourceSummaryLabel->setStyleSheet("padding: 8px; background: palette(alternate-base); border-radius: 4px;");
    layout->addWidget(sourceGroup);
    layout->addWidget(m_sourceSummaryLabel);
    layout->addStretch();

    return page;
}

QWidget* NetlistImportDialog::createStepComponents() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    // Component table
    m_componentTable = new QTableWidget();
    m_componentTable->setColumnCount(5);
    m_componentTable->setHorizontalHeaderLabels({"Reference", "Value", "Type", "Pin Count", "Footprint"});
    m_componentTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_componentTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_componentTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_componentTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_componentTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_componentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_componentTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(m_componentTable);

    // Toolbar
    QHBoxLayout* toolbar = new QHBoxLayout();
    m_autoSuggestBtn = new QPushButton("💡 Auto-Suggest Footprints");
    m_autoSuggestBtn->setToolTip("Automatically assign footprints based on component type and value");
    toolbar->addWidget(m_autoSuggestBtn);
    toolbar->addStretch();
    m_componentSummaryLabel = new QLabel("0 components");
    toolbar->addWidget(m_componentSummaryLabel);
    layout->addLayout(toolbar);

    connect(m_autoSuggestBtn, &QPushButton::clicked, this, &NetlistImportDialog::onAutoSuggestFootprints);
    connect(m_componentTable, &QTableWidget::cellChanged, this, &NetlistImportDialog::onFootprintCellChanged);

    return page;
}

QWidget* NetlistImportDialog::createStepNets() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    m_netTable = new QTableWidget();
    m_netTable->setColumnCount(3);
    m_netTable->setHorizontalHeaderLabels({"Net Name", "Pins", "Connections"});
    m_netTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_netTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_netTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_netTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_netTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_netTable);

    m_netSummaryLabel = new QLabel("0 nets");
    m_netSummaryLabel->setStyleSheet("padding: 4px;");
    layout->addWidget(m_netSummaryLabel);

    return page;
}

QWidget* NetlistImportDialog::createStepValidation() {
    QWidget* page = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(page);

    m_validationText = new QTextEdit();
    m_validationText->setReadOnly(true);
    m_validationText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_validationText);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("font-weight: bold; padding: 8px;");
    layout->addWidget(m_statusLabel);

    return page;
}

// ============================================================================
// Navigation
// ============================================================================

void NetlistImportDialog::onNext() {
    if (m_currentStep == 0) {
        // Load netlist
        if (m_sourceCombo->currentIndex() == 0) {
            QString filePath = m_filePathEdit->text().trimmed();
            if (filePath.isEmpty()) {
                QMessageBox::warning(this, "No File Selected", "Please select a netlist file to import.");
                return;
            }
            if (!QFileInfo::exists(filePath)) {
                QMessageBox::warning(this, "File Not Found", "The specified file does not exist:\n" + filePath);
                return;
            }
            loadNetlistFile(filePath);
        } else {
            loadFromSchematic();
        }

        if (m_importPkg.components.isEmpty() && m_importPkg.nets.isEmpty()) {
            QMessageBox::warning(this, "Import Failed", "Failed to parse the netlist. Check the file format and content.");
            return;
        }

        populateComponentTable();
        populateNetTable();
    }

    if (m_currentStep == 2) {
        updateValidationStatus();
    }

    m_currentStep++;
    if (m_currentStep >= m_totalSteps) m_currentStep = m_totalSteps - 1;

    m_stackedWidget->setCurrentIndex(m_currentStep);
    updateButtonStates();
}

void NetlistImportDialog::onBack() {
    m_currentStep--;
    if (m_currentStep < 0) m_currentStep = 0;

    m_stackedWidget->setCurrentIndex(m_currentStep);
    updateButtonStates();
}

void NetlistImportDialog::updateButtonStates() {
    m_backBtn->setEnabled(m_currentStep > 0);
    m_nextBtn->setVisible(m_currentStep < m_totalSteps - 1);
    m_importBtn->setVisible(m_currentStep == m_totalSteps - 1);

    switch (m_currentStep) {
        case 0: m_stepLabel->setText("Step 1 of 4: Select Netlist Source"); break;
        case 1: m_stepLabel->setText("Step 2 of 4: Review & Assign Footprints"); break;
        case 2: m_stepLabel->setText("Step 3 of 4: Review Net Connectivity"); break;
        case 3: m_stepLabel->setText("Step 4 of 4: Validate & Import"); break;
    }
}

// ============================================================================
// Step 1: Source loading
// ============================================================================

void NetlistImportDialog::onSourceChanged(int index) {
    bool isFile = (index == 0);
    m_filePathEdit->setEnabled(isFile);
    m_browseBtn->setEnabled(isFile);
    m_formatCombo->setEnabled(isFile);
}

void NetlistImportDialog::onBrowseFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "Select Netlist File", "",
        "Netlist Files (*.json *.net *.cir *.sp *.txt);;JSON Netlist (*.json);;Protel Netlist (*.net);;SPICE Circuit (*.cir *.sp);;All Files (*)");
    if (!filePath.isEmpty()) {
        m_filePathEdit->setText(filePath);
    }
}

void NetlistImportDialog::loadNetlistFile(const QString& filePath) {
    PCBNetlistImporter::Format fmt = PCBNetlistImporter::AutoDetect;
    switch (m_formatCombo->currentIndex()) {
        case 1: fmt = PCBNetlistImporter::FluxJSON; break;
        case 2: fmt = PCBNetlistImporter::Protel; break;
        case 3: fmt = PCBNetlistImporter::SPICE; break;
        default: break;
    }

    m_importPkg = PCBNetlistImporter::loadFromFile(filePath, fmt);

    if (m_importPkg.components.isEmpty() && m_importPkg.nets.isEmpty()) {
        m_sourceSummaryLabel->setText("❌ Failed to parse netlist file.");
        m_sourceSummaryLabel->setStyleSheet("padding: 8px; background: palette(alternate-base); color: palette(error); border-radius: 4px;");
    } else {
        m_sourceSummaryLabel->setText("✅ Loaded: " + PCBNetlistImporter::summary(m_importPkg));
        m_sourceSummaryLabel->setStyleSheet("padding: 8px; background: palette(alternate-base); border-radius: 4px;");
    }
}

void NetlistImportDialog::loadFromSchematic() {
    // TODO: Integrate with schematic editor to pull the ECOPackage via SyncManager
    // For now, show a placeholder message
    m_sourceSummaryLabel->setText("ℹ️ Schematic integration requires an open schematic project.\n"
                                   "Use 'File → Import Netlist' from the schematic editor, or load a netlist file.");
    m_sourceSummaryLabel->setStyleSheet("padding: 8px; background: palette(alternate-base); color: palette(text); border-radius: 4px;");
}

// ============================================================================
// Step 2: Component table
// ============================================================================

void NetlistImportDialog::populateComponentTable() {
    m_componentTable->setRowCount(m_importPkg.components.size());

    for (int row = 0; row < m_importPkg.components.size(); ++row) {
        const auto& comp = m_importPkg.components[row];

        auto* refItem = new QTableWidgetItem(comp.reference);
        refItem->setFlags(refItem->flags() & ~Qt::ItemIsEditable);
        m_componentTable->setItem(row, 0, refItem);

        auto* valItem = new QTableWidgetItem(comp.value);
        valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
        m_componentTable->setItem(row, 1, valItem);

        auto* typeItem = new QTableWidgetItem(comp.typeName);
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_componentTable->setItem(row, 2, typeItem);

        auto* pinItem = new QTableWidgetItem(QString::number(comp.pinCount));
        pinItem->setFlags(pinItem->flags() & ~Qt::ItemIsEditable);
        pinItem->setTextAlignment(Qt::AlignCenter);
        m_componentTable->setItem(row, 3, pinItem);

        auto* fpItem = new QTableWidgetItem(comp.footprint);
        if (comp.footprint.isEmpty()) {
            fpItem->setForeground(Qt::red);
            fpItem->setText("⚠ Not assigned");
        }
        m_componentTable->setItem(row, 4, fpItem);
    }

    m_componentSummaryLabel->setText(
        QString("%1 components | %2 missing footprints")
            .arg(m_importPkg.components.size())
            .arg(std::count_if(m_importPkg.components.begin(), m_importPkg.components.end(),
                [](const NetlistImportComponent& c) { return c.footprint.isEmpty(); }))
    );
}

void NetlistImportDialog::onAutoSuggestFootprints() {
    QStringList footprints = getAvailableFootprints();
    if (footprints.isEmpty()) {
        QMessageBox::information(this, "No Footprints Available",
            "No footprints found in the library. Please add footprints first.");
        return;
    }

    PCBNetlistImporter::suggestFootprints(m_importPkg, footprints);
    populateComponentTable();
    m_componentSummaryLabel->setText(m_componentSummaryLabel->text() + " | 💡 Auto-suggested");
}

void NetlistImportDialog::onFootprintCellChanged(int row, int col) {
    if (col == 4 && row < m_importPkg.components.size()) { // Footprint column
        QTableWidgetItem* item = m_componentTable->item(row, col);
        if (item) {
            m_importPkg.components[row].footprint = item->text().trimmed();
            if (m_importPkg.components[row].footprint.isEmpty()) {
                item->setForeground(Qt::red);
            } else {
                item->setForeground(Qt::green);
            }
        }
    }
}

QStringList NetlistImportDialog::getAvailableFootprints() {
    QStringList footprints;
    auto& libMgr = FootprintLibraryManager::instance();
    for (auto* lib : libMgr.libraries()) {
        footprints.append(lib->getFootprintNames());
    }
    return footprints;
}

// ============================================================================
// Step 3: Net table
// ============================================================================

void NetlistImportDialog::populateNetTable() {
    m_netTable->setRowCount(m_importPkg.nets.size());

    for (int row = 0; row < m_importPkg.nets.size(); ++row) {
        const auto& net = m_importPkg.nets[row];

        auto* nameItem = new QTableWidgetItem(net.name);
        m_netTable->setItem(row, 0, nameItem);

        // Pin list
        QStringList pinStrs;
        for (const auto& pin : net.pins) {
            pinStrs.append(QString("%1:%2").arg(pin.componentRef, pin.pinName));
        }
        auto* pinsItem = new QTableWidgetItem(pinStrs.join(", "));
        pinsItem->setToolTip(pinStrs.join("\n"));
        m_netTable->setItem(row, 1, pinsItem);

        auto* connItem = new QTableWidgetItem(QString::number(net.pins.size()));
        connItem->setTextAlignment(Qt::AlignCenter);
        m_netTable->setItem(row, 2, connItem);
    }

    m_netSummaryLabel->setText(
        QString("%1 nets | %2 total connections")
            .arg(m_importPkg.nets.size())
            .arg(std::accumulate(m_importPkg.nets.begin(), m_importPkg.nets.end(), 0,
                [](int sum, const NetlistImportNet& n) { return sum + n.pins.size(); }))
    );
}

// ============================================================================
// Step 4: Validation & Import
// ============================================================================

void NetlistImportDialog::updateValidationStatus() {
    QStringList issues = PCBNetlistImporter::validate(m_importPkg);

    QString html;
    int errors = 0, warnings = 0;

    for (const QString& issue : issues) {
        if (issue.startsWith("Error:")) {
            html += QString("<p style='color: #ff6b6b;'>❌ %1</p>").arg(issue.mid(7).trimmed());
            errors++;
        } else {
            html += QString("<p style='color: #ffd93d;'>⚠️ %1</p>").arg(issue.mid(9).trimmed());
            warnings++;
        }
    }

    if (errors == 0 && warnings == 0) {
        html = "<p style='color: #51cf66;'>✅ No issues found. Ready to import.</p>";
    }

    html += "<hr>";
    html += QString("<p><b>Summary:</b></p><pre>%1</pre>").arg(PCBNetlistImporter::summary(m_importPkg));

    m_validationText->setHtml(html);

    if (errors == 0) {
        m_statusLabel->setText("✅ Ready to import");
        m_statusLabel->setStyleSheet("font-weight: bold; padding: 8px; color: #51cf66;");
        m_importBtn->setEnabled(true);
    } else {
        m_statusLabel->setText(QString("❌ %1 error(s) must be resolved before importing").arg(errors));
        m_statusLabel->setStyleSheet("font-weight: bold; padding: 8px; color: #ff6b6b;");
        m_importBtn->setEnabled(false);
    }
}

void NetlistImportDialog::onImport() {
    // Convert to ECOPackage
    m_resultECO = PCBNetlistImporter::toECOPackage(m_importPkg);
    m_importReady = true;

    // Push to SyncManager for PCB to pick up
    SyncManager::instance().pushECO(m_resultECO, SyncManager::ECOTarget::PCB);

    emit importRequested(m_resultECO);

    QMessageBox::information(this, "Import Successful",
        QString("Netlist imported successfully!\n\n"
                "%1 components\n"
                "%2 nets\n\n"
                "The components and nets have been added to the PCB board.")
            .arg(m_importPkg.components.size())
            .arg(m_importPkg.nets.size()));

    accept();
}

// ============================================================================
// Theme
// ============================================================================

void NetlistImportDialog::applyTheme() {
    auto theme = ThemeManager::theme();
    // ThemeManager applies stylesheets globally, so we just set object names for CSS targeting
    setObjectName("NetlistImportDialog");
    m_componentTable->setObjectName("importComponentTable");
    m_netTable->setObjectName("importNetTable");
    m_validationText->setObjectName("importValidationText");
}
