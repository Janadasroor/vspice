#include "project_explorer_widget.h"
#include "../core/theme_manager.h"
#include <QHeaderView>
#include <QSortFilterProxyModel>

class FileFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit FileFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        if (sourceModel()->hasChildren(index)) return true; // Always show directories
        
        QString fileName = sourceModel()->data(index).toString().toLower();
        // Only show relevant engineering files
        return fileName.endsWith(".sch") || fileName.endsWith(".sym") || 
               fileName.endsWith(".lib") || fileName.endsWith(".sclib") ||
               fileName.contains(filterRegularExpression());
    }
};

ProjectExplorerWidget::ProjectExplorerWidget(QWidget *parent)
    : QWidget(parent)
{
    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    
    m_proxyModel = new FileFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    setupUi();
    applyTheme();
}

void ProjectExplorerWidget::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search bar
    m_searchBox = new QLineEdit(this);
    m_searchBox->setPlaceholderText("Filter files...");
    m_searchBox->setFixedHeight(36);
    m_searchBox->setClearButtonEnabled(true);
    layout->addWidget(m_searchBox);

    // Tree View
    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_proxyModel);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(12);
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);
    
    // Hide size, type, date columns
    for (int i = 1; i < 4; ++i) m_treeView->hideColumn(i);
    
    layout->addWidget(m_treeView, 1);

    connect(m_treeView, &QTreeView::doubleClicked, this, &ProjectExplorerWidget::onDoubleClicked);
    connect(m_searchBox, &QLineEdit::textChanged, this, &ProjectExplorerWidget::onFilterChanged);
}

void ProjectExplorerWidget::setRootPath(const QString& path) {
    m_rootPath = path;
    m_model->setRootPath(path);
    m_treeView->setRootIndex(m_proxyModel->mapFromSource(m_model->index(path)));
}

void ProjectExplorerWidget::onDoubleClicked(const QModelIndex& index) {
    QModelIndex sourceIndex = m_proxyModel->mapToSource(index);
    QString path = m_model->filePath(sourceIndex);
    if (QFileInfo(path).isFile()) {
        emit fileDoubleClicked(path);
    }
}

void ProjectExplorerWidget::onFilterChanged(const QString& text) {
    if (text.isEmpty()) {
        m_proxyModel->setFilterRegularExpression(QRegularExpression());
    } else {
        m_proxyModel->setFilterRegularExpression(QRegularExpression(text, QRegularExpression::CaseInsensitiveOption));
    }
}

void ProjectExplorerWidget::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return;

    QString bg = theme->panelBackground().name();
    QString fg = theme->textColor().name();
    QString border = theme->panelBorder().name();
    QString accent = theme->accentColor().name();
    QString inputBg = (theme->type() == PCBTheme::Light) ? "#f1f5f9" : "#1a1a1a";
    QString hoverBg = (theme->type() == PCBTheme::Light) ? "#eff6ff" : "#2d2d30";

    setStyleSheet(QString(
        "QWidget { background-color: %1; color: %2; }"
        "QLineEdit { background-color: %5; border: none; border-bottom: 1px solid %3; padding: 8px 12px; border-radius: 0; }"
        "QLineEdit:focus { background-color: %1; border-bottom: 2px solid %4; }"
        "QTreeView { background-color: %1; border: none; outline: none; }"
        "QTreeView::item { padding: 6px; border-radius: 4px; margin: 1px 4px; }"
        "QTreeView::item:hover { background-color: %6; }"
        "QTreeView::item:selected { background-color: %4; color: white; }"
    ).arg(bg, fg, border, accent, inputBg, hoverBg));
}
