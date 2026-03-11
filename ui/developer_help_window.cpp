#include "developer_help_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QStatusBar>
#include <QApplication>
#include <QLabel>

DeveloperHelpWindow::DeveloperHelpWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Viora EDA - Developer Portal & Architecture");
    resize(1200, 800);
    
    setupUi();
    populateTechnicalDocs();
    applyDeveloperTheme();
    
    statusBar()->showMessage("Developer Mode Active");
}

DeveloperHelpWindow::~DeveloperHelpWindow() {}

void DeveloperHelpWindow::setupUi() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Banner Header
    QLabel* banner = new QLabel("   DEVELOPER PORTAL & ENGINEERING GUIDES");
    banner->setFixedHeight(40);
    banner->setStyleSheet("background-color: #1e1e1e; color: #4ec9b0; font-weight: bold; font-size: 14px; border-bottom: 2px solid #4ec9b0;");
    mainLayout->addWidget(banner);

    m_splitter = new QSplitter(Qt::Horizontal, central);
    
    // Left panel: Doc Tree (Technical categories)
    m_docTree = new QTreeWidget(m_splitter);
    m_docTree->setHeaderHidden(true);
    m_docTree->setFixedWidth(300);
    m_docTree->setIconSize(QSize(16, 16));
    m_docTree->setIndentation(20);
    m_docTree->setAnimated(true);

    // Right panel: Content viewer
    m_contentViewer = new QTextBrowser(m_splitter);
    m_contentViewer->setOpenExternalLinks(true);
    m_contentViewer->setReadOnly(true);
    
    m_splitter->addWidget(m_docTree);
    m_splitter->addWidget(m_contentViewer);
    m_splitter->setStretchFactor(1, 1);
    
    mainLayout->addWidget(m_splitter);
    
    connect(m_docTree, &QTreeWidget::itemClicked, this, &DeveloperHelpWindow::onDocSelected);
}

void DeveloperHelpWindow::populateTechnicalDocs() {
    m_docTree->clear();
    
    // Scan docs directory recursively
    QString docsPath = qApp->applicationDirPath() + "/docs";
    if (!QDir(docsPath).exists()) {
        docsPath = "../docs";
    }
    if (!QDir(docsPath).exists()) {
        docsPath = "/home/jnd/qt_projects/VioraEDA/docs";
    }

    QTreeWidgetItem* root = new QTreeWidgetItem(m_docTree);
    root->setText(0, "Technical Documentation");
    root->setExpanded(true);
    
    scanDirectory(docsPath, root);
    
    if (m_docTree->topLevelItemCount() > 0) {
        // Find first file leaf to select
        QTreeWidgetItemIterator it(m_docTree);
        while (*it) {
            if (!(*it)->data(0, Qt::UserRole).toString().isEmpty()) {
                m_docTree->setCurrentItem(*it);
                onDocSelected(*it, 0);
                break;
            }
            ++it;
        }
    }
}

void DeveloperHelpWindow::scanDirectory(const QString& path, QTreeWidgetItem* parent) {
    QDir dir(path);
    
    // Add subdirectories first
    QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& subDirInfo : subDirs) {
        // Skip assets/archive
        if (subDirInfo.fileName() == "assets" || subDirInfo.fileName() == "archive") continue;
        
        QTreeWidgetItem* item = new QTreeWidgetItem(parent);
        item->setText(0, subDirInfo.fileName().toUpper());
        item->setIcon(0, QIcon(":/icons/folder_closed.svg"));
        scanDirectory(subDirInfo.absoluteFilePath(), item);
    }
    
    // Add Markdown files
    QStringList filters;
    filters << "*.md";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    
    for (const QFileInfo& file : files) {
        QString title = file.baseName().replace("_", " ");
        if (title.length() > 0) title[0] = title[0].toUpper();
        
        QTreeWidgetItem* item = new QTreeWidgetItem(parent);
        item->setText(0, title);
        item->setData(0, Qt::UserRole, file.absoluteFilePath());
        item->setIcon(0, QIcon(":/icons/component_file.svg"));
    }
}

void DeveloperHelpWindow::onDocSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty()) {
        loadMarkdown(path);
    }
}

void DeveloperHelpWindow::loadMarkdown(const QString& path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = file.readAll();
        m_contentViewer->setMarkdown(content);
        file.close();
        statusBar()->showMessage("Loaded Engineering Guide: " + QFileInfo(path).fileName());
    }
}

void DeveloperHelpWindow::applyDeveloperTheme() {
    setStyleSheet(
        "QMainWindow, QWidget { background-color: #1e1e1e; color: #d4d4d4; }"
        "QTreeWidget { background-color: #252526; border: 1px solid #333; color: #d4d4d4; font-size: 12px; font-family: 'Cascadia Code', 'Consolas', monospace; }"
        "QTreeWidget::item { padding: 4px; border: none; }"
        "QTreeWidget::item:selected { background-color: #094771; color: #ffffff; }"
        "QTextBrowser { background-color: #1e1e1e; border: 1px solid #333; padding: 30px; color: #e0e0e0; font-family: 'Inter', sans-serif; font-size: 14px; line-height: 1.6; }"
        "QStatusBar { background-color: #6a9955; color: white; }" // VS Code Green for Dev
        "QSplitter::handle { background: #333; }"
    );
}
