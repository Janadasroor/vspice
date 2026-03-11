#include "model_browser_widget.h"
#include "../../core/theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QFileInfo>
#include <QGroupBox>
#include <QApplication>

ModelBrowserWidget::ModelBrowserWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    
    connect(ModelLibraryManager::instance().ptr(), &ModelLibraryManager::libraryReloaded, this, &ModelBrowserWidget::onLibraryReloaded);
    onLibraryReloaded();
}

// Helper to handle singleton signals if not using pointer (ModelLibraryManager is not a QObject in my header, wait)
// I should make ModelLibraryManager a QObject. It already is in my .h! Good.

ModelBrowserWidget::~ModelBrowserWidget() {}

void ModelBrowserWidget::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    // --- Search & Controls ---
    auto* topLayout = new QHBoxLayout();
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search SPICE models...");
    m_searchBox->setClearButtonEnabled(true);
    
    auto* reloadBtn = new QPushButton();
    reloadBtn->setIcon(QIcon(":/icons/toolbar_refresh.png"));
    reloadBtn->setToolTip("Reload Libraries");
    reloadBtn->setFixedWidth(30);

    topLayout->addWidget(m_searchBox);
    topLayout->addWidget(reloadBtn);
    layout->addLayout(topLayout);

    // --- Tree View ---
    m_tree = new QTreeWidget();
    m_tree->setHeaderLabels({"Model Name", "Type"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(true);
    layout->addWidget(m_tree);

    // --- Detail Panel ---
    auto* detailGrp = new QGroupBox("Selection Details");
    auto* detailLayout = new QVBoxLayout(detailGrp);
    
    m_detailLabel = new QLabel("Select a model to see details.");
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setStyleSheet("color: #888888; font-size: 11px;");
    detailLayout->addWidget(m_detailLabel);
    
    m_applyBtn = new QPushButton("Apply to Selected Component");
    m_applyBtn->setEnabled(false);
    m_applyBtn->setStyleSheet(
        "QPushButton { background-color: #3b82f6; color: white; border-radius: 4px; padding: 6px; font-weight: bold; }"
        "QPushButton:disabled { background-color: #2d2d32; color: #555555; }"
    );
    detailLayout->addWidget(m_applyBtn);
    
    layout->addWidget(detailGrp);

    // --- Connections ---
    connect(m_searchBox, &QLineEdit::textChanged, this, &ModelBrowserWidget::onSearchChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ModelBrowserWidget::onItemSelectionChanged);
    connect(m_applyBtn, &QPushButton::clicked, this, &ModelBrowserWidget::onApplyClicked);
    connect(reloadBtn, &QPushButton::clicked, this, &ModelBrowserWidget::onReloadClicked);
}

void ModelBrowserWidget::populateTree(const QVector<SpiceModelInfo>& models) {
    m_tree->clear();
    m_currentModels = models;
    
    QMap<QString, QTreeWidgetItem*> categories;
    
    for (const auto& info : models) {
        if (!categories.contains(info.type)) {
            auto* catItem = new QTreeWidgetItem(m_tree);
            catItem->setText(0, info.type);
            catItem->setFont(0, QFont("Inter", 10, QFont::Bold));
            catItem->setForeground(0, QColor("#3b82f6"));
            categories[info.type] = catItem;
            catItem->setExpanded(true);
        }
        
        auto* item = new QTreeWidgetItem(categories[info.type]);
        item->setText(0, info.name);
        item->setText(1, info.type);
        item->setData(0, Qt::UserRole, info.name);
    }
}

void ModelBrowserWidget::onSearchChanged(const QString& text) {
    populateTree(ModelLibraryManager::instance().search(text));
}

void ModelBrowserWidget::onItemSelectionChanged() {
    auto* item = m_tree->currentItem();
    if (!item || item->parent() == nullptr) {
        m_detailLabel->setText("Select a model to see details.");
        m_applyBtn->setEnabled(false);
        return;
    }
    
    QString name = item->data(0, Qt::UserRole).toString();
    SpiceModelInfo found;
    bool ok = false;
    for (const auto& info : m_currentModels) {
        if (info.name == name) {
            found = info;
            ok = true;
            break;
        }
    }
    
    if (ok) {
        QString details = QString("<b>Name:</b> %1<br>"
                                  "<b>Type:</b> %2<br>"
                                  "<b>Source:</b> %3<br>"
                                  "<b>Params:</b> %4")
            .arg(found.name)
            .arg(found.type)
            .arg(QFileInfo(found.libraryPath).fileName())
            .arg(found.params.join(", "));
        
        if (!found.description.isEmpty()) {
            details += "<br><b>Note:</b> " + found.description;
        }
        
        m_detailLabel->setText(details);
        m_applyBtn->setEnabled(true);
        emit modelSelected(found);
    }
}

void ModelBrowserWidget::onApplyClicked() {
    auto* item = m_tree->currentItem();
    if (!item) return;
    
    QString name = item->data(0, Qt::UserRole).toString();
    for (const auto& info : m_currentModels) {
        if (info.name == name) {
            emit applyModelRequested(info);
            break;
        }
    }
}

void ModelBrowserWidget::onReloadClicked() {
    ModelLibraryManager::instance().reload();
}

void ModelBrowserWidget::onLibraryReloaded() {
    populateTree(ModelLibraryManager::instance().allModels());
}
