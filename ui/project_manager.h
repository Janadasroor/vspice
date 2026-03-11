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
#include <QMouseEvent>
#include <QList>
#include <QStatusBar>
#include <QSplitter>
#include "recent_projects.h"

class LauncherTile; // Forward declaration

class ProjectManager : public QMainWindow {
    Q_OBJECT

public:
    ProjectManager(QWidget* parent = nullptr);
    ~ProjectManager();

private slots:
    void createNewProject();
    void openExistingProject();
    void openProject(const QString& path);
    void openSchematicEditor();
    void openSymbolEditor();
    void openCalculatorTools();
    void openPluginsManager();
    void onSettings();
    void onToggleSidebar();
    void showAbout();
    void showHelp();
    void showDeveloperHelp();
    void onProjectAudit();
    void onProjectTreeItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onProjectTreeContextMenu(const QPoint& pos);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void applyKiCadStyle();
    void updateThemeStyle();
    void createMenuBar();
    QWidget* createProjectFilesPanel();
    QToolBar* createVerticalToolbar();
    QWidget* createLauncherArea();
    void updateLauncherLayout();

    void launchSchematicEditor(const QString& projectPath = QString());
    void populateProjectTree(const QString& projectPath);
    void addLauncherTile(QGridLayout* grid, int row, int col, 
                         const QString& title, const QString& desc, 
                         const QString& iconPath, 
                         void (ProjectManager::*slot)());
    QIcon createDocumentIcon(const QColor& accentColor, const QString& label, bool isActive = false) const;
    QIcon createFolderIcon(bool open = false) const;
    QIcon getProjectFileIcon(const QString& fileName) const;
    QString resolveProjectPath(const QString& inputPath, const QString& extension);

    // UI components
    QWidget* m_centralWidget;
    QSplitter* m_splitter;
    QGridLayout* m_launcherGrid;
    QWidget* m_launcherScrollContent;
    
    // Project Files Panel (Left)
    QWidget* m_projectPanel;
    QTreeWidget* m_projectTree;
    
    // Vertical Toolbar
    QToolBar* m_verticalToolbar;
    
    // Launcher Area (Right)
    QWidget* m_launcherArea;
    QList<LauncherTile*> m_launcherTiles;

    // Current project state
    QStringList m_openProjects;
    QString m_activeProjectFile;
    
    // Core methods
    void refreshProjectTree();
    void addProjectToTree(const QString& projectPath);
    void updateRecentProjectsMenu();

    // UI elements
    QMenu* m_recentProjectsMenu;

    // Actions
    QAction* m_newProjectAction;
    QAction* m_openProjectAction;
    QAction* m_activateProjectAction;
    QAction* m_exitAction;
    QAction* m_aboutAction;

public slots:
    void activateProject(const QString& projectFile);
};

// Launcher Tile - Large button with icon and description
class LauncherTile : public QFrame {
    Q_OBJECT
public:
    LauncherTile(const QString& iconPath, const QString& title, 
                 const QString& description, QWidget* parent = nullptr);
    
    void setIcon(const QIcon& icon);
    void setIconFromChar(const QString& character, const QColor& bgColor);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QLabel* m_iconLabel;
    QLabel* m_titleLabel;
    QLabel* m_descLabel;
};

#endif // PROJECTMANAGER_H