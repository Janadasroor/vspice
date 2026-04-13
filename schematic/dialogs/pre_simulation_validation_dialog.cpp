#include "pre_simulation_validation_dialog.h"
#include "theme_manager.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QIcon>
#include <QClipboard>
#include <QFont>

PreSimulationValidationDialog::PreSimulationValidationDialog(QWidget* parent)
    : QDialog(parent)
{
    setupUi();
}

void PreSimulationValidationDialog::setupUi() {
    setWindowTitle("Pre-Simulation Validation");
    setMinimumSize(650, 450);
    setSizeGripEnabled(true);

    // Dark theme styling
    QString style = "QDialog { background-color: #1e293b; color: #f8fafc; } "
                    "QLabel { color: #f8fafc; font-family: 'Inter'; } "
                    "QTableWidget { background-color: #0f172a; color: #cbd5e1; border: 1px solid #334155; gridline-color: #334155; font-family: 'Inter'; } "
                    "QHeaderView::section { background-color: #1e293b; color: #94a3b8; padding: 4px; border: 1px solid #334155; font-size: 11px; font-weight: bold; } "
                    "QPushButton { background-color: #334155; color: #f8fafc; border-radius: 4px; padding: 8px 20px; font-weight: bold; font-size: 13px; } "
                    "QPushButton:hover { background-color: #475569; } "
                    "QPushButton:disabled { background-color: #1e293b; color: #64748b; } "
                    "QPushButton#btnProceed { background-color: #059669; } "
                    "QPushButton#btnProceed:hover { background-color: #10b981; } "
                    "QPushButton#btnAbort { background-color: #b91c1c; } "
                    "QPushButton#btnAbort:hover { background-color: #dc2626; } "
                    "QPushButton#btnCopy { background-color: #1d4ed8; } "
                    "QPushButton#btnCopy:hover { background-color: #2563eb; } "
                    "QLabel#summaryLabel { font-size: 13px; padding: 8px; } ";
    setStyleSheet(style);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    // Header section
    m_summaryLabel = new QLabel("Validation in progress...");
    m_summaryLabel->setObjectName("summaryLabel");
    m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    // Table for issues
    m_table = new QTableWidget();
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({"Severity", "Category", "Message"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->setStyleSheet("QTableWidget::item { padding: 6px; } ");
    layout->addWidget(m_table, 1);

    // Button layout
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    m_btnCopy = new QPushButton("Copy to Clipboard");
    m_btnCopy->setObjectName("btnCopy");
    m_btnCopy->setIcon(QIcon::fromTheme("edit-copy"));
    connect(m_btnCopy, &QPushButton::clicked, this, [this]() {
        copyIssuesToClipboard();
    });
    btnLayout->addWidget(m_btnCopy);

    btnLayout->addStretch();

    m_btnAbort = new QPushButton("Abort");
    m_btnAbort->setObjectName("btnAbort");
    m_btnAbort->setIcon(QIcon::fromTheme("process-stop"));
    connect(m_btnAbort, &QPushButton::clicked, this, [this]() {
        m_proceed = false;
        reject();
    });
    btnLayout->addWidget(m_btnAbort);

    m_btnProceed = new QPushButton("Run Simulation");
    m_btnProceed->setObjectName("btnProceed");
    m_btnProceed->setIcon(QIcon::fromTheme("media-playback-start"));
    connect(m_btnProceed, &QPushButton::clicked, this, [this]() {
        m_proceed = true;
        accept();
    });
    btnLayout->addWidget(m_btnProceed);

    layout->addLayout(btnLayout);
}

void PreSimulationValidationDialog::addIssue(const ValidationIssue& issue) {
    m_issues.append(issue);
    populateIssues();
    updateButtonStates();
}

void PreSimulationValidationDialog::addIssues(const QList<ValidationIssue>& issues) {
    m_issues.append(issues);
    populateIssues();
    updateButtonStates();
}

bool PreSimulationValidationDialog::hasErrors() const {
    for (const auto& issue : m_issues) {
        if (issue.severity == ValidationIssue::Error) {
            return true;
        }
    }
    return false;
}

bool PreSimulationValidationDialog::hasWarnings() const {
    for (const auto& issue : m_issues) {
        if (issue.severity == ValidationIssue::Warning) {
            return true;
        }
    }
    return false;
}

void PreSimulationValidationDialog::populateIssues() {
    m_table->setRowCount(0);
    
    if (m_issues.isEmpty()) {
        m_table->setRowCount(1);
        
        auto* sevItem = new QTableWidgetItem("✓");
        sevItem->setForeground(QColor("#10b981"));
        sevItem->setFont(QFont("Inter", 12, QFont::Bold));
        m_table->setItem(0, 0, sevItem);
        
        auto* catItem = new QTableWidgetItem("Status");
        m_table->setItem(0, 1, catItem);
        
        auto* msgItem = new QTableWidgetItem("No issues detected. Ready to run simulation.");
        msgItem->setForeground(QColor("#10b981"));
        msgItem->setFont(QFont("Inter", 10, QFont::Bold));
        m_table->setItem(0, 2, msgItem);
        
        m_summaryLabel->setText("✓ Validation passed - No issues detected");
        m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #10b981;");
        return;
    }

    int errorCount = 0;
    int warningCount = 0;
    int infoCount = 0;

    m_table->setRowCount(m_issues.size());
    for (int i = 0; i < m_issues.size(); ++i) {
        const auto& issue = m_issues[i];
        
        // Severity column
        auto* sevItem = new QTableWidgetItem();
        QString sevText;
        QColor sevColor;
        switch (issue.severity) {
            case ValidationIssue::Error:
                sevText = "✗ ERROR";
                sevColor = QColor("#ef4444");
                errorCount++;
                break;
            case ValidationIssue::Warning:
                sevText = "⚠ WARNING";
                sevColor = QColor("#f59e0b");
                warningCount++;
                break;
            case ValidationIssue::Info:
            default:
                sevText = "ℹ INFO";
                sevColor = QColor("#64748b");
                infoCount++;
                break;
        }
        sevItem->setText(sevText);
        sevItem->setForeground(sevColor);
        sevItem->setFont(QFont("Inter", 10, QFont::Bold));
        m_table->setItem(i, 0, sevItem);

        // Category column
        auto* catItem = new QTableWidgetItem(issue.category.isEmpty() ? "General" : issue.category);
        catItem->setForeground(QColor("#94a3b8"));
        catItem->setFont(QFont("Inter", 9));
        m_table->setItem(i, 1, catItem);

        // Message column
        auto* msgItem = new QTableWidgetItem(issue.message);
        msgItem->setFont(QFont("Inter", 10));
        msgItem->setForeground(QColor("#e2e8f0"));
        m_table->setItem(i, 2, msgItem);
    }

    // Update summary
    QString summaryText;
    if (errorCount > 0 && warningCount > 0) {
        summaryText = QString("✗ Found %1 error%2 and %3 warning%4")
                         .arg(errorCount).arg(errorCount > 1 ? "s" : "")
                         .arg(warningCount).arg(warningCount > 1 ? "s" : "");
        m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #ef4444;");
    } else if (errorCount > 0) {
        summaryText = QString("✗ Found %1 error%2")
                         .arg(errorCount).arg(errorCount > 1 ? "s" : "");
        m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #ef4444;");
    } else if (warningCount > 0) {
        summaryText = QString("⚠ Found %1 warning%2")
                         .arg(warningCount).arg(warningCount > 1 ? "s" : "");
        m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #f59e0b;");
    } else {
        summaryText = QString("ℹ %1 info message%2")
                         .arg(infoCount).arg(infoCount > 1 ? "s" : "");
        m_summaryLabel->setStyleSheet("font-weight: bold; font-size: 14px; color: #64748b;");
    }
    m_summaryLabel->setText(summaryText);
}

void PreSimulationValidationDialog::updateButtonStates() {
    // Disable "Run Simulation" button if there are errors
    const bool hasErrors = this->hasErrors();
    m_btnProceed->setEnabled(!hasErrors);
    
    if (hasErrors) {
        m_btnProceed->setToolTip("Cannot proceed: Please fix the errors above");
    } else {
        m_btnProceed->setToolTip("");
    }
}

void PreSimulationValidationDialog::copyIssuesToClipboard() const {
    if (m_issues.isEmpty()) {
        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setText("No issues detected. Ready to run simulation.");
        return;
    }

    QString text;
    for (const auto& issue : m_issues) {
        QString sevStr;
        switch (issue.severity) {
            case ValidationIssue::Error:
                sevStr = "ERROR";
                break;
            case ValidationIssue::Warning:
                sevStr = "WARNING";
                break;
            case ValidationIssue::Info:
            default:
                sevStr = "INFO";
                break;
        }
        
        text += QString("[%1] [%2] %3\n").arg(sevStr, issue.category.isEmpty() ? "General" : issue.category, issue.message);
    }

    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(text);
}
