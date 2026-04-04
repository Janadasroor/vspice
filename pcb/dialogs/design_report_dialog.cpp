#include "design_report_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDateTime>

DesignReportDialog::DesignReportDialog(QGraphicsScene* scene, QWidget* parent)
    : QDialog(parent), m_scene(scene)
{
    setWindowTitle("Generate Design Report");
    resize(750, 650);
    setupUI();
    updatePreview();
}

DesignReportDialog::~DesignReportDialog() = default;

void DesignReportDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // === Output Settings ===
    QGroupBox* outputGroup = new QGroupBox("Output Settings");
    QGridLayout* outputLayout = new QGridLayout(outputGroup);

    outputLayout->addWidget(new QLabel("Format:"), 0, 0);
    m_formatCombo = new QComboBox();
    m_formatCombo->addItem("PDF Document (.pdf)");
    m_formatCombo->addItem("HTML Document (.html)");
    outputLayout->addWidget(m_formatCombo, 0, 1);

    outputLayout->addWidget(new QLabel("Output File:"), 1, 0);
    m_filePathEdit = new QLineEdit();
    m_filePathEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                            + "/pcb_design_report.pdf");
    outputLayout->addWidget(m_filePathEdit, 1, 1);

    m_browseBtn = new QPushButton("Browse...");
    outputLayout->addWidget(m_browseBtn, 1, 2);

    outputLayout->addWidget(new QLabel("Company:"), 2, 0);
    m_companyEdit = new QLineEdit();
    m_companyEdit->setPlaceholderText("Your company name");
    outputLayout->addWidget(m_companyEdit, 2, 1);

    outputLayout->addWidget(new QLabel("Project:"), 3, 0);
    m_projectEdit = new QLineEdit();
    m_projectEdit->setPlaceholderText("Project name");
    outputLayout->addWidget(m_projectEdit, 3, 1);

    mainLayout->addWidget(outputGroup);

    // === Report Sections ===
    QGroupBox* sectionsGroup = new QGroupBox("Report Sections");
    QVBoxLayout* sectionsLayout = new QVBoxLayout(sectionsGroup);

    m_includeStats = new QCheckBox("Design Statistics (component count, trace length, etc.)");
    m_includeStats->setChecked(true);
    sectionsLayout->addWidget(m_includeStats);

    m_includeLayers = new QCheckBox("Layer Stackup (thickness, material, copper weight)");
    m_includeLayers->setChecked(true);
    sectionsLayout->addWidget(m_includeLayers);

    m_includeDRC = new QCheckBox("DRC Summary (errors, warnings, violations)");
    m_includeDRC->setChecked(true);
    sectionsLayout->addWidget(m_includeDRC);

    m_includeNets = new QCheckBox("Net Summary (total nets, unrouted, trace length)");
    m_includeNets->setChecked(true);
    sectionsLayout->addWidget(m_includeNets);

    m_includeComponents = new QCheckBox("Components by Type (footprint breakdown)");
    m_includeComponents->setChecked(true);
    sectionsLayout->addWidget(m_includeComponents);

    m_includeNetClasses = new QCheckBox("Net Classes (width, clearance, via rules)");
    m_includeNetClasses->setChecked(true);
    sectionsLayout->addWidget(m_includeNetClasses);

    m_includeBOM = new QCheckBox("Bill of Materials Summary (value, footprint, references, quantity)");
    m_includeBOM->setChecked(true);
    sectionsLayout->addWidget(m_includeBOM);

    mainLayout->addWidget(sectionsGroup);

    // === Preview ===
    QGroupBox* previewGroup = new QGroupBox("Preview");
    QVBoxLayout* previewLayout = new QVBoxLayout(previewGroup);
    m_previewEdit = new QTextEdit();
    m_previewEdit->setReadOnly(true);
    previewLayout->addWidget(m_previewEdit);
    mainLayout->addWidget(previewGroup);

    // === Status ===
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("padding: 6px; font-weight: bold;");
    mainLayout->addWidget(m_statusLabel);

    // === Buttons ===
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_previewBtn = new QPushButton("👁 Preview HTML");
    m_generateBtn = new QPushButton("📄 Generate Report");
    m_closeBtn = new QPushButton("Close");

    m_generateBtn->setStyleSheet("background-color: #007acc; color: white; font-weight: bold; padding: 8px;");

    btnLayout->addStretch();
    btnLayout->addWidget(m_previewBtn);
    btnLayout->addWidget(m_generateBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_browseBtn, &QPushButton::clicked, this, &DesignReportDialog::onBrowse);
    connect(m_previewBtn, &QPushButton::clicked, this, &DesignReportDialog::onPreview);
    connect(m_generateBtn, &QPushButton::clicked, this, &DesignReportDialog::onGenerate);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DesignReportDialog::onFormatChanged);
}

void DesignReportDialog::onFormatChanged() {
    QString path = m_filePathEdit->text();
    if (m_formatCombo->currentIndex() == 0) {
        // PDF
        if (path.endsWith(".html", Qt::CaseInsensitive)) {
            path.chop(5);
            path += ".pdf";
        }
    } else {
        // HTML
        if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
            path.chop(4);
            path += ".html";
        }
    }
    m_filePathEdit->setText(path);
}

void DesignReportDialog::onBrowse() {
    QString filter = (m_formatCombo->currentIndex() == 0)
        ? "PDF Files (*.pdf);;All Files (*)"
        : "HTML Files (*.html *.htm);;All Files (*)";
    QString path = QFileDialog::getSaveFileName(this, "Save Design Report",
                                                 m_filePathEdit->text(), filter);
    if (!path.isEmpty()) {
        m_filePathEdit->setText(path);
    }
}

DesignReportGenerator::ReportOptions DesignReportDialog::collectOptions() {
    DesignReportGenerator::ReportOptions opts;
    opts.format = (m_formatCombo->currentIndex() == 0)
        ? DesignReportGenerator::PDF
        : DesignReportGenerator::HTML;
    opts.includeStatistics = m_includeStats->isChecked();
    opts.includeLayers = m_includeLayers->isChecked();
    opts.includeDRC = m_includeDRC->isChecked();
    opts.includeNets = m_includeNets->isChecked();
    opts.includeComponents = m_includeComponents->isChecked();
    opts.includeNetClasses = m_includeNetClasses->isChecked();
    opts.includeBOM = m_includeBOM->isChecked();
    opts.companyName = m_companyEdit->text().trimmed();
    opts.projectName = m_projectEdit->text().trimmed();
    return opts;
}

void DesignReportDialog::updatePreview() {
    auto opts = collectOptions();
    opts.format = DesignReportGenerator::HTML; // Always preview as HTML
    DesignReportData data = DesignReportGenerator::collectData(m_scene);
    QString html = DesignReportGenerator::generateHTML(data, opts);
    m_previewEdit->setHtml(html);
}

void DesignReportDialog::onPreview() {
    auto opts = collectOptions();
    opts.format = DesignReportGenerator::HTML;
    DesignReportData data = DesignReportGenerator::collectData(m_scene);
    QString html = DesignReportGenerator::generateHTML(data, opts);

    // Show in a temporary HTML file via browser
    QString tempPath = QDir::temp().filePath("viospice_report_preview.html");
    QFile tempFile(tempPath);
    if (tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&tempFile);
        out << html;
        tempFile.close();
        QDesktopServices::openUrl(QUrl::fromLocalFile(tempPath));
        m_statusLabel->setText("Preview opened in browser");
        m_statusLabel->setStyleSheet("padding: 6px; font-weight: bold; color: #17a2b8;");
    }
}

void DesignReportDialog::onGenerate() {
    QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "No Output File", "Please specify an output file path.");
        return;
    }

    auto opts = collectOptions();
    QString err;
    bool ok = DesignReportGenerator::generateReport(m_scene, path, opts, &err);

    if (!ok) {
        QMessageBox::warning(this, "Report Generation Failed",
            err.isEmpty() ? "Unknown error occurred." : err);
        m_statusLabel->setText("❌ Report generation failed");
        m_statusLabel->setStyleSheet("padding: 6px; font-weight: bold; color: #dc3545;");
        return;
    }

    m_statusLabel->setText("✅ Report generated: " + path);
    m_statusLabel->setStyleSheet("padding: 6px; font-weight: bold; color: #28a745;");

    QMessageBox::information(this, "Report Generated",
        QString("Design report successfully generated:\n\n%1\n\n"
                "Format: %2\n"
                "Sections: %3\n"
                "DRC: %4 errors, %5 warnings")
            .arg(path)
            .arg(opts.format == DesignReportGenerator::PDF ? "PDF" : "HTML")
            .arg(opts.includeDRC ? "DRC ✓" : "")
            .arg(DesignReportGenerator::collectData(m_scene).drcErrors)
            .arg(DesignReportGenerator::collectData(m_scene).drcWarnings));
}
