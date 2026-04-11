#include "erc_diagnostics_panel.h"
#include "../items/schematic_item.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QIcon>
#include <QApplication>
#include <QClipboard>
#include <QLibrary>
#include <QFileInfo>
#include <dlfcn.h>

// Forward declarations for ngspice functions (to find library path via dladdr)
extern "C" {
    int ngSpice_Init(void*, void*, void*, void*, void*, void*, void*);
    int ngSpice_Command(char*);
}

ERCDiagnosticsPanel::ERCDiagnosticsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Library Info Label
    m_libraryInfoLabel = new QLabel();
    m_libraryInfoLabel->setStyleSheet("background: #1a1a2e; color: #a0a0c0; padding: 6px; font-size: 11px; border-bottom: 1px solid #3c3c5c;");
    m_libraryInfoLabel->setTextFormat(Qt::RichText);
    m_libraryInfoLabel->setWordWrap(true);
    updateLibraryInfo();
    layout->addWidget(m_libraryInfoLabel);

    // Toolbar
    QWidget* toolbar = new QWidget();
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(4, 4, 4, 4);

    m_severityFilter = new QComboBox();
    m_severityFilter->addItems({"All Issues", "Errors Only", "Warnings Only"});
    connect(m_severityFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ERCDiagnosticsPanel::onFilterChanged);
    toolbarLayout->addWidget(m_severityFilter);

    toolbarLayout->addStretch();

    QPushButton* aiBtn = new QPushButton("✨ AI Fix");
    aiBtn->setToolTip("Ask Gemini AI to suggest fixes for these violations");
    connect(aiBtn, &QPushButton::clicked, this, [this]() {
        QString summary;
        for (const auto& v : m_violations) {
            summary += QString("- [%1] %2\n").arg(v.severity == ERCViolation::Error ? "Error" : "Warning").arg(v.message);
        }
        Q_EMIT aiFixRequested(summary);
    });
    toolbarLayout->addWidget(aiBtn);

    layout->addWidget(toolbar);

    // Tree Widget
    m_treeWidget = new QTreeWidget();
    m_treeWidget->setColumnCount(3);
    m_treeWidget->setHeaderLabels({"!", "Item", "Message"});
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_treeWidget->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_treeWidget->setAlternatingRowColors(true);
    m_treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    
    connect(m_treeWidget, &QTreeWidget::itemDoubleClicked, this, &ERCDiagnosticsPanel::onItemDoubleClicked);
    connect(m_treeWidget, &QTreeWidget::customContextMenuRequested, this, &ERCDiagnosticsPanel::onCustomContextMenu);

    layout->addWidget(m_treeWidget);
}

void ERCDiagnosticsPanel::updateLibraryInfo() {
    if (!m_libraryInfoLabel) return;

    // Check which ngspice library is loaded
    Dl_info info;
    QString libPath;
    bool isVioMatrix = false;
    
    // Try to find the path to ngSpice_Init or ngSpice_Circ
    if (dladdr((void*)ngSpice_Init, &info) && info.dli_fname) {
        libPath = QString::fromLocal8Bit(info.dli_fname);
    } else if (dladdr((void*)ngSpice_Command, &info) && info.dli_fname) {
        libPath = QString::fromLocal8Bit(info.dli_fname);
    }

    // Determine if it's VioMATRIXC
    isVioMatrix = libPath.contains("VioMATRIXC", Qt::CaseInsensitive) || 
                  libPath.contains("releasesh", Qt::CaseInsensitive);

    QString icon = isVioMatrix ? "🔧" : "⚙️";
    QString type = isVioMatrix ? "VioMATRIXC (Custom Fork)" : "System ngspice";
    QString color = isVioMatrix ? "#4ade80" : "#fbbf24";
    QString status = isVioMatrix ? "✅ Full feature set (WAV, LTspice compat, XSPICE)" : "⚠️ Standard features only";

    if (libPath.isEmpty()) {
        libPath = "Not loaded";
        color = "#f87171";
    }

    m_libraryInfoLabel->setText(
        QString("<b>%1 Simulation Engine:</b> <span style='color:%2'>%3</span><br>"
                "<span style='color:#808080'>Library:</span> %4<br>"
                "<span style='color:%2'>%5</span>")
            .arg(icon)
            .arg(color)
            .arg(type)
            .arg(libPath.isEmpty() ? "Unknown" : QFileInfo(libPath).fileName())
            .arg(status)
    );
}

void ERCDiagnosticsPanel::setViolations(const QList<ERCViolation>& violations) {
    m_violations = violations;
    updateList();
}

void ERCDiagnosticsPanel::clear() {
    m_violations.clear();
    m_treeWidget->clear();
}

void ERCDiagnosticsPanel::updateList() {
    m_treeWidget->clear();
    int filter = m_severityFilter->currentIndex();

    for (int i = 0; i < m_violations.size(); ++i) {
        const auto& v = m_violations[i];
        
        if (filter == 1 && v.severity != ERCViolation::Error && v.severity != ERCViolation::Critical) continue;
        if (filter == 2 && v.severity != ERCViolation::Warning) continue;

        auto* item = new QTreeWidgetItem(m_treeWidget);
        item->setIcon(0, iconForSeverity(v.severity));
        
        QString itemRef = v.item ? v.item->reference() : "Global";
        if (itemRef.isEmpty() && v.item) itemRef = v.item->itemTypeName();
        
        item->setText(1, itemRef);
        item->setText(2, v.message);
        item->setData(0, Qt::UserRole, i); // Store index
        
        item->setForeground(0, colorForSeverity(v.severity));
    }
}

QIcon ERCDiagnosticsPanel::iconForSeverity(ERCViolation::Severity severity) {
    // In a real app we'd use themed icons. Fallback to standard colors.
    if (severity == ERCViolation::Error || severity == ERCViolation::Critical) return QIcon::fromTheme("dialog-error");
    return QIcon::fromTheme("dialog-warning");
}

QColor ERCDiagnosticsPanel::colorForSeverity(ERCViolation::Severity severity) {
    if (severity == ERCViolation::Error || severity == ERCViolation::Critical) return QColor("#f87171"); // Red
    return QColor("#fbbf24"); // Amber
}

void ERCDiagnosticsPanel::onItemDoubleClicked(QTreeWidgetItem* item, int) {
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx >= 0 && idx < m_violations.size()) {
        Q_EMIT violationSelected(m_violations[idx]);
    }
}

void ERCDiagnosticsPanel::onFilterChanged() {
    updateList();
}

void ERCDiagnosticsPanel::onCustomContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_treeWidget->itemAt(pos);
    if (!item) return;

    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_violations.size()) return;
    
    const auto& v = m_violations[idx];

    QMenu menu;
    menu.addAction("Jump to Item", [this, v]() { Q_EMIT violationSelected(v); });
    menu.addAction("Ignore Violation", [this, v]() { Q_EMIT ignoreRequested(v); });
    menu.addSeparator();
    menu.addAction("Copy Message", [v]() { QApplication::clipboard()->setText(v.message); });
    menu.exec(m_treeWidget->mapToGlobal(pos));
}
