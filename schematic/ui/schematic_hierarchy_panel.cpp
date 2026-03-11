#include "schematic_hierarchy_panel.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QHeaderView>

SchematicHierarchyPanel::SchematicHierarchyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_treeView = new QTreeView();
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({"Design Hierarchy"});
    
    m_treeView->setModel(m_model);
    m_treeView->setHeaderHidden(true);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    connect(m_treeView, &QTreeView::doubleClicked, this, &SchematicHierarchyPanel::onItemDoubleClicked);
    
    layout->addWidget(m_treeView);
}

void SchematicHierarchyPanel::setProjectContext(const QString& rootFile) {
    m_rootFile = rootFile;
    refresh();
}

void SchematicHierarchyPanel::refresh() {
    m_model->clear();
    m_model->setHorizontalHeaderLabels({"Design Hierarchy"});
    
    if (m_rootFile.isEmpty()) return;

    QSet<QString> visited;
    QStandardItem* rootItem = new QStandardItem(QFileInfo(m_rootFile).baseName());
    rootItem->setData(m_rootFile, Qt::UserRole);
    rootItem->setIcon(QIcon::fromTheme("document-properties"));
    m_model->appendRow(rootItem);

    scanHierarchy(m_rootFile, rootItem, visited);
    m_treeView->expandAll();
}

void SchematicHierarchyPanel::scanHierarchy(const QString& filePath, QStandardItem* parentItem, QSet<QString>& visited) {
    if (visited.contains(filePath)) return;
    visited.insert(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    QJsonArray items = root["items"].toArray();

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject obj = items[i].toObject();
        if (obj["type"].toString() == "Sheet") {
            QString sheetName = obj["sheetName"].toString();
            QString childFile = obj["fileName"].toString();
            
            if (QFileInfo(childFile).isRelative()) {
                childFile = QFileInfo(filePath).absolutePath() + "/" + childFile;
            }

            QStandardItem* sheetItem = new QStandardItem(sheetName);
            sheetItem->setData(childFile, Qt::UserRole);
            sheetItem->setIcon(QIcon::fromTheme("folder-documents"));
            parentItem->appendRow(sheetItem);

            scanHierarchy(childFile, sheetItem, visited);
        }
    }
}

void SchematicHierarchyPanel::onItemDoubleClicked(const QModelIndex& index) {
    QString filePath = index.data(Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit sheetSelected(filePath);
    }
}
