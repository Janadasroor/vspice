#include "branch_comparison_dialog.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QSplitter>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QProcess>
#include <QFileInfo>
#include <QDir>

BranchComparisonDialog::BranchComparisonDialog(const QString& workingDir, QWidget* parent)
    : QDialog(parent)
    , m_workingDir(workingDir)
    , m_branchACombo(nullptr)
    , m_branchBCombo(nullptr)
    , m_compareBtn(nullptr)
    , m_mergeBtn(nullptr)
    , m_refreshBtn(nullptr)
    , m_comparisonTable(nullptr)
    , m_summaryLabel(nullptr)
    , m_splitter(nullptr) {
    setupUi();
    loadBranches();
}

BranchComparisonDialog::~BranchComparisonDialog() {
}

void BranchComparisonDialog::setupUi() {
    setWindowTitle("Branch Comparison");
    resize(900, 600);
    setStyleSheet("background: #0a0a0c;");
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);
    
    auto* headerLayout = new QHBoxLayout;
    
    QLabel* labelA = new QLabel("Branch A:", this);
    labelA->setStyleSheet("color: #e6edf3;");
    headerLayout->addWidget(labelA);
    
    m_branchACombo = new QComboBox(this);
    m_branchACombo->setStyleSheet("QComboBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; border-radius: 4px; padding: 6px; }");
    headerLayout->addWidget(m_branchACombo, 1);
    
    QLabel* labelB = new QLabel("Branch B:", this);
    labelB->setStyleSheet("color: #e6edf3;");
    headerLayout->addWidget(labelB);
    
    m_branchBCombo = new QComboBox(this);
    m_branchBCombo->setStyleSheet("QComboBox { background: #161b22; color: #e6edf3; border: 1px solid #30363d; border-radius: 4px; padding: 6px; }");
    headerLayout->addWidget(m_branchBCombo, 1);
    
    m_compareBtn = new QPushButton("Compare", this);
    m_compareBtn->setStyleSheet("QPushButton { background: #238636; color: white; border: none; border-radius: 4px; padding: 8px 16px; } QPushButton:hover { background: #2ea043; }");
    headerLayout->addWidget(m_compareBtn);
    
    m_mergeBtn = new QPushButton("Merge →", this);
    m_mergeBtn->setStyleSheet("QPushButton { background: #1f6feb; color: white; border: none; border-radius: 4px; padding: 8px 16px; } QPushButton:hover { background: #388bfd; }");
    headerLayout->addWidget(m_mergeBtn);
    
    m_refreshBtn = new QPushButton("Refresh", this);
    m_refreshBtn->setStyleSheet("QPushButton { background: #21262d; color: #e6edf3; border: 1px solid #30363d; border-radius: 4px; padding: 8px 16px; } QPushButton:hover { background: #30363d; }");
    headerLayout->addWidget(m_refreshBtn);
    
    mainLayout->addLayout(headerLayout);
    
    m_summaryLabel = new QLabel("Select branches to compare", this);
    m_summaryLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
    mainLayout->addWidget(m_summaryLabel);
    
    m_comparisonTable = new QTableWidget(0, 4, this);
    m_comparisonTable->setHorizontalHeaderLabels({"File", "Status", "Added", "Removed"});
    m_comparisonTable->horizontalHeader()->setStretchLastSection(false);
    m_comparisonTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_comparisonTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_comparisonTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_comparisonTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_comparisonTable->setStyleSheet("QTableWidget { background: #161b22; border: 1px solid #30363d; color: #e6edf3; } QTableWidget::item { padding: 6px; border-bottom: 1px solid #21262d; } QHeaderView::section { background: #21262d; color: #8b949e; border: none; padding: 6px; }");
    mainLayout->addWidget(m_comparisonTable, 1);
    
    connect(m_branchACombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BranchComparisonDialog::onBranchAChanged);
    connect(m_branchBCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BranchComparisonDialog::onBranchBChanged);
    connect(m_compareBtn, &QPushButton::clicked, this, &BranchComparisonDialog::onCompareClicked);
    connect(m_mergeBtn, &QPushButton::clicked, this, &BranchComparisonDialog::onMergeClicked);
    connect(m_comparisonTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem* item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            onFileDoubleClicked(new QListWidgetItem(path));
        }
    });
    connect(m_refreshBtn, &QPushButton::clicked, this, &BranchComparisonDialog::onRefreshClicked);
}

void BranchComparisonDialog::loadBranches() {
    m_branchACombo->clear();
    m_branchBCombo->clear();
    m_branches.clear();
    
    if (m_workingDir.isEmpty()) return;
    
    GitBackend git(m_workingDir);
    m_branches = git.branches();
    
    QString currentBranch = git.currentBranch();
    
    for (const GitBranch& branch : m_branches) {
        if (!branch.isRemote) {
            m_branchACombo->addItem(branch.name);
            m_branchBCombo->addItem(branch.name);
            
            if (branch.name == currentBranch) {
                m_branchACombo->setCurrentText(branch.name);
            }
        }
    }
    
    if (m_branchBCombo->count() > 1) {
        m_branchBCombo->setCurrentIndex(1);
    }
}

void BranchComparisonDialog::compareBranches(const QString& branchA, const QString& branchB) {
    m_branchACombo->setCurrentText(branchA);
    m_branchBCombo->setCurrentText(branchB);
    onCompareClicked();
}

void BranchComparisonDialog::onBranchAChanged(int index) {
    if (index >= 0 && index < m_branches.size()) {
        m_branchA = m_branches[index].name;
    }
}

void BranchComparisonDialog::onBranchBChanged(int index) {
    if (index >= 0 && index < m_branches.size()) {
        m_branchB = m_branches[index].name;
    }
}

void BranchComparisonDialog::onCompareClicked() {
    m_branchA = m_branchACombo->currentText();
    m_branchB = m_branchBCombo->currentText();
    
    if (m_branchA.isEmpty() || m_branchB.isEmpty()) {
        QMessageBox::warning(this, "Branch Comparison", "Please select both branches to compare.");
        return;
    }
    
    compareSchematics(m_branchA, m_branchB);
}

void BranchComparisonDialog::compareSchematics(const QString& branchA, const QString& branchB) {
    m_fileDiffs.clear();
    m_comparisonTable->setRowCount(0);
    
    if (m_workingDir.isEmpty()) return;
    
    QProcess process;
    process.setWorkingDirectory(m_workingDir);
    process.start("git", QStringList() << "diff" << "--name-status" << QString("%1...%2").arg(branchA).arg(branchB));
    process.waitForFinished(5000);
    
    QString output = QString::fromUtf8(process.readAllStandardOutput());
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    
    int addedFiles = 0, modifiedFiles = 0, deletedFiles = 0;
    
    for (const QString& line : lines) {
        QStringList parts = line.split('\t');
        if (parts.size() < 2) continue;
        
        QString status = parts[0];
        QString path = parts[1];
        
        FileDiff diff;
        diff.path = path;
        diff.status = status;
        diff.isSchematic = path.endsWith(".sch") || path.endsWith(".flxsch");
        
        QProcess statProcess;
        statProcess.setWorkingDirectory(m_workingDir);
        statProcess.start("git", QStringList() << "diff" << "--stat" << QString("%1...%2").arg(branchA).arg(branchB) << "--" << path);
        statProcess.waitForFinished(3000);
        
        QString statOutput = QString::fromUtf8(statProcess.readAllStandardOutput());
        QRegularExpression re("(\\d+) insertion.*?(\\d+) deletion");
        QRegularExpressionMatch match = re.match(statOutput);
        if (match.hasMatch()) {
            diff.addedLines = match.captured(1).toInt();
            diff.removedLines = match.captured(2).toInt();
        }
        
        m_fileDiffs.append(diff);
        
        if (status.startsWith("A")) addedFiles++;
        else if (status.startsWith("M")) modifiedFiles++;
        else if (status.startsWith("D")) deletedFiles++;
    }
    
    populateComparisonTable();
    
    m_summaryLabel->setText(QString("Comparing %1 ↔ %2: %3 files changed (%4 added, %5 modified, %6 deleted)")
        .arg(branchA).arg(branchB)
        .arg(m_fileDiffs.size())
        .arg(addedFiles)
        .arg(modifiedFiles)
        .arg(deletedFiles));
}

void BranchComparisonDialog::populateComparisonTable() {
    m_comparisonTable->setRowCount(m_fileDiffs.size());
    
    for (int i = 0; i < m_fileDiffs.size(); ++i) {
        const FileDiff& diff = m_fileDiffs[i];
        
        QTableWidgetItem* pathItem = new QTableWidgetItem(diff.path);
        pathItem->setData(Qt::UserRole, diff.path);
        m_comparisonTable->setItem(i, 0, pathItem);
        
        QString statusText;
        QColor statusColor;
        if (diff.status.startsWith("A")) {
            statusText = "Added";
            statusColor = QColor(63, 185, 80);
        } else if (diff.status.startsWith("M")) {
            statusText = "Modified";
            statusColor = QColor(210, 153, 34);
        } else if (diff.status.startsWith("D")) {
            statusText = "Deleted";
            statusColor = QColor(248, 81, 73);
        } else {
            statusText = diff.status;
            statusColor = QColor(139, 148, 158);
        }
        
        QTableWidgetItem* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(statusColor);
        m_comparisonTable->setItem(i, 1, statusItem);
        
        m_comparisonTable->setItem(i, 2, new QTableWidgetItem(QString::number(diff.addedLines)));
        m_comparisonTable->setItem(i, 3, new QTableWidgetItem(QString::number(diff.removedLines)));
    }
}

void BranchComparisonDialog::onMergeClicked() {
    if (m_branchA.isEmpty() || m_branchB.isEmpty()) return;
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Merge Branches",
        QString("Merge %1 into %2?\n\nThis will merge all changes from %1 into %2.").arg(m_branchB).arg(m_branchA),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        emit mergeRequested(m_branchB, m_branchA);
        accept();
    }
}

void BranchComparisonDialog::onFileDoubleClicked(QListWidgetItem* item) {
    QString path = item->text();
    if (path.isEmpty()) return;
    
    QProcess process;
    process.setWorkingDirectory(m_workingDir);
    process.start("git", QStringList() << "diff" << QString("%1...%2").arg(m_branchA).arg(m_branchB) << "--" << path);
    process.waitForFinished(5000);
    
    QString diff = QString::fromUtf8(process.readAllStandardOutput());
    
    QMessageBox::information(this, QString("Diff: %1").arg(QFileInfo(path).fileName()),
        diff.isEmpty() ? "No differences found." : diff.left(5000));
}

void BranchComparisonDialog::onRefreshClicked() {
    loadBranches();
    if (!m_branchA.isEmpty() && !m_branchB.isEmpty()) {
        compareSchematics(m_branchA, m_branchB);
    }
}