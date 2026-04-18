#include "project_explorer_widget.h"
#include "../schematic/io/netlist_to_schematic.h"
#include "../core/theme_manager.h"
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QInputDialog>
#include <QMessageBox>
#include <QToolButton>
#include <QAction>
#include <QRegularExpression>
#include <QProcess>
#include <QDateTime>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>

class ProjectExplorerDelegate : public QStyledItemDelegate {
    QFileSystemModel* m_fsModel;
    QSortFilterProxyModel* m_proxy;
    const QHash<QString, QString>* m_statusMap;
public:
    explicit ProjectExplorerDelegate(QFileSystemModel* fsModel, QSortFilterProxyModel* proxy, const QHash<QString, QString>* statusMap, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_fsModel(fsModel), m_proxy(proxy), m_statusMap(statusMap) {}

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        
        QModelIndex srcIndex = m_proxy->mapToSource(index);
        QString path = m_fsModel->filePath(srcIndex);
        
        if (!m_statusMap->value(path).isEmpty()) {
            option->text = ""; // clear text to prevent base paint from drawing it
        }
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        // Base paint will use our initStyleOption and draw everything EXCEPT text
        QStyledItemDelegate::paint(painter, option, index);
        
        QModelIndex srcIndex = m_proxy->mapToSource(index);
        QString path = m_fsModel->filePath(srcIndex);
        
        QString status = m_statusMap->value(path);
        QColor color;
        if (!status.isEmpty()) {
            bool isLight = ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light;
            if (status.contains("U") || status.contains("A") || status == "?") {
                color = isLight ? QColor("#16825D") : QColor("#73C991");
            } else if (status.contains("M")) {
                color = isLight ? QColor("#895503") : QColor("#E2C08D");
            } else if (status.contains("D")) {
                color = isLight ? QColor("#AD322D") : QColor("#F14C4C");
            }
        }
        
        if (color.isValid()) {
            QStyleOptionViewItem opt = option;
            QStyledItemDelegate::initStyleOption(&opt, index); // Formally retrieve the real text
            
            painter->save();
            painter->setPen(color);
            
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
            QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
            
            // Ensure proper font rendering without artificial margins
            painter->setFont(opt.font);
            painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, opt.text);
            painter->restore();
        }
    }
};

class FileFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit FileFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    QString m_rootPath;
    QStringList m_workspaceFolders;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (role == Qt::DisplayRole && !m_rootPath.isEmpty()) {
            QFileSystemModel* fs = qobject_cast<QFileSystemModel*>(sourceModel());
            if (fs) {
                QString path = fs->filePath(mapToSource(index));
                if (path == m_rootPath && m_workspaceFolders.size() <= 1) {
                    return QFileInfo(m_rootPath).fileName() + " (Workspace)";
                } else if (path == m_rootPath) {
                    return QString("Workspace (%1 Folders)").arg(m_workspaceFolders.size());
                }
            }
        }
        
        if (role == Qt::DecorationRole) {
            QFileSystemModel* fs = qobject_cast<QFileSystemModel*>(sourceModel());
            if (fs) {
                QModelIndex srcIndex = mapToSource(index);
                QString fileName = fs->fileName(srcIndex);
                bool isDir = fs->isDir(srcIndex);
                
                // --- Modern Procedural Icon Generator ---
                const int size = 20;
                QPixmap pixmap(size * 2, size * 2); // High DPI
                pixmap.setDevicePixelRatio(2.0);
                pixmap.fill(Qt::transparent);
                
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                
                PCBTheme* theme = ThemeManager::theme();
                bool isLight = theme && theme->type() == PCBTheme::Light;
                
                QColor accent = QColor("#64748b"); // Default
                QString label;
                
                if (isDir) {
                    accent = isLight ? QColor("#64748b") : QColor("#94a3b8");
                    QRectF folderBase(2, 6, 16, 10);
                    QRectF folderTab(2, 3, 7, 4);
                    QPainterPath fpath;
                    fpath.addRoundedRect(folderBase, 2, 2);
                    fpath.addRoundedRect(folderTab, 1.5, 1.5);
                    painter.setPen(QPen(accent, 1.5));
                    painter.setBrush(accent.lighter(isLight ? 160 : 120));
                    painter.drawPath(fpath);
                    return QIcon(pixmap);
                } else {
                    QString fn = fileName.toLower();
                    if (fn.endsWith(".flxsch") || fn.endsWith(".sch") || fn.endsWith(".flux")) { accent = QColor("#3b82f6"); label = "S"; }
                    else if (fn.endsWith(".kicad_sch")) { accent = QColor("#10b981"); label = "K"; }
                    else if (fn.endsWith(".schdoc")) { accent = QColor("#f97316"); label = "A"; }
                    else if (fn.endsWith(".sym") || fn.endsWith(".viosym") || fn.endsWith(".sclib") || fn.endsWith(".lib")) { accent = QColor("#8b5cf6"); label = "L"; }
                    else if (fn.endsWith(".cir") || fn.endsWith(".net") || fn.endsWith(".spice") || fn.endsWith(".model")) { accent = QColor("#06b6d4"); label = "N"; }
                    else if (fn.endsWith(".png") || fn.endsWith(".jpg") || fn.endsWith(".jpeg") || fn.endsWith(".bmp") || fn.endsWith(".svg") || fn.endsWith(".gif") || fn.endsWith(".webp")) { accent = QColor("#ec4899"); label = "I"; }
                    else if (fn.endsWith(".md") || fn.endsWith(".txt") || fn.endsWith(".json")) { accent = QColor("#94a3b8"); label = "D"; }
                    else { label = "F"; }

                    QRectF docRect(4, 2, 12, 16);
                    QPainterPath dpath;
                    dpath.addRoundedRect(docRect, 2, 2);
                    
                    // Fill with slightly tinted background
                    QColor fillColor = isLight ? QColor("#ffffff") : QColor("#1e293b");
                    painter.setPen(QPen(accent, 1.2));
                    painter.setBrush(fillColor);
                    painter.drawPath(dpath);
                    
                    // Color bar at bottom for extra vibe
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(accent);
                    painter.drawRoundedRect(4, 15, 12, 3, 1, 1);
                    
                    // Draw Label
                    painter.setPen(isLight ? accent.darker(150) : accent.lighter(150));
                    QFont font = painter.font();
                    font.setPointSize(7);
                    font.setBold(true);
                    painter.setFont(font);
                    painter.drawText(docRect.adjusted(0, 0, 0, -2), Qt::AlignCenter, label);
                    
                    return QIcon(pixmap);
                }
            }
        }
        
        return QSortFilterProxyModel::data(index, role);
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        
        QFileSystemModel* fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fs) return true;
        
        QString path = fs->filePath(index);

        // Determine if path belongs to designated workspace branches
        bool isWorkspaceChain = false;
        if (!m_workspaceFolders.isEmpty()) {
            const QString pathWithSlash = (path == "/") ? "/" : (path + "/");
            for (const QString& wf : m_workspaceFolders) {
                if (path == wf || path.startsWith(wf + "/")) {
                    isWorkspaceChain = true; break; // Path is inside a workspace folder
                }
                if (wf.startsWith(pathWithSlash) || path == "/") {
                    isWorkspaceChain = true; break; // Path is an ancestor leading to a workspace folder
                }
            }
            const QString rootWithSlash = (path == "/") ? "/" : (path + "/");
            if (!isWorkspaceChain && path != m_rootPath && !m_rootPath.startsWith(rootWithSlash)) {
                return false;
            }
        }

        QFileInfo currentFileInfo(path);
        QString fileName = currentFileInfo.fileName();

        if (currentFileInfo.isDir()) {
            if (fileName == ".viora") return true; // Explicitly allow .viora
            if (fileName.startsWith(".") && fileName != ".viora") return false; // Hide other dot-folders (.git, .trash, etc)
            if (fileName == "build" || fileName == "cmake" || fileName == "CMakeFiles" || fileName == "_deps") return false;
            
            // For directories: if there's an active search, show those that match or have children that match
            if (filterRegularExpression().isValid() && !filterRegularExpression().pattern().isEmpty()) {
                // Qt will automatically show parent directories if children match
                return true;
            }
            return true; 
        }
        
        // Skip hidden files unless they are inside .viora (like SKILL.md)
        if (fileName.startsWith(".") && !path.contains("/.viora/")) return false;

        QString fileNameLower = fileName.toLower();
        
        // If there's an active search filter, show any file that matches
        if (filterRegularExpression().isValid() && !filterRegularExpression().pattern().isEmpty()) {
            return fileNameLower.contains(filterRegularExpression());
        }
        
        // Default view: only show known project files
        return fileNameLower.endsWith(".sch") || fileNameLower.endsWith(".sym") ||
               fileName.endsWith(".flxsch") ||
               fileName.endsWith(".cir") || fileName.endsWith(".spice") || fileName.endsWith(".net") ||
               fileName.endsWith(".lib") || fileName.endsWith(".sclib") ||
               fileName.endsWith(".png") || fileName.endsWith(".jpg") || fileName.endsWith(".jpeg") ||
               fileName.endsWith(".bmp") || fileName.endsWith(".svg") || fileName.endsWith(".gif") ||
               fileName.endsWith(".md") || fileName.endsWith(".txt") || fileName.endsWith(".json") ||
               fileName.endsWith(".flux") || fileName.endsWith(".viosym") || fileName.endsWith(".kicad_sch") ||
               fileName.endsWith(".schdoc") || fileName.endsWith(".model") || fileName.endsWith(".webp");
    }
};

ProjectExplorerWidget::ProjectExplorerWidget(QWidget *parent)
    : QWidget(parent)
{
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
    
    m_proxyModel = new FileFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    setupUi();
    applyTheme();

    connect(&SourceControlManager::instance(), &SourceControlManager::statusUpdated, this, &ProjectExplorerWidget::onGitStatusUpdated);
    onGitStatusUpdated();
}

ProjectExplorerWidget::~ProjectExplorerWidget() {
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    if (m_treeView) {
        m_treeView->setItemDelegate(nullptr);
        m_treeView->setModel(nullptr);
    }
}

void ProjectExplorerWidget::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Header Section (VS Code style) ---
    QWidget* header = new QWidget(this);
    header->setFixedHeight(32);
    header->setObjectName("ExplorerHeader");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 4, 0);
    headerLayout->setSpacing(4);

    m_titleLabel = new QLabel("PROJECT", header);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 10px; color: #888;");
    headerLayout->addWidget(m_titleLabel, 1);

    auto addHeaderAction = [&](const QString& iconPath, const QString& tooltip, auto slot) {
        QToolButton* btn = new QToolButton(header);
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(14, 14));
        btn->setToolTip(tooltip);
        btn->setFixedSize(20, 20);
        btn->setAutoRaise(true);
        btn->setStyleSheet("QToolButton { background: transparent; border: none; padding: 2px; border-radius: 3px; } "
                          "QToolButton:hover { background-color: rgba(128, 128, 128, 0.15); }");
        connect(btn, &QToolButton::clicked, this, slot);
        headerLayout->addWidget(btn);
        return btn;
    };

    addHeaderAction(":/icons/tool_sync.svg", "Refresh", [this]() { this->onRefreshRequested(); });
    m_undoBtn = addHeaderAction(":/icons/undo.svg", "Undo Delete (Ctrl+Z)", [this]() { this->undoLastDelete(); });
    m_undoBtn->setVisible(false);

    layout->addWidget(header);

    // Ctrl+Z shortcut for undo delete - restricted to widget so it doesn't steal schematic undo
    QShortcut* undoShortcut = new QShortcut(QKeySequence::Undo, this, nullptr, nullptr, Qt::WidgetShortcut);
    connect(undoShortcut, &QShortcut::activated, this, [this]() { undoLastDelete(); });

    // Search bar container for better padding
    QWidget* searchContainer = new QWidget(this);
    QVBoxLayout* searchLayout = new QVBoxLayout(searchContainer);
    searchLayout->setContentsMargins(8, 4, 8, 8);
    searchLayout->setSpacing(0);

    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Filter files...");
    m_searchBox->setFixedHeight(28);
    m_searchBox->setClearButtonEnabled(true);
    
    // Add search icon to line edit
    QAction* searchAction = new QAction(QIcon(":/icons/tool_select.svg"), "", m_searchBox);
    m_searchBox->addAction(searchAction, QLineEdit::LeadingPosition);
    
    searchLayout->addWidget(m_searchBox);
    layout->addWidget(searchContainer);

    // Tree View
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_proxyModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(14);
    m_treeView->setIconSize(QSize(16, 16));
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setItemDelegate(new ProjectExplorerDelegate(m_model, m_proxyModel, &m_gitStatusMap, this));
    
    // Hide size, type, date columns
    for (int i = 1; i < 4; ++i) m_treeView->hideColumn(i);
    
    layout->addWidget(m_treeView, 1);

    connect(m_treeView, &QTreeView::doubleClicked, this, &ProjectExplorerWidget::onDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ProjectExplorerWidget::onContextMenuRequested);
    connect(m_searchBox, &QLineEdit::textChanged, this, &ProjectExplorerWidget::onFilterChanged);
}

void ProjectExplorerWidget::onRefreshRequested() {
    if (!m_rootPath.isEmpty()) {
        m_model->setRootPath("");
        m_model->setRootPath(QFileInfo(m_rootPath).absolutePath());
    }
}

void ProjectExplorerWidget::onGitStatusUpdated() {
    m_gitStatusMap.clear();
    
    QString projDir = SourceControlManager::instance().projectDir();
    if (projDir.isEmpty()) {
        m_treeView->viewport()->update();
        return;
    }
    
    auto statuses = SourceControlManager::instance().fileStatuses();
    for (const auto& st : statuses) {
        QString absPath = QDir::cleanPath(projDir + "/" + st.path);
        QString code = !st.worktreeStatus.isEmpty() && st.worktreeStatus != "?" ? st.worktreeStatus : st.indexStatus;
        if (code.isEmpty() && st.isUntracked) code = "U";
        
        if (!code.isEmpty()) {
            m_gitStatusMap[absPath] = code;
            
            // Bubble up to parents so directories turn yellow
            QDir d = QFileInfo(absPath).absoluteDir();
            while (d.absolutePath() != projDir && d.absolutePath().length() >= projDir.length()) {
                if (!m_gitStatusMap.contains(d.absolutePath())) {
                    m_gitStatusMap[d.absolutePath()] = "M"; // Mark parent dirs as modified
                }
                if (!d.cdUp()) break;
            }
        }
    }
    m_treeView->viewport()->update();
}

void ProjectExplorerWidget::onCollapseAllRequested() {
    if (m_treeView) {
        m_treeView->collapseAll();
    }
}

void ProjectExplorerWidget::setRootPath(const QString& path) {
    if (path.isEmpty()) return;
    setWorkspaceFolders(QStringList() << path);
}

void ProjectExplorerWidget::setWorkspaceFolders(const QStringList& folders) {
    if (folders.isEmpty()) return;
    
    m_workspaceFolders = folders;
    
    qDebug() << "[Explorer] setWorkspaceFolders called with" << folders.size() << "folders:" << folders;
    
    // Determine the Longest Common Ancestor among disjoint workspace directories
    QString commonPath = QDir::cleanPath(folders[0]);
    for (int i = 1; i < folders.size(); ++i) {
        QString p2 = QDir::cleanPath(folders[i]);
        while (!commonPath.isEmpty() && p2 != commonPath && !p2.startsWith(commonPath + "/")) {
            int lastSlash = commonPath.lastIndexOf('/');
            if (lastSlash <= 0) { commonPath = "/"; break; }
            commonPath = commonPath.left(lastSlash);
        }
    }
    
    qDebug() << "[Explorer] Common ancestor path:" << commonPath;
    
    m_rootPath = commonPath;
    
    FileFilterProxyModel* filterModel = static_cast<FileFilterProxyModel*>(m_proxyModel);
    filterModel->m_rootPath = commonPath;
    filterModel->m_workspaceFolders = m_workspaceFolders;
    
    // Tell QFileSystemModel to start loading the common ancestor path
    m_model->setRootPath(commonPath);
    
    // Disconnect any existing directoryLoaded connection to avoid duplicate handlers
    disconnect(m_model, &QFileSystemModel::directoryLoaded, this, nullptr);
    
    // IMPORTANT: QFileSystemModel is async. We cannot call mapFromSource immediately after
    // setRootPath because the directory hasn't been scanned yet and the proxy returns invalid.
    // Instead, defer setRootIndex to the directoryLoaded signal.
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this](const QString& loadedPath) {
        QString cleaned = QDir::cleanPath(loadedPath);
        
        // Once the common ancestor is loaded, set the tree root and expand workspace folders
        if (cleaned == QDir::cleanPath(m_rootPath)) {
            QModelIndex sourceIndex = m_model->index(m_rootPath);
            if (sourceIndex.isValid()) {
                QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
                qDebug() << "[Explorer] directoryLoaded - setting root. proxyIndex valid:" << proxyIndex.isValid();
                m_treeView->setRootIndex(proxyIndex);
                
                // Expand each top-level workspace folder
                for (const QString& wf : m_workspaceFolders) {
                    QModelIndex wfSource = m_model->index(wf);
                    if (wfSource.isValid()) {
                        m_treeView->expand(m_proxyModel->mapFromSource(wfSource));
                    }
                }
            }
        }
    });
}

void ProjectExplorerWidget::onDoubleClicked(const QModelIndex& index) {
    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    QString path = m_model->filePath(sourceIndex);
    if (QFileInfo(path).isFile()) {
        Q_EMIT fileDoubleClicked(path);
    }
}

void ProjectExplorerWidget::onFilterChanged(const QString& text) {
    if (text.isEmpty()) {
        m_proxyModel->setFilterRegularExpression(QRegularExpression());
    } else {
        m_proxyModel->setFilterRegularExpression(QRegularExpression(text, QRegularExpression::CaseInsensitiveOption));
    }
}

void ProjectExplorerWidget::onContextMenuRequested(const QPoint& pos) {
    QModelIndex index = m_treeView->indexAt(pos);
    if (!index.isValid()) return;

    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    QString path = m_model->filePath(sourceIndex);
    if (path.isEmpty()) return;

    QFileInfo info(path);
    QMenu menu(this);

    if (info.isFile()) {
        QAction* openAct = menu.addAction("Open");
        connect(openAct, &QAction::triggered, this, [this, path]() { Q_EMIT fileDoubleClicked(path); });

        QAction* openExtAct = menu.addAction("Open in External Editor");
        connect(openExtAct, &QAction::triggered, this, [path]() {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });

        if (info.suffix().toLower() == "flxsch") {
            menu.addSeparator();
            QAction* netlistAct = menu.addAction("Extract Netlist");
            connect(netlistAct, &QAction::triggered, this, [this, path]() {
                const QString projectDir = QFileInfo(path).absolutePath();
                const QString baseName = QFileInfo(path).completeBaseName();
                const QString outPath = projectDir + "/" + baseName + ".cir";

                // Remove stale output so we can detect fresh creation
                QFile::remove(outPath);

                QString cliPath = QCoreApplication::applicationDirPath() + "/viora";
                if (!QFile::exists(cliPath)) {
                    cliPath = "viora";
                }

                QProcess proc;
                proc.setWorkingDirectory(projectDir);
                proc.start(cliPath, {"schematic-netlist", path, "--out", outPath});
                proc.waitForFinished(30000);

                if (QFile::exists(outPath) && QFileInfo(outPath).size() > 0) {
                    QMessageBox::information(this, "Extract Netlist",
                        "Netlist saved to:\n" + outPath);
                } else {
                    QString err = QString::fromUtf8(proc.readAllStandardError());
                    QMessageBox::warning(this, "Extract Netlist",
                        "Failed to extract netlist.\n" + err);
                }
            });
        }

        const QString suffix = info.suffix().toLower();
        if (suffix == "cir" || suffix == "spice" || suffix == "net") {
            menu.addSeparator();
            QAction* fromNetlistAct = menu.addAction("New Schematic from Netlist");
            connect(fromNetlistAct, &QAction::triggered, this, [this, path]() {
                const QString baseName = QFileInfo(path).completeBaseName();
                const QString outDir = QFileInfo(path).absolutePath();
                const QString outPath = outDir + "/" + baseName + "_from_netlist.flxsch";

                auto result = NetlistToSchematic::convert(path, outPath);
                if (result.success) {
                    QMessageBox::information(this, "New Schematic from Netlist",
                        QString("Created schematic with %1 components and %2 air wires.\n\n%3")
                            .arg(result.componentCount)
                            .arg(result.airWireCount)
                            .arg(result.outputPath));
                    Q_EMIT fileDoubleClicked(result.outputPath);
                } else {
                    QMessageBox::warning(this, "New Schematic from Netlist",
                        "Failed to generate schematic:\n" + result.errorMessage);
                }
            });
        }
    } else {
        QAction* expandAct = menu.addAction(m_treeView->isExpanded(index) ? "Collapse" : "Expand");
        connect(expandAct, &QAction::triggered, this, [this, index]() {
            if (m_treeView->isExpanded(index)) m_treeView->collapse(index);
            else m_treeView->expand(index);
        });
    }

    menu.addSeparator();

    QAction* renameAct = menu.addAction("Rename...");
    connect(renameAct, &QAction::triggered, this, [this, path, info]() {
        const QString currentName = info.fileName();
        bool ok = false;
        QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, currentName, &ok);
        if (!ok || newName.isEmpty() || newName == currentName) return;

        const QString newPath = info.absoluteDir().absoluteFilePath(newName);
        if (QFileInfo::exists(newPath)) {
            QMessageBox::warning(this, "Rename Failed", "A file or folder with that name already exists.");
            return;
        }
        bool success = info.isDir() ? QDir().rename(path, newPath) : QFile::rename(path, newPath);
        if (!success) {
            QMessageBox::warning(this, "Rename Failed", "Could not rename item.");
            return;
        }
        onRefreshRequested();
    });

    QAction* deleteAct = menu.addAction("Delete");
    connect(deleteAct, &QAction::triggered, this, [this, path, info]() {
        const QString name = info.fileName();
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Confirm Delete",
            QString("Are you sure you want to delete '%1'?\n\nThe file will be moved to trash and can be restored with Undo Delete.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (reply != QMessageBox::Yes) return;
        deleteItem(path, info.isDir());
    });

    QAction* undoAct = menu.addAction("Undo Delete  (Ctrl+Z)");
    undoAct->setEnabled(!m_deleteHistory.isEmpty());
    connect(undoAct, &QAction::triggered, this, [this]() { undoLastDelete(); });

    menu.addSeparator();

    QAction* copyPathAct = menu.addAction("Copy Path");
    connect(copyPathAct, &QAction::triggered, this, [path]() {
        QApplication::clipboard()->setText(path);
    });

    if (!m_rootPath.isEmpty()) {
        const QString rel = QDir(m_rootPath).relativeFilePath(path);
        QAction* copyRelAct = menu.addAction("Copy Relative Path");
        connect(copyRelAct, &QAction::triggered, this, [rel]() {
            QApplication::clipboard()->setText(rel);
        });
    }

    QAction* revealAct = menu.addAction("Reveal in File Manager");
    connect(revealAct, &QAction::triggered, this, [path, info]() {
        const QString dir = info.isDir() ? path : info.absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

QString ProjectExplorerWidget::trashDir() const {
    return m_rootPath + "/.trash";
}

void ProjectExplorerWidget::deleteItem(const QString& path, bool isDir) {
    QFileInfo info(path);
    if (!info.exists()) return;

    QString td = trashDir();
    QDir().mkpath(td);

    // Generate unique name in trash by appending timestamp
    QString trashName = info.fileName();
    QString trashPath = td + "/" + trashName;
    if (QFileInfo::exists(trashPath)) {
        QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString base = info.completeBaseName();
        QString ext = info.suffix().isEmpty() ? "" : "." + info.suffix();
        trashName = base + "_" + ts + ext;
        trashPath = td + "/" + trashName;
    }

    bool ok;
    if (isDir) {
        ok = QDir().rename(path, trashPath);
    } else {
        ok = QFile::rename(path, trashPath);
    }

    if (!ok) {
        QMessageBox::warning(this, "Delete Failed", "Could not move item to trash.");
        return;
    }

    DeletedEntry entry;
    entry.originalPath = path;
    entry.trashPath = trashPath;
    entry.isDir = isDir;
    m_deleteHistory.push(entry);
    m_undoBtn->setVisible(true);

    onRefreshRequested();
}

void ProjectExplorerWidget::undoLastDelete() {
    if (m_deleteHistory.isEmpty()) return;

    DeletedEntry entry = m_deleteHistory.pop();

    if (!QFileInfo::exists(entry.trashPath)) {
        QMessageBox::warning(this, "Undo Delete", "Trashed item no longer exists.");
        return;
    }

    // Check if something already exists at original location
    if (QFileInfo::exists(entry.originalPath)) {
        QMessageBox::warning(this, "Undo Delete",
            "Cannot restore: a file already exists at:\n" + entry.originalPath);
        return;
    }

    // Ensure parent directory exists
    QDir parentDir = QFileInfo(entry.originalPath).absoluteDir();
    if (!parentDir.exists()) {
        QDir().mkpath(parentDir.absolutePath());
    }

    bool ok;
    if (entry.isDir) {
        ok = QDir().rename(entry.trashPath, entry.originalPath);
    } else {
        ok = QFile::rename(entry.trashPath, entry.originalPath);
    }

    if (!ok) {
        QMessageBox::warning(this, "Undo Delete", "Failed to restore item.");
        return;
    }

    m_undoBtn->setVisible(!m_deleteHistory.isEmpty());
    onRefreshRequested();
}

void ProjectExplorerWidget::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString bg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    QString hoverBg = (theme->type() == PCBTheme::Light) ? "#e7f0ff" : "#2a2a2f";
    QString altRow = (theme->type() == PCBTheme::Light) ? "#eef2f7" : "#202025";
    QString selectionBg = (theme->type() == PCBTheme::Light) ? "#cfe2ff" : "#1f2a44";

    // Generate SVGs for branch arrows and save them to temp files to bypass Qt's stylesheet URI parser bugs
    QString tempPath = QDir::tempPath();
    QString rightPath = tempPath + "/viospice_tree_right.svg";
    QString downPath = tempPath + "/viospice_tree_down.svg";

    QString rightSvg = QString("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polyline points='9 18 15 12 9 6'/></svg>").arg(fg);
    QString downSvg = QString("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%1' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><polyline points='6 9 12 15 18 9'/></svg>").arg(fg);
    
    QFile rightFile(rightPath);
    if (rightFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&rightFile);
        out << rightSvg;
        rightFile.close();
    }
    
    QFile downFile(downPath);
    if (downFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&downFile);
        out << downSvg;
        downFile.close();
    }

    setStyleSheet(QString(
        "QWidget { background-color: %1; color: %2; }"
        "QWidget#ExplorerHeader { background-color: %1; border-bottom: 1px solid %3; }"
        "QLineEdit { background-color: %5; border: 1px solid %3; padding: 6px 12px; border-radius: 6px; color: %2; font-size: 13px; }"
        "QLineEdit:focus { border: 1px solid %4; background-color: %1; }"
        "QTreeView { background-color: %1; border: none; outline: none; alternate-background-color: %6; show-decoration-selected: 0; selection-background-color: transparent; }"
        "QTreeView::item { padding: 6px 8px; border-radius: 4px; margin: 1px 10px 1px 0px; height: 24px; color: %2; font-size: 13px; }"
        "QTreeView::item:hover:!selected { background-color: %7; }"
        "QTreeView::item:selected { "
        "   background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %4, stop:0.04 %4, stop:0.041 rgba(86, 156, 214, 20), stop:1 rgba(86, 156, 214, 10));"
        "   color: %4; font-weight: bold; border-radius: 0px; "
        "}"
        "QTreeView::branch { background-color: transparent; }"
        "QTreeView::branch:has-children:closed:has-siblings, QTreeView::branch:has-children:closed:!has-siblings { image: url(%9); }"
        "QTreeView::branch:open:has-children:has-siblings, QTreeView::branch:open:has-children:!has-siblings { image: url(%10); }"
    ).arg(bg, fg, border, accent, inputBg, altRow, hoverBg, selectionBg, rightPath, downPath));

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString("font-weight: bold; font-size: 10px; color: %1; text-transform: uppercase; letter-spacing: 0.5px;")
                                    .arg((theme->type() == PCBTheme::Light) ? "#64748b" : "#94a3b8"));
    }
}
