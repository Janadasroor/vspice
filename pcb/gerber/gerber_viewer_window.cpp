#include "gerber_viewer_window.h"
#include "gerber_parser.h"
#include "config_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QColorDialog>
#include <QDockWidget>
#include <QLabel>
#include <QStatusBar>

namespace {
QColor gerberLayerColor(const QString& fileName) {
    const QString name = fileName.toLower();
    if (name.contains("drill") || name.endsWith(".drl")) {
        return QColor(35, 35, 35);
    }
    if (name.contains("edge") || name.contains("outline") || name.endsWith(".gm1") || name.endsWith(".gko")) {
        return QColor(255, 208, 92);
    }
    if (name.endsWith(".gtl") || (name.contains("top") && name.contains("copper"))) {
        return QColor(232, 119, 34);
    }
    if (name.endsWith(".gbl") || (name.contains("bottom") && name.contains("copper"))) {
        return QColor(59, 130, 246);
    }
    if (name.endsWith(".gts") || (name.contains("top") && name.contains("mask"))) {
        return QColor(35, 111, 62, 150);
    }
    if (name.endsWith(".gbs") || (name.contains("bottom") && name.contains("mask"))) {
        return QColor(27, 83, 49, 150);
    }
    if (name.endsWith(".gto") || name.endsWith(".gbo") || name.contains("silk") || name.contains("legend")) {
        return QColor(238, 238, 232);
    }
    if (name.contains("paste")) {
        return QColor(180, 180, 180, 180);
    }
    if (name.contains("courtyard")) {
        return QColor(125, 180, 255);
    }
    if (name.contains("fabrication")) {
        return QColor(150, 150, 180);
    }
    return QColor("#ffffff");
}
}

GerberViewerWindow::GerberViewerWindow(QWidget* parent)
    : QMainWindow(parent),
      m_backgroundColor(ConfigManager::instance().toolProperty("gerber_viewer", "background_color", QColor(Qt::black)).value<QColor>()) {
    setWindowTitle("Viora EDA - Gerber Viewer");
    resize(1100, 750);
    setupUI();
}

GerberViewerWindow::~GerberViewerWindow() {
    qDeleteAll(m_loadedLayers);
}

void GerberViewerWindow::setupUI() {
    // Toolbar
    QToolBar* toolbar = addToolBar("Files");
    toolbar->setMovable(false);
    
    QAction* openAct = toolbar->addAction("Open Gerber Files...");
    connect(openAct, &QAction::triggered, this, &GerberViewerWindow::onOpenFiles);

    QAction* backgroundAct = toolbar->addAction("Background...");
    connect(backgroundAct, &QAction::triggered, this, &GerberViewerWindow::onSelectBackgroundColor);
    
    toolbar->addSeparator();
    
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    
    QAction* zoomFitAct = toolbar->addAction("Zoom Fit");
    connect(zoomFitAct, &QAction::triggered, [this](){ 
        if (m_tabWidget->currentIndex() == 0) m_view->zoomFit(); 
        else m_3dView->fitToBoard();
    });
    
    QAction* zoomInAct = toolbar->addAction("Zoom In");
    connect(zoomInAct, &QAction::triggered, [this](){ 
        if (m_tabWidget->currentIndex() == 0) m_view->scale(1.2, 1.2); 
        else m_3dView->zoomIn();
    });
    
    QAction* zoomOutAct = toolbar->addAction("Zoom Out");
    connect(zoomOutAct, &QAction::triggered, [this](){ 
        if (m_tabWidget->currentIndex() == 0) m_view->scale(1.0/1.2, 1.0/1.2); 
        else m_3dView->zoomOut();
    });

    // Main Tabbed Views
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet("QTabWidget::pane { border: none; } QTabBar::tab { padding: 8px 20px; }");
    
    m_view = new GerberView(this);
    m_3dView = nullptr;
    m_3dPage = new QWidget(this);
    QVBoxLayout* pageLayout = new QVBoxLayout(m_3dPage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* hint = new QLabel("3D View loads on demand when this tab is opened.", m_3dPage);
    hint->setAlignment(Qt::AlignCenter);
    pageLayout->addWidget(hint);
    applyBackgroundColor(m_backgroundColor);
    
    m_tabWidget->addTab(m_view, "2D View");
    m_tabWidget->addTab(m_3dPage, "3D View");
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &GerberViewerWindow::onTabChanged);
    
    setCentralWidget(m_tabWidget);

    // Layer List Dock
    QDockWidget* dock = new QDockWidget("Layers", this);
    m_layerList = new QListWidget(this);
    m_layerList->setStyleSheet("QListWidget { background-color: #1e1e1e; color: #ccc; }");
    connect(m_layerList, &QListWidget::itemChanged, this, &GerberViewerWindow::onLayerToggled);
    dock->setWidget(m_layerList);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    statusBar()->showMessage("Ready");
}

void GerberViewerWindow::onOpenFiles() {
    QStringList paths = QFileDialog::getOpenFileNames(this, "Open Gerber Files", "", 
        "Gerber Files (*.gbr *.gtl *.gbl *.gto *.gbo *.gts *.gbs *.gm1 *.drl);;All Files (*)");
    
    if (paths.isEmpty()) return;

    openFiles(paths);
}

void GerberViewerWindow::openFiles(const QStringList& paths) {
    if (paths.isEmpty()) return;

    m_isBulkLoading = true;
    const QSignalBlocker blocker(m_layerList);
    for (const QString& path : paths) {
        addGerberFile(path);
    }
    m_isBulkLoading = false;
    
    refreshViews();
    m_view->zoomFit();
}

void GerberViewerWindow::addGerberFile(const QString& path) {
GerberLayer* layer = GerberParser::parse(path);
    if (!layer) return;

    layer->setName(QFileInfo(path).fileName());
    layer->setColor(gerberLayerColor(layer->name()));

    m_loadedLayers.append(layer);

    // Add to list widget
    QListWidgetItem* item = new QListWidgetItem(layer->name(), m_layerList);
    item->setCheckState(Qt::Checked);
    
    // Show color in icon
    QPixmap pix(16, 16);
    pix.fill(layer->color());
    item->setIcon(QIcon(pix));
}

void GerberViewerWindow::onClearAll() {
    m_view->clear();
    if (m_3dView) {
        m_3dView->setLayers({});
    }
    m_layerList->clear();
    qDeleteAll(m_loadedLayers);
    m_loadedLayers.clear();
}

void GerberViewerWindow::onLayerToggled(QListWidgetItem* item) {
    if (m_isBulkLoading) {
        return;
    }

    int idx = m_layerList->row(item);
    if (idx >= 0 && idx < m_loadedLayers.size()) {
        m_loadedLayers[idx]->setVisible(item->checkState() == Qt::Checked);
        refreshViews();
    }
}

void GerberViewerWindow::onSelectBackgroundColor() {
    const QColor color = QColorDialog::getColor(
        m_backgroundColor,
        this,
        "Gerber Viewer Background",
        QColorDialog::ShowAlphaChannel);
    if (!color.isValid()) {
        return;
    }

    applyBackgroundColor(color);
    ConfigManager::instance().setToolProperty("gerber_viewer", "background_color", color);
}

void GerberViewerWindow::applyBackgroundColor(const QColor& color) {
    if (!color.isValid()) {
        return;
    }

    m_backgroundColor = color;
    if (m_view) {
        m_view->setBackgroundColor(color);
    }
    if (m_3dView) {
        m_3dView->setBackgroundColor(color);
    }
}

void GerberViewerWindow::onTabChanged(int index) {
    if (index == m_tabWidget->indexOf(m_3dPage)) {
        ensure3DView();
    }
}

void GerberViewerWindow::ensure3DView() {
    if (m_3dView || !m_3dPage) {
        return;
    }

    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(m_3dPage->layout());
    if (!layout) {
        layout = new QVBoxLayout(m_3dPage);
        layout->setContentsMargins(0, 0, 0, 0);
    }

    while (QLayoutItem* child = layout->takeAt(0)) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    m_3dView = new Gerber3DView(m_3dPage);
    m_3dView->setBackgroundColor(m_backgroundColor);
    layout->addWidget(m_3dView);
    m_3dView->setLayers(m_loadedLayers);
}

void GerberViewerWindow::refreshViews() {
    if (m_view) {
        m_view->setLayers(m_loadedLayers);
    }
    if (m_3dView) {
        m_3dView->setLayers(m_loadedLayers);
    }
}
