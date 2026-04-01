#include "template_gallery_widget.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QTabWidget>
#include <QGridLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPalette>
#include <QColor>
#include <QPainter>
#include <QGraphicsScene>
#include <QFileDialog>
#include <QScrollBar>
#include <QDir>
#include "theme_manager.h"
#include "theme.h"

TemplateGalleryWidget::TemplateGalleryWidget(QWidget* parent)
    : QWidget(parent)
    , m_searchEdit(nullptr)
    , m_categoryTabs(nullptr)
    , m_scrollArea(nullptr)
    , m_cardContainer(nullptr)
    , m_cardGrid(nullptr) {
    setupUi();
    loadTemplates();
}

TemplateGalleryWidget::~TemplateGalleryWidget() {
}

QString TemplateGalleryWidget::templatesDirectory() const {
    QString appDir = QApplication::applicationDirPath();
    QStringList paths = {
        appDir + "/../templates/circuits",
        appDir + "/templates/circuits",
        QDir::home().filePath(".viospice/templates/circuits")
    };
    
    for (const QString& path : paths) {
        if (QFile::exists(path)) return path;
    }
    
    return paths.first();
}

void TemplateGalleryWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    m_titleLabel = new QLabel("Circuit Templates", this);
    m_titleLabel->setObjectName("GalleryTitle");
    mainLayout->addWidget(m_titleLabel);
    
    m_subtitleLabel = new QLabel("Start with 25+ ready-to-simulate circuits", this);
    m_subtitleLabel->setObjectName("GallerySubtitle");
    mainLayout->addWidget(m_subtitleLabel);
    
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search templates...");
    m_searchEdit->setObjectName("GallerySearch");
    m_searchEdit->setMinimumHeight(40);
    mainLayout->addWidget(m_searchEdit);
    
    m_categoryTabs = new QTabWidget(this);
    m_categoryTabs->setObjectName("GalleryTabs");
    
    m_categoryTabs->addTab(new QWidget(), "All");
    m_categoryTabs->addTab(new QWidget(), "Amplifiers");
    m_categoryTabs->addTab(new QWidget(), "Filters");
    m_categoryTabs->addTab(new QWidget(), "Oscillators");
    m_categoryTabs->addTab(new QWidget(), "Power");
    m_categoryTabs->addTab(new QWidget(), "Timers");
    m_categoryTabs->addTab(new QWidget(), "Basics");
    
    mainLayout->addWidget(m_categoryTabs);
    
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("GalleryScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    
    m_cardContainer = new QWidget;
    m_cardContainer->setObjectName("GalleryCardContainer");
    
    m_cardGrid = new QGridLayout(m_cardContainer);
    m_cardGrid->setSpacing(16);
    m_cardGrid->setContentsMargins(0, 0, 0, 0);
    
    m_scrollArea->setWidget(m_cardContainer);
    mainLayout->addWidget(m_scrollArea, 1);
    
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TemplateGalleryWidget::onSearchChanged);
    connect(m_categoryTabs, &QTabWidget::currentChanged, this, &TemplateGalleryWidget::onCategoryChanged);
}

void TemplateGalleryWidget::updateTheme() {
    auto* theme = ThemeManager::theme();
    if (!theme) return;

    const bool isLight = theme->type() == PCBTheme::Light;
    const QString windowBg    = isLight ? "#f1f5f9" : theme->windowBackground().name();
    const QString textPrimary = theme->textColor().name();
    const QString textSec     = theme->textSecondary().name();
    const QString accent      = theme->accentColor().name();
    const QString border      = theme->panelBorder().name();
    const QString inputBg     = isLight ? "#ffffff" : "#161b22";
    const QString cardBg      = isLight ? "#ffffff" : "#161b22";

    setStyleSheet(QString(
        "TemplateGalleryWidget { background: %1; }"
        "QLabel#GalleryTitle { color: %2; font-size: 24px; font-weight: bold; background:transparent; }"
        "QLabel#GallerySubtitle { color: %3; font-size: 14px; background:transparent; }"
        
        "QLineEdit#GallerySearch {"
        "  background: %7; border: 1px solid %5; border-radius: 6px;"
        "  padding: 10px; color: %2; font-size: 14px;"
        "}"
        "QLineEdit#GallerySearch:focus { border-color: %4; }"

        "QTabWidget#GalleryTabs::pane { background: transparent; border: none; }"
        "QTabBar::tab {"
        "  background: %7; color: %3; padding: 8px 16px; border: 1px solid %5;"
        "  border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px;"
        "  margin-right: 2px;"
        "}"
        "QTabBar::tab:selected { background: %4; color: #fff; border-color: %4; }"
        "QTabBar::tab:hover:!selected { background: %8; }"

        "QScrollArea#GalleryScrollArea { background: transparent; border: none; }"
        "QWidget#GalleryCardContainer { background: transparent; }"
        
        "QScrollBar:vertical { background:transparent; width:8px; }"
        "QScrollBar::handle:vertical { background:%5; border-radius:4px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
    )
    .arg(windowBg)       // 1
    .arg(textPrimary)    // 2
    .arg(textSec)        // 3
    .arg(accent)         // 4
    .arg(border)         // 5
    .arg(cardBg)         // 6
    .arg(inputBg)        // 7
    .arg(isLight ? "#f1f5f9" : "#21262d") // 8
    );
    
    // Refresh cards
    populateGallery();
}

void TemplateGalleryWidget::loadTemplates() {
    m_templatesById.clear();
    
    QString metaPath = templatesDirectory() + "/metadata.json";
    QFile metaFile(metaPath);
    if (metaFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll());
        m_metadata = doc.object();
        metaFile.close();
    }
    
    QString basePath = templatesDirectory();
    
    for (auto it = m_metadata.begin(); it != m_metadata.end(); ++it) {
        QString id = it.key();
        QJsonObject obj = it.value().toObject();
        
        Template tpl;
        tpl.id = id;
        tpl.name = obj.value("name").toString(id);
        tpl.category = obj.value("category").toString("basics");
        tpl.description = obj.value("description").toString("");
        QJsonArray tagsArray = obj.value("tags").toArray();
        for (const QJsonValue& tagVal : tagsArray) {
            tpl.tags.append(tagVal.toString());
        }
        tpl.hasSimulation = obj.value("hasSimulation").toBool(false);
        tpl.simulationType = obj.value("simulationType").toString("op");
        
        QString schPath = obj.value("schematic").toString();
        if (!schPath.isEmpty()) {
            tpl.schematicPath = basePath + "/" + schPath;
        }
        
        QString cirPath = obj.value("netlist").toString();
        if (!cirPath.isEmpty()) {
            tpl.netlistPath = basePath + "/" + cirPath;
        }
        
        tpl.thumbnailPath = tpl.schematicPath + ".png";
        
        m_templatesById[id] = tpl;
    }
    
    populateGallery();
}

void TemplateGalleryWidget::populateGallery() {
    while (m_cardGrid->count() > 0) {
        QLayoutItem* item = m_cardGrid->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_tryItButtons.clear();
    
    int col = 0, row = 0;
    int maxCols = 4;
    
    auto* theme = ThemeManager::theme();
    const bool isLight = theme ? theme->type() == PCBTheme::Light : false;
    const QString cardBg = isLight ? "#ffffff" : "#161b22";
    const QString cardBorder = theme ? theme->panelBorder().name() : "#30363d";
    const QString textPrimary = theme ? theme->textColor().name() : "#e6edf3";
    const QString textSec = theme ? theme->textSecondary().name() : "#8b949e";
    const QString accent = theme ? theme->accentColor().name() : "#58a6ff";

    for (const Template& tpl : m_filteredTemplates) {
        auto* card = new QFrame(this);
        card->setStyleSheet(QString(
            "QFrame { background: %1; border: 1px solid %2; border-radius: 8px; padding: 12px; }"
            "QFrame:hover { border-color: %3; }"
        ).arg(cardBg).arg(cardBorder).arg(accent));
        card->setFixedSize(220, 260);
        
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setSpacing(8);
        
        QLabel* thumbLabel = new QLabel(card);
        thumbLabel->setFixedSize(196, 120);
        thumbLabel->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(isLight ? "#f1f5f9" : "#21262d"));
        thumbLabel->setAlignment(Qt::AlignCenter);
        
        if (QFile::exists(tpl.thumbnailPath)) {
            thumbLabel->setPixmap(QPixmap(tpl.thumbnailPath).scaled(196, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            thumbLabel->setText("📄");
            thumbLabel->setStyleSheet(QString("font-size: 48px; background: %1; border-radius: 4px; color: %2;")
                                      .arg(isLight ? "#e2e8f0" : "#21262d")
                                      .arg(textSec));
        }
        
        cardLayout->addWidget(thumbLabel);
        
        QLabel* nameLabel = new QLabel(tpl.name, card);
        nameLabel->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(textPrimary));
        nameLabel->setWordWrap(true);
        cardLayout->addWidget(nameLabel);
        
        QLabel* catLabel = new QLabel(tpl.category, card);
        catLabel->setStyleSheet(QString("font-size: 11px; color: %1; background: %2; padding: 2px 8px; border-radius: 10px;")
                                .arg(accent).arg(isLight ? "#e0f2fe" : "#21262d"));
        cardLayout->addWidget(catLabel);
        
        QLabel* descLabel = new QLabel(tpl.description, card);
        descLabel->setStyleSheet(QString("font-size: 11px; color: %1;").arg(textSec));
        descLabel->setWordWrap(true);
        descLabel->setMaximumHeight(40);
        cardLayout->addWidget(descLabel);
        
        QPushButton* tryItBtn = new QPushButton("Try It ▶", card);
        tryItBtn->setStyleSheet(
            "QPushButton { background: #238636; color: white; border: none; border-radius: 6px; "
            "padding: 8px 16px; font-weight: bold; } "
            "QPushButton:hover { background: #2ea043; }"
        );
        tryItBtn->setCursor(Qt::PointingHandCursor);
        
        connect(tryItBtn, &QPushButton::clicked, this, [this, tpl]() {
            onTryItClicked(tpl.id);
        });
        
        m_tryItButtons[tpl.id] = tryItBtn;
        cardLayout->addWidget(tryItBtn);
        
        m_cardGrid->addWidget(card, row, col);
        
        col++;
        if (col >= maxCols) {
            col = 0;
            row++;
        }
    }
}

void TemplateGalleryWidget::onCategoryChanged(int index) {
    QString category = m_categoryTabs->tabText(index);
    if (category == "All") category = "";
    filterByCategory(category);
}

void TemplateGalleryWidget::onSearchChanged(const QString& text) {
    filterBySearch(text);
}

void TemplateGalleryWidget::filterByCategory(const QString& category) {
    m_filteredTemplates.clear();
    
    for (const Template& tpl : m_templatesById.values()) {
        if (category.isEmpty() || tpl.category == category) {
            m_filteredTemplates.append(tpl);
        }
    }
    
    QString searchText = m_searchEdit->text();
    if (!searchText.isEmpty()) {
        filterBySearch(searchText);
    } else {
        populateGallery();
    }
}

void TemplateGalleryWidget::filterBySearch(const QString& query) {
    QString searchQuery = query.toLower();
    
    m_filteredTemplates.clear();
    
    QString currentCategory = m_categoryTabs->tabText(m_categoryTabs->currentIndex());
    if (currentCategory == "All") currentCategory = "";
    
    for (const Template& tpl : m_templatesById.values()) {
        bool matchesCategory = currentCategory.isEmpty() || tpl.category == currentCategory;
        bool matchesSearch = searchQuery.isEmpty() ||
                             tpl.name.toLower().contains(searchQuery) ||
                             tpl.description.toLower().contains(searchQuery) ||
                             tpl.tags.contains(searchQuery);
        
        if (matchesCategory && matchesSearch) {
            m_filteredTemplates.append(tpl);
        }
    }
    
    populateGallery();
}

void TemplateGalleryWidget::onTryItClicked(const QString& templateId) {
    Template tpl = m_templatesById.value(templateId);
    if (!tpl.schematicPath.isEmpty()) {
        emit openSchematicRequested(tpl.schematicPath);
    }
}

void TemplateGalleryWidget::setProjectDirectory(const QString& dir) {
    m_projectDir = dir;
}

void TemplateGalleryWidget::renderThumbnailIfNeeded(const Template& tpl) {
    if (QFile::exists(tpl.thumbnailPath)) return;
    // Thumbnail rendering would go here if needed
}