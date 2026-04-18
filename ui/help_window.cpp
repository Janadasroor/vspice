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
#include <QIcon>

HelpWindow::HelpWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("VioSpice User Manual");
    resize(1100, 750);
    
    setupUi();
    populateGuides();
    applyTheme();
    
    statusBar()->showMessage("Welcome to VioSpice Help");
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
    
    // Left panel: Search + Tree
    QWidget* leftPanel = new QWidget(m_splitter);
    leftPanel->setObjectName("sidebar");
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(10, 10, 10, 10);
    leftLayout->setSpacing(10);
    
    m_searchEdit = new QLineEdit(leftPanel);
    m_searchEdit->setPlaceholderText("Search guides...");
    m_searchEdit->setFixedHeight(30);
    m_searchEdit->setClearButtonEnabled(true);
    leftLayout->addWidget(m_searchEdit);
    
    m_docTree = new QTreeWidget(leftPanel);
    m_docTree->setHeaderHidden(true);
    m_docTree->setIndentation(15);
    m_docTree->setAnimated(true);
    m_docTree->setFrameShape(QFrame::NoFrame);
    m_docTree->setIconSize(QSize(18, 18));
    leftLayout->addWidget(m_docTree);
    
    // Right panel: Content viewer
    m_contentViewer = new QTextBrowser(m_splitter);
    m_contentViewer->setOpenExternalLinks(true);
    m_contentViewer->setReadOnly(true);
    m_contentViewer->setFrameShape(QFrame::NoFrame);
    m_contentViewer->setPlaceholderText("Select a guide from the sidebar to view details.");
    
    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(m_contentViewer);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({280, 820});
    
    mainLayout->addWidget(m_splitter);
    
    connect(m_docTree, &QTreeWidget::itemClicked, this, &HelpWindow::onDocSelected);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HelpWindow::onSearchChanged);
}

void HelpWindow::populateGuides() {
    m_docTree->clear();
    
    QString baseDir = qApp->applicationDirPath();
    QStringList paths = {
        baseDir + "/docs/user",
        "../docs/user",
        "../../docs/user",
        "/home/jnd/qt_projects/viospice/docs/user"
    };

    QDir docsDir;
    for (const QString& path : paths) {
        if (QDir(path).exists()) {
            docsDir = QDir(path);
            break;
        }
    }

    if (!docsDir.exists()) return;

    // Create Categories
    QTreeWidgetItem* catBasics = new QTreeWidgetItem(m_docTree);
    catBasics->setText(0, "GETTING STARTED");
    catBasics->setFirstColumnSpanned(true);
    
    QTreeWidgetItem* catWorkflow = new QTreeWidgetItem(m_docTree);
    catWorkflow->setText(0, "WORKFLOWS & FEATURES");
    catWorkflow->setFirstColumnSpanned(true);

    QTreeWidgetItem* catRef = new QTreeWidgetItem(m_docTree);
    catRef->setText(0, "REFERENCES");
    catRef->setFirstColumnSpanned(true);

    // Apply bold font to headers
    QFont headerFont = m_docTree->font();
    headerFont.setBold(true);
    catBasics->setFont(0, headerFont);
    catWorkflow->setFont(0, headerFont);
    catRef->setFont(0, headerFont);

    QStringList filters;
    filters << "*.md";
    QFileInfoList files = docsDir.entryInfoList(filters, QDir::Files, QDir::Name);
    
    for (const QFileInfo& file : files) {
        QString rawName = file.baseName();
        QString title = rawName;
        if (title.contains("_")) {
            title = title.mid(title.indexOf("_") + 1).replace("_", " ");
        }
        if (title.length() > 0) title[0] = title[0].toUpper();
        
        QTreeWidgetItem* parent = catBasics;
        if (rawName.contains("Drawing") || rawName.contains("Simulation")) parent = catWorkflow;
        else if (rawName.contains("Shortcut")) parent = catRef;

        QTreeWidgetItem* item = new QTreeWidgetItem(parent);
        item->setText(0, title);
        item->setData(0, Qt::UserRole, file.absoluteFilePath());
        item->setIcon(0, QIcon(":/icons/component_file.svg"));
    }
    
    m_docTree->expandAll();

    // Select first item
    if (catBasics->childCount() > 0) {
        m_docTree->setCurrentItem(catBasics->child(0));
        onDocSelected(catBasics->child(0), 0);
    }
}

void HelpWindow::onDocSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    QString path = item->data(0, Qt::UserRole).toString();
    if (!path.isEmpty()) {
        loadGuide(path);
    }
}

void HelpWindow::onSearchChanged(const QString& text) {
    QTreeWidgetItemIterator it(m_docTree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        if (item->childCount() > 0) { // It's a category
            item->setHidden(false); // Categories stay visible usually
        } else {
            bool match = item->text(0).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
        }
        ++it;
    }
}

void HelpWindow::loadGuide(const QString& filename) {
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = file.readAll();
        m_contentViewer->document()->setDefaultStyleSheet(markdownStyleSheet());
        m_contentViewer->setMarkdown(content);
        file.close();
        statusBar()->showMessage("Guide: " + QFileInfo(filename).baseName().replace("_", " "));
    }
}

QString HelpWindow::markdownStyleSheet() const {
    PCBTheme* theme = ThemeManager::theme();
    bool isLight = theme && theme->type() == PCBTheme::Light;
    
    QString bodyColor = isLight ? "#1e293b" : "#e0e0e0";
    QString hColor = isLight ? "#0f172a" : "#ffffff";
    QString codeBg = isLight ? "#f1f5f9" : "#2d2d30";
    QString preBg = isLight ? "#f8fafc" : "#1a1a1a";
    QString accent = isLight ? "#2563eb" : "#4fc1ff";

    return QString(
        "body { color: %1; font-family: 'Inter', 'Segoe UI', sans-serif; line-height: 1.7; padding: 30px; }"
        "h1 { color: %2; font-size: 28px; border-bottom: 2px solid %3; padding-bottom: 12px; margin-bottom: 20px; }"
        "h2 { color: %2; font-size: 20px; margin-top: 30px; border-bottom: 1px solid #333; padding-bottom: 5px; }"
        "h3 { color: %2; font-size: 16px; font-weight: bold; margin-top: 20px; }"
        "p { margin-bottom: 15px; }"
        "ul, ol { margin-bottom: 15px; margin-left: 20px; }"
        "li { margin-bottom: 5px; }"
        "code { background: %4; color: %5; padding: 2px 5px; border-radius: 4px; font-family: 'Cascadia Code', monospace; font-size: 13px; }"
        "pre { background: %6; border: 1px solid #333; padding: 20px; border-radius: 10px; margin: 20px 0; }"
    ).arg(bodyColor, hColor, accent, codeBg, accent, preBg);
}

void HelpWindow::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString bg = theme->windowBackground().name();
    QString panelBg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString itemHover = (theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#2d2d30";
    QString searchBg = (theme->type() == PCBTheme::Light) ? "#ffffff" : "#1a1a1a";
    QString secondaryFg = theme->textSecondary().name();

    setStyleSheet(QString(
        "QMainWindow { background-color: %1; }"
        "QWidget#sidebar { background-color: %2; border-right: 1px solid %4; }"
        
        "QLineEdit { background-color: %7; border: 1px solid %4; border-radius: 6px; padding: 6px 12px; color: %3; font-size: 13px; }"
        "QLineEdit:focus { border-color: %5; }"
        
        "QTreeWidget { background-color: transparent; border: none; color: %3; outline: none; "
        "               show-decoration-selected: 0; selection-background-color: transparent; }"
        
        /* Category styling (Items with no data) */
        "QTreeWidget::item { padding: 8px 12px; border-radius: 4px; color: %3; font-size: 13px; }"
        
        /* Article selection - Left bar + subtle background */
        "QTreeWidget::item:selected { "
        "   background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %5, stop:0.04 %5, stop:0.041 rgba(86, 156, 214, 20), stop:1 rgba(86, 156, 214, 10));"
        "   color: %5; font-weight: bold; border-radius: 0px; "
        "}"
        
        "QTreeWidget::item:hover:!selected { background-color: %6; }"
        
        /* Modern branch indicators */
        "QTreeWidget::branch { background-color: transparent; }"
        "QTreeWidget::branch:has-children:closed:has-siblings, QTreeWidget::branch:has-children:closed:!has-siblings { image: url(:/icons/chevron_right.svg); }"
        "QTreeWidget::branch:open:has-children:has-siblings, QTreeWidget::branch:open:has-children:!has-siblings { image: url(:/icons/chevron_down.svg); }"
        
        "QTextBrowser { background-color: %1; color: %3; selection-background-color: %5; border: none; }"
        "QSplitter::handle { background-color: %4; }"
        "QStatusBar { background-color: %2; color: %8; border-top: 1px solid %4; font-size: 11px; }"
    ).arg(bg, panelBg, fg, border, accent, itemHover, searchBg, secondaryFg));
}
