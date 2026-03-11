#include "spice_model_architect.h"
#include "../core/theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QClipboard>
#include <QGroupBox>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QComboBox>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QJsonValue>

SpiceModelArchitect::SpiceModelArchitect(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("SPICE Model Architect");
    resize(850, 650);

    // Initialize Device Definitions
    m_deviceDefinitions["Diode (D)"] = {
        {"IS", "1e-14", "Saturation current", "A"},
        {"RS", "0", "Ohmic resistance", "Ω"},
        {"N", "1", "Emission coefficient", ""},
        {"CJO", "0", "Zero-bias junction capacitance", "F"},
        {"VJ", "1", "Junction potential", "V"},
        {"M", "0.5", "Grading coefficient", ""},
        {"TT", "0", "Transit time", "s"},
        {"BV", "inf", "Reverse breakdown voltage", "V"},
        {"IBV", "1e-10", "Current at breakdown voltage", "A"}
    };

    m_deviceDefinitions["NPN BJT (NPN)"] = {
        {"IS", "1e-16", "Transport saturation current", "A"},
        {"BF", "100", "Ideal maximum forward beta", ""},
        {"NF", "1", "Forward current emission coefficient", ""},
        {"VAF", "inf", "Forward Early voltage", "V"},
        {"IKF", "inf", "Corner for forward beta high current roll-off", "A"},
        {"RB", "0", "Zero bias base resistance", "Ω"},
        {"RE", "0", "Emitter resistance", "Ω"},
        {"RC", "0", "Collector resistance", "Ω"},
        {"CJE", "0", "Zero bias B-E junction capacitance", "F"},
        {"CJC", "0", "Zero bias B-C junction capacitance", "F"}
    };

    m_deviceDefinitions["Power MOSFET (VDMOS)"] = {
        {"VTO", "0", "Threshold voltage", "V"},
        {"KP", "2e-5", "Transconductance parameter", "A/V²"},
        {"RG", "0", "Gate resistance", "Ω"},
        {"RD", "0", "Drain resistance", "Ω"},
        {"RS", "0", "Source resistance", "Ω"},
        {"LAMBDA", "0", "Channel-length modulation", "1/V"},
        {"MTRIODE", "1", "Triode region modulation", ""},
        {"KSUBTHRES", "0", "Subthreshold factor", ""},
        {"CGDMAX", "0", "Maximum gate-drain capacitance", "F"},
        {"CGDMIN", "0", "Minimum gate-drain capacitance", "F"},
        {"CGS", "0", "Gate-source capacitance", "F"},
        {"CJO", "0", "Body diode zero-bias capacitance", "F"},
        {"IS", "1e-14", "Body diode saturation current", "A"},
        {"RB", "0", "Body diode resistance", "Ω"},
        {"Ron", "0", "On-resistance", "Ω"},
        {"Qg", "0", "Total gate charge", "C"}
    };

    m_deviceDefinitions["NMOS (NMOS)"] = {
        {"VTO", "0", "Threshold voltage", "V"},
        {"KP", "2e-5", "Transconductance parameter", "A/V²"},
        {"GAMMA", "0", "Bulk threshold parameter", "V^½"},
        {"PHI", "0.6", "Surface potential", "V"},
        {"LAMBDA", "0", "Channel-length modulation", "1/V"},
        {"RD", "0", "Drain resistance", "Ω"},
        {"RS", "0", "Source resistance", "Ω"},
        {"CGSO", "0", "Gate-source overlap capacitance", "F/m"},
        {"CGDO", "0", "Gate-drain overlap capacitance", "F/m"}
    };

    setupUi();
    onTypeChanged(0);
    applyTheme();
}

SpiceModelArchitect::~SpiceModelArchitect() {}

void SpiceModelArchitect::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // ─── Top Section: Type and Name ─────────────────────────────────────────
    QHBoxLayout* topLayout = new QHBoxLayout();
    
    QVBoxLayout* typeCol = new QVBoxLayout();
    typeCol->addWidget(new QLabel("Device Category:"));
    m_typeCombo = new QComboBox();
    m_typeCombo->addItems(m_deviceDefinitions.keys());
    m_typeCombo->setFixedHeight(32);
    typeCol->addWidget(m_typeCombo);
    
    QVBoxLayout* nameCol = new QVBoxLayout();
    nameCol->addWidget(new QLabel("Model Name (Reference):"));
    m_nameEdit = new QLineEdit("NEW_MODEL");
    m_nameEdit->setFixedHeight(32);
    nameCol->addWidget(m_nameEdit);

    topLayout->addLayout(typeCol, 1);
    topLayout->addLayout(nameCol, 1);
    mainLayout->addLayout(topLayout);

    // ─── Middle Section: Parameter Table ─────────────────────────────────────
    m_paramTable = new QTableWidget(0, 4);
    m_paramTable->setHorizontalHeaderLabels({"Parameter", "Value", "Unit", "Description"});
    m_paramTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_paramTable->horizontalHeader()->setStretchLastSection(true);
    m_paramTable->setColumnWidth(0, 120);
    m_paramTable->setColumnWidth(1, 150);
    m_paramTable->setColumnWidth(2, 60);
    m_paramTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paramTable->setAlternatingRowColors(true);
    m_paramTable->setShowGrid(false);
    m_paramTable->verticalHeader()->setVisible(false);
    
    mainLayout->addWidget(m_paramTable, 1);

    // ─── Bottom Section: Preview and Actions ────────────────────────────────
    QGroupBox* previewGroup = new QGroupBox("Generated SPICE Line");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    
    m_previewArea = new QTextEdit();
    m_previewArea->setReadOnly(true);
    m_previewArea->setFixedHeight(80);
    m_previewArea->setFont(QFont("JetBrains Mono", 10));
    previewLayout->addWidget(m_previewArea);
    
    mainLayout->addWidget(previewGroup);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    
    QPushButton* resetBtn = new QPushButton("Reset Defaults");
    QPushButton* autoBtn = new QPushButton("Auto-Architect (PDF)");
    QPushButton* copyBtn = new QPushButton("Copy to Clipboard");
    QPushButton* saveBtn = new QPushButton("Save to Library");
    
    autoBtn->setStyleSheet("background-color: #3b82f6; color: white; font-weight: bold;");
    saveBtn->setStyleSheet("background-color: #238636; color: white; font-weight: bold;");
    
    actionLayout->addWidget(resetBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(autoBtn);
    actionLayout->addWidget(copyBtn);
    actionLayout->addWidget(saveBtn);
    
    mainLayout->addLayout(actionLayout);

    // Connections
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SpiceModelArchitect::onTypeChanged);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &SpiceModelArchitect::updatePreview);
    connect(m_paramTable, &QTableWidget::itemChanged, this, &SpiceModelArchitect::onParameterChanged);
    connect(copyBtn, &QPushButton::clicked, this, &SpiceModelArchitect::onCopyClicked);
    connect(resetBtn, &QPushButton::clicked, this, &SpiceModelArchitect::onResetClicked);
    connect(saveBtn, &QPushButton::clicked, this, &SpiceModelArchitect::onSaveToLibraryClicked);
    connect(autoBtn, &QPushButton::clicked, this, &SpiceModelArchitect::onAutoArchitectClicked);
}

void SpiceModelArchitect::onAutoArchitectClicked() {
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open Datasheet PDF"), "", tr("PDF Files (*.pdf)"));
    
    if (!fileName.isEmpty()) {
        runAutoArchitect(fileName);
    }
}

void SpiceModelArchitect::runAutoArchitect(const QString& pdfPath) {
    QProgressDialog progress("Analyzing datasheet with AI...", "Cancel", 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.show();

    // In a real environment, we'd use the bundled python from venv
    QString pythonPath = "python3"; 
    
    // Attempt to locate script relative to app or project root
    QString scriptPath = QApplication::applicationDirPath() + "/../python/scripts/run_spice_model_agent.py";
    if (!QFile::exists(scriptPath)) {
        scriptPath = QDir::current().absoluteFilePath("python/scripts/run_spice_model_agent.py");
    }

    QStringList arguments;
    arguments << scriptPath << "--pdf" << pdfPath << "--type" << m_typeCombo->currentText();
    
    QProcess process;
    process.start(pythonPath, arguments);
    
    // Simple event loop to keep UI responsive
    while (!process.waitForFinished(100) && !progress.wasCanceled()) {
        QApplication::processEvents();
    }

    if (progress.wasCanceled()) {
        process.terminate();
        return;
    }

    QByteArray output = process.readAllStandardOutput();
    QByteArray error = process.readAllStandardError();

    if (process.exitCode() != 0) {
        QMessageBox::critical(this, "AI Analysis Failed", 
            "The AI agent encountered an error:\n" + QString::fromUtf8(error));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::critical(this, "Parse Error", "Failed to parse AI response. Raw output:\n" + QString::fromUtf8(output));
        return;
    }

    QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        QMessageBox::warning(this, "AI Warning", obj["error"].toString());
        return;
    }

    // Update the table with extracted values
    m_paramTable->blockSignals(true);
    int matchCount = 0;
    for (int i = 0; i < m_paramTable->rowCount(); ++i) {
        QString paramName = m_paramTable->item(i, 0)->text();
        if (obj.contains(paramName)) {
            QJsonValue val = obj[paramName];
            QString valStr;
            if (val.isDouble()) valStr = QString::number(val.toDouble());
            else valStr = val.toString();
            
            m_paramTable->item(i, 1)->setText(valStr);
            m_paramTable->item(i, 1)->setBackground(QBrush(QColor(59, 130, 246, 50))); // Highlight
            matchCount++;
        }
    }
    m_paramTable->blockSignals(false);
    updatePreview();

    QMessageBox::information(this, "Success", 
        QString("Successfully extracted %1 parameters from datasheet.").arg(matchCount));
}

void SpiceModelArchitect::onTypeChanged(int index) {
    Q_UNUSED(index);
    populateParameters(m_typeCombo->currentText());
}

void SpiceModelArchitect::onParameterChanged() {
    updatePreview();
}

void SpiceModelArchitect::populateParameters(const QString& type) {
    m_paramTable->setRowCount(0);
    m_paramTable->blockSignals(true);
    
    const auto& params = m_deviceDefinitions[type];
    for (const auto& p : params) {
        int row = m_paramTable->rowCount();
        m_paramTable->insertRow(row);
        
        QTableWidgetItem* nameItem = new QTableWidgetItem(p.name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        
        QTableWidgetItem* valItem = new QTableWidgetItem(p.defaultValue);
        
        QTableWidgetItem* unitItem = new QTableWidgetItem(p.unit);
        unitItem->setFlags(unitItem->flags() & ~Qt::ItemIsEditable);
        unitItem->setTextAlignment(Qt::AlignCenter);
        
        QTableWidgetItem* descItem = new QTableWidgetItem(p.description);
        descItem->setFlags(descItem->flags() & ~Qt::ItemIsEditable);
        
        m_paramTable->setItem(row, 0, nameItem);
        m_paramTable->setItem(row, 1, valItem);
        m_paramTable->setItem(row, 2, unitItem);
        m_paramTable->setItem(row, 3, descItem);
    }
    
    m_paramTable->blockSignals(false);
    updatePreview();
}

QString SpiceModelArchitect::generateModelLine() const {
    QString typeStr = m_typeCombo->currentText();
    QString spiceType = "D";
    if (typeStr.contains("NPN")) spiceType = "NPN";
    else if (typeStr.contains("PNP")) spiceType = "PNP";
    else if (typeStr.contains("VDMOS")) spiceType = "VDMOS";
    else if (typeStr.contains("NMOS")) spiceType = "NMOS";
    else if (typeStr.contains("PMOS")) spiceType = "PMOS";

    QString line = QString(".model %1 %2(").arg(m_nameEdit->text(), spiceType);
    
    QStringList params;
    for (int i = 0; i < m_paramTable->rowCount(); ++i) {
        QString name = m_paramTable->item(i, 0)->text();
        QString val = m_paramTable->item(i, 1)->text().trimmed();
        
        // Only include if value is not empty or if it's explicitly set
        if (!val.isEmpty()) {
            params.append(QString("%1=%2").arg(name, val));
        }
    }
    
    line += params.join(" ") + ")";
    return line;
}

void SpiceModelArchitect::updatePreview() {
    m_previewArea->setText(generateModelLine());
}

void SpiceModelArchitect::onCopyClicked() {
    QApplication::clipboard()->setText(generateModelLine());
}

void SpiceModelArchitect::onResetClicked() {
    onTypeChanged(m_typeCombo->currentIndex());
}

void SpiceModelArchitect::onSaveToLibraryClicked() {
    QString modelLine = generateModelLine();
    QString modelName = m_nameEdit->text().trimmed();
    QString typeStr = m_typeCombo->currentText();
    
    if (modelName.isEmpty()) {
        QMessageBox::warning(this, "Save Failed", "Please enter a valid model name.");
        return;
    }

    // Determine filename based on type
    QString filename = "misc.lib";
    if (typeStr.contains("Diode")) filename = "diode.lib";
    else if (typeStr.contains("BJT")) filename = "bjt.lib";
    else if (typeStr.contains("MOSFET") || typeStr.contains("NMOS") || typeStr.contains("PMOS")) filename = "mosfet.lib";

    // Setup library path (ensure directory exists)
    QDir libDir(QApplication::applicationDirPath() + "/library/models");
    if (!libDir.exists()) {
        libDir.mkpath(".");
    }
    
    QString filePath = libDir.absoluteFilePath(filename);
    QFile file(filePath);
    
    // Check if model already exists to avoid exact duplicates (simple check)
    bool exists = false;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.contains(".model " + modelName, Qt::CaseInsensitive)) {
                exists = true;
                break;
            }
        }
        file.close();
    }

    if (exists) {
        auto res = QMessageBox::question(this, "Model Exists", 
            QString("A model named '%1' already exists in %2. Overwrite?").arg(modelName, filename),
            QMessageBox::Yes | QMessageBox::No);
        if (res == QMessageBox::No) return;
        
        // Overwrite logic (simple: read all, filter out old, write back)
        QStringList lines;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (!line.contains(".model " + modelName, Qt::CaseInsensitive)) {
                    lines << line;
                }
            }
            file.close();
        }
        lines << modelLine;
        
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            for (const QString& l : lines) out << l << "\n";
            file.close();
        }
    } else {
        // Append new model
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << modelLine << "\n";
            file.close();
        } else {
            QMessageBox::critical(this, "Save Error", "Could not open library file for writing:\n" + filePath);
            return;
        }
    }

    QMessageBox::information(this, "Success", 
        QString("Model '%1' saved to %2\nPath: %3").arg(modelName, filename, filePath));
    accept();
}

void SpiceModelArchitect::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString bg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();

    setStyleSheet(QString(
        "QDialog { background-color: %1; }"
        "QLabel { color: %2; font-weight: 600; font-size: 11px; }"
        "QLineEdit, QComboBox, QTextEdit { background-color: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
        "QTableWidget { background-color: %3; color: %2; border: 1px solid %4; border-radius: 8px; gridline-color: transparent; }"
        "QHeaderView::section { background-color: %1; color: %2; padding: 8px; border: none; border-bottom: 1px solid %4; font-weight: bold; }"
        "QPushButton { background-color: %3; border: 1px solid %4; border-radius: 6px; padding: 8px 16px; color: %2; font-weight: 600; }"
        "QPushButton:hover { border-color: %5; }"
        "QGroupBox { color: %2; font-weight: bold; border: 1px solid %4; border-radius: 8px; margin-top: 12px; padding-top: 10px; }"
    ).arg(bg, fg, panelBg, border, accent));
}
