#include "pcb_components_widget.h"
#include "footprint_preview_view.h"
#include "../dialogs/footprint_browser_dialog.h"
#include "theme_manager.h"
#include "../../footprints/footprint_library.h"
#include "../../footprints/footprint_editor.h"
#include <QPushButton>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QDebug>

PCBComponentsWidget::PCBComponentsWidget(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Search bar ──────────────────────────────────────────────────────
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("🔍  Search footprints...");
    m_searchBox->setClearButtonEnabled(true);
    m_searchBox->setStyleSheet(
        "QLineEdit {"
        "   background-color: #1a1a1a;"
        "   border: 1px solid #333333;"
        "   border-radius: 0px;"
        "   padding: 7px 10px;"
        "   color: #e0e0e0;"
        "   font-size: 12px;"
        "   margin: 0px;"
        "}"
        "QLineEdit:focus {"
        "   border-color: #007acc;"
        "   background-color: #202020;"
        "}"
    );
    
    connect(m_searchBox, &QLineEdit::textChanged, this, &PCBComponentsWidget::onSearchTextChanged);
    layout->addWidget(m_searchBox);

    // ── Action Cards ──────────────────────────────────────────────────
    QWidget* actionContainer = new QWidget(this);
    QVBoxLayout* actionLayout = new QVBoxLayout(actionContainer);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(1);

    auto createActionCard = [this, actionLayout](const QString& title, const QString& subTitle, const char* slot) {
        QPushButton* btn = new QPushButton(this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(65);
        
        QVBoxLayout* btnLayout = new QVBoxLayout(btn);
        btnLayout->setContentsMargins(15, 0, 15, 0);
        btnLayout->setSpacing(2);
        
        QLabel* titleLabel = new QLabel(title);
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleLabel->setStyleSheet("color: #ffffff; font-weight: 600; font-size: 13px; border: none; background: transparent;");
        
        QLabel* descLabel = new QLabel(subTitle);
        descLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        descLabel->setStyleSheet("color: #888888; font-size: 11px; border: none; background: transparent;");
        
        btnLayout->addWidget(titleLabel);
        btnLayout->addWidget(descLabel);
        
        btn->setStyleSheet(
            "QPushButton {"
            "   background-color: #222222;"
            "   border: none;"
            "   border-bottom: 1px solid #2d2d2d;"
            "   text-align: left;"
            "}"
            "QPushButton:hover {"
            "   background-color: #2d2d2d;"
            "}"
        );
        
        connect(btn, SIGNAL(clicked()), this, slot);
        actionLayout->addWidget(btn);
        return btn;
    };

    createActionCard("New Footprint", "Open wizard to generate footprints", SLOT(onCreateFootprint()));
    createActionCard("Browse Libraries", "Search global footprint database", SLOT(onOpenLibraryBrowser()));

    layout->addWidget(actionContainer);

    // ── Section Header ──────────────────────────────────────────────────
    QLabel* listHeader = new QLabel("   FOOTPRINT LIBRARIES", this);
    listHeader->setFixedHeight(28);
    listHeader->setStyleSheet(
        "background-color: #1a1a1a;"
        "color: #71717a;"
        "font-size: 10px;"
        "font-weight: 700;"
        "border-bottom: 1px solid #2d2d2d;"
    );
    layout->addWidget(listHeader);

    // ── Footprint Tree ──────────────────────────────────────────────────
    m_componentList = new QTreeWidget(this);
    m_componentList->setFrameShape(QFrame::NoFrame);
    m_componentList->setHeaderHidden(true);
    m_componentList->setIndentation(16);
    m_componentList->setStyleSheet(
        "QTreeWidget {"
        "   background-color: #1a1a1a;"
        "   border: none;"
        "   color: #dcdcdc;"
        "   font-size: 12px;"
        "}"
        "QTreeWidget::item:hover { background-color: #2a2a2a; }"
        "QTreeWidget::item:selected { background-color: #094771; color: white; }"
    );

    connect(m_componentList, &QTreeWidget::itemClicked, this, &PCBComponentsWidget::onItemClicked);
    layout->addWidget(m_componentList, 1);

    // ── Preview Panel ──────────────────────────────────────────────────
    QWidget* previewPanel = new QWidget(this);
    previewPanel->setFixedHeight(150);
    previewPanel->setStyleSheet("background-color: #1a1a1a; border-top: 1px solid #2d2d2d;");
    
    QVBoxLayout* previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(10, 10, 10, 10);
    
    m_previewView = new FootprintPreviewView(this);
    m_previewView->setStyleSheet("background-color: #0c0c0c; border: 1px solid #333;");
    
    previewLayout->addWidget(m_previewView);
    layout->addWidget(previewPanel);

    populate();
}

PCBComponentsWidget::~PCBComponentsWidget() {}

void PCBComponentsWidget::populate() {
    m_componentList->clear();
    
    QIcon libIcon(":/icons/folder_open.svg");
    QIcon catIcon(":/icons/folder_closed.svg");
    QIcon fpIcon(":/icons/component_file.svg");

    auto libraries = FootprintLibraryManager::instance().libraries();
    
    // Pass 1: User / Project Libraries
    for (auto* lib : libraries) {
        if (lib->isBuiltIn()) continue;
        
        QTreeWidgetItem* libItem = new QTreeWidgetItem(m_componentList);
        libItem->setText(0, lib->name().toUpper());
        libItem->setIcon(0, libIcon);
        libItem->setData(0, Qt::UserRole + 2, "Library");
        libItem->setForeground(0, QBrush(QColor("#fbbf24"))); // Amber
        
        QFont libFont = libItem->font(0);
        libFont.setBold(true);
        libFont.setPointSize(10);
        libItem->setFont(0, libFont);
        
        QMap<QString, QTreeWidgetItem*> categories;
        for (const auto& fpName : lib->getFootprintNames()) {
            FootprintDefinition def = lib->getFootprint(fpName);
            QString catName = def.category();
            if (catName.isEmpty()) catName = "Uncategorized";
            
            QTreeWidgetItem* catItem = nullptr;
            if (!categories.contains(catName)) {
                catItem = new QTreeWidgetItem(libItem);
                catItem->setText(0, catName);
                catItem->setIcon(0, catIcon);
                catItem->setData(0, Qt::UserRole + 2, "Category");
                categories[catName] = catItem;
            } else {
                catItem = categories[catName];
            }
            
            QTreeWidgetItem* item = new QTreeWidgetItem(catItem);
            item->setText(0, fpName);
            item->setData(0, Qt::UserRole, fpName);
            item->setData(0, Qt::UserRole + 1, lib->name());
            item->setData(0, Qt::UserRole + 2, "Footprint");
            item->setIcon(0, fpIcon);
        }
        libItem->setExpanded(true);
    }

    // Pass 2: Built-in Libraries
    for (auto* lib : libraries) {
        if (!lib->isBuiltIn()) continue;

        QTreeWidgetItem* libItem = new QTreeWidgetItem(m_componentList);
        libItem->setText(0, lib->name().toUpper() + " [BUILT-IN]");
        libItem->setIcon(0, libIcon);
        libItem->setData(0, Qt::UserRole + 2, "Library");
        libItem->setForeground(0, QBrush(QColor("#94a3b8"))); // Grey
        
        QFont libFont = libItem->font(0);
        libFont.setBold(true);
        libFont.setPointSize(10);
        libItem->setFont(0, libFont);
        
        QMap<QString, QTreeWidgetItem*> categories;
        for (const auto& fpName : lib->getFootprintNames()) {
            FootprintDefinition def = lib->getFootprint(fpName);
            QString catName = def.category();
            if (catName.isEmpty()) catName = "Uncategorized";
            
            QTreeWidgetItem* catItem = nullptr;
            if (!categories.contains(catName)) {
                catItem = new QTreeWidgetItem(libItem);
                catItem->setText(0, catName);
                catItem->setIcon(0, catIcon);
                catItem->setData(0, Qt::UserRole + 2, "Category");
                categories[catName] = catItem;
            } else {
                catItem = categories[catName];
            }
            
            QTreeWidgetItem* item = new QTreeWidgetItem(catItem);
            item->setText(0, fpName);
            item->setData(0, Qt::UserRole, fpName);
            item->setData(0, Qt::UserRole + 1, lib->name());
            item->setData(0, Qt::UserRole + 2, "Footprint");
            item->setIcon(0, fpIcon);
        }
        libItem->setExpanded(true);
    }
}

void PCBComponentsWidget::onSearchTextChanged(const QString &text) {
    if (text.isEmpty()) {
        for (int i = 0; i < m_componentList->topLevelItemCount(); ++i) {
            QTreeWidgetItem* lib = m_componentList->topLevelItem(i);
            lib->setHidden(false);
            for (int j = 0; j < lib->childCount(); ++j) {
                QTreeWidgetItem* cat = lib->child(j);
                cat->setHidden(false);
                for (int k = 0; k < cat->childCount(); ++k) cat->child(k)->setHidden(false);
            }
        }
        return;
    }

    for (int i = 0; i < m_componentList->topLevelItemCount(); ++i) {
        QTreeWidgetItem* lib = m_componentList->topLevelItem(i);
        bool libMatches = false;
        for (int j = 0; j < lib->childCount(); ++j) {
            QTreeWidgetItem* cat = lib->child(j);
            bool catMatches = false;
            for (int k = 0; k < cat->childCount(); ++k) {
                QTreeWidgetItem* item = cat->child(k);
                bool matches = item->text(0).contains(text, Qt::CaseInsensitive);
                item->setHidden(!matches);
                if (matches) catMatches = true;
            }
            cat->setHidden(!catMatches);
            if (catMatches) {
                cat->setExpanded(true);
                libMatches = true;
            }
        }
        lib->setHidden(!libMatches);
        if (libMatches) lib->setExpanded(true);
    }
}

void PCBComponentsWidget::onItemClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    if (!item) return;

    if (item->data(0, Qt::UserRole + 2).toString() == "Footprint") {
        QString fpName = item->data(0, Qt::UserRole).toString();
        QString libName = item->data(0, Qt::UserRole + 1).toString();
        
        if (!fpName.isEmpty()) {
            updatePreview(fpName, libName);
            emit footprintSelected(fpName);
        }
    }
}

void PCBComponentsWidget::updatePreview(const QString& fpName, const QString& libName) {
    FootprintDefinition def;
    if (!libName.isEmpty()) {
        auto* lib = FootprintLibraryManager::instance().findLibrary(libName);
        if (lib) def = lib->getFootprint(fpName);
    }
    
    if (!def.isValid()) {
        def = FootprintLibraryManager::instance().findFootprint(fpName);
    }
    
    if (def.isValid()) {
        m_previewView->setFootprint(def);
    } else {
        m_previewView->clear();
    }
}

void PCBComponentsWidget::onCreateFootprint() {
     // Trigger footprint editor
     emit footprintCreated("");
}

void PCBComponentsWidget::onOpenLibraryBrowser() {
    FootprintBrowserDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        FootprintDefinition fp = dialog.selectedFootprint();
        if (!fp.name().isEmpty()) {
            emit footprintSelected(fp.name());
        }
    }
}
