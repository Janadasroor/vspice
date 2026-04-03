#include "gerber_viewer_window.h"
#include "gerber_parser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QDockWidget>
#include <QStatusBar>

GerberViewerWindow::GerberViewerWindow(QWidget* parent)
    : QMainWindow(parent) {
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
    m_3dView = new Gerber3DView(this);
    
    m_tabWidget->addTab(m_view, "2D View");
    m_tabWidget->addTab(m_3dView, "3D View");
    
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

    for (const QString& path : paths) {
        addGerberFile(path);
    }
    
    m_3dView->setLayers(m_loadedLayers);
    m_view->zoomFit();
}

void GerberViewerWindow::addGerberFile(const QString& path) {
GerberLayer* layer = GerberParser::parse(path);
    if (!layer) return;

    // Assign a color based on filename/index
    static QList<QColor> colors = {
        QColor("#ef4444"), // Top Copper (Red)
        QColor("#3b82f6"), // Bottom Copper (Blue)
        QColor("#10b981"), // Silk (Greenish)
        QColor("#f59e0b"), // Mask (Amber)
        QColor("#8b5cf6"), // Edge (Violet)
        QColor("#ffffff")  // Others
    };
    layer->setColor(colors[m_loadedLayers.size() % colors.size()]);
    layer->setName(QFileInfo(path).fileName());

    m_loadedLayers.append(layer);
    m_view->addLayer(layer);

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
    m_3dView->setLayers({});
    m_layerList->clear();
    qDeleteAll(m_loadedLayers);
    m_loadedLayers.clear();
}

void GerberViewerWindow::onLayerToggled(QListWidgetItem* item) {
    int idx = m_layerList->row(item);
    if (idx >= 0 && idx < m_loadedLayers.size()) {
        m_loadedLayers[idx]->setVisible(item->checkState() == Qt::Checked);
        // Trigger full redraw
        m_view->clear();
        for (auto* l : m_loadedLayers) m_view->addLayer(l);
        m_3dView->setLayers(m_loadedLayers);
    }
}
