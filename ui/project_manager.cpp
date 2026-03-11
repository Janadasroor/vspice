#include "project_manager.h"
#include "help_window.h"
#include "developer_help_window.h"
#include "../core/ui/project_audit_dialog.h"
#include "schematic_editor.h"
#include "symbol_editor.h"
#include "calculator_dialog.h"
#include "plugin_manager_dialog.h"
#include "../schematic/ui/netlist_editor.h"
#include "csv_viewer.h"
#include "../core/config_manager.h"
#include "../core/settings_dialog.h"
#include <QPainter>
#include <QPixmap>
#include "project.h"
#include "theme_manager.h"
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QHeaderView>
#include <QStyle>
#include <QScrollArea>
#include <QGridLayout>
#include <QMouseEvent>
#include <QSplitter>
#include <QPainter>
#include <cmath>
#include <QTabWidget>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QTimer>
#include <QResizeEvent>

ProjectManager::ProjectManager(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle("Viora EDA");
    setMinimumSize(900, 600);
    
    applyKiCadStyle();
    setupUI();

    // Try to load last project or show empty
    updateRecentProjectsMenu();

    // Try to load last recent project
    const QStringList recentProjects = RecentProjects::instance().projects();
    if (!recentProjects.isEmpty()) {
        QString lastPath = recentProjects.first();
        if (QFile::exists(lastPath)) {
            openProject(lastPath);
        }
    } else {
        // Default to Documents folder if nothing else is open
        QString docPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (QDir(docPath).exists()) {
            m_openProjects.append(docPath);
        }
    }
    refreshProjectTree();

    // Restore UI State
    QByteArray geom = ConfigManager::instance().windowGeometry("ProjectManager");
    QByteArray state = ConfigManager::instance().windowState("ProjectManager");
    if (!geom.isEmpty()) restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

ProjectManager::~ProjectManager() {}

void ProjectManager::closeEvent(QCloseEvent* event) {
    ConfigManager::instance().saveWindowState("ProjectManager", saveGeometry(), saveState());
    event->accept();
}

void ProjectManager::setupUI() {
    createMenuBar();
    
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);

    QHBoxLayout* mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Vertical toolbar on the left edge
    m_verticalToolbar = createVerticalToolbar();
    mainLayout->addWidget(m_verticalToolbar);

    // Splitter for project files and launcher area
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setChildrenCollapsible(false);
    
    // Project Files Panel
    m_projectPanel = createProjectFilesPanel();
    m_splitter->addWidget(m_projectPanel);
    
    // Launcher Area
    m_launcherArea = createLauncherArea();
    m_splitter->addWidget(m_launcherArea);
    
    // Set initial sizes (250px for tree, rest for launcher)
    m_splitter->setSizes({250, 650});
    
    mainLayout->addWidget(m_splitter);

    // Status bar
    statusBar()->showMessage("Project: (no project loaded)");
}

QWidget* ProjectManager::createProjectFilesPanel() {
    QWidget* panel = new QWidget;
    panel->setObjectName("ProjectFilesPanel");
    panel->setMinimumWidth(220);
    
    QVBoxLayout* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header
    QLabel* header = new QLabel("  EXPLORER");
    header->setObjectName("PanelHeader");
    layout->addWidget(header);

    // Search field
    auto* searchField = new QLineEdit();
    searchField->setPlaceholderText("  Search files...");
    searchField->setClearButtonEnabled(true);
    searchField->setObjectName("ProjectSearch");
    layout->addWidget(searchField);

    // Tree widget
    m_projectTree = new QTreeWidget;
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setRootIsDecorated(true);
    m_projectTree->setObjectName("ProjectTree");
    m_projectTree->setAnimated(true);
    m_projectTree->setIndentation(16);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    connect(m_projectTree, &QTreeWidget::customContextMenuRequested,
            this, &ProjectManager::onProjectTreeContextMenu);
    
    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, &ProjectManager::onProjectTreeItemDoubleClicked);

    // Search filtering
    connect(searchField, &QLineEdit::textChanged, this, [this](const QString& text) {
        std::function<bool(QTreeWidgetItem*)> filterItem = [&](QTreeWidgetItem* item) -> bool {
            bool childVisible = false;
            for (int i = 0; i < item->childCount(); ++i) {
                if (filterItem(item->child(i))) childVisible = true;
            }
            bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match && !childVisible);
            return match || childVisible;
        };
        for (int i = 0; i < m_projectTree->topLevelItemCount(); ++i) {
            filterItem(m_projectTree->topLevelItem(i));
        }
    });
    
    layout->addWidget(m_projectTree);

    return panel;
}

QToolBar* ProjectManager::createVerticalToolbar() {
    QToolBar* toolbar = new QToolBar;
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setMovable(false);
    toolbar->setObjectName("VerticalToolbar");
    toolbar->setIconSize(QSize(22, 22));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    
    // Toggle Sidebar Action
    QAction* toggleSidebarAct = toolbar->addAction(QIcon(":/icons/chevron_right.svg"), "Toggle Explorer");
    toggleSidebarAct->setCheckable(true);
    toggleSidebarAct->setChecked(true);
    connect(toggleSidebarAct, &QAction::triggered, this, &ProjectManager::onToggleSidebar);
    
    toolbar->addSeparator();

    // Add quick actions
    QAction* newAction = toolbar->addAction(QIcon(":/icons/toolbar_new.png"), "New Project");
    newAction->setToolTip("New Project (Ctrl+N)");
    connect(newAction, &QAction::triggered, this, &ProjectManager::createNewProject);
    
    QAction* openAction = toolbar->addAction(QIcon(":/icons/folder_open.svg"), "Open Project");
    openAction->setToolTip("Open Project (Ctrl+O)");
    connect(openAction, &QAction::triggered, this, &ProjectManager::openExistingProject);
    
    toolbar->addSeparator();
    
    QAction* schAction = toolbar->addAction(QIcon(":/icons/toolbar_schematic.png"), "Schematic Editor");
    schAction->setToolTip("Schematic Editor");
    connect(schAction, &QAction::triggered, this, &ProjectManager::openSchematicEditor);
    
    toolbar->addSeparator();
    
    QAction* refreshAction = toolbar->addAction(QIcon(":/icons/toolbar_refresh.png"), "Refresh");
    refreshAction->setToolTip("Refresh Project Tree");
    connect(refreshAction, &QAction::triggered, this, &ProjectManager::refreshProjectTree);

    toolbar->addSeparator();
    
    QAction* themeAction = toolbar->addAction(QIcon(":/icons/tool_anchor.svg"), "Switch Theme");
    themeAction->setToolTip("Cycle Global Theme");
    connect(themeAction, &QAction::triggered, this, [this]() {
        auto& tm = ThemeManager::instance();
        if (tm.currentTheme()->type() == PCBTheme::Engineering) tm.setTheme(PCBTheme::Dark);
        else if (tm.currentTheme()->type() == PCBTheme::Dark) tm.setTheme(PCBTheme::Light);
        else tm.setTheme(PCBTheme::Engineering);
        updateThemeStyle();
        statusBar()->showMessage("Theme changed globally", 3000);
    });

    return toolbar;
}

// Helper to add a launcher tile to the grid
void ProjectManager::addLauncherTile(QGridLayout* grid, int row, int col, 
                                     const QString& title, const QString& desc, 
                                     const QString& iconPath, 
                                     void (ProjectManager::*slot)()) {
    LauncherTile* tile = new LauncherTile("", title, desc);
    tile->setIcon(QIcon(iconPath));
    connect(tile, &LauncherTile::clicked, this, slot);
    grid->addWidget(tile, row, col);
    m_launcherTiles.append(tile);
}

QWidget* ProjectManager::createLauncherArea() {
    QScrollArea* scroll = new QScrollArea;
    scroll->setObjectName("LauncherScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background-color: #0a0a0c; border: none; }");

    m_launcherScrollContent = new QWidget;
    m_launcherScrollContent->setObjectName("LauncherArea");
    
    QVBoxLayout* layout = new QVBoxLayout(m_launcherScrollContent);
    layout->setContentsMargins(40, 30, 40, 20);
    layout->setSpacing(24);

    // Welcome header
    QLabel* welcomeLabel = new QLabel("Viora EDA");
    welcomeLabel->setObjectName("WelcomeTitle");
    layout->addWidget(welcomeLabel);

    QLabel* subtitleLabel = new QLabel("Professional Electronic Design Automation");
    subtitleLabel->setObjectName("WelcomeSubtitle");
    layout->addWidget(subtitleLabel);

    // Thin accent line
    QFrame* accentLine = new QFrame();
    accentLine->setFixedHeight(2);
    accentLine->setStyleSheet("background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #007acc, stop:0.5 #00c8ff, stop:1 transparent);");
    layout->addWidget(accentLine);

    // Grid layout for launcher tiles (will be managed by updateLauncherLayout)
    m_launcherGrid = new QGridLayout;
    m_launcherGrid->setSpacing(24);
    m_launcherGrid->setContentsMargins(0, 10, 0, 10);
    m_launcherGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    // Initialize tiles list if not already
    m_launcherTiles.clear();

    auto createAndStoreTile = [&](const QString& title, const QString& desc, const QString& iconPath, void (ProjectManager::*slot)()) {
        LauncherTile* tile = new LauncherTile("", title, desc);
        tile->setIcon(QIcon(iconPath));
        connect(tile, &LauncherTile::clicked, this, slot);
        m_launcherTiles.append(tile);
    };

    createAndStoreTile("Schematic Editor", "Capture and edit circuit schematics", ":/icons/schematic_editor.png", &ProjectManager::openSchematicEditor);
    createAndStoreTile("Symbol Editor", "Create and manage schematic symbol libraries", ":/icons/symbol_editor.png", &ProjectManager::openSymbolEditor);
    createAndStoreTile("Calculator Tools", "Resistance, trace width, and impedance calculators", ":/icons/calculator_tools.png", &ProjectManager::openCalculatorTools);
    createAndStoreTile("Plugins Manager", "Manage extensions, importers, and add-ons", ":/icons/plugins_manager.png", &ProjectManager::openPluginsManager);
    createAndStoreTile("Help Documentation", "Software guides, tutorials, and documentation", ":/icons/tool_search.svg", &ProjectManager::showHelp);

    layout->addLayout(m_launcherGrid);
    layout->addStretch();

    // Version footer
    QLabel* versionLabel = new QLabel("Viora EDA v0.2.0  ·  Modern Engineering Suite");
    versionLabel->setObjectName("VersionFooter");
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    scroll->setWidget(m_launcherScrollContent);
    
    // Trigger initial layout
    QTimer::singleShot(0, this, &ProjectManager::updateLauncherLayout);

    return scroll;
}

void ProjectManager::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateLauncherLayout();
}

void ProjectManager::updateLauncherLayout() {
    if (!m_launcherGrid || m_launcherTiles.isEmpty()) return;

    // Calculate available width for the grid
    int availableWidth = m_launcherScrollContent->width() - 80; // Accounting for margins
    int tileMinWidth = 320; // 300px min-width + 20px spacing buffer
    
    int columns = qMax(1, availableWidth / tileMinWidth);
    
    // Clear current grid (without deleting tiles)
    while (m_launcherGrid->count() > 0) {
        m_launcherGrid->takeAt(0);
    }

    // Re-populate grid based on columns
    for (int i = 0; i < m_launcherTiles.size(); ++i) {
        int row = i / columns;
        int col = i % columns;
        m_launcherGrid->addWidget(m_launcherTiles[i], row, col);
    }
}

void ProjectManager::refreshProjectTree() {
    m_projectTree->clear();
    
    if (m_openProjects.isEmpty()) {
       statusBar()->showMessage("No projects open");
       return;
    }

    for (const QString& path : m_openProjects) {
        addProjectToTree(path);
    }
    
    if (m_openProjects.size() == 1) {
       statusBar()->showMessage("Project: " + m_openProjects.first());
    } else {
       statusBar()->showMessage(QString("%1 projects open").arg(m_openProjects.size()));
    }
}

void ProjectManager::addProjectToTree(const QString& projectPath) {
    QDir dir(projectPath);
    if (!dir.exists()) return;

    // Create root item for project folder
    QTreeWidgetItem* projectRoot = new QTreeWidgetItem(m_projectTree);
    projectRoot->setText(0, dir.dirName());
    projectRoot->setIcon(0, createFolderIcon(true));
    projectRoot->setData(0, Qt::UserRole, projectPath);
    projectRoot->setExpanded(true);

    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QStringList allFiles = dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    
    // Folders to skip (common source repo folders to avoid clutter)
    QStringList skipFolders = {
        "build", "core", "ui", "schematic", "symbols", "scripts", "tests", "include", 
        "cmake", ".git", "python", "cli", "linux", "resources", "CMakeFiles", "_deps",
        "venv", ".qt", "footprints", "pcb", "reverse_engineering", "simulator"
    };

    // Add subdirectories under projectRoot
    for (const QString& subdir : subdirs) {
        if (skipFolders.contains(subdir)) continue;
        
        QTreeWidgetItem* item = new QTreeWidgetItem(projectRoot);
        item->setText(0, subdir);
        item->setIcon(0, createFolderIcon(false));
        item->setData(0, Qt::UserRole, dir.absoluteFilePath(subdir));
    }

    // Identify Flux Projects
    QStringList fluxFiles;
    for (const QString& f : allFiles) {
        if (f.endsWith(".flux", Qt::CaseInsensitive))
             fluxFiles << f;
    }
    
    // Auto-activate logic if only one found and none active
    if (fluxFiles.size() == 1 && m_activeProjectFile.isEmpty()) {
        m_activeProjectFile = dir.absoluteFilePath(fluxFiles.first());
    }
    
    QMap<QString, QTreeWidgetItem*> projectMap;
    
    // Add Flux Projects under projectRoot
    for (const QString& f : fluxFiles) {
        QTreeWidgetItem* item = new QTreeWidgetItem(projectRoot);
        QString absPath = dir.absoluteFilePath(f);
        item->setData(0, Qt::UserRole, absPath);
        
        if (absPath == m_activeProjectFile) {
            item->setIcon(0, createDocumentIcon(QColor("#10b981"), "F", true)); // Flux Project
            
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setText(0, f + " (Active)");
        } else {
            item->setIcon(0, createDocumentIcon(QColor("#10b981"), "F"));
            item->setText(0, f);
        }
        item->setExpanded(true);
        projectMap[QFileInfo(f).completeBaseName()] = item;
    }
    
    // Add Files Hierarchically
    QTreeWidgetItem* miscRoot = nullptr;
    
    QStringList projectExtensions = {
        "flux", "sch", "kicad_sch", "SchDoc",                      // Schematics
        "sym", "sclib", "lib", "model",                           // Symbols / Models
        "cir", "net", "sp", "spice"                               // Netlists
    };

    for (const QString& f : allFiles) {
        // Skip hidden or system files explicitly
        if (f.startsWith(".")) continue;
        if (f.endsWith(".flux", Qt::CaseInsensitive)) continue; // Already handled
        
        QFileInfo fi(f);
        QString ext = fi.suffix().toLower();
        
        // Filter: only show project-related files (EXCLUDE source code and build configs)
        if (!projectExtensions.contains(ext)) continue;

        QTreeWidgetItem* parentItem = nullptr;
        
        // Try to associate file with a project
        if (projectMap.contains(fi.completeBaseName())) {
            parentItem = projectMap[fi.completeBaseName()];
        } else if (projectMap.contains(fi.baseName())) {
            parentItem = projectMap[fi.baseName()];
        }
        
        // If no project association found, put in Miscellaneous or root
        if (!parentItem) {
             if (!fluxFiles.isEmpty()) {
                 if (!miscRoot) {
                     miscRoot = new QTreeWidgetItem(projectRoot);
                     miscRoot->setText(0, "Project Assets");
                     miscRoot->setIcon(0, createFolderIcon(true));
                     miscRoot->setExpanded(true);
                 }
                 parentItem = miscRoot;
             } else {
                 parentItem = projectRoot; 
             }
        }
        
        QTreeWidgetItem* item = new QTreeWidgetItem(parentItem);
        item->setText(0, f);
        item->setData(0, Qt::UserRole, dir.absoluteFilePath(f));
        item->setIcon(0, getProjectFileIcon(f));
    }
}

QIcon ProjectManager::createFolderIcon(bool open) const {
    const int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QColor folderColor = QColor("#d4d4d8"); // Modern Zinc/Slate color
    
    if (open) {
        // Draw an open folder profile
        QPainterPath path;
        path.moveTo(2, 4);
        path.lineTo(7, 4);
        path.lineTo(9, 6);
        path.lineTo(18, 6);
        path.lineTo(18, 16);
        path.lineTo(2, 16);
        path.closeSubpath();
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#3f3f46"));
        painter.drawPath(path);
        
        QPainterPath front;
        front.moveTo(2, 8);
        front.lineTo(18, 8);
        front.lineTo(16, 17);
        front.lineTo(4, 17);
        front.closeSubpath();
        
        QLinearGradient grad(0, 8, 0, 17);
        grad.setColorAt(0, folderColor);
        grad.setColorAt(1, QColor("#a1a1aa"));
        
        painter.setBrush(grad);
        painter.drawPath(front);
    } else {
        // Draw a closed folder profile
        QPainterPath path;
        path.moveTo(2, 4);
        path.lineTo(7, 4);
        path.lineTo(9, 6);
        path.lineTo(18, 6);
        path.lineTo(18, 15);
        path.lineTo(2, 15);
        path.closeSubpath();
        
        QLinearGradient grad(0, 4, 0, 15);
        grad.setColorAt(0, folderColor);
        grad.setColorAt(1, QColor("#a1a1aa"));
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(grad);
        painter.drawPath(path);
        
        // Tab highlight
        painter.setBrush(folderColor);
        painter.drawRect(2, 4, 5, 2);
    }
    
    return QIcon(pixmap);
}

QIcon ProjectManager::createDocumentIcon(const QColor& accentColor, const QString& label, bool isActive) const {
    const int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw document base (rounded rect with fold)
    QRectF docRect(2, 2, 16, 16);
    QPainterPath path;
    path.addRoundedRect(docRect, 3, 3);
    
    // Fill with a dark base and light top border
    QLinearGradient grad(0, 0, 0, size);
    grad.setColorAt(0, QColor("#22242a"));
    grad.setColorAt(1, QColor("#1a1c22"));
    
    painter.setPen(Qt::NoPen);
    painter.setBrush(grad);
    painter.drawPath(path);
    
    // Draw accent bar or left border
    painter.setBrush(accentColor);
    painter.drawRoundedRect(2, 2, 3, 16, 2, 2);
    
    // If active, add a subtle outer glow or distinct border
    if (isActive) {
        QPen activePen(accentColor, 1.5);
        painter.setPen(activePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(1, 1, 18, 18, 4, 4);
        
        // Active indicator dot
        painter.setBrush(accentColor);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(15, 15, 4, 4);
    } else {
        painter.setPen(QPen(QColor(255, 255, 255, 20), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    
    // Draw label
    painter.setPen(isActive ? Qt::white : QColor("#a0a0a0"));
    QFont font = painter.font();
    font.setPointSize(8);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(docRect.adjusted(3, 0, 0, 0), Qt::AlignCenter, label);
    
    return QIcon(pixmap);
}

QIcon ProjectManager::getProjectFileIcon(const QString& fileName) const {
    if (fileName.endsWith(".flux", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#10b981"), "F");
    } else if (fileName.endsWith(".pcb", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#8b5cf6"), "P");
    } else if (fileName.endsWith(".sch", Qt::CaseInsensitive) || fileName.endsWith(".kicad_sch", Qt::CaseInsensitive) || fileName.endsWith(".SchDoc", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#3b82f6"), "S");
    } else if (fileName.endsWith(".sym", Qt::CaseInsensitive) || fileName.endsWith(".sclib", Qt::CaseInsensitive) || fileName.endsWith(".lib", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#f59e0b"), "L"); // Library / Symbol
    } else if (fileName.endsWith(".cir", Qt::CaseInsensitive) || fileName.endsWith(".net", Qt::CaseInsensitive) || fileName.endsWith(".sp", Qt::CaseInsensitive) || fileName.endsWith(".model", Qt::CaseInsensitive)) {
        return createDocumentIcon(QColor("#ec4899"), "N"); // Netlist / Model
    } 
    return createDocumentIcon(QColor("#64748b"), "D");
}

void ProjectManager::activateProject(const QString& projectFile) {
    m_activeProjectFile = projectFile;
    refreshProjectTree();
    QMessageBox::information(this, "Viora EDA", 
        "Project " + QFileInfo(projectFile).fileName() + " is now active.");
}

void ProjectManager::onProjectTreeItemDoubleClicked(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column)
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;

    QFileInfo info(path);
    if (info.isDir()) return;

    if (path.endsWith(".sch", Qt::CaseInsensitive) || 
        path.endsWith(".kicad_sch", Qt::CaseInsensitive) || 
        path.endsWith(".SchDoc", Qt::CaseInsensitive)) {
        launchSchematicEditor(path);
    } else if (path.endsWith(".flux", Qt::CaseInsensitive)) {
        openProject(path);
    } else if (path.endsWith(".cir", Qt::CaseInsensitive) || 
               path.endsWith(".net", Qt::CaseInsensitive) || 
               path.endsWith(".sp", Qt::CaseInsensitive) ||
               path.endsWith(".spice", Qt::CaseInsensitive) ||
               path.endsWith(".model", Qt::CaseInsensitive) ||
               path.endsWith(".txt", Qt::CaseInsensitive)) {
        NetlistEditor* editor = new NetlistEditor();
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->loadFile(path);
        editor->show();
    } else if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        CsvViewer* viewer = new CsvViewer();
        viewer->setAttribute(Qt::WA_DeleteOnClose);
        viewer->loadFile(path);
        viewer->show();
    }
}

void ProjectManager::onProjectTreeContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_projectTree->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    
    QString path = item->data(0, Qt::UserRole).toString();
    QFileInfo info(path);

    // Identify Project Context
    QTreeWidgetItem* root = item;
    while (root->parent()) {
        root = root->parent();
    }
    QString projectRootPath = root->data(0, Qt::UserRole).toString();
    
    if (m_openProjects.contains(projectRootPath)) {
        QString projectName;
        QDir d(projectRootPath);
        QStringList flux = d.entryList(QStringList() << "*.flux", QDir::Files);
        if (!flux.empty()) projectName = QFileInfo(flux.first()).completeBaseName();
        else projectName = d.dirName();
        
        QString schPath = projectRootPath + "/" + projectName + ".sch";
        
        bool missingSch = !QFile::exists(schPath);
        
        if (missingSch) {
             QAction* addSch = menu.addAction("Add Schematic");
             addSch->setIcon(QIcon(":/icons/file_flux_sch.png"));
             connect(addSch, &QAction::triggered, [this, schPath]() {
                 QFile f(schPath);
                 if (f.open(QIODevice::WriteOnly)) { f.write("{}"); f.close(); }
                 refreshProjectTree();
             });
             menu.addSeparator();
        }
    }
    
    // Add Close Project for open projects (roots)
    if (m_openProjects.contains(path)) {
        QAction* closeAction = menu.addAction("Close Project");
        closeAction->setIcon(QIcon(":/icons/close.png")); // Assuming icon exists or fallback
        connect(closeAction, &QAction::triggered, [this, path](){
             m_openProjects.removeAll(path);
             // Check if active file belongs to this project
             if (!m_activeProjectFile.isEmpty()) {
                 QString activeDir = QFileInfo(m_activeProjectFile).absolutePath();
                 // Compare assuming normalized paths without trailing slashes
                 if (activeDir == path) {
                     m_activeProjectFile.clear();
                 }
             }
             refreshProjectTree();
        });
        menu.addSeparator();
    }

    // Add Activate Action for .flux files
    if (path.endsWith(".flux", Qt::CaseInsensitive)) {
        QAction* activate = menu.addAction("Activate Project");
        
        QPixmap pix(16, 16); 
        pix.fill(Qt::transparent);
        QPainter p(&pix); 
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#4CAF50")); 
        p.setPen(Qt::NoPen); 
        p.drawEllipse(3,3,10,10);
        activate->setIcon(QIcon(pix));
        
        connect(activate, &QAction::triggered, [this, path](){
             activateProject(path);
        });
        menu.addSeparator();
    }

    QAction* openAction = menu.addAction("Open");
    connect(openAction, &QAction::triggered, [=]() {
        onProjectTreeItemDoubleClicked(item, 0);
    });

    // New File sub-menu
    if (info.isDir()) {
        menu.addSeparator();
        QMenu* newMenu = menu.addMenu("New");
        
        QAction* newSch = newMenu->addAction("Schematic (.sch)");
        newSch->setIcon(getProjectFileIcon("a.sch"));
        connect(newSch, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Schematic", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".sch")) name += ".sch";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("{}"); f.close(); }
            refreshProjectTree();
        });

        QAction* newSym = newMenu->addAction("Symbol Library (.sym)");
        newSym->setIcon(getProjectFileIcon("a.sym"));
        connect(newSym, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Symbol Library", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".sym")) name += ".sym";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("{\"symbols\": []}"); f.close(); }
            refreshProjectTree();
        });

        QAction* newNet = newMenu->addAction("SPICE Netlist (.cir)");
        newNet->setIcon(getProjectFileIcon("a.cir"));
        connect(newNet, &QAction::triggered, [this, path]() {
            QString name = QInputDialog::getText(this, "New Netlist", "File name:");
            if (name.isEmpty()) return;
            if (!name.endsWith(".cir")) name += ".cir";
            QFile f(path + "/" + name);
            if (f.open(QIODevice::WriteOnly)) { f.write("* New SPICE Netlist\n"); f.close(); }
            refreshProjectTree();
        });
    }

    menu.addSeparator();

    // Common file/folder operations
    if (!m_openProjects.contains(path)) {
        QAction* renameAction = menu.addAction("Rename...");
        connect(renameAction, &QAction::triggered, [this, path, info]() {
            QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, info.fileName());
            if (newName.isEmpty() || newName == info.fileName()) return;
            
            QString newPath = info.absolutePath() + "/" + newName;
            if (QFile::rename(path, newPath)) {
                refreshProjectTree();
            } else {
                QMessageBox::warning(this, "Rename Error", "Could not rename file. Ensure it is not open or restricted.");
            }
        });

        QAction* duplicateAction = menu.addAction("Duplicate");
        connect(duplicateAction, &QAction::triggered, [this, path, info]() {
            QString newPath = info.absolutePath() + "/Copy_of_" + info.fileName();
            bool success = false;
            if (info.isDir()) {
                // Simplistic dir copy (Qt doesn't have a direct recursive copy in QFile)
                // For now just handle files to be safe, or use a helper
                QMessageBox::information(this, "Duplicate", "Duplicating folders is not yet supported.");
                return;
            } else {
                success = QFile::copy(path, newPath);
            }
            if (success) refreshProjectTree();
        });

        menu.addSeparator();
    }

    QAction* showInExplorer = menu.addAction("Show in File Manager");
    connect(showInExplorer, &QAction::triggered, [=]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    });

    if (!m_openProjects.contains(path)) {
        menu.addSeparator();
        QAction* deleteAction = menu.addAction("Delete");
        deleteAction->setIcon(QIcon(":/icons/tool_delete.svg"));
        connect(deleteAction, &QAction::triggered, [=]() {
             if (QMessageBox::question(this, "Delete", "Are you sure you want to delete " + info.fileName() + "?\nThis cannot be undone.") == QMessageBox::Yes) {
                 if (info.isDir()) QDir(path).removeRecursively();
                 else QFile::remove(path);
                 refreshProjectTree();
             }
        });
    }

    menu.exec(m_projectTree->mapToGlobal(pos));
}

void ProjectManager::applyKiCadStyle() {
    updateThemeStyle();
}

void ProjectManager::updateThemeStyle() {
    auto* theme = ThemeManager::theme();
    if (!theme) return;

    QString windowBg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString textColor = theme->textColor().name();
    QString textSecondary = theme->textSecondary().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString accentHover = theme->accentHover().name();

    bool isLight = theme->type() == PCBTheme::Light;
    QString toolbarBg = isLight ? "#f8fafc" : "#0a0a0c";
    QString headerBg = isLight ? "#f1f5f9" : "#121214";
    QString inputBg = isLight ? "#ffffff" : "#121214";
    QString treeHover = isLight ? "#f1f5f9" : "#1f1f23";

    QString pmStyle = QString(
        "QMainWindow { background-color: %1; color: %3; font-family: 'Inter', 'Segoe UI', sans-serif; }"
        "QMenuBar { background-color: %9; color: %4; border-bottom: 1px solid %5; padding: 4px; }"
        "QMenuBar::item:selected { background-color: %6; color: white; border-radius: 4px; }"
        
        "QToolBar#VerticalToolbar {"
        "   background-color: %8;"
        "   border-right: 1px solid %5;"
        "   padding: 15px 8px;"
        "   spacing: 12px;"
        "}"
        "QToolBar#VerticalToolbar QToolButton {"
        "   background: transparent;"
        "   border: none;"
        "   padding: 10px;"
        "   color: %4;"
        "   transition: color 0.2s;"
        "}"
        "QToolBar#VerticalToolbar QToolButton:hover { color: %6; }"
        "QToolBar#VerticalToolbar QToolButton:checked { color: %7; }"

        "QWidget#ProjectFilesPanel { background-color: %2; border-right: 1px solid %5; }"
        "QLabel#PanelHeader {"
        "   color: %6; font-size: 10px; font-weight: 800; padding: 6px 12px; "
        "   letter-spacing: 2px; text-transform: uppercase; background: %9;"
        "}"

        "QLineEdit#ProjectSearch {"
        "   background-color: %10; color: %3; border: none; border-bottom: 1px solid %5;"
        "   padding: 6px 12px; margin: 0; font-size: 12px;"
        "}"
        "QLineEdit#ProjectSearch:focus { border-bottom: 2px solid %6; background: %10; }"

        "QTreeWidget#ProjectTree { background-color: transparent; border: none; outline: 0; color: %4; }"
        "QTreeWidget#ProjectTree::item { padding: 6px 10px; border-radius: 4px; margin: 1px 5px; }"
        "QTreeWidget#ProjectTree::item:hover { background-color: %11; color: %3; }"
        "QTreeWidget#ProjectTree::item:selected { background-color: %6; color: white; }"

        "QWidget#LauncherArea { background-color: %1; }"
        "QLabel#WelcomeTitle { color: %3; font-size: 36px; font-weight: 900; letter-spacing: -1px; }"
        "QLabel#WelcomeSubtitle { color: %4; font-size: 16px; font-weight: 400; margin-top: -5px; }"

        "LauncherTile {"
        "   background: %10;"
        "   border: 1px solid %5;"
        "   border-radius: 14px;"
        "   min-width: 280px;"
        "   min-height: 100px;"
        "   margin: 4px;"
        "}"
        "LauncherTile:hover {"
        "   border: 1px solid %6;"
        "}"
        "QLabel#TileTitle { color: %3; font-size: 16px; font-weight: 700; }"
        "QLabel#TileDesc { color: %4; font-size: 13px; line-height: 1.5; }"
        
        "QStatusBar { background-color: %9; color: %4; border-top: 1px solid %5; padding: 5px; }"
        "QSplitter::handle { background-color: %5; width: 1px; }"
    )
    .arg(windowBg)       // 1
    .arg(panelBg)        // 2
    .arg(textColor)      // 3
    .arg(textSecondary)  // 4
    .arg(border)         // 5
    .arg(accent)         // 6
    .arg(accentHover)    // 7
    .arg(toolbarBg)      // 8
    .arg(headerBg)       // 9
    .arg(inputBg)        // 10
    .arg(treeHover);     // 11

    setStyleSheet(pmStyle);
    
    // Also update tiles manually since they use some hardcoded styles in their constructor
    for (auto* tile : m_launcherTiles) {
        tile->setStyleSheet(QString(
            "LauncherTile { background: %1; border: 1px solid %2; border-radius: 14px; }"
            "LauncherTile:hover { border: 1px solid %3; }"
            "QLabel#TileTitle { color: %4; }"
            "QLabel#TileDesc { color: %5; }"
        ).arg(inputBg, border, accent, textColor, textSecondary));
    }
}


void ProjectManager::createMenuBar() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    m_newProjectAction = fileMenu->addAction("&New Project...", this, &ProjectManager::createNewProject);
    m_newProjectAction->setShortcut(QKeySequence::New);
    m_openProjectAction = fileMenu->addAction("&Open Project...", this, &ProjectManager::openExistingProject);
    m_openProjectAction->setShortcut(QKeySequence::Open);

    m_recentProjectsMenu = fileMenu->addMenu("Open &Recent");
    updateRecentProjectsMenu();
    
    fileMenu->addSeparator();
    m_exitAction = fileMenu->addAction("E&xit", qApp, &QApplication::quit);
    m_exitAction->setShortcut(QKeySequence::Quit);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Refresh", this, &ProjectManager::refreshProjectTree);

    QMenu* toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Schematic Editor", this, &ProjectManager::openSchematicEditor);
    toolsMenu->addAction("Symbol Editor", this, &ProjectManager::openSymbolEditor);

    QMenu* prefsMenu = menuBar()->addMenu("&Preferences");
    prefsMenu->addAction("Settings", this, &ProjectManager::onSettings);

    QMenu* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&Help & Guides", this, &ProjectManager::showHelp, QKeySequence::HelpContents);
    helpMenu->addAction("&Developer Documentation", this, &ProjectManager::showDeveloperHelp, QKeySequence("Ctrl+Shift+F1"));
    helpMenu->addAction("Project &Health Audit...", this, &ProjectManager::onProjectAudit);
    m_aboutAction = helpMenu->addAction("&About Viora EDA", this, &ProjectManager::showAbout);
}

void ProjectManager::createNewProject() {
    QString projectName = QInputDialog::getText(this, "New Project", "Project Name:");
    if (projectName.isEmpty()) return;

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir().mkpath(defaultPath);
    QString projectPath = QFileDialog::getExistingDirectory(this, "Location", defaultPath);
    if (projectPath.isEmpty()) return;

    QString fullProjectPath = projectPath + "/" + projectName;
    Project* project = new Project(projectName, fullProjectPath);
    if (project->createNew()) {
        RecentProjects::instance().addProject(project->projectFilePath());
        updateRecentProjectsMenu();
        openProject(project->projectFilePath());
    } else {
        QMessageBox::critical(this, "Error", "Failed to create project");
    }
    delete project;
}

void ProjectManager::openExistingProject() {
    QString fileName = QFileDialog::getOpenFileName(this, "Open Project", QString(), "Viora EDA (*.flux)");
    if (!fileName.isEmpty()) {
        RecentProjects::instance().addProject(fileName);
        updateRecentProjectsMenu();
        openProject(fileName);
    }
}

void ProjectManager::openProject(const QString& path) {
    QFileInfo info(path);
    QString dirPath;

    if (info.isFile()) {
        dirPath = info.absolutePath();
    } else {
        dirPath = path;
    }
    
    // Normalize path
    dirPath = QDir(dirPath).absolutePath();
    
    if (!m_openProjects.contains(dirPath)) {
        m_openProjects.append(dirPath);
    }
    
    if (info.isFile() && path.endsWith(".flux", Qt::CaseInsensitive)) {
        m_activeProjectFile = info.absoluteFilePath();
    }
    
    refreshProjectTree();
}

void ProjectManager::updateRecentProjectsMenu() {
    if (!m_recentProjectsMenu) return;

    m_recentProjectsMenu->clear();
    const QStringList projects = RecentProjects::instance().projects();
    
    for (const QString& path : projects) {
        if (!QFile::exists(path) && !QDir(path).exists()) continue;

        QAction* action = m_recentProjectsMenu->addAction(QFileInfo(path).fileName());
        action->setData(path);
        connect(action, &QAction::triggered, [this, path]() {
            openProject(path); 
        });
    }
    
    if (projects.isEmpty()) {
        m_recentProjectsMenu->addAction("(No recent projects)")->setEnabled(false);
    } else {
        m_recentProjectsMenu->addSeparator();
        QAction* clear = m_recentProjectsMenu->addAction("Clear Recent Projects");
        connect(clear, &QAction::triggered, [this]() {
            RecentProjects::instance().clear();
            updateRecentProjectsMenu();
        });
    }
}

void ProjectManager::openSchematicEditor() { 
    if (m_activeProjectFile.isEmpty()) {
        int ret = QMessageBox::question(this, "No Active Project", 
            "No project is currently active. Do you want to create a new project?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret == QMessageBox::Yes) {
             createNewProject();
             if (m_activeProjectFile.isEmpty()) return;
        } else {
             return;
        }
    }
    
    QFileInfo fi(m_activeProjectFile);
    QString schPath = fi.absolutePath() + "/" + fi.completeBaseName() + ".sch";
    if (QFile::exists(schPath)) launchSchematicEditor(schPath);
    else launchSchematicEditor();
}

void ProjectManager::openSymbolEditor() {
    SymbolEditor* editor = new SymbolEditor(this);
    editor->show();
}

void ProjectManager::openCalculatorTools() {
    CalculatorDialog* dlg = new CalculatorDialog(this);
    dlg->show();
}

void ProjectManager::showAbout() {
    QMessageBox::about(this, "About Viora EDA",
        "Viora EDA v0.1.0\n\n"
        "Professional Electronic Design Automation\n\n"
        "Open-source PCB design software");
}

void ProjectManager::showHelp() {
    HelpWindow* help = new HelpWindow();
    help->setAttribute(Qt::WA_DeleteOnClose);
    help->show();
}

void ProjectManager::showDeveloperHelp() {
    DeveloperHelpWindow* devHelp = new DeveloperHelpWindow();
    devHelp->setAttribute(Qt::WA_DeleteOnClose);
    devHelp->show();
}

void ProjectManager::onProjectAudit() {
    ProjectAuditDialog dlg(this);
    dlg.exec();
}

void ProjectManager::openPluginsManager() {
    PluginManagerDialog dlg(this);
    dlg.exec();
}

void ProjectManager::onSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // Apply global changes if needed, though most are persisted in ConfigManager
        statusBar()->showMessage("Global settings updated.", 3000);
    }
}

void ProjectManager::launchSchematicEditor(const QString& projectPath) {
    SchematicEditor* editor = new SchematicEditor;
    
    QString pFile = resolveProjectPath(projectPath, "sch");
    
    QString pDir, pName;
    if (!pFile.isEmpty()) {
        QFileInfo fi(pFile);
        pDir = fi.absolutePath();
        pName = fi.completeBaseName();
    }
    
    editor->setProjectContext(pName, pDir);

    if (QFile::exists(pFile)) {
        editor->openFile(pFile);
    }
    editor->show();
}

QString ProjectManager::resolveProjectPath(const QString& inputPath, const QString& extension) {
    if (inputPath.isEmpty()) return QString();

    QFileInfo info(inputPath);
    QString pDir, pName, pFile;

    if (info.isDir()) {
         pDir = info.absoluteFilePath();
         pName = info.fileName();
         
         QDir d(pDir);
         QStringList flux = d.entryList(QStringList() << "*.flux", QDir::Files);
         if (!flux.isEmpty()) pName = QFileInfo(flux.first()).completeBaseName();

         QStringList files = d.entryList(QStringList() << ("*." + extension), QDir::Files);
         if (!files.isEmpty()) pFile = d.absoluteFilePath(files.first());
         else pFile = pDir + "/" + pName + "." + extension;
         
    } else {
         pDir = info.absolutePath();
         pName = info.completeBaseName();
         
         if (inputPath.endsWith("." + extension, Qt::CaseInsensitive)) pFile = inputPath;
         else pFile = pDir + "/" + pName + "." + extension;
    }
    return pFile;
}

// ============ LauncherTile Implementation ============

LauncherTile::LauncherTile(const QString& iconPath, const QString& title, 
                           const QString& description, QWidget* parent)
    : QFrame(parent) {
    setObjectName("LauncherTile");
    setCursor(Qt::PointingHandCursor);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(16);

    // Icon with subtle round background
    m_iconLabel = new QLabel;
    m_iconLabel->setObjectName("TileIcon");
    m_iconLabel->setFixedSize(56, 56);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet(
        "background-color: #1a1c22;"
        "border: 1px solid #2d2d30;"
        "border-radius: 16px;"
        "padding: 8px;"
    );
    layout->addWidget(m_iconLabel);

    // Text container
    QVBoxLayout* textLayout = new QVBoxLayout;
    textLayout->setSpacing(4);
    textLayout->setContentsMargins(0, 4, 0, 4);

    m_titleLabel = new QLabel(title);
    m_titleLabel->setObjectName("TileTitle");
    textLayout->addWidget(m_titleLabel);

    m_descLabel = new QLabel(description);
    m_descLabel->setObjectName("TileDesc");
    m_descLabel->setWordWrap(true);
    textLayout->addWidget(m_descLabel);

    textLayout->addStretch();
    layout->addLayout(textLayout, 1);

    // Small arrow indicator on the right
    QLabel* arrowLabel = new QLabel("›");
    arrowLabel->setStyleSheet("color: #3c3c3c; font-size: 22px; font-weight: bold; background: transparent;");
    arrowLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(arrowLabel);

    Q_UNUSED(iconPath)
}

void LauncherTile::setIcon(const QIcon& icon) {
    m_iconLabel->setPixmap(icon.pixmap(40, 40));
}

void LauncherTile::setIconFromChar(const QString& character, const QColor& bgColor) {
    m_iconLabel->setText(character);
    m_iconLabel->setStyleSheet(QString(
        "background-color: %1; "
        "border-radius: 12px; "
        "font-size: 22px; "
        "color: white;"
    ).arg(bgColor.name()));
}

void LauncherTile::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isEnabled()) {
        emit clicked();
    }
}

void LauncherTile::enterEvent(QEnterEvent*) {
    // Hover effect handled by CSS
}

void LauncherTile::leaveEvent(QEvent*) {
    // Leave effect handled by CSS
}

void ProjectManager::onToggleSidebar() {
    if (!m_projectPanel || !m_splitter) return;
    
    bool isVisible = m_projectPanel->isVisible();
    m_projectPanel->setVisible(!isVisible);
    
    if (!isVisible) {
        // Just became visible, restore some width
        m_splitter->setSizes({250, width() - 250});
    }
}