#include "help_window.h"
#include "../core/theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QStatusBar>
#include <QApplication>
#include <QLabel>

HelpWindow::HelpWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("viospice - Documentation & Guides");
    resize(1000, 700);
    
    setupUi();
    populateGuides();
    applyTheme();
    
    statusBar()->showMessage("Ready");
}

HelpWindow::~HelpWindow() {}

void HelpWindow::setupUi() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    m_splitter = new QSplitter(Qt::Horizontal, central);
    m_splitter->setHandleWidth(1);
    
    // Left panel: Guides list
    QWidget* leftPanel = new QWidget(m_splitter);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    
    QLabel* listHeader = new QLabel(" GUIDES", leftPanel);
    listHeader->setFixedHeight(32);
    leftLayout->addWidget(listHeader);
    
    m_guidesList = new QListWidget(leftPanel);
    m_guidesList->setFixedWidth(250);
    m_guidesList->setFrameShape(QFrame::NoFrame);
    leftLayout->addWidget(m_guidesList);
    
    // Right panel: Content viewer
    m_contentViewer = new QTextBrowser(m_splitter);
    m_contentViewer->setOpenExternalLinks(true);
    m_contentViewer->setReadOnly(true);
    m_contentViewer->setFrameShape(QFrame::NoFrame);
    
    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(m_contentViewer);
    m_splitter->setStretchFactor(1, 1);
    
    mainLayout->addWidget(m_splitter);
    
    connect(m_guidesList, &QListWidget::itemClicked, this, &HelpWindow::onGuideSelected);
}

void HelpWindow::populateGuides() {
    m_guidesList->clear();
    
    // Scan docs directory
    QDir docsDir(qApp->applicationDirPath() + "/docs");
    if (!docsDir.exists()) docsDir = QDir("../docs");
    if (!docsDir.exists()) docsDir = QDir("/home/jnd/qt_projects/viospice/docs");
    if (!docsDir.exists()) docsDir = QDir("/home/jnd/qt_projects/VioraEDA/docs");

    QStringList filters;
    filters << "*.md";
    QFileInfoList files = docsDir.entryInfoList(filters, QDir::Files, QDir::Name);
    
    for (const QFileInfo& file : files) {
        QString title = file.baseName().replace("_", " ");
        if (title.length() > 0) title[0] = title[0].toUpper();
        
        QListWidgetItem* item = new QListWidgetItem(title, m_guidesList);
        item->setData(Qt::UserRole, file.absoluteFilePath());
    }
    
    if (m_guidesList->count() > 0) {
        m_guidesList->setCurrentRow(0);
        onGuideSelected(m_guidesList->item(0));
    }
}

void HelpWindow::onGuideSelected(QListWidgetItem* item) {
    if (!item) return;
    QString filePath = item->data(Qt::UserRole).toString();
    loadGuide(filePath);
}

void HelpWindow::loadGuide(const QString& filename) {
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = file.readAll();
        m_contentViewer->document()->setDefaultStyleSheet(markdownStyleSheet());
        m_contentViewer->setMarkdown(content);
        file.close();
        statusBar()->showMessage("Loaded: " + QFileInfo(filename).fileName());
    }
}

QString HelpWindow::markdownStyleSheet() const {
    PCBTheme* theme = ThemeManager::theme();
    bool isLight = theme && theme->type() == PCBTheme::Light;
    
    if (isLight) {
        return "body { color: #1e293b; font-family: 'Inter', sans-serif; line-height: 1.6; padding: 20px; }"
               "h1 { color: #0f172a; font-size: 24px; border-bottom: 1px solid #e2e8f0; padding-bottom: 10px; }"
               "h2 { color: #0f172a; font-size: 18px; margin-top: 20px; }"
               "code { background: #f1f5f9; color: #2563eb; padding: 2px 4px; border-radius: 4px; font-family: monospace; }"
               "pre { background: #f8fafc; border: 1px solid #e2e8f0; padding: 15px; border-radius: 8px; }";
    }
    return "body { color: #d4d4d4; font-family: 'Inter', sans-serif; line-height: 1.6; padding: 20px; }"
           "h1 { color: #ffffff; font-size: 24px; border-bottom: 1px solid #333; padding-bottom: 10px; }"
           "h2 { color: #ffffff; font-size: 18px; margin-top: 20px; }"
           "code { background: #2d2d30; color: #9cdcfe; padding: 2px 4px; border-radius: 4px; font-family: monospace; }"
           "pre { background: #1e1e1e; border: 1px solid #333; padding: 15px; border-radius: 8px; }";
}

void HelpWindow::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString bg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString secFg = theme->textSecondary().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString hoverBg = (theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2d2d30";

    setStyleSheet(QString(
        "QMainWindow, QWidget { background-color: %1; color: %3; }"
        "QLabel { color: %4; font-size: 10px; font-weight: 800; letter-spacing: 1px; padding: 10px; }"
        "QListWidget { background-color: %2; border-right: 1px solid %5; color: %3; outline: none; }"
        "QListWidget::item { padding: 10px 15px; border-bottom: 1px solid %5; }"
        "QListWidget::item:selected { background-color: %6; color: white; border: none; }"
        "QListWidget::item:hover:!selected { background-color: %7; }"
        "QTextBrowser { background-color: %1; color: %3; padding: 30px; selection-background-color: %6; }"
        "QSplitter::handle { background-color: %5; }"
        "QStatusBar { background-color: %6; color: white; }"
    ).arg(bg, panelBg, fg, secFg, border, accent, hoverBg));
}
