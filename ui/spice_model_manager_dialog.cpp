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
#include <QTextBrowser>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>

// --- Custom Delegate for Search Highlighting ---
class SearchHighlightDelegate : public QStyledItemDelegate {
public:
    explicit SearchHighlightDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}

    void setSearchString(const QString& text) {
        m_searchString = text;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();
        
        // Draw standard background and focus
        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        opt.text = ""; // clear text for base draw, we draw it manually
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        // Draw custom text with highlight
        QString text = index.data(Qt::DisplayRole).toString();
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        
        // If selection is active, use white text, otherwise standard
        QColor textColor = (opt.state & QStyle::State_Selected) ? Qt::white : QColor("#d4d4d8");

        if (m_searchString.isEmpty()) {
            painter->setPen(textColor);
            painter->drawText(textRect, opt.displayAlignment, text);
        } else {
            // Find occurrences of search string
            int pos = 0;
            int lastPos = 0;
            int xOffset = textRect.left();
            QFontMetrics fm(opt.font);

            while ((pos = text.indexOf(m_searchString, lastPos, Qt::CaseInsensitive)) != -1) {
                // Draw text before match
                QString before = text.mid(lastPos, pos - lastPos);
                if (!before.isEmpty()) {
                    painter->setPen(textColor);
                    painter->drawText(xOffset, textRect.top(), fm.horizontalAdvance(before), textRect.height(), opt.displayAlignment, before);
                    xOffset += fm.horizontalAdvance(before);
                }

                // Draw highlighted match
                QString match = text.mid(pos, m_searchString.length());
                QRect highlightRect(xOffset, textRect.top() + (textRect.height() - fm.height()) / 2, fm.horizontalAdvance(match), fm.height());
                
                painter->fillRect(highlightRect, QColor(202, 138, 4)); // Yellow/Orange accent
                painter->setPen(Qt::white);
                painter->drawText(highlightRect, opt.displayAlignment, match);
                xOffset += fm.horizontalAdvance(match);

                lastPos = pos + m_searchString.length();
            }

            // Draw remaining text
            QString after = text.mid(lastPos);
            if (!after.isEmpty()) {
                painter->setPen(textColor);
                painter->drawText(QRect(xOffset, textRect.top(), fm.horizontalAdvance(after) + 10, textRect.height()), opt.displayAlignment, after);
            }
        }
        
        painter->restore();
    }

private:
    QString m_searchString;
};

// ---------------------------------------------

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
    addPathBtn->setIcon(QIcon(":/icons/toolbar_new.png")); // Apply + icon if available
    addPathBtn->setFixedHeight(32);
    addPathBtn->setStyleSheet("QPushButton { background-color: #007acc; border: none; border-radius: 6px; color: white; padding: 0 15px; font-weight: bold; } "
                             "QPushButton:hover { background-color: #008be5; }");
    connect(addPathBtn, &QPushButton::clicked, this, &SpiceModelManagerDialog::onAddLibraryPath);
    toolbarLayout->addWidget(addPathBtn);

    mainLayout->addWidget(toolbar);

    // Modern Scrollbar Stylesheet
    QString scrollbarStyle = R"(
        QScrollBar:vertical {
            border: none;
            background: #0f1012;
            width: 10px;
            margin: 0px 0px 0px 0px;
        }
        QScrollBar::handle:vertical {
            background: #3f3f46;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #52525b;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )";
    this->setStyleSheet(scrollbarStyle);

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
                              "QTreeWidget::item:selected { background-color: #1e3a8a; }");
    connect(m_modelTree, &QTreeWidget::itemClicked, this, &SpiceModelManagerDialog::onModelSelected);
    
    // Assign custom highlighter delegate
    m_modelTree->setItemDelegate(new SearchHighlightDelegate(m_modelTree));
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

    m_modelDetails = new QTextBrowser;
    m_modelDetails->setOpenExternalLinks(true);
    m_modelDetails->setStyleSheet("QTextBrowser { background-color: #0a0a0c; border: 1px solid #2d2d30; border-radius: 8px; padding: 15px; font-family: 'Inter', 'Segoe UI', sans-serif; }");
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
        
        // Add supportive icons based on type
        if (info.type == "Subcircuit") {
            item->setIcon(0, QIcon(":/icons/comp_ic.svg"));
        } else if (info.type == "NMOS" || info.type == "PMOS" || info.type == "NPN" || info.type == "PNP") {
            item->setIcon(0, QIcon(":/icons/comp_transistor.svg")); // fallback if missing is automatic
        } else {
            item->setIcon(0, QIcon(":/icons/comp_diode.svg"));
        }
        item->setIcon(2, QIcon(":/icons/toolbar_netlist.png"));
    }
}

void SpiceModelManagerDialog::onSearchChanged(const QString& query) {
    if (auto* delegate = dynamic_cast<SearchHighlightDelegate*>(m_modelTree->itemDelegate())) {
        delegate->setSearchString(query);
    }
    updateModelList(ModelLibraryManager::instance().search(query));
    // Trigger repaint of tree view
    m_modelTree->viewport()->update();
}

void SpiceModelManagerDialog::onModelSelected(QTreeWidgetItem* item) {
    if (!item) return;
    QString name = item->data(0, Qt::UserRole).toString();
    
    QVector<SpiceModelInfo> models = ModelLibraryManager::instance().allModels();
    for (const auto& info : models) {
        if (info.name == name) {
            m_modelTitle->setText(info.name);
            m_modelMeta->setText(""); // Replaced by HTML inside m_modelDetails
            
            QString html = "<html><head><style>"
                           "body { color: #d4d4d8; font-family: 'Inter', sans-serif; font-size: 13px; }"
                           ".badge { background-color: #1e40af; color: #eff6ff; padding: 3px 8px; border-radius: 4px; font-weight: 600; margin-right: 10px; }"
                           ".source { color: #60a5fa; text-decoration: none; font-weight: 500; font-family: 'JetBrains Mono', monospace; }"
                           ".params-grid { margin-top: 20px; display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; font-family: 'JetBrains Mono', monospace; }"
                           ".param-item { background: #18181b; padding: 6px 12px; border-radius: 4px; border: 1px solid #27272a; color: #a1a1aa; }"
                           "</style></head><body>";
            
            html += QString("<div><span class='badge'>%1</span> <span style='color: #71717a;'>Source:</span> <span class='source'>%2</span></div>")
                        .arg(info.type, QFileInfo(info.libraryPath).fileName());
            
            html += "<div style='margin-top: 20px; font-size: 11px; font-weight: 800; color: #71717a; letter-spacing: 1.5px;'>PARAMETERS</div>";
            
            if (info.type == "Subcircuit") {
                html += QString("<div class='param-item' style='margin-top: 10px; color: #fff;'>%1</div>").arg(info.description);
            } else {
                html += "<div class='params-grid' style='margin-top: 10px;'>";
                for (int i = 0; i < info.params.size(); ++i) {
                    html += QString("<div style='background: #18181b; padding: 6px 12px; border-radius: 4px; border: 1px solid #27272a; color: #a1a1aa; font-family: monospace; display: inline-block; margin: 4px;'>%1</div>").arg(info.params[i]);
                }
                html += "</div>";
            }
            
            html += "</body></html>";
            m_modelDetails->setHtml(html);
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
