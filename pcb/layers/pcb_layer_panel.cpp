#include "pcb_layer_panel.h"
#include "pcb_layer.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QColorDialog>
#include <QPainter>
#include <QSignalBlocker>

PCBLayerPanel::PCBLayerPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    refreshLayers();

    // Connect to layer manager signals
    connect(&PCBLayerManager::instance(), &PCBLayerManager::activeLayerChanged,
            this, [this](int layerId) {
                refreshLayers();
                emit activeLayerChanged(layerId);
            });

    connect(&PCBLayerManager::instance(), &PCBLayerManager::layerVisibilityChanged,
            this, [this](int layerId, bool visible) {
                emit layerVisibilityChanged(layerId, visible);
            });
}

void PCBLayerPanel::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search and Filter area
    QWidget* filterArea = new QWidget(this);
    filterArea->setStyleSheet("background-color: #1a1a1a; border-bottom: 1px solid #333333;");
    QVBoxLayout* filterLayout = new QVBoxLayout(filterArea);
    filterLayout->setContentsMargins(10, 10, 10, 10);
    filterLayout->setSpacing(8);

    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Filter layers...");
    m_searchEdit->setStyleSheet(R"(
        QLineEdit {
            background: #121212;
            color: #e4e4e7;
            border: 1px solid #333333;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 11px;
        }
        QLineEdit:focus { border-color: #6366f1; }
    )");
    filterLayout->addWidget(m_searchEdit);

    layout->addWidget(filterArea);

    // Active layer indicator (integrated into tree area or separate)
    m_activeLayerLabel = new QLabel("Active: Top Copper");
    m_activeLayerLabel->setObjectName("ActiveLayerLabel");
    m_activeLayerLabel->setStyleSheet(R"(
        QLabel#ActiveLayerLabel {
            background-color: #161618;
            border-bottom: 1px solid #27272a;
            padding: 8px 12px;
            font-weight: 600;
            font-size: 11px;
            color: #d4d4d8;
        }
    )");
    layout->addWidget(m_activeLayerLabel);

    // Layer tree
    m_layerTree = new QTreeWidget();
    m_layerTree->setHeaderLabels({"", "Layer", ""});
    m_layerTree->setColumnWidth(0, 34);  // Visibility checkbox
    m_layerTree->setColumnWidth(2, 34);  // Color swatch
    m_layerTree->header()->setStretchLastSection(false);
    m_layerTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_layerTree->setRootIsDecorated(false);
    m_layerTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layerTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_layerTree->setFrameShape(QFrame::NoFrame);
    m_layerTree->setStyleSheet(R"(
        QTreeWidget {
            background: #121212;
            border: none;
            outline: none;
            font-family: 'Inter', sans-serif;
        }
        QTreeWidget::item {
            padding: 6px 4px;
            border-bottom: 1px solid #1a1a1a;
            color: #e4e4e7;
            font-size: 11px;
        }
        QTreeWidget::item:selected {
            background: rgba(99, 102, 241, 0.15);
            color: #ffffff;
            border-left: 2px solid #6366f1;
        }
        QTreeWidget::item:hover:!selected {
            background: rgba(255, 255, 255, 0.02);
        }
        QHeaderView::section {
            background: #1a1a1a;
            color: #71717a;
            border: none;
            border-bottom: 1px solid #27272a;
            padding: 4px;
            font-size: 9px;
            font-weight: 700;
            text-transform: uppercase;
        }
    )");

    connect(m_layerTree, &QTreeWidget::itemClicked, this, &PCBLayerPanel::onLayerItemClicked);
    connect(m_layerTree, &QTreeWidget::itemDoubleClicked, this, &PCBLayerPanel::onLayerItemDoubleClicked);
    connect(m_layerTree, &QTreeWidget::customContextMenuRequested, this, &PCBLayerPanel::onLayerContextMenu);

    layout->addWidget(m_layerTree, 1);

    // Quick action buttons
    QWidget* footer = new QWidget(this);
    footer->setStyleSheet("background: #1a1a1a; border-top: 1px solid #333333;");
    QHBoxLayout* buttonLayout = new QHBoxLayout(footer);
    buttonLayout->setContentsMargins(8, 8, 8, 8);
    buttonLayout->setSpacing(6);

    m_showAllBtn = new QPushButton("Show All");
    m_showAllBtn->setStyleSheet(R"(
        QPushButton {
            background: #27272a;
            border: 1px solid #3f3f46;
            border-radius: 4px;
            padding: 4px 10px;
            color: #d4d4d8;
            font-size: 10px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #3f3f46;
            border-color: #52525b;
        }
    )");
    connect(m_showAllBtn, &QPushButton::clicked, this, &PCBLayerPanel::onShowAllLayers);

    m_hideAllBtn = new QPushButton("Hide All");
    m_hideAllBtn->setStyleSheet(m_showAllBtn->styleSheet());
    connect(m_hideAllBtn, &QPushButton::clicked, this, &PCBLayerPanel::onHideAllLayers);

    buttonLayout->addWidget(m_showAllBtn);
    buttonLayout->addWidget(m_hideAllBtn);
    buttonLayout->addStretch();

    layout->addWidget(footer);

    // Connect search
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
            bool match = item->text(1).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
        }
    });
}

void PCBLayerPanel::refreshLayers() {
    PCBLayerManager& mgr = PCBLayerManager::instance();
    int activeId = mgr.activeLayerId();
    PCBLayer* activeLayer = mgr.activeLayer();
    if (activeLayer && !activeLayer->isVisible()) {
        setExclusiveVisibleLayer(activeId);
        return;
    }

    int visibleCount = 0;
    for (const PCBLayer& layer : mgr.layers()) {
        if (layer.isVisible()) ++visibleCount;
    }
    if (visibleCount != 1 && activeLayer) {
        setExclusiveVisibleLayer(activeId);
        return;
    }

    m_layerTree->clear();

    for (const PCBLayer& layer : mgr.layers()) {
        createLayerItem(layer.id(), layer.name(), layer.color(), 
                        layer.isVisible(), layer.id() == activeId);
    }

    // Update active layer label
    if (activeLayer) {
        m_activeLayerLabel->setText(QString("⚡ Active: %1").arg(activeLayer->name()));
        
        // Update label color based on layer
        m_activeLayerLabel->setStyleSheet(QString(R"(
            QLabel#ActiveLayerLabel {
                background-color: rgba(%1, %2, %3, 40);
                border: 1px solid rgba(%1, %2, %3, 80);
                border-radius: 0px;
                padding: 8px 12px;
                font-weight: 600;
                font-size: 12px;
                color: #f0f0f0;
            }
        )").arg(activeLayer->color().red()).arg(activeLayer->color().green()).arg(activeLayer->color().blue()));
    }
}

void PCBLayerPanel::createLayerItem(int layerId, const QString& name, 
                                     const QColor& color, bool visible, bool active) {
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setData(0, Qt::UserRole, layerId);
    item->setText(1, name);
    
    // Visibility checkbox column
    item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
    
    // Active layer highlighting
    if (active) {
        item->setBackground(1, QBrush(QColor(99, 102, 241, 50)));
        QFont font = item->font(1);
        font.setBold(true);
        item->setFont(1, font);
    }

    // Color indicator
    item->setIcon(2, createColorSwatchIcon(color));
    item->setText(2, "");
    item->setTextAlignment(2, Qt::AlignCenter);

    m_layerTree->addTopLevelItem(item);
}

void PCBLayerPanel::selectLayer(int layerId) {
    PCBLayerManager::instance().setActiveLayer(layerId);
}

void PCBLayerPanel::onLayerItemClicked(QTreeWidgetItem* item, int column) {
    int layerId = item->data(0, Qt::UserRole).toInt();

    if (column == 0) {
        bool visible = item->checkState(0) == Qt::Checked;
        if (visible) {
            setExclusiveVisibleLayer(layerId);
        } else {
            // Keep the clicked layer visible so there is always a single active checkbox.
            QSignalBlocker blocker(m_layerTree);
            item->setCheckState(0, Qt::Checked);
            setExclusiveVisibleLayer(layerId);
        }
    } else {
        // Select as active layer
        PCBLayerManager::instance().setActiveLayer(layerId);
        refreshLayers();
    }
}

void PCBLayerPanel::onLayerItemDoubleClicked(QTreeWidgetItem* item, int column) {
    int layerId = item->data(0, Qt::UserRole).toInt();

    if (column == 2) {
        // Change color
        PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
        if (layer) {
            QColor newColor = QColorDialog::getColor(layer->color(), this, 
                QString("Select color for %1").arg(layer->name()));
            if (newColor.isValid()) {
                layer->setColor(newColor);
                refreshLayers();
            }
        }
    }
}

void PCBLayerPanel::onLayerContextMenu(const QPoint& pos) {
    QTreeWidgetItem* item = m_layerTree->itemAt(pos);
    if (!item) return;

    int layerId = item->data(0, Qt::UserRole).toInt();
    PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
    if (!layer) return;

    QMenu menu(this);
    menu.setStyleSheet(R"(
        QMenu {
            background: #252526;
            border: 1px solid #3e3e42;
            border-radius: 0px;
            padding: 4px;
        }
        QMenu::item {
            padding: 8px 20px;
            color: #e4e4e7;
        }
        QMenu::item:selected {
            background: rgba(99, 102, 241, 0.3);
            border-radius: 4px;
        }
    )");

    QAction* setActiveAction = menu.addAction("Set as Active Layer");
    connect(setActiveAction, &QAction::triggered, this, [this, layerId]() {
        selectLayer(layerId);
    });

    menu.addSeparator();

    QAction* toggleVisibleAction = menu.addAction(layer->isVisible() ? "Hide Layer" : "Show Layer");
    connect(toggleVisibleAction, &QAction::triggered, this, [this, layer, layerId]() {
        if (layer->isVisible()) {
            setExclusiveVisibleLayer(layerId);
        } else {
            setExclusiveVisibleLayer(layerId);
        }
    });

    QAction* lockAction = menu.addAction(layer->isLocked() ? "Unlock Layer" : "Lock Layer");
    connect(lockAction, &QAction::triggered, this, [layer, layerId]() {
        PCBLayerManager::instance().setLayerLocked(layerId, !layer->isLocked());
    });

    menu.addSeparator();

    QAction* changeColorAction = menu.addAction("Change Color...");
    connect(changeColorAction, &QAction::triggered, this, [this, layer, layerId]() {
        QColor newColor = QColorDialog::getColor(layer->color(), this);
        if (newColor.isValid()) {
            layer->setColor(newColor);
            refreshLayers();
        }
    });

    menu.exec(m_layerTree->mapToGlobal(pos));
}

void PCBLayerPanel::onShowAllLayers() {
    PCBLayerManager& mgr = PCBLayerManager::instance();
    setExclusiveVisibleLayer(mgr.activeLayerId());
}

void PCBLayerPanel::onHideAllLayers() {
    PCBLayerManager& mgr = PCBLayerManager::instance();
    setExclusiveVisibleLayer(mgr.activeLayerId());
}

void PCBLayerPanel::onToggleTopLayers() {
    PCBLayerManager& mgr = PCBLayerManager::instance();
    for (const PCBLayer& layer : mgr.layers()) {
        if (layer.side() == PCBLayer::Top) {
            mgr.toggleLayerVisibility(layer.id());
        }
    }
    refreshLayers();
}

void PCBLayerPanel::onToggleBottomLayers() {
    PCBLayerManager& mgr = PCBLayerManager::instance();
    for (const PCBLayer& layer : mgr.layers()) {
        if (layer.side() == PCBLayer::Bottom) {
            mgr.toggleLayerVisibility(layer.id());
        }
    }
    refreshLayers();
}

void PCBLayerPanel::setExclusiveVisibleLayer(int layerId) {
    PCBLayerManager& mgr = PCBLayerManager::instance();
    for (const PCBLayer& layer : mgr.layers()) {
        mgr.setLayerVisible(layer.id(), layer.id() == layerId);
    }
    refreshLayers();
}

QIcon PCBLayerPanel::createColorSwatchIcon(const QColor& color) {
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(24, 24, 27));
    painter.setBrush(color);
    painter.drawRect(1, 1, 10, 10);

    return QIcon(pixmap);
}
