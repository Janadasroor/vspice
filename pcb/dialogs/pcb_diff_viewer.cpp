#include "pcb_diff_viewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>

PCBDiffViewer::PCBDiffViewer(const DiffReport& report, QGraphicsScene* sceneA, QGraphicsScene* sceneB,
                             QWidget* parent)
    : QDialog(parent), m_report(report), m_sceneA(sceneA), m_sceneB(sceneB)
{
    setWindowTitle(QString("Board Comparison: %1 vs %2")
        .arg(report.boardAName, report.boardBName));
    resize(900, 650);
    setupUI();
}

PCBDiffViewer::~PCBDiffViewer() = default;

void PCBDiffViewer::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Summary banner
    QString bannerText;
    if (m_report.identical) {
        bannerText = QString("✅ The two boards are <b>identical</b> — no differences found.");
    } else {
        bannerText = QString("⚠️ <b>%1 differences</b> found between <b>%2</b> and <b>%3</b>")
            .arg(m_report.stats.totalDifferences()).arg(m_report.boardAName).arg(m_report.boardBName);
    }
    QLabel* banner = new QLabel(bannerText);
    banner->setWordWrap(true);
    banner->setStyleSheet(QString(
        "padding: 12px; font-size: 14px; background: %1; color: white; border-radius: 4px;")
        .arg(m_report.identical ? "#28a745" : "#ffc107"));
    mainLayout->addWidget(banner);

    // Tabs
    m_tabs = new QTabWidget();

    // Tab 1: Diff table
    QWidget* tablePage = new QWidget();
    QVBoxLayout* tableLayout = new QVBoxLayout(tablePage);

    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->addWidget(new QLabel("Filter:"));
    m_categoryFilter = new QComboBox();
    m_categoryFilter->addItem("All Categories");
    m_categoryFilter->addItem("Component");
    m_categoryFilter->addItem("Trace");
    m_categoryFilter->addItem("Via");
    m_categoryFilter->addItem("CopperPour");
    m_categoryFilter->addItem("Net");
    filterLayout->addWidget(m_categoryFilter);
    filterLayout->addStretch();
    tableLayout->addLayout(filterLayout);

    m_diffTable = new QTableWidget();
    m_diffTable->setColumnCount(5);
    m_diffTable->setHorizontalHeaderLabels({"", "Category", "Identifier", "Description", "Layer"});
    m_diffTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_diffTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_diffTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_diffTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_diffTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_diffTable->setColumnWidth(0, 40);
    m_diffTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_diffTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableLayout->addWidget(m_diffTable);

    m_summaryLabel = new QLabel();
    m_summaryLabel->setStyleSheet("padding: 6px;");
    tableLayout->addWidget(m_summaryLabel);

    m_tabs->addTab(tablePage, "📋 Differences");

    // Tab 2: Statistics
    m_statsEdit = new QTextEdit();
    m_statsEdit->setReadOnly(true);
    m_tabs->addTab(m_statsEdit, "📊 Statistics");

    // Tab 3: JSON Report
    m_reportEdit = new QTextEdit();
    m_reportEdit->setReadOnly(true);
    m_tabs->addTab(m_reportEdit, "📄 JSON Report");

    mainLayout->addWidget(m_tabs);

    // Bottom buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_saveBtn = new QPushButton("💾 Save Report...");
    m_closeBtn = new QPushButton("Close");
    btnLayout->addStretch();
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);

    // Connections
    connect(m_categoryFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PCBDiffViewer::onCategoryFilterChanged);
    connect(m_diffTable, &QTableWidget::itemClicked,
            this, &PCBDiffViewer::onDiffEntryClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &PCBDiffViewer::onSaveReport);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Populate
    populateDiffTable();
    populateStatsTab();
    populateReportTab();
}

void PCBDiffViewer::populateDiffTable() {
    m_diffTable->setRowCount(0);

    QString category = m_categoryFilter->currentText();
    QList<DiffEntry> entries;

    if (category == "All Categories") {
        entries = m_report.entries;
    } else {
        entries = m_report.byCategory(category);
    }

    m_diffTable->setRowCount(entries.size());

    for (int row = 0; row < entries.size(); ++row) {
        const auto& entry = entries[row];

        // Icon
        auto* iconItem = new QTableWidgetItem(entry.icon());
        iconItem->setTextAlignment(Qt::AlignCenter);
        iconItem->setForeground(QColor(entry.colorCode()));
        m_diffTable->setItem(row, 0, iconItem);

        // Category
        auto* catItem = new QTableWidgetItem(entry.category);
        m_diffTable->setItem(row, 1, catItem);

        // Identifier
        auto* idItem = new QTableWidgetItem(entry.identifier);
        m_diffTable->setItem(row, 2, idItem);

        // Description
        auto* descItem = new QTableWidgetItem(entry.description);
        descItem->setForeground(QColor(entry.colorCode()));
        m_diffTable->setItem(row, 3, descItem);

        // Layer
        QString layerStr = (entry.layer >= 0) ? QString::number(entry.layer) : "—";
        auto* layerItem = new QTableWidgetItem(layerStr);
        layerItem->setTextAlignment(Qt::AlignCenter);
        m_diffTable->setItem(row, 4, layerItem);
    }

    m_summaryLabel->setText(QString("Showing %1 of %2 differences")
        .arg(entries.size()).arg(m_report.entries.size()));
}

void PCBDiffViewer::populateStatsTab() {
    const auto& s = m_report.stats;

    QString html;
    html += "<h2>Diff Statistics</h2>";
    html += QString("<p><b>Comparison:</b> %1 → %2</p>").arg(m_report.boardAName, m_report.boardBName);
    html += QString("<p><b>Total differences:</b> %1</p>").arg(m_report.stats.totalDifferences());
    html += "<hr>";

    if (m_report.identical) {
        html += "<h3 style='color: #28a745;'>✅ Boards are identical!</h3>";
        m_statsEdit->setHtml(html);
        return;
    }

    // Components
    html += "<h3>📦 Components</h3>";
    html += "<table style='width: 100%;'>";
    if (s.componentsAdded) html += QString("<tr><td style='color: #28a745;'>➕ Added:</td><td><b>%1</b></td></tr>").arg(s.componentsAdded);
    if (s.componentsRemoved) html += QString("<tr><td style='color: #dc3545;'>➖ Removed:</td><td><b>%1</b></td></tr>").arg(s.componentsRemoved);
    if (s.componentsModified) html += QString("<tr><td style='color: #ffc107;'>🔄 Modified:</td><td><b>%1</b></td></tr>").arg(s.componentsModified);
    html += "</table>";

    // Traces
    html += "<h3>〰️ Traces</h3>";
    html += "<table style='width: 100%;'>";
    if (s.tracesAdded) html += QString("<tr><td style='color: #28a745;'>➕ Added:</td><td><b>%1</b></td></tr>").arg(s.tracesAdded);
    if (s.tracesRemoved) html += QString("<tr><td style='color: #dc3545;'>➖ Removed:</td><td><b>%1</b></td></tr>").arg(s.tracesRemoved);
    if (s.tracesModified) html += QString("<tr><td style='color: #ffc107;'>🔄 Modified:</td><td><b>%1</b></td></tr>").arg(s.tracesModified);
    html += "</table>";

    // Vias
    html += "<h3>⚫ Vias</h3>";
    html += "<table style='width: 100%;'>";
    if (s.viasAdded) html += QString("<tr><td style='color: #28a745;'>➕ Added:</td><td><b>%1</b></td></tr>").arg(s.viasAdded);
    if (s.viasRemoved) html += QString("<tr><td style='color: #dc3545;'>➖ Removed:</td><td><b>%1</b></td></tr>").arg(s.viasRemoved);
    if (s.viasModified) html += QString("<tr><td style='color: #ffc107;'>🔄 Modified:</td><td><b>%1</b></td></tr>").arg(s.viasModified);
    html += "</table>";

    // Copper pours
    html += "<h3>🔲 Copper Pours</h3>";
    html += "<table style='width: 100%;'>";
    if (s.copperPoursAdded) html += QString("<tr><td style='color: #28a745;'>➕ Added:</td><td><b>%1</b></td></tr>").arg(s.copperPoursAdded);
    if (s.copperPoursRemoved) html += QString("<tr><td style='color: #dc3545;'>➖ Removed:</td><td><b>%1</b></td></tr>").arg(s.copperPoursRemoved);
    html += "</table>";

    // Nets
    html += "<h3>🔗 Nets</h3>";
    html += "<table style='width: 100%;'>";
    if (s.netsAdded) html += QString("<tr><td style='color: #28a745;'>➕ Added:</td><td><b>%1</b></td></tr>").arg(s.netsAdded);
    if (s.netsRemoved) html += QString("<tr><td style='color: #dc3545;'>➖ Removed:</td><td><b>%1</b></td></tr>").arg(s.netsRemoved);
    html += "</table>";

    m_statsEdit->setHtml(html);
}

void PCBDiffViewer::populateReportTab() {
    QString json = m_report.toJson();

    // Pretty-print JSON
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError) {
        json = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    m_reportEdit->setPlainText(json);
}

void PCBDiffViewer::onDiffEntryClicked(QTableWidgetItem* item) {
    int row = item->row();
    // Find the entry for this row (considering filter)
    QString category = m_categoryFilter->currentText();
    QList<DiffEntry> entries;

    if (category == "All Categories") {
        entries = m_report.entries;
    } else {
        entries = m_report.byCategory(category);
    }

    if (row < 0 || row >= entries.size()) return;

    const DiffEntry& entry = entries[row];

    // Highlight the difference in the scene
    // For now, just show a message — full overlay highlighting requires more complex scene manipulation
    QMessageBox::information(this, entry.identifier,
        QString("<b>%1</b><br><br>Type: %2<br>Category: %3<br>Location: (%4, %5)<br>Layer: %6<br><br>%7")
            .arg(entry.identifier)
            .arg(entry.icon())
            .arg(entry.category)
            .arg(entry.location.x(), 0, 'f', 2)
            .arg(entry.location.y(), 0, 'f', 2)
            .arg(entry.layer >= 0 ? QString::number(entry.layer) : "N/A")
            .arg(entry.description));
}

void PCBDiffViewer::onCategoryFilterChanged(int index) {
    (void)index;
    populateDiffTable();
}

void PCBDiffViewer::onSaveReport() {
    QString path = QFileDialog::getSaveFileName(this, "Save Diff Report",
                                                 "board_diff.json", "JSON Files (*.json)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_report.toJson().toUtf8());
        file.close();
        QMessageBox::information(this, "Report Saved", "Diff report saved to:\n" + path);
    } else {
        QMessageBox::warning(this, "Save Failed", "Could not write to:\n" + path);
    }
}
