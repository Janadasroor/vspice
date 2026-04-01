#include "schematic_timeline_view.h"

#include "../schematic/io/schematic_file_io.h"
#include "../schematic/analysis/schematic_diff_engine.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QPainter>
#include <QMessageBox>
#include <QInputDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QApplication>

SchematicTimelineView::SchematicTimelineView(QWidget* parent)
    : QWidget(parent)
    , m_commitList(nullptr)
    , m_thumbnailScroll(nullptr)
    , m_thumbnailLabel(nullptr)
    , m_commitInfoLabel(nullptr)
    , m_diffSummaryLabel(nullptr)
    , m_compareBtn(nullptr)
    , m_checkoutBtn(nullptr)
    , m_createBranchBtn(nullptr)
    , m_refreshBtn(nullptr)
    , m_infoPanel(nullptr) {
    setupUi();
}

SchematicTimelineView::~SchematicTimelineView() {
}

void SchematicTimelineView::setupUi() {
    setStyleSheet("background: #0a0a0c;");
    
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);
    
    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);
    
    auto* headerLayout = new QHBoxLayout;
    QLabel* titleLabel = new QLabel("Commit History", this);
    titleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #e6edf3;");
    headerLayout->addWidget(titleLabel);
    
    m_refreshBtn = new QPushButton("Refresh", this);
    m_refreshBtn->setStyleSheet("QPushButton { background: #21262d; color: #e6edf3; border: 1px solid #30363d; border-radius: 4px; padding: 6px 12px; } QPushButton:hover { background: #30363d; }");
    headerLayout->addWidget(m_refreshBtn);
    leftLayout->addLayout(headerLayout);
    
    m_commitList = new QListWidget(this);
    m_commitList->setStyleSheet("QListWidget { background: #161b22; border: 1px solid #30363d; border-radius: 6px; } QListWidget::item { padding: 8px; border-bottom: 1px solid #21262d; } QListWidget::item:selected { background: #1f6feb; } QListWidget::item:hover { background: #21262d; }");
    m_commitList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_commitList, 1);
    
    auto* actionLayout = new QHBoxLayout;
    m_compareBtn = new QPushButton("Compare", this);
    m_compareBtn->setStyleSheet("QPushButton { background: #238636; color: white; border: none; border-radius: 4px; padding: 8px 16px; } QPushButton:hover { background: #2ea043; } QPushButton:disabled { background: #30363d; color: #8b949e; }");
    m_compareBtn->setEnabled(false);
    actionLayout->addWidget(m_compareBtn);
    
    m_checkoutBtn = new QPushButton("Checkout", this);
    m_checkoutBtn->setStyleSheet("QPushButton { background: #1f6feb; color: white; border: none; border-radius: 4px; padding: 8px 16px; } QPushButton:hover { background: #388bfd; }");
    actionLayout->addWidget(m_checkoutBtn);
    
    m_createBranchBtn = new QPushButton("Branch", this);
    m_createBranchBtn->setStyleSheet("QPushButton { background: #8b949e; color: white; border: none; border-radius: 4px; padding: 8px 16px; } QPushButton:hover { background: #a0a8b4; }");
    actionLayout->addWidget(m_createBranchBtn);
    
    leftLayout->addLayout(actionLayout);
    mainLayout->addWidget(leftPanel, 1);
    
    m_infoPanel = new QFrame(this);
    m_infoPanel->setStyleSheet("QFrame { background: #161b22; border: 1px solid #30363d; border-radius: 8px; }");
    auto* infoLayout = new QVBoxLayout(m_infoPanel);
    infoLayout->setContentsMargins(16, 16, 16, 16);
    infoLayout->setSpacing(12);
    
    m_commitInfoLabel = new QLabel("Select a commit to view details", this);
    m_commitInfoLabel->setStyleSheet("font-size: 14px; color: #e6edf3; font-weight: bold;");
    m_commitInfoLabel->setWordWrap(true);
    infoLayout->addWidget(m_commitInfoLabel);
    
    m_diffSummaryLabel = new QLabel("", this);
    m_diffSummaryLabel->setStyleSheet("color: #8b949e; font-size: 12px;");
    m_diffSummaryLabel->setWordWrap(true);
    infoLayout->addWidget(m_diffSummaryLabel);
    
    m_thumbnailScroll = new QScrollArea(this);
    m_thumbnailScroll->setStyleSheet("QScrollArea { background: #21262d; border: 1px solid #30363d; border-radius: 4px; }");
    m_thumbnailScroll->setWidgetResizable(true);
    m_thumbnailScroll->setMinimumSize(400, 300);
    
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setStyleSheet("color: #8b949e;");
    m_thumbnailLabel->setText("No preview available");
    m_thumbnailScroll->setWidget(m_thumbnailLabel);
    
    infoLayout->addWidget(m_thumbnailScroll, 1);
    mainLayout->addWidget(m_infoPanel, 1);
    
    connect(m_commitList, &QListWidget::currentItemChanged, this, &SchematicTimelineView::onCommitSelected);
    connect(m_compareBtn, &QPushButton::clicked, this, &SchematicTimelineView::onCompareClicked);
    connect(m_checkoutBtn, &QPushButton::clicked, this, &SchematicTimelineView::onCheckoutClicked);
    connect(m_createBranchBtn, &QPushButton::clicked, this, &SchematicTimelineView::onCreateBranchClicked);
    connect(m_refreshBtn, &QPushButton::clicked, this, &SchematicTimelineView::onRefreshClicked);
}

void SchematicTimelineView::setWorkingDir(const QString& dir) {
    m_workingDir = dir;
}

void SchematicTimelineView::loadHistory(const QString& schematicPath) {
    m_schematicPath = schematicPath;
    loadCommits();
}

void SchematicTimelineView::loadCommits() {
    m_commitList->clear();
    m_commits.clear();
    m_thumbnails.clear();
    
    if (m_workingDir.isEmpty()) return;
    
    GitBackend git(m_workingDir);
    if (!git.isGitRepo()) return;
    
    m_commits = git.log(100);
    
    QString relativePath = QFileInfo(m_schematicPath).fileName();
    
    for (const GitCommit& commit : m_commits) {
        QListWidgetItem* item = new QListWidgetItem(m_commitList);
        
        QString status = git.diffFile(relativePath).isEmpty() ? "" : "modified";
        
        QString displayText = QString("%1 - %2\n%3")
            .arg(commit.shortSha)
            .arg(commit.date)
            .arg(commit.subject);
        
        item->setText(displayText);
        item->setData(Qt::UserRole, commit.sha);
        
        m_commitList->addItem(item);
    }
    
    if (m_commitList->count() > 0) {
        m_commitList->setCurrentRow(0);
    }
}

void SchematicTimelineView::onCommitSelected(QListWidgetItem* item) {
    if (!item) return;
    
    QString sha = item->data(Qt::UserRole).toString();
    m_currentSha = sha;
    
    GitBackend git(m_workingDir);
    GitCommit commit;
    for (const GitCommit& c : m_commits) {
        if (c.sha == sha) {
            commit = c;
            break;
        }
    }
    
    m_commitInfoLabel->setText(QString("<b>%1</b><br>Author: %2<br>Date: %3")
        .arg(commit.subject)
        .arg(commit.author)
        .arg(commit.date));
    
    QString content = getSchematicContent(sha, QFileInfo(m_schematicPath).fileName());
    if (!content.isEmpty()) {
        QPixmap thumb = renderSchematicThumbnail(content);
        if (!thumb.isNull()) {
            m_thumbnailLabel->setPixmap(thumb.scaled(380, 280, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_thumbnails[sha] = thumb;
        }
    }
    
    m_compareBtn->setEnabled(true);
    
    emit commitSelected(sha, QJsonDocument::fromJson(content.toUtf8()).object());
}

void SchematicTimelineView::onCompareClicked() {
    if (m_currentSha.isEmpty()) return;
    
    bool ok = false;
    QStringList shaList;
    for (const GitCommit& c : m_commits) {
        if (c.sha != m_currentSha) {
            shaList.append(QString("%1 - %2").arg(c.shortSha).arg(c.subject));
        }
    }
    
    QString selected = QInputDialog::getItem(this, "Compare With", "Select commit to compare:", shaList, 0, false, &ok);
    if (!ok || selected.isEmpty()) return;
    
    QString compareSha = selected.split(" - ").first();
    emit compareCommitsRequested(m_currentSha, compareSha);
}

void SchematicTimelineView::onCheckoutClicked() {
    if (m_currentSha.isEmpty()) return;
    emit checkoutRequested(m_currentSha);
}

void SchematicTimelineView::onCreateBranchClicked() {
    if (m_currentSha.isEmpty()) return;
    
    bool ok = false;
    QString branchName = QInputDialog::getText(this, "Create Branch", "Branch name:", QLineEdit::Normal, "", &ok);
    if (!ok || branchName.isEmpty()) return;
    
    emit createBranchRequested(m_currentSha, branchName);
}

void SchematicTimelineView::onRefreshClicked() {
    loadHistory(m_schematicPath);
}

QString SchematicTimelineView::getSchematicContent(const QString& sha, const QString& path) const {
    if (m_workingDir.isEmpty()) return QString();
    
    QProcess process;
    process.setWorkingDirectory(m_workingDir);
    process.start("git", QStringList() << "show" << QString("%1:%2").arg(sha).arg(path));
    process.waitForFinished(5000);
    
    return QString::fromUtf8(process.readAllStandardOutput());
}

QPixmap SchematicTimelineView::renderSchematicThumbnail(const QString& jsonContent) {
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return QPixmap();
    }
    
    QJsonObject root = doc.object();
    QJsonArray items = root.value("items").toArray();
    if (items.isEmpty()) {
        return QPixmap();
    }
    
    QPixmap pixmap(400, 300);
    pixmap.fill(QColor(30, 30, 34));
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    int x = 20, y = 20;
    int itemWidth = 40, itemHeight = 30;
    int cols = 0, rows = 0;
    
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        QString type = item.value("type").toString();
        
        QColor color;
        if (type == "resistor") color = QColor(255, 165, 0);
        else if (type == "capacitor") color = QColor(0, 191, 255);
        else if (type == "inductor") color = QColor(255, 215, 0);
        else if (type == "voltage_source") color = QColor(255, 0, 0);
        else if (type == "gnd") color = QColor(0, 255, 0);
        else if (type == "wire") color = QColor(200, 200, 200);
        else color = QColor(150, 150, 150);
        
        painter.fillRect(x, y, itemWidth, itemHeight, color);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(x + 2, y + 12, type.left(3));
        
        x += itemWidth + 5;
        cols++;
        if (cols >= 8) {
            cols = 0;
            x = 20;
            y += itemHeight + 5;
            rows++;
            if (rows >= 8) break;
        }
    }
    
    painter.end();
    return pixmap;
}

void SchematicTimelineView::updateSelectedCommitInfo() {
    if (m_currentSha.isEmpty()) return;
    
    GitBackend git(m_workingDir);
    QString diff = git.diffFile(QFileInfo(m_schematicPath).fileName());
    
    if (!diff.isEmpty()) {
        int added = diff.count("+");
        int removed = diff.count("-");
        m_diffSummaryLabel->setText(QString("+%1 / -%2 lines changed").arg(added).arg(removed));
    } else {
        m_diffSummaryLabel->setText("No changes in this commit");
    }
}