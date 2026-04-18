#include "model_browser_widget.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QHeaderView>
#include <QFileInfo>
#include <QGroupBox>
#include <QApplication>

ModelBrowserWidget::ModelBrowserWidget(QWidget* parent)
    : QWidget(parent) {
    m_model = new SpiceModelListModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setFilterKeyColumn(-1);
    
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
    m_treeView = new QTreeView();
    m_treeView->setModel(m_proxyModel);
    m_treeView->setUniformRowHeights(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setIndentation(0);
    m_treeView->header()->setStretchLastSection(true);
    layout->addWidget(m_treeView);

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
    connect(m_treeView->selectionModel(), &QItemSelectionModel::currentChanged, this, &ModelBrowserWidget::onItemSelectionChanged);
    connect(m_applyBtn, &QPushButton::clicked, this, &ModelBrowserWidget::onApplyClicked);
    connect(reloadBtn, &QPushButton::clicked, this, &ModelBrowserWidget::onReloadClicked);
}


void ModelBrowserWidget::onSearchChanged(const QString& text) {
    m_proxyModel->setFilterFixedString(text);
}

void ModelBrowserWidget::onItemSelectionChanged(const QModelIndex& current) {
    if (!current.isValid()) {
        m_detailLabel->setText("Select a model to see details.");
        m_applyBtn->setEnabled(false);
        return;
    }
    
    QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
    const auto& found = m_model->modelInfo(sourceIndex.row());
    
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
    Q_EMIT modelSelected(found);
}

void ModelBrowserWidget::onApplyClicked() {
    QModelIndex current = m_treeView->currentIndex();
    if (!current.isValid()) return;
    
    QModelIndex sourceIndex = m_proxyModel->mapToSource(current);
    const auto& info = m_model->modelInfo(sourceIndex.row());
    Q_EMIT applyModelRequested(info);
}

void ModelBrowserWidget::onReloadClicked() {
    ModelLibraryManager::instance().reload();
}

void ModelBrowserWidget::onLibraryReloaded() {
    m_model->setModels(ModelLibraryManager::instance().allModels());
}
