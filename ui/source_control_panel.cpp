#include "source_control_panel.h"
#include "schematic/dialogs/schematic_diff_dialog.h"
#include "schematic_timeline_view.h"
#include "branch_comparison_dialog.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "diff_viewer_dialog.h"
#include "branch_dialog.h"
#include "theme_manager.h"
#include "core/config_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMenu>
#include <QMessageBox>
#include <QInputDialog>
#include <QFont>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QFileDialog>
#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QCheckBox>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QFileInfo>
#include <QDirIterator>

class SCFileDelegate : public QStyledItemDelegate {
    SourceControlManager& m_mgr;
public:
    explicit SCFileDelegate(SourceControlManager& mgr, QObject* parent = nullptr) 
        : QStyledItemDelegate(parent), m_mgr(mgr) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if (!index.data(Qt::UserRole).isValid() || index.data(Qt::UserRole).toString().isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        painter->save();

        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &option, painter, option.widget);

        bool isSelected = option.state & QStyle::State_Selected;
        QString path = index.data(Qt::UserRole).toString();
        QString projName = index.data(Qt::UserRole + 2).toString();
        QString statusChar = index.data(Qt::UserRole + 3).toString();
        QColor statusColor = index.data(Qt::UserRole + 4).value<QColor>();

        QFileInfo fi(path);
        QString fileName = fi.fileName();
        QString dir = fi.path();
        if (dir == ".") dir = ""; 
        else dir = " • " + dir;

        QRect r = option.rect.adjusted(8, 2, -8, -2);

        painter->setPen(isSelected ? option.palette.highlightedText().color() : option.palette.text().color());
        QFont font = option.font;
        painter->setFont(font);
        
        QFontMetrics fm(font);
        int textY = r.top() + (r.height() - fm.height()) / 2 + fm.ascent();
        
        painter->drawText(r.left(), textY, fileName);
        int fnWidth = fm.horizontalAdvance(fileName);
        
        if (!isSelected) {
            painter->setPen(QColor(136, 136, 136));
        }
        QFont pathFont = font;
        pathFont.setPointSize(qMax(8, font.pointSize() - 1));
        painter->setFont(pathFont);
        painter->drawText(r.left() + fnWidth + 8, textY, projName + dir);

        if (!isSelected) {
           painter->setPen(statusColor);
        }
        QFont stFont = font;
        stFont.setBold(true);
        painter->setFont(stFont);
        int stWidth = QFontMetrics(stFont).horizontalAdvance(statusChar);
        painter->drawText(r.right() - stWidth, textY, statusChar);

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        if (index.data(Qt::UserRole).isValid() && !index.data(Qt::UserRole).toString().isEmpty()) {
            s.setHeight(s.height() + 4);
        }
        return s;
    }
};

SourceControlPanel::SourceControlPanel(QWidget* parent)
    : QWidget(parent), m_mgr(SourceControlManager::instance())
{
    setupUi();
    applyTheme();

    connect(&m_mgr, &SourceControlManager::statusUpdated, this, &SourceControlPanel::onStatusUpdated);
    connect(&m_mgr, &SourceControlManager::branchChanged, this, &SourceControlPanel::onBranchChanged);
    connect(&m_mgr, &SourceControlManager::operationStarted, this, &SourceControlPanel::onOperationStarted);
    connect(&m_mgr, &SourceControlManager::operationFinished, this, &SourceControlPanel::onOperationFinished);
    connect(&m_mgr, &SourceControlManager::repoChanged, this, &SourceControlPanel::onRepoChanged);

    connect(m_refreshBtn, &QPushButton::clicked, this, &SourceControlPanel::refresh);
    connect(m_commitBtn, &QPushButton::clicked, this, &SourceControlPanel::onCommitClicked);
    connect(m_commitPushBtn, &QPushButton::clicked, this, &SourceControlPanel::onCommitPushClicked);
    connect(m_pushBtn, &QPushButton::clicked, this, &SourceControlPanel::onPushClicked);
    connect(m_pullBtn, &QPushButton::clicked, this, &SourceControlPanel::onPullClicked);
    connect(m_fetchBtn, &QPushButton::clicked, this, &SourceControlPanel::onFetchClicked);
    connect(m_syncBtn, &QPushButton::clicked, this, &SourceControlPanel::onSyncClicked);
    connect(m_branchLabel, &QLabel::linkActivated, this, &SourceControlPanel::onBranchClicked);
    connect(m_initBtn, &QPushButton::clicked, this, &SourceControlPanel::onInitRepoClicked);
    connect(m_cloneBtn, &QPushButton::clicked, this, &SourceControlPanel::onCloneRepoClicked);
    connect(m_fileList, &QListWidget::itemDoubleClicked, this, &SourceControlPanel::onFileDoubleClicked);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &SourceControlPanel::onFileContextMenu);
    connect(m_stageAllBtn, &QPushButton::clicked, &m_mgr, &SourceControlManager::stageAll);
    connect(m_unstageAllBtn, &QPushButton::clicked, &m_mgr, &SourceControlManager::unstageAll);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &SourceControlPanel::onFilterChanged);
    connect(m_showStagedCheck, &QCheckBox::toggled, this, [this]() { updateFileList(); });
    connect(m_showUntrackedCheck, &QCheckBox::toggled, this, [this]() { updateFileList(); });

    connect(m_commitMsg, &QTextEdit::textChanged, this, [this]() {
        bool canCommit = !m_commitMsg->toPlainText().trimmed().isEmpty() && m_mgr.stagedCount() > 0;
        m_commitBtn->setEnabled(canCommit);
        m_commitPushBtn->setEnabled(canCommit);
    });

    connect(m_workspaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SourceControlPanel::onWorkspaceChanged);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyTheme();
    });

    updateWorkspaceCombo();
}

void SourceControlPanel::setupUi() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === Workspace Selector ===
    m_workspaceCombo = new QComboBox(this);
    m_workspaceCombo->setStyleSheet("QComboBox { padding: 4px; border: none; font-weight: bold; }");
    m_workspaceCombo->hide(); // Hidden by default, shown via updateWorkspaceCombo if > 1 folder
    mainLayout->addWidget(m_workspaceCombo);

    // === Header ===
    QWidget* header = new QWidget(this);
    header->setFixedHeight(32);
    header->setObjectName("SCHeader");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 4, 0);

    m_branchLabel = new QLabel(header);
    m_branchLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    m_branchLabel->setCursor(Qt::PointingHandCursor);
    m_branchLabel->setToolTip("Click to manage branches");
    headerLayout->addWidget(m_branchLabel, 1);

    m_summaryLabel = new QLabel(header);
    m_summaryLabel->setObjectName("SCSummary");
    m_summaryLabel->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    headerLayout->addWidget(m_summaryLabel, 0);

    m_refreshBtn = new QPushButton("Refresh", header);
    m_refreshBtn->setFixedHeight(24);
    headerLayout->addWidget(m_refreshBtn);

    mainLayout->addWidget(header);

    // === Not-a-repo view ===
    m_notRepoView = new QWidget(this);
    QVBoxLayout* notRepoLayout = new QVBoxLayout(m_notRepoView);
    notRepoLayout->setContentsMargins(16, 24, 16, 16);
    notRepoLayout->setSpacing(12);

    QLabel* noRepoLabel = new QLabel("This project is not a Git repository.", m_notRepoView);
    noRepoLabel->setWordWrap(true);
    noRepoLabel->setAlignment(Qt::AlignCenter);
    notRepoLayout->addWidget(noRepoLabel);

    m_initBtn = new QPushButton("Initialize Repository", m_notRepoView);
    notRepoLayout->addWidget(m_initBtn);

    m_cloneBtn = new QPushButton("Clone from GitHub...", m_notRepoView);
    notRepoLayout->addWidget(m_cloneBtn);

    notRepoLayout->addStretch();
    mainLayout->addWidget(m_notRepoView);

    // === Repo view (hidden until confirmed as repo) ===
    m_repoView = new QWidget(this);
    QVBoxLayout* repoLayout = new QVBoxLayout(m_repoView);
    repoLayout->setContentsMargins(0, 0, 0, 0);
    repoLayout->setSpacing(0);

    // --- Remote actions ---
    QWidget* remoteBar = new QWidget(m_repoView);
    remoteBar->setObjectName("SCRemoteBar");
    QHBoxLayout* remoteLayout = new QHBoxLayout(remoteBar);
    remoteLayout->setContentsMargins(12, 4, 12, 4);
    remoteLayout->setSpacing(6);

    m_pushBtn = new QPushButton("Push", remoteBar);
    m_pullBtn = new QPushButton("Pull", remoteBar);
    m_fetchBtn = new QPushButton("Fetch", remoteBar);
    m_syncBtn = new QPushButton("Sync", remoteBar);
    m_pushBtn->setFixedHeight(24);
    m_pullBtn->setFixedHeight(24);
    m_fetchBtn->setFixedHeight(24);
    m_syncBtn->setFixedHeight(24);
    remoteLayout->addWidget(m_pushBtn);
    remoteLayout->addWidget(m_pullBtn);
    remoteLayout->addWidget(m_fetchBtn);
    remoteLayout->addWidget(m_syncBtn);
    remoteLayout->addStretch();
    repoLayout->addWidget(remoteBar);

    // --- Changes section ---
    QWidget* changesSection = new QWidget(m_repoView);
    QVBoxLayout* changesLayout = new QVBoxLayout(changesSection);
    changesLayout->setContentsMargins(8, 4, 8, 4);
    changesLayout->setSpacing(4);

    m_changesLabel = new QLabel("Changes", changesSection);
    m_changesLabel->setStyleSheet("font-weight: bold; font-size: 11px; padding: 4px 0;");
    changesLayout->addWidget(m_changesLabel);

    QWidget* filterBar = new QWidget(changesSection);
    QHBoxLayout* filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(0, 2, 0, 2);
    filterLayout->setSpacing(6);
    m_filterEdit = new QLineEdit(filterBar);
    m_filterEdit->setPlaceholderText("Filter changes (path)...");
    m_filterEdit->setClearButtonEnabled(true);
    m_showStagedCheck = new QCheckBox("Staged", filterBar);
    m_showStagedCheck->setChecked(true);
    m_showUntrackedCheck = new QCheckBox("Untracked", filterBar);
    m_showUntrackedCheck->setChecked(true);
    filterLayout->addWidget(m_filterEdit, 1);
    filterLayout->addWidget(m_showStagedCheck);
    filterLayout->addWidget(m_showUntrackedCheck);
    changesLayout->addWidget(filterBar);

    m_fileList = new QListWidget(changesSection);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->setItemDelegate(new SCFileDelegate(m_mgr, m_fileList));
    changesLayout->addWidget(m_fileList, 1);

    QWidget* stageBar = new QWidget(changesSection);
    QHBoxLayout* stageLayout = new QHBoxLayout(stageBar);
    stageLayout->setContentsMargins(0, 2, 0, 2);
    stageLayout->setSpacing(4);
    m_stageAllBtn = new QPushButton("Stage All", stageBar);
    m_unstageAllBtn = new QPushButton("Unstage All", stageBar);
    m_stageAllBtn->setFixedHeight(24);
    m_unstageAllBtn->setFixedHeight(24);
    stageLayout->addWidget(m_stageAllBtn);
    stageLayout->addWidget(m_unstageAllBtn);
    stageLayout->addStretch();
    changesLayout->addWidget(stageBar);

    repoLayout->addWidget(changesSection, 1);

    // --- Commit section ---
    QWidget* commitSection = new QWidget(m_repoView);
    QVBoxLayout* commitLayout = new QVBoxLayout(commitSection);
    commitLayout->setContentsMargins(8, 4, 8, 4);
    commitLayout->setSpacing(4);

    QLabel* commitLabel = new QLabel("Message", commitSection);
    commitLabel->setStyleSheet("font-weight: bold; font-size: 11px; padding: 4px 0;");
    commitLayout->addWidget(commitLabel);

    m_commitMsg = new QTextEdit(commitSection);
    m_commitMsg->setPlaceholderText("Commit message...");
    m_commitMsg->setFixedHeight(60);
    commitLayout->addWidget(m_commitMsg);

    m_commitBtn = new QPushButton("Commit", commitSection);
    m_commitBtn->setEnabled(false);
    m_commitPushBtn = new QPushButton("Commit & Push", commitSection);
    m_commitPushBtn->setEnabled(false);
    m_amendCheck = new QCheckBox("Amend", commitSection);
    QWidget* commitActionBar = new QWidget(commitSection);
    QHBoxLayout* commitActionLayout = new QHBoxLayout(commitActionBar);
    commitActionLayout->setContentsMargins(0, 2, 0, 2);
    commitActionLayout->setSpacing(6);
    commitActionLayout->addWidget(m_amendCheck);
    commitActionLayout->addStretch();
    commitActionLayout->addWidget(m_commitBtn);
    commitActionLayout->addWidget(m_commitPushBtn);
    commitLayout->addWidget(commitActionBar);

    repoLayout->addWidget(commitSection);

    // --- Recent commits ---
    QWidget* logSection = new QWidget(m_repoView);
    QVBoxLayout* logLayout = new QVBoxLayout(logSection);
    logLayout->setContentsMargins(8, 4, 8, 4);
    logLayout->setSpacing(4);

    QLabel* logLabel = new QLabel("Recent Commits", logSection);
    logLabel->setStyleSheet("font-weight: bold; font-size: 11px; padding: 4px 0;");
    logLayout->addWidget(logLabel);

    m_commitList = new QListWidget(logSection);
    m_commitList->setAlternatingRowColors(true);
    logLayout->addWidget(m_commitList, 1);

    repoLayout->addWidget(logSection, 1);

    // --- Status label ---
    m_statusLabel = new QLabel(m_repoView);
    m_statusLabel->setFixedHeight(24);
    m_statusLabel->setContentsMargins(8, 2, 8, 2);
    m_statusLabel->setStyleSheet("font-size: 10px; color: #888;");
    repoLayout->addWidget(m_statusLabel);

    // --- Version control action buttons ---
    auto* vcLayout = new QHBoxLayout;
    vcLayout->setContentsMargins(0, 8, 0, 0);
    vcLayout->setSpacing(8);
    
    m_timelineBtn = new QPushButton("Timeline", m_repoView);
    m_timelineBtn->setToolTip("View commit timeline");
    vcLayout->addWidget(m_timelineBtn);
    
    m_compareBranchesBtn = new QPushButton("Compare Branches", m_repoView);
    m_compareBranchesBtn->setToolTip("Compare two branches");
    vcLayout->addWidget(m_compareBranchesBtn);
    
    vcLayout->addStretch();
    repoLayout->addLayout(vcLayout);

    mainLayout->addWidget(m_repoView);

    // Initial state
    m_notRepoView->hide();
    m_repoView->hide();
}

void SourceControlPanel::refresh() {
    updateWorkspaceCombo();
    m_mgr.refresh();
}

void SourceControlPanel::onRepoChanged(bool isRepo) {
    if (isRepo) {
        showRepoView();
    } else {
        showNotRepoView();
    }
}

void SourceControlPanel::showNotRepoView() {
    m_notRepoView->show();
    m_repoView->hide();
}

void SourceControlPanel::showRepoView() {
    m_notRepoView->hide();
    m_repoView->show();
}

void SourceControlPanel::onStatusUpdated() {
    updateFileList();
    updateCommitList();
    bool canCommit = !m_commitMsg->toPlainText().trimmed().isEmpty() && m_mgr.stagedCount() > 0;
    m_commitBtn->setEnabled(canCommit);
    m_commitPushBtn->setEnabled(canCommit);
    int staged = m_mgr.stagedCount();
    int unstaged = m_mgr.unstagedCount();
    int untracked = m_mgr.untrackedCount();
    m_summaryLabel->setText(QString("Staged %1  |  Changes %2  |  Untracked %3")
        .arg(staged).arg(unstaged).arg(untracked));
    m_commitBtn->setText(QString("Commit (%1)").arg(staged));
    m_commitPushBtn->setText(QString("Commit & Push (%1)").arg(staged));
}

void SourceControlPanel::onBranchChanged(const QString& branch) {
    QString displayBranch = branch.isEmpty() ? "No Branch" : branch;
    m_branchLabel->setText(QString("<a href='#' style='text-decoration:none; color:inherit;'>  %1  </a>").arg(displayBranch));
}

void SourceControlPanel::updateWorkspaceCombo() {
    QStringList folders = ConfigManager::instance().workspaceFolders();
    
    // Disconnect so we don't trigger updates during population
    disconnect(m_workspaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SourceControlPanel::onWorkspaceChanged);
    
    m_workspaceCombo->clear();
    
    if (folders.size() > 1) {
        m_workspaceCombo->show();
        for (const QString& folder : folders) {
            QFileInfo fi(folder);
            m_workspaceCombo->addItem(fi.fileName(), folder);
        }
        
        // Try to select the currently managed project
        QString currentProject = m_mgr.projectDir();
        int idx = m_workspaceCombo->findData(currentProject);
        if (idx >= 0) {
            m_workspaceCombo->setCurrentIndex(idx);
        } else {
            m_workspaceCombo->setCurrentIndex(0);
        }
    } else {
        m_workspaceCombo->hide();
    }
    
    // Reconnect
    connect(m_workspaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SourceControlPanel::onWorkspaceChanged);
}

void SourceControlPanel::onWorkspaceChanged(int index) {
    if (index < 0) return;
    QString folderPath = m_workspaceCombo->itemData(index).toString();
    m_mgr.setProjectDir(folderPath);
    refresh();
}

void SourceControlPanel::updateFileList() {
    m_fileList->clear();
    auto files = m_mgr.fileStatuses();
    int staged = 0, unstaged = 0, untracked = 0;

    QVector<GitFileStatus> stagedFiles;
    QVector<GitFileStatus> unstagedFiles;
    QVector<GitFileStatus> untrackedFiles;

    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
    QColor stagedColor = isLight ? QColor("#0f766e") : QColor("#4ec9b0");
    QColor unstagedColor = isLight ? QColor("#b45309") : QColor("#ce9178");
    QColor untrackedColor = isLight ? QColor("#64748b") : QColor("#808080");
    QColor headerBg = isLight ? QColor("#f1f5f9") : QColor("#2a2d2e");
    QColor headerFg = isLight ? QColor("#475569") : QColor("#a9b1ba");

    for (const auto& f : files) {
        if (f.staged) {
            staged++;
        } else if (f.isUntracked) {
            untracked++;
        } else {
            unstaged++;
        }

        if (!m_filterText.isEmpty() && !f.path.contains(m_filterText, Qt::CaseInsensitive)) {
            continue;
        }

        if (f.staged) stagedFiles.append(f);
        else if (f.isUntracked) untrackedFiles.append(f);
        else unstagedFiles.append(f);
    }

    auto addHeader = [&](const QString& title, int count) {
        QListWidgetItem* header = new QListWidgetItem(QString("%1 (%2)").arg(title).arg(count));
        header->setFlags(Qt::NoItemFlags);
        QFont f = header->font();
        f.setBold(true);
        header->setFont(f);
        header->setBackground(headerBg);
        header->setForeground(headerFg);
        m_fileList->addItem(header);
    };

    auto addFileItem = [&](const GitFileStatus& f, const QColor& /* groupColor */, const QString& statusText) {
        QListWidgetItem* item = new QListWidgetItem();
        item->setToolTip(QString("%1: %2").arg(statusText, f.path));
        item->setData(Qt::UserRole, f.path);
        item->setData(Qt::UserRole + 1, f.staged);
        
        QString projName = QFileInfo(m_mgr.projectDir()).fileName();
        if (projName.isEmpty()) projName = "repo";
        item->setData(Qt::UserRole + 2, projName);
        
        QString statusChar = f.staged ? f.indexStatus : (f.isUntracked ? "U" : f.worktreeStatus);
        if (statusChar.length() > 1) statusChar = statusChar.left(1);
        if (statusChar.isEmpty()) statusChar = "M";
        if (statusChar == "?") statusChar = "U";
        item->setData(Qt::UserRole + 3, statusChar);
        
        // Apply precise VS Code color mapping
        QColor letterColor;
        if (statusChar == "U" || statusChar == "A") {
            letterColor = isLight ? QColor("#1ca040") : QColor("#73c991"); // Green
        } else if (statusChar == "M") {
            letterColor = isLight ? QColor("#895503") : QColor("#e2c08d"); // Orange/Yellow
        } else if (statusChar == "D") {
            letterColor = isLight ? QColor("#c33828") : QColor("#c74e39"); // Red
        } else {
            letterColor = isLight ? QColor("#64748b") : QColor("#808080"); // Grey
        }
        item->setData(Qt::UserRole + 4, letterColor);
        
        m_fileList->addItem(item);
    };

    bool showStaged = m_showStagedCheck && m_showStagedCheck->isChecked();
    bool showUntracked = m_showUntrackedCheck && m_showUntrackedCheck->isChecked();

    if (showStaged && !stagedFiles.isEmpty()) {
        addHeader("Staged Changes", staged);
        for (const auto& f : stagedFiles) {
            QString statusText = (f.indexStatus == "A") ? "Added" :
                (f.indexStatus == "M") ? "Modified" :
                (f.indexStatus == "D") ? "Deleted" :
                (f.indexStatus == "R") ? "Renamed" : f.indexStatus;
            addFileItem(f, stagedColor, statusText);
        }
    }

    if (!unstagedFiles.isEmpty()) {
        addHeader("Changes", unstaged);
        for (const auto& f : unstagedFiles) {
            QString statusText = (f.worktreeStatus == "M") ? "Modified" :
                (f.worktreeStatus == "D") ? "Deleted" : f.worktreeStatus;
            addFileItem(f, unstagedColor, statusText);
        }
    }

    if (showUntracked && !untrackedFiles.isEmpty()) {
        addHeader("Untracked", untracked);
        for (const auto& f : untrackedFiles) {
            addFileItem(f, untrackedColor, "Untracked");
        }
    }

    if (m_fileList->count() == 0) {
        QListWidgetItem* emptyItem = new QListWidgetItem("No changes");
        emptyItem->setFlags(Qt::NoItemFlags);
        emptyItem->setForeground(headerFg);
        m_fileList->addItem(emptyItem);
    }

    m_changesLabel->setText(QString("Changes (%1)").arg(files.size()));
}

void SourceControlPanel::updateCommitList() {
    m_commitList->clear();
    auto commits = m_mgr.recentCommits();
    for (const auto& c : commits) {
        QString display = QString("%1  %2").arg(c.shortSha, c.subject);
        QListWidgetItem* item = new QListWidgetItem(display);
        item->setToolTip(QString("%1\n%2\n%3").arg(c.sha, c.author, c.date));
        item->setData(Qt::UserRole, c.sha);
        m_commitList->addItem(item);
    }
}

void SourceControlPanel::onCommitClicked() {
    QString msg = m_commitMsg->toPlainText().trimmed();
    if (msg.isEmpty()) return;
    m_mgr.commit(msg, m_amendCheck && m_amendCheck->isChecked());
    m_commitMsg->clear();
}

void SourceControlPanel::onCommitPushClicked() {
    QString msg = m_commitMsg->toPlainText().trimmed();
    if (msg.isEmpty()) return;
    m_mgr.commitAndPush(msg, m_amendCheck && m_amendCheck->isChecked());
    m_commitMsg->clear();
}

void SourceControlPanel::onPushClicked() {
    m_mgr.push();
}

void SourceControlPanel::onPullClicked() {
    m_mgr.pull();
}

void SourceControlPanel::onFetchClicked() {
    m_mgr.fetch();
}

void SourceControlPanel::onSyncClicked() {
    m_mgr.sync();
}

void SourceControlPanel::onBranchClicked() {
    BranchDialog dlg(this);
    dlg.setBranches(m_mgr.fileStatuses().isEmpty()
        ? QVector<GitBranch>() : GitBackend(m_mgr.projectDir()).branches(),
        m_mgr.currentBranch());
    dlg.setRemotes(m_mgr.remoteNames());

    connect(&dlg, &BranchDialog::switchRequested, &m_mgr, &SourceControlManager::switchBranch);
    connect(&dlg, &BranchDialog::createRequested, &m_mgr, &SourceControlManager::createBranch);
    connect(&dlg, &BranchDialog::deleteRequested, &m_mgr, &SourceControlManager::deleteBranch);
    connect(&dlg, &BranchDialog::mergeRequested, &m_mgr, &SourceControlManager::mergeBranch);
    connect(&dlg, &BranchDialog::addRemoteRequested, &m_mgr, &SourceControlManager::addRemote);

    dlg.exec();
}

void SourceControlPanel::onInitRepoClicked() {
    GitBackend backend(m_mgr.projectDir());
    if (backend.initRepo()) {
        m_mgr.refresh();
    } else {
        QMessageBox::warning(this, "Init Repository", "Failed to initialize repository.\n" + backend.lastError());
    }
}

void SourceControlPanel::onCloneRepoClicked() {
    bool ok = false;
    QString url = QInputDialog::getText(this, "Clone Repository",
        "GitHub URL:", QLineEdit::Normal, "https://github.com/user/repo.git", &ok);
    if (!ok || url.isEmpty()) return;

    QString destDir = QFileDialog::getExistingDirectory(this, "Clone Destination");
    if (destDir.isEmpty()) return;

    GitBackend backend;
    if (backend.cloneRepo(url, destDir)) {
        m_mgr.setProjectDir(destDir);
    } else {
        QMessageBox::warning(this, "Clone Failed", backend.lastError());
    }
}

void SourceControlPanel::onFileDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    if (!(item->flags() & Qt::ItemIsSelectable)) return;
    QString path = item->data(Qt::UserRole).toString();
    bool staged = item->data(Qt::UserRole + 1).toBool();

    GitBackend backend(m_mgr.projectDir());
    QString diff;
    if (staged) {
        diff = backend.diffCached();
    } else {
        diff = backend.diffFile(path);
    }

    if (diff.isEmpty()) {
        diff = "No diff available. The file may be untracked or not yet committed.";
    }

    DiffViewerDialog dlg(this);
    dlg.setDiff(diff, path);
    dlg.exec();
}

void SourceControlPanel::onVisualDiffRequested(const QString& path) {
    // 1. Get current worktree content
    QString fullPath = m_mgr.projectDir() + "/" + path;
    QFile f(fullPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QByteArray dataB = f.readAll();
    f.close();

    // 2. Get HEAD content
    QString contentA = m_mgr.getFileContent("HEAD", path);
    if (contentA.isEmpty()) {
        QMessageBox::information(this, "Visual Diff", "Could not retrieve HEAD version. Is this a new file?");
        return;
    }

    QJsonObject jsonA = QJsonDocument::fromJson(contentA.toUtf8()).object();
    QJsonObject jsonB = QJsonDocument::fromJson(dataB).object();

    if (jsonA.isEmpty() || jsonB.isEmpty()) {
        QMessageBox::warning(this, "Visual Diff", "Failed to parse schematic JSON for comparison.");
        return;
    }

    SchematicDiffDialog dlg(this);
    dlg.compare(jsonA, jsonB, "HEAD VERSION", "WORKING COPY");
    dlg.exec();
}

void SourceControlPanel::onFileContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_fileList->itemAt(pos);
    if (!item) return;
    if (!(item->flags() & Qt::ItemIsSelectable)) return;

    QString path = item->data(Qt::UserRole).toString();
    bool staged = item->data(Qt::UserRole + 1).toBool();

    QMenu menu(this);

    if (staged) {
        QAction* unstageAct = menu.addAction("Unstage");
        connect(unstageAct, &QAction::triggered, this, [this, path]() {
            m_mgr.unstageFile(path);
        });
    } else {
        QAction* stageAct = menu.addAction("Stage");
        connect(stageAct, &QAction::triggered, this, [this, path]() {
            m_mgr.stageFile(path);
        });
    }

    menu.addSeparator();

    QAction* diffAct = menu.addAction("View Diff");
    connect(diffAct, &QAction::triggered, this, [this, item]() {
        onFileDoubleClicked(item);
    });

    if (path.endsWith(".sch")) {
        QAction* visualDiffAct = menu.addAction("Visual Schematic Diff");
        connect(visualDiffAct, &QAction::triggered, this, [this, path]() {
            onVisualDiffRequested(path);
        });
    }

    QAction* openAct = menu.addAction("Open File");
    connect(openAct, &QAction::triggered, this, [this, path]() {
        QString fullPath = m_mgr.projectDir() + "/" + path;
        QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath));
    });

    if (!staged) {
        menu.addSeparator();
        QAction* discardAct = menu.addAction("Discard Changes");
        connect(discardAct, &QAction::triggered, this, [this, path]() {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Discard",
                QString("Discard all changes to '%1'?").arg(path),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                m_mgr.discardChanges(path);
            }
        });
    }

    QAction* copyAct = menu.addAction("Copy Path");
    connect(copyAct, &QAction::triggered, this, [path]() {
        QApplication::clipboard()->setText(path);
    });

    menu.exec(m_fileList->viewport()->mapToGlobal(pos));
}

void SourceControlPanel::onOperationStarted(const QString& op) {
    m_statusLabel->setText(op + "...");
    m_refreshBtn->setEnabled(false);
}

void SourceControlPanel::onOperationFinished(const QString& op, bool success, const QString& msg) {
    m_statusLabel->setText(msg);
    m_refreshBtn->setEnabled(true);
    if (!success) {
        QMessageBox::warning(this, op + " Failed", msg);
    }
}

void SourceControlPanel::onFilterChanged(const QString& text) {
    m_filterText = text.trimmed();
    updateFileList();
}

void SourceControlPanel::applyTheme() {
    bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;

    if (isLight) {
        setStyleSheet(
            "QWidget#SCHeader { background-color: #f8fafc; border-bottom: 1px solid #e2e8f0; }"
            "QWidget#SCRemoteBar { background-color: #f8fafc; border-bottom: 1px solid #e2e8f0; }"
            "QListWidget { background-color: #ffffff; color: #1e293b; border: 1px solid #e2e8f0; border-radius: 6px; }"
            "QListWidget::item { padding: 4px 8px; border-radius: 4px; margin: 1px 4px; }"
            "QListWidget::item:selected { background-color: #dbeafe; color: #1e40af; }"
            "QListWidget::item:alternate { background-color: #f8fafc; }"
            "QListWidget::item:hover { background-color: #f1f5f9; }"
            "QTextEdit { background-color: #ffffff; color: #1e293b; border: 1px solid #cbd5e1; border-radius: 6px; padding: 6px; }"
            "QTextEdit:focus { border-color: #3b82f6; }"
            "QLineEdit { background-color: #ffffff; color: #1e293b; border: 1px solid #cbd5e1; border-radius: 6px; padding: 4px 8px; }"
            "QLineEdit:focus { border-color: #3b82f6; }"
            "QPushButton { background-color: #ffffff; color: #334155; border: 1px solid #cbd5e1; padding: 4px 12px; border-radius: 6px; font-size: 11px; font-weight: 500; }"
            "QPushButton:hover { background-color: #f1f5f9; border-color: #94a3b8; }"
            "QPushButton:pressed { background-color: #e2e8f0; border-color: #64748b; }"
            "QPushButton:disabled { background-color: #f8fafc; color: #94a3b8; border-color: #e2e8f0; }"
            "QLabel { color: #334155; }"
            "QLabel#SCSummary { color: #64748b; font-size: 10px; }"
            "QCheckBox { color: #475569; font-size: 10px; }"
        );

        // Accent commit button
        if (m_commitBtn) {
            m_commitBtn->setStyleSheet(
                "QPushButton { background-color: #3b82f6; color: white; border: none; padding: 6px 16px; border-radius: 6px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #2563eb; }"
                "QPushButton:pressed { background-color: #1d4ed8; }"
                "QPushButton:disabled { background-color: #e2e8f0; color: #94a3b8; }"
            );
        }
        if (m_commitPushBtn) {
            m_commitPushBtn->setStyleSheet(
                "QPushButton { background-color: #0ea5e9; color: white; border: none; padding: 6px 16px; border-radius: 6px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #0284c7; }"
                "QPushButton:pressed { background-color: #0369a1; }"
                "QPushButton:disabled { background-color: #e2e8f0; color: #94a3b8; }"
            );
        }
        // Accent branch label
        if (m_branchLabel) {
            m_branchLabel->setStyleSheet(
                "font-weight: bold; font-size: 11px; color: #1e40af; background-color: #dbeafe; "
                "border: 1px solid #93c5fd; border-radius: 10px; padding: 2px 8px;"
            );
        }
    } else {
        setStyleSheet(
            "QWidget#SCHeader { background-color: #252526; border-bottom: 1px solid #3c3c3c; }"
            "QWidget#SCRemoteBar { background-color: #252526; border-bottom: 1px solid #3c3c3c; }"
            "QListWidget { background-color: #1e1e1e; color: #cccccc; border: 1px solid #3c3c3c; border-radius: 6px; }"
            "QListWidget::item { padding: 4px 8px; border-radius: 4px; margin: 1px 4px; }"
            "QListWidget::item:selected { background-color: #094771; }"
            "QListWidget::item:alternate { background-color: #1a1a1a; }"
            "QListWidget::item:hover { background-color: #2a2d2e; }"
            "QTextEdit { background-color: #3c3c3c; color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 6px; padding: 6px; }"
            "QTextEdit:focus { border-color: #0078d4; }"
            "QLineEdit { background-color: #3c3c3c; color: #d4d4d4; border: 1px solid #3c3c3c; border-radius: 6px; padding: 4px 8px; }"
            "QLineEdit:focus { border-color: #0078d4; }"
            "QPushButton { background-color: #2d2d30; color: #cccccc; border: 1px solid #3c3c3c; padding: 4px 12px; border-radius: 6px; font-size: 11px; }"
            "QPushButton:hover { background-color: #3c3c3c; border-color: #555; }"
            "QPushButton:pressed { background-color: #094771; }"
            "QPushButton:disabled { background-color: #2d2d30; color: #555; border-color: #333; }"
            "QLabel { color: #cccccc; }"
            "QLabel#SCSummary { color: #8f99a3; font-size: 10px; }"
            "QCheckBox { color: #c0c0c0; font-size: 10px; }"
        );

        if (m_commitBtn) {
            m_commitBtn->setStyleSheet(
                "QPushButton { background-color: #0e639c; color: white; border: none; padding: 6px 16px; border-radius: 6px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #1177bb; }"
                "QPushButton:pressed { background-color: #094771; }"
                "QPushButton:disabled { background-color: #3c3c3c; color: #666; }"
            );
        }
        if (m_commitPushBtn) {
            m_commitPushBtn->setStyleSheet(
                "QPushButton { background-color: #0098d4; color: white; border: none; padding: 6px 16px; border-radius: 6px; font-size: 11px; font-weight: 600; }"
                "QPushButton:hover { background-color: #11a7e4; }"
                "QPushButton:pressed { background-color: #0078d4; }"
                "QPushButton:disabled { background-color: #3c3c3c; color: #666; }"
            );
        }
        if (m_branchLabel) {
            m_branchLabel->setStyleSheet(
                "font-weight: bold; font-size: 11px; color: #72b8f2; background-color: #1a3a5c; "
                "border: 1px solid #0e639c; border-radius: 10px; padding: 2px 8px;"
            );
        }
    }
}

void SourceControlPanel::onTimelineClicked() {
    QString workingDir = m_mgr.projectDir();
    if (workingDir.isEmpty()) return;

    SchematicTimelineView* timeline = new SchematicTimelineView(this);
    timeline->setWorkingDir(workingDir);

    QString projectDir = workingDir;
    QDirIterator it(projectDir, {"*.sch", "*.flxsch"}, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        timeline->loadHistory(it.next());
    }

    timeline->show();
}

void SourceControlPanel::onCompareBranchesClicked() {
    QString workingDir = m_mgr.projectDir();
    if (workingDir.isEmpty()) return;

    BranchComparisonDialog* dialog = new BranchComparisonDialog(workingDir, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    connect(dialog, &BranchComparisonDialog::mergeRequested, this, [this](const QString& source, const QString& target) {
        Q_UNUSED(source);
        Q_UNUSED(target);
        // TODO: Implement merge with source/target parameters
        // For now, just merge the source branch
        m_mgr.mergeBranch(source);
    });

    dialog->exec();
}
