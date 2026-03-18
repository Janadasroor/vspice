#include "spice_model_manager_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include "../core/theme_manager.h"
#include "../core/config_manager.h"

SpiceModelManagerDialog::SpiceModelManagerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("SPICE Model Manager");
    setMinimumSize(850, 600);
    setupUI();
    
    connect(&ModelLibraryManager::instance(), &ModelLibraryManager::libraryReloaded, 
            this, [this]() { updateModelList(ModelLibraryManager::instance().allModels()); });
    
    updateModelList(ModelLibraryManager::instance().allModels());
}

void SpiceModelManagerDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header / Toolbar
    auto* toolbar = new QWidget;
    toolbar->setFixedHeight(50);
    toolbar->setStyleSheet("background-color: #1a1c22; border-bottom: 1px solid #2d2d30;");
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(15, 0, 15, 0);

    m_searchField = new QLineEdit;
    m_searchField->setPlaceholderText("Search models by name, type, or manufacturer...");
    m_searchField->setFixedHeight(32);
    m_searchField->setStyleSheet("background-color: #0a0a0c; border: 1px solid #3f3f46; border-radius: 6px; padding: 0 10px; color: white;");
    connect(m_searchField, &QLineEdit::textChanged, this, &SpiceModelManagerDialog::onSearchChanged);
    toolbarLayout->addWidget(m_searchField, 1);

    m_reloadBtn = new QPushButton("Reload Libraries");
    m_reloadBtn->setFixedHeight(32);
    m_reloadBtn->setStyleSheet("QPushButton { background-color: #27272a; border: 1px solid #3f3f46; border-radius: 6px; color: #d4d4d8; padding: 0 15px; } "
                              "QPushButton:hover { background-color: #3f3f46; }");
    connect(m_reloadBtn, &QPushButton::clicked, this, &SpiceModelManagerDialog::onReloadLibraries);
    toolbarLayout->addWidget(m_reloadBtn);

    auto* addPathBtn = new QPushButton("Add Path...");
    addPathBtn->setFixedHeight(32);
    addPathBtn->setStyleSheet("QPushButton { background-color: #007acc; border: none; border-radius: 6px; color: white; padding: 0 15px; font-weight: bold; } "
                             "QPushButton:hover { background-color: #008be5; }");
    connect(addPathBtn, &QPushButton::clicked, this, &SpiceModelManagerDialog::onAddLibraryPath);
    toolbarLayout->addWidget(addPathBtn);

    mainLayout->addWidget(toolbar);

    // Main Splitter
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setStyleSheet("QSplitter::handle { background-color: #2d2d30; width: 1px; }");

    // Left Panel: Model List
    m_modelTree = new QTreeWidget;
    m_modelTree->setColumnCount(3);
    m_modelTree->setHeaderLabels({"Model Name", "Type", "Library File"});
    m_modelTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_modelTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_modelTree->header()->setStretchLastSection(true);
    m_modelTree->setIndentation(0);
    m_modelTree->setStyleSheet("QTreeWidget { background-color: #0a0a0c; border: none; color: #d4d4d8; outline: 0; }"
                              "QTreeWidget::item { padding: 8px; border-bottom: 1px solid #1f1f23; }"
                              "QTreeWidget::item:selected { background-color: #1e3a8a; color: white; }");
    connect(m_modelTree, &QTreeWidget::itemClicked, this, &SpiceModelManagerDialog::onModelSelected);
    splitter->addWidget(m_modelTree);

    // Right Panel: Details
    auto* detailsPanel = new QWidget;
    detailsPanel->setStyleSheet("background-color: #0f1012;");
    auto* detailsLayout = new QVBoxLayout(detailsPanel);
    detailsLayout->setContentsMargins(25, 25, 25, 25);
    detailsLayout->setSpacing(15);

    m_modelTitle = new QLabel("Select a model to view details");
    m_modelTitle->setStyleSheet("font-size: 24px; font-weight: 800; color: white;");
    detailsLayout->addWidget(m_modelTitle);

    m_modelMeta = new QLabel("");
    m_modelMeta->setStyleSheet("color: #007acc; font-weight: bold; font-family: monospace;");
    detailsLayout->addWidget(m_modelMeta);

    auto* detailHeader = new QLabel("PARAMETERS AND MAPPING");
    detailHeader->setStyleSheet("font-size: 10px; font-weight: 800; color: #71717a; letter-spacing: 1.5px;");
    detailsLayout->addWidget(detailHeader);

    m_modelDetails = new QTextEdit;
    m_modelDetails->setReadOnly(true);
    m_modelDetails->setStyleSheet("QTextEdit { background-color: #0a0a0c; border: 1px solid #2d2d30; border-radius: 8px; color: #a1a1aa; padding: 15px; font-family: 'JetBrains Mono', 'Cascadia Code', monospace; font-size: 13px; line-height: 1.6; }");
    detailsLayout->addWidget(m_modelDetails);

    splitter->addWidget(detailsPanel);
    splitter->setSizes({400, 450});

    mainLayout->addWidget(splitter);
}

void SpiceModelManagerDialog::updateModelList(const QVector<SpiceModelInfo>& models) {
    m_modelTree->clear();
    for (const auto& info : models) {
        auto* item = new QTreeWidgetItem(m_modelTree);
        item->setText(0, info.name);
        item->setText(1, info.type);
        item->setText(2, QFileInfo(info.libraryPath).fileName());
        item->setData(0, Qt::UserRole, info.name);
    }
}

void SpiceModelManagerDialog::onSearchChanged(const QString& query) {
    updateModelList(ModelLibraryManager::instance().search(query));
}

void SpiceModelManagerDialog::onModelSelected(QTreeWidgetItem* item) {
    if (!item) return;
    QString name = item->data(0, Qt::UserRole).toString();
    
    QVector<SpiceModelInfo> models = ModelLibraryManager::instance().allModels();
    for (const auto& info : models) {
        if (info.name == name) {
            m_modelTitle->setText(info.name);
            m_modelMeta->setText(QString("[%1] - Source: %2").arg(info.type, QFileInfo(info.libraryPath).fileName()));
            
            QString details;
            if (info.type == "Subcircuit") {
                details += "TYPE: SPICE Subcircuit (.SUBCKT)\n";
                details += QString("PINS: %1\n\n").arg(info.description);
            } else {
                details += QString("TYPE: SPICE Model (.MODEL %1)\n").arg(info.type);
                details += "PARAMETERS:\n";
                for (const auto& p : info.params) details += "  " + p + "\n";
            }
            m_modelDetails->setPlainText(details);
            break;
        }
    }
}

void SpiceModelManagerDialog::onReloadLibraries() {
    ModelLibraryManager::instance().reload();
    QMessageBox::information(this, "Success", "Libraries reloaded successfully.");
}

void SpiceModelManagerDialog::onAddLibraryPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "Add Library Path", QDir::homePath());
    if (!dir.isEmpty()) {
        QStringList paths = ConfigManager::instance().modelPaths();
        if (!paths.contains(dir)) {
            paths.append(dir);
            ConfigManager::instance().setModelPaths(paths);
            onReloadLibraries();
        }
    }
}
