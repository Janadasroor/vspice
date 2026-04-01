#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QMainWindow>
#include <QMenuBar>
#include <QTreeWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QScrollArea>
#include <QGridLayout>
#include <QFrame>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QList>
#include <QStatusBar>
#include <QSplitter>
#include <QPainter>
#include <QPainterPath>
#include "recent_projects.h"

class LauncherTile;
class CollapsibleSection;

class ProjectManager : public QMainWindow {
    Q_OBJECT

public:
    ProjectManager(QWidget* parent = nullptr);
    ~ProjectManager();

private slots:
    void createNewProject();
    void openExistingProject();
    void addFolderToWorkspace();
    void openProject(const QString& path);
    void openSchematicEditor();
    void openSchematicFromTemplate(const QString& filePath);
    void openSymbolEditor();
    void openCalculatorTools();
    void openPluginsManager();
    void openSpiceModelManager();
    void importLtspiceBatch();
    void importKicadBatch();
    void importLtspiceDiodeModels();
    void importLtspiceJfetModels();
    void importLtspiceBjtModels();
    void importLtspiceMosModels();
    void importLtspiceResistorModels();
    void importLtspiceCapacitorModels();
    void importLtspiceInductorModels();
    void importLtspiceStandardPassiveModels();
    void onSettings();
    void showAbout();
    void showHelp();
    void showDeveloperHelp();
    void onProjectAudit();
    void onProjectTreeItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onProjectTreeContextMenu(const QPoint& pos);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void setupUI();
    void applyKiCadStyle();
    void updateThemeStyle();
    void createMenuBar();
    QWidget* createProjectFilesPanel();
    QWidget* createLauncherArea();
    void updateLauncherLayout();

    void launchSchematicEditor(const QString& projectPath = QString());
    void populateProjectTree(const QString& projectPath);
    void addLauncherTile(QGridLayout* grid, int row, int col,
                         const QString& title, const QString& desc,
                         const QString& iconPath,
                         void (ProjectManager::*slot)());
    QIcon createDocumentIcon(const QColor& accentColor, const QString& label) const;
    QIcon createFolderIcon(bool open = false) const;
    QIcon getProjectFileIcon(const QString& fileName) const;
    QString resolveProjectPath(const QString& inputPath, const QString& extension);

    // UI components
    QWidget* m_centralWidget;
    QSplitter* m_splitter;
    QWidget* m_launcherScrollContent;

    // Three categorized grids
    QGridLayout* m_launcherGrid;   // Design + Utility tiles
    QGridLayout* m_importGrid;     // Import tiles (inside collapsible section)

    // Project Files Panel (Left)
    QWidget* m_projectPanel;
    QTreeWidget* m_projectTree;

    // Launcher Area (Right)
    QWidget* m_launcherArea;
    QList<LauncherTile*> m_launcherTiles;   // Design + Utility tiles
    QList<LauncherTile*> m_importTiles;     // Import tiles
    CollapsibleSection* m_importSection;    // Collapsible import section

    // Current project state
    QStringList m_workspaceFolders;
    QString m_workspaceFilePath;
    bool m_workspaceDirty;
    QString m_pendingTemplateFile;

    // Core methods
    void refreshProjectTree();
    void addFolderToTree(const QString& folderPath, class QTreeWidgetItem* parent = nullptr);
    void updateRecentProjectsMenu();
    void saveWorkspace();
    void loadWorkspace(const QString& path);

    // UI elements
    QMenu* m_recentProjectsMenu;

    // Actions
    QAction* m_newProjectAction;
    QAction* m_openProjectAction;
    QAction* m_addFolderAction;
    QAction* m_exitAction;
    QAction* m_aboutAction;
};

// ─────────────────────────────────────────────────────────────────────────────
// LauncherTile — card with paintEvent-rendered accent stripe + hover glow
// ─────────────────────────────────────────────────────────────────────────────
class LauncherTile : public QWidget {
    Q_OBJECT
public:
    LauncherTile(const QString& iconPath, const QString& title,
                 const QString& description,
                 const QColor& accentColor = QColor("#3b82f6"),
                 const QString& category = QString(),
                 QWidget* parent = nullptr);

    void setIcon(const QIcon& icon);
    void setIconFromChar(const QString& character, const QColor& bgColor);
    void setAccentColor(const QColor& color);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_descLabel;
    QColor  m_accentColor;
    bool    m_hovered;
};

// ─────────────────────────────────────────────────────────────────────────────
// CollapsibleSection — clickable header that shows/hides a grid of tiles
// ─────────────────────────────────────────────────────────────────────────────
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, const QColor& accentColor,
                       bool startCollapsed = true, QWidget* parent = nullptr);

    QGridLayout* grid()  const { return m_grid; }
    QWidget*     content() const { return m_content; }
    QList<LauncherTile*>& tiles() { return m_tiles; }
    bool isCollapsed() const { return m_collapsed; }
    void updateCount(int n);

signals:
    void toggled(bool collapsed);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QWidget*     m_header;
    QWidget*     m_content;
    QGridLayout* m_grid;
    QLabel*      m_arrowLabel;
    QLabel*      m_countBadge;
    bool         m_collapsed;
    bool         m_hovered;
    QColor       m_accentColor;
    QList<LauncherTile*> m_tiles;
};

#endif // PROJECTMANAGER_H
