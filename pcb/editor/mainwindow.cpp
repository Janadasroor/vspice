// mainwindow.cpp
#include "mainwindow.h"
#include "pcb_api.h"
#include "pcb_view.h"
#include "pcb_item_registry.h"
#include "pcb_tool_registry_builtin.h"
#include "pcb_component_tool.h"
#include "pcb_plugin_manager.h"
#include "pcb_layer.h"
#include "pcb_layer_panel.h"
#include "pcb_drc_panel.h"
#include "../dialogs/board_setup_dialog.h"
#include "../dialogs/via_stitching_dialog.h"
#include "../dialogs/gerber_export_dialog.h"
#include "../dialogs/netlist_import_dialog.h"
#include "../dialogs/pick_place_export_dialog.h"
#include "../dialogs/auto_router_dialog.h"
#include "../dialogs/length_matching_dialog.h"
#include "../gerber/gerber_exporter.h"
#include "../manufacturing/manufacturing_exporter.h"
#include "../mcad/mcad_exporter.h"
#include <QPrinter>
#include <QSvgGenerator>
#include "../../core/ui/selection_filter_widget.h"
#include "theme_manager.h"
#include "footprint_editor.h"
#include "footprint_library.h"
#include "../gerber/gerber_viewer_window.h"
#include "../ui/pcb_components_widget.h"
#include "gemini_dialog.h"
#include "gemini_panel.h"
#include "component_item.h"
#include "pcb_commands.h"
#include "../analysis/pcb_ratsnest_manager.h"
#include "pad_item.h"
#include "trace_item.h"
#include "via_item.h"
#include "../ui/pcb_3d_window.h"
#include "copper_pour_item.h"
#include "../../core/ui/command_palette.h"
#include <QMenuBar>
#include <QMenu>
#include <QScreen>
#include <QFile>
#include "ratsnest_item.h"
#include "sync_manager.h"
#include "../io/pcb_file_io.h"
#include "../../core/settings_dialog.h"
#include "../../core/config_manager.h"
#include <QMenu>
#include <QMenuBar>
#include <QTreeWidget>
#include <QLineEdit>
#include <QActionGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QCloseEvent>
#include <QWheelEvent>
#include <QScrollBar>
#include <QFrame>
#include <QTimer>
#include <QSet>
#include <QPainter>
#include <QPixmap>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <algorithm>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_scene(nullptr)
    , m_view(nullptr)
    , m_layerDock(nullptr)
    , m_propertiesDock(nullptr)
    , m_libraryDock(nullptr)
    , m_drcDock(nullptr)
    , m_layerPanel(nullptr)
    , m_drcPanel(nullptr)
    , m_propertyEditor(nullptr)
    , m_componentsPanel(nullptr)
    , m_geminiDock(nullptr)
    , m_geminiPanel(nullptr)
    , m_componentTool(nullptr)
    , m_coordLabel(nullptr)
    , m_gridCombo(nullptr)
    , m_layerLabel(nullptr)
    , m_undoStack(new QUndoStack(this))
    , m_api(new PCBAPI(nullptr, m_undoStack, this))
    , m_selectionFilter(nullptr) {

    setWindowTitle("Viora EDA - PCB Editor");
    setMinimumSize(800, 600);
    resize(1100, 800);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    PCBItemRegistry::registerBuiltInItems();
    PCBToolRegistryBuiltIn::registerBuiltInTools();

    setupCanvas();
    createMenuBar();
    createToolBar();
    createDockWidgets();
    createStatusBar();
    applyTheme();

    connect(&SyncManager::instance(), &SyncManager::ecoAvailable, this, &MainWindow::handleIncomingECO);
    
    // Smart Cross-Probing
    connect(&SyncManager::instance(), &SyncManager::crossProbeReceived, this, [this](const QString& refDes, const QString& netName) {
        if (!m_scene) return;
        const bool autoFocus = ConfigManager::instance().autoFocusOnCrossProbe();
        
        bool found = false;
        
        for (QGraphicsItem* item : m_scene->items()) {
            if (auto* comp = dynamic_cast<class ComponentItem*>(item)) {
                if (comp->name() == refDes) {
                    // Fix infinite loop: if already selected, don't re-select
                    if (comp->isSelected() && m_scene->selectedItems().size() == 1) {
                         if (autoFocus) m_view->centerOn(comp);
                         return;
                    }
                    
                    m_scene->clearSelection();
                    comp->setSelected(true);
                    if (autoFocus) m_view->centerOn(comp);
                    found = true;
                    statusBar()->showMessage("Cross-probed: " + refDes, 2000);
                    break;
                }
            }
        }
        
        if (!found && !netName.isEmpty()) {
            // Highlight net
            m_netHighlightEnabled = true;
            // Assuming we have a way to set the active net for highlighting
            // For now, we'll try to find an item on that net and let the existing logic handle it
            for (auto* item : m_scene->items()) {
                if (auto* pi = dynamic_cast<PCBItem*>(item)) {
                    if (pi->netName() == netName) {
                        m_scene->clearSelection();
                        pi->setSelected(true);
                        if (autoFocus) m_view->centerOn(pi);
                        statusBar()->showMessage("Cross-probed net: " + netName, 2000);
                        break;
                    }
                }
            }
        }
    });

    if (SyncManager::instance().hasPendingECO()) {
        QTimer::singleShot(500, this, &MainWindow::handleIncomingECO);
    }

    // Restore UI State (Deferred to prevent startup crashes)
    QTimer::singleShot(0, this, [this](){
        QByteArray geom = ConfigManager::instance().windowGeometry("PCBEditor");
        QByteArray state = ConfigManager::instance().windowState("PCBEditor");
        if (!geom.isEmpty()) restoreGeometry(geom);
        if (!state.isEmpty()) restoreState(state);

        ensureRightBottomDockTabs();

        qDebug() << "PCB Editor UI state restored";
    });
}

MainWindow::~MainWindow() {
}

void MainWindow::applyTheme() {
    PCBTheme* theme = ThemeManager::theme();
    theme->applyToWidget(this);
    
    // Apply specific toolbars and dock styling for premium look
    for (auto toolbar : findChildren<QToolBar*>()) {
        toolbar->setStyleSheet(theme->toolbarStylesheet());
    }
    for (auto dock : findChildren<QDockWidget*>()) {
        dock->setStyleSheet(theme->dockStylesheet());
    }
    
    if (statusBar()) {
        statusBar()->setStyleSheet(theme->statusBarStylesheet());
    }
    
    if (m_view) {
        m_view->setBackgroundBrush(QBrush(theme->canvasBackground()));
    }
}

void MainWindow::setupCanvas() {
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-5000, -5000, 10000, 10000);
    if (m_api) m_api->setScene(m_scene);
    updateGrid();
    
    // Initialize Ratsnest
    PCBRatsnestManager::instance().setScene(m_scene);

    m_view = new PCBView(this);
    m_view->setScene(m_scene);
    m_view->setUndoStack(m_undoStack);
    connect(m_scene, &QGraphicsScene::selectionChanged, m_view, &PCBView::selectionChanged);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updatePropertyBar);
    
    // Live Ratsnest and Copper Pour Updates
    connect(m_undoStack, &QUndoStack::indexChanged, this, [this](){
        PCBRatsnestManager::instance().update();
        for (auto* item : m_scene->items()) {
            if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                pour->rebuild();
                pour->update();
            }
        }
        if (m_3dWindow) m_3dWindow->updateView();
    });

    setCentralWidget(m_view);
    connect(m_view, &PCBView::toolChanged, this, &MainWindow::updateOptionsBar);
    connect(m_view, &PCBView::coordinatesChanged, this, &MainWindow::updateCoordinates);
    connect(m_view, &PCBView::statusMessage, this, [this](const QString& msg){
        statusBar()->showMessage(msg, 3000);
    });
}

void MainWindow::updateGrid() {
    // Grid size is managed by m_gridCombo selection
}

void MainWindow::createMenuBar() {
    QMenuBar *menuBar = this->menuBar();

    QMenu *fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("&New", QKeySequence::New, this, &MainWindow::onNewProject);
    fileMenu->addAction("&Open", QKeySequence::Open, this, &MainWindow::onOpenProject);
    fileMenu->addAction("Open Gerber Viewer...", QKeySequence(), this, &MainWindow::onOpenGerberViewer);
    fileMenu->addAction("&Save", QKeySequence::Save, this, &MainWindow::onSaveProject);
    fileMenu->addAction("Save &As...", QKeySequence::SaveAs, this, &MainWindow::onSaveProjectAs);

    fileMenu->addSeparator();
    fileMenu->addAction("📥 Import Netlist...", QKeySequence("Ctrl+I"), this, &MainWindow::onImportNetlist);

    fileMenu->addSeparator();
    QMenu* exportMenu = fileMenu->addMenu("Export");
    exportMenu->addAction("Export as Image...", this, &MainWindow::onExportImage);
    exportMenu->addAction("Export as PDF...", this, &MainWindow::onExportPDF);
    exportMenu->addAction("Export as SVG...", this, &MainWindow::onExportSVG);
    exportMenu->addAction("Export Assembly Drawing...", this, &MainWindow::onExportAssemblyDrawing);
    exportMenu->addSeparator();
    exportMenu->addAction("Export IPC-2581...", this, &MainWindow::onExportIPC2581);
    exportMenu->addAction("Export ODB++ Package...", this, &MainWindow::onExportODBpp);
    exportMenu->addSeparator();
    exportMenu->addAction("Export Pick and Place...", this, &MainWindow::onExportPickPlace);
    exportMenu->addSeparator();
    exportMenu->addAction("Export STEP (Wireframe)...", this, &MainWindow::onExportSTEP);
    exportMenu->addAction("Export IGES (Wireframe)...", this, &MainWindow::onExportIGES);
    
    fileMenu->addAction("Generate Gerber Files...", this, &MainWindow::onGenerateGerbers);
    
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

    QMenu *editMenu = menuBar->addMenu("&Edit");
    QAction* undoAction = m_undoStack->createUndoAction(this, "&Undo");
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    QAction* redoAction = m_undoStack->createRedoAction(this, "&Redo");
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction("Cu&t", QKeySequence::Cut, nullptr, nullptr);
    editMenu->addAction("&Copy", QKeySequence::Copy, nullptr, nullptr);
    editMenu->addAction("&Paste", QKeySequence::Paste, nullptr, nullptr);
    editMenu->addSeparator();
    editMenu->addAction("&Delete", QKeySequence::Delete, this, &MainWindow::onDeleteSelection);
    editMenu->addAction("Select &All", QKeySequence::SelectAll, this, [this]() {
        if (!m_scene) return;
        m_scene->clearSelection();

        int count = 0;
        for (QGraphicsItem* item : m_scene->items()) {
            auto* pcbItem = dynamic_cast<PCBItem*>(item);
            if (!pcbItem) continue;

            // Select only top-level selectable PCB items to avoid selecting child pads twice.
            if (dynamic_cast<PCBItem*>(pcbItem->parentItem()) != nullptr) continue;
            if (!(pcbItem->flags() & QGraphicsItem::ItemIsSelectable)) continue;
            if (!pcbItem->isVisible()) continue;

            pcbItem->setSelected(true);
            ++count;
        }

        statusBar()->showMessage(QString("Selected %1 item(s)").arg(count), 2000);
    });
    editMenu->addSeparator();
    editMenu->addAction("Settings...", this, &MainWindow::onSettings);

    QMenu *viewMenu = menuBar->addMenu("&View");
    viewMenu->addAction("Zoom &In", QKeySequence::ZoomIn, this, &MainWindow::onZoomIn);
    viewMenu->addAction("Zoom &Out", QKeySequence::ZoomOut, this, &MainWindow::onZoomOut);
    viewMenu->addAction("&Fit to Window", QKeySequence("F"), this, &MainWindow::onZoomFit);
    viewMenu->addAction("Zoom to &Components", QKeySequence("Alt+F"), this, &MainWindow::onZoomAllComponents);
    viewMenu->addSeparator();
    QAction* netFocusAct = viewMenu->addAction("Highlight Selected Net");
    netFocusAct->setCheckable(true);
    netFocusAct->setShortcut(QKeySequence("H"));
    connect(netFocusAct, &QAction::toggled, this, [this](bool on) {
        m_netHighlightEnabled = on;
        if (!on) {
            clearNetHighlighting();
            statusBar()->showMessage("Net highlighting disabled", 2000);
            return;
        }
        applyNetHighlighting();
    });
    viewMenu->addAction("3D View", QKeySequence("Alt+3"), this, &MainWindow::onToggle3DView);
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Default Layout", this, [this]() {
        ensureRightBottomDockTabs();
        
        if (m_libraryDock) m_libraryDock->show();

        // Clear saved state so it sticks
        ConfigManager::instance().saveWindowState("PCBEditor", saveGeometry(), saveState());
        statusBar()->showMessage("Workspace layout reset to default tabs", 3000);
    });
    
    QMenu *toolsMenu = menuBar->addMenu("&Tools");
    QAction* drcAction = toolsMenu->addAction("Design Rule Check");
    drcAction->setShortcut(QKeySequence("Shift+D"));
    connect(drcAction, &QAction::triggered, this, &MainWindow::onRunDRC);
    QAction* courtyardAction = toolsMenu->addAction("Courtyard Validation");
    courtyardAction->setShortcut(QKeySequence("Shift+C"));
    connect(courtyardAction, &QAction::triggered, this, &MainWindow::onRunCourtyardValidation);
    QAction* arrayAction = toolsMenu->addAction("Create Linear Array...");
    arrayAction->setShortcut(QKeySequence("Ctrl+Shift+A"));
    connect(arrayAction, &QAction::triggered, this, &MainWindow::onCreateLinearArray);
    QAction* circularArrayAction = toolsMenu->addAction("Create Circular Array...");
    circularArrayAction->setShortcut(QKeySequence("Ctrl+Alt+A"));
    connect(circularArrayAction, &QAction::triggered, this, &MainWindow::onCreateCircularArray);
    QAction* panelizeAction = toolsMenu->addAction("Panelize Board...");
    panelizeAction->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(panelizeAction, &QAction::triggered, this, &MainWindow::onPanelizeBoard);
    toolsMenu->addAction("Measure Distance");
    toolsMenu->addAction("Board Setup", this, &MainWindow::onBoardSetup);
    toolsMenu->addAction("Via Stitching...", this, &MainWindow::onViaStitching);
    toolsMenu->addSeparator();
    QAction* autoRouteAction = toolsMenu->addAction("🚀 Auto-Router...");
    autoRouteAction->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(autoRouteAction, &QAction::triggered, this, &MainWindow::onAutoRoute);

    QAction* lengthMatchAction = toolsMenu->addAction("📏 Length Matching...");
    lengthMatchAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(lengthMatchAction, &QAction::triggered, this, &MainWindow::onLengthMatching);

    toolsMenu->addSeparator();
    QAction* paletteAction = toolsMenu->addAction("Command Palette...");
    paletteAction->setShortcut(QKeySequence("Ctrl+K"));
    connect(paletteAction, &QAction::triggered, this, &MainWindow::onOpenCommandPalette);
}

void MainWindow::createToolBar() {
    QToolBar *toolbar = addToolBar("Main Toolbar");
    toolbar->setObjectName("MainToolbar");
    toolbar->setIconSize(QSize(22, 22));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setMovable(false);
    toolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, toolbar);
    toolbar->setStyleSheet(
        "QToolBar#MainToolbar {"
        "  background-color: #1a1a1c;"
        "  border-right: 1px solid #101010;"
        "  padding: 8px 6px;"
        "  spacing: 8px;"
        "}"
        "QToolBar#MainToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#MainToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#MainToolbar QToolButton:checked, QToolBar#MainToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "  color: white;"
        "}"
    );

    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);
    
    QStringList toolNames = {"Select", "Erase", "Zoom Area", "Trace", "Diff Pair", "Length Tuning", "Pad", "Via", "Rectangle", "Polygon Pour", "Measure"};
    QMap<QString, QIcon> toolIcons;
    toolIcons["Select"] = QIcon(":/icons/tool_select.svg");
    toolIcons["Erase"] = QIcon(":/icons/tool_erase.svg");
    toolIcons["Zoom Area"] = QIcon(":/icons/tool_zoom_area.svg");
    toolIcons["Trace"] = QIcon(":/icons/tool_wire.svg");
    toolIcons["Diff Pair"] = QIcon(":/icons/tool_diff_pair.svg");
    toolIcons["Length Tuning"] = QIcon(":/icons/tool_meander.svg");
    toolIcons["Pad"] = QIcon(":/icons/tool_pad.svg");
    toolIcons["Via"] = QIcon(":/icons/tool_circle.svg");
    toolIcons["Rectangle"] = QIcon(":/icons/tool_rect.svg");
    toolIcons["Polygon Pour"] = QIcon(":/icons/tool_polygon.svg");
    toolIcons["Measure"] = QIcon(":/icons/tool_measure.svg");
    
    auto availableTools = PCBToolRegistry::instance().registeredTools();
    
    for (const QString& toolName : toolNames) {
        if (availableTools.contains(toolName)) {
            QIcon icon = toolIcons.value(toolName);
            if (icon.isNull()) icon = QIcon(":/icons/tool_generic.svg");
            
            QAction* action = toolbar->addAction(icon, toolName);
            action->setCheckable(true);
            action->setData(toolName);
            
            // Assign hotkeys
            if (toolName == "Select") action->setShortcut(QKeySequence("Esc"));
            else if (toolName == "Erase") action->setShortcut(QKeySequence("E"));
            else if (toolName == "Trace") action->setShortcut(QKeySequence("W"));
            else if (toolName == "Diff Pair") action->setShortcut(QKeySequence("D"));
            else if (toolName == "Via") action->setShortcut(QKeySequence("V"));
            else if (toolName == "Zoom Area") action->setShortcut(QKeySequence("Z"));
            else if (toolName == "Measure") action->setShortcut(QKeySequence("M"));
            else if (toolName == "Pad") action->setShortcut(QKeySequence("P"));
            else if (toolName == "Rectangle") action->setShortcut(QKeySequence("R"));
            else if (toolName == "Polygon Pour") action->setShortcut(QKeySequence("O"));
            else if (toolName == "Length Tuning") action->setShortcut(QKeySequence("T"));
            
            m_toolActions[toolName] = action;
            toolGroup->addAction(action);
            connect(action, &QAction::triggered, this, &MainWindow::onToolSelected);
        }
    }

    if (m_toolActions.contains("Select")) {
        m_toolActions["Select"]->setChecked(true);
    }

    toolbar->addSeparator();

    if (availableTools.contains("Component")) {
        QAction* action = toolbar->addAction(QIcon(":/icons/comp_ic.svg"), "Component");
        action->setCheckable(true);
        action->setData("Component");
        m_toolActions["Component"] = action;
        toolGroup->addAction(action);
        connect(action, &QAction::triggered, this, &MainWindow::onToolSelected);
        toolbar->addSeparator();
    }

    // Undo button for quick access
    QAction* undoToolbarAction = toolbar->addAction(QIcon(":/icons/undo.svg"), "Undo");
    undoToolbarAction->setShortcut(QKeySequence::Undo);
    undoToolbarAction->setToolTip("Undo last action (Ctrl+Z)");
    connect(undoToolbarAction, &QAction::triggered, m_undoStack, &QUndoStack::undo);

    QAction* deleteAction = toolbar->addAction(QIcon(":/icons/tool_delete.svg"), "Delete");
    deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    deleteAction->setToolTip("Delete selected items (Del / Bksp)");
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSelection);
    addAction(deleteAction);

    toolbar->addSeparator();
    QAction* drcToolbarAction = toolbar->addAction(QIcon(":/icons/check.svg"), "DRC");
    drcToolbarAction->setToolTip("Run Design Rule Check (Shift+D)");
    connect(drcToolbarAction, &QAction::triggered, this, &MainWindow::onRunDRC);

    // Layer shortcuts
    QAction* layer1Act = new QAction(this);
    layer1Act->setShortcut(QKeySequence("1"));
    connect(layer1Act, &QAction::triggered, this, [this](){ onActiveLayerChanged(0); m_layerPanel->selectLayer(0); });
    addAction(layer1Act);

    QAction* layer2Act = new QAction(this);
    layer2Act->setShortcut(QKeySequence("2"));
    connect(layer2Act, &QAction::triggered, this, [this](){ onActiveLayerChanged(1); m_layerPanel->selectLayer(1); });
    addAction(layer2Act);
    
    QAction* rotateAction = toolbar->addAction(QIcon(":/icons/tool_rotate.svg"), "Rotate");
    rotateAction->setToolTip("Rotate selected items (R)");
    rotateAction->setShortcut(QKeySequence("R"));
    connect(rotateAction, &QAction::triggered, this, &MainWindow::onRotate);
    
    QAction* mirrorAction = toolbar->addAction(QIcon(":/icons/flip_h.svg"), "Mirror");
    mirrorAction->setToolTip("Mirror selected items (M)");
    mirrorAction->setShortcut(QKeySequence("Ctrl+M"));
    connect(mirrorAction, &QAction::triggered, this, &MainWindow::onMirror);

    toolbar->addSeparator();

    QAction* snapAction = toolbar->addAction(QIcon(":/icons/snap_grid.svg"), "Snap Grid");
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    snapAction->setToolTip("Enable/Disable Grid Snapping (S)");
    snapAction->setShortcut(QKeySequence("S"));
    connect(snapAction, &QAction::toggled, this, [this](bool checked){
        if (m_view) m_view->setSnapToGrid(checked);
    });

    toolbar->addSeparator();

    QAction* zoomInAction = toolbar->addAction(QIcon(":/icons/view_zoom_in.svg"), "Zoom In"); 
    zoomInAction->setToolTip("Zoom in (+)");
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);
    QAction* zoomOutAction = toolbar->addAction(QIcon(":/icons/view_zoom_out.svg"), "Zoom Out"); 
    zoomOutAction->setToolTip("Zoom out (-)");
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
    QAction* zoomFitAction = toolbar->addAction(QIcon(":/icons/view_fit.svg"), "Fit All");
    zoomFitAction->setToolTip("Fit all items to window (F)");
    zoomFitAction->setShortcut(QKeySequence("F"));
    connect(zoomFitAction, &QAction::triggered, this, &MainWindow::onZoomFit);

    QAction* zoomCompAction = toolbar->addAction(QIcon(":/icons/view_zoom_components.svg"), "Zoom Components");
    zoomCompAction->setToolTip("Zoom to fit all components (Alt+F)");
    zoomCompAction->setShortcut(QKeySequence("Alt+F"));
    connect(zoomCompAction, &QAction::triggered, this, &MainWindow::onZoomAllComponents);

    QAction* zoomSelAction = toolbar->addAction(QIcon(":/icons/view_zoom_selection.svg"), "Zoom Selection");
    zoomSelAction->setToolTip("Zoom to fit selected items (Ctrl+0)");
    zoomSelAction->setShortcut(QKeySequence("Ctrl+0"));
    connect(zoomSelAction, &QAction::triggered, this, &MainWindow::onZoomSelection);

    // ─── Property Bar (Dynamic Ribbon) ──────────────────────────────────────
    m_propertyBar = new QToolBar("Selection Properties", this);
    m_propertyBar->setObjectName("PropertyBar");
    m_propertyBar->setIconSize(QSize(18, 18));
    m_propertyBar->setMovable(false);
    m_propertyBar->setFixedHeight(40);
    m_propertyBar->setStyleSheet(
        "QToolBar#PropertyBar {"
        "  background: #1e1e20;"
        "  border-bottom: 1px solid #333336;"
        "  spacing: 15px;"
        "  padding-left: 15px;"
        "}"
        "QLabel { color: #3b82f6; font-weight: 600; font-size: 11px; text-transform: uppercase; }"
        "QLineEdit, QComboBox, QDoubleSpinBox {"
        "  background: #121214;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 4px;"
        "  padding: 3px 8px;"
        "  color: #ffffff;"
        "  min-width: 80px;"
        "}"
        "QLineEdit:focus { border-color: #3b82f6; }"
    );
    addToolBar(Qt::TopToolBarArea, m_propertyBar);
    
    updatePropertyBar();

    toolbar->addSeparator();

    QAction* view3DAct = toolbar->addAction(QIcon(":/icons/tool_3d.svg"), "3D View");
    view3DAct->setToolTip("Open PCB 3D Preview (Alt+3)");
    view3DAct->setShortcut(QKeySequence("Alt+3"));
    connect(view3DAct, &QAction::triggered, this, &MainWindow::onToggle3DView);

    // ─── Top Main Menu Bar Replacement ──────────────────────────────────────────
    QToolBar *topToolbar = addToolBar("Top Main Config");
    topToolbar->setObjectName("TopMainToolbar");
    topToolbar->setIconSize(QSize(20, 20));
    topToolbar->setMovable(false);
    topToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    topToolbar->setStyleSheet(
        "QToolBar#TopMainToolbar {"
        "  background-color: #2d2d30;"
        "  border-bottom: 1px solid #1e1e1e;"
        "  padding: 4px 8px;"
        "  spacing: 3px;"
        "}"
        "QToolBar#TopMainToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 3px;"
        "  padding: 4px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#TopMainToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#TopMainToolbar QToolButton:checked, QToolBar#TopMainToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "}"
        "QToolBar#TopMainToolbar QLabel {"
        "  color: #888;"
        "  font-size: 11px;"
        "}"
        "QToolBar#TopMainToolbar QComboBox {"
        "  background-color: #1e1e1e;"
        "  color: #cccccc;"
        "  border: 1px solid #3c3c3c;"
        "  border-radius: 3px;"
        "  padding: 2px 6px;"
        "  font-size: 11px;"
        "}"
    );

    // Grid size to Top Toolbar
    topToolbar->addSeparator();
    topToolbar->addWidget(new QLabel("  Grid: "));
    auto* gridCombo = new QComboBox();
    gridCombo->addItems({"0.1", "0.5", "1.0", "2.5", "5.0", "10.0", "25.0", "50.0"});
    gridCombo->setCurrentText(QString::number(1.0, 'f', 1));
    gridCombo->setFixedWidth(60);
    connect(gridCombo, &QComboBox::currentTextChanged, this, [this](const QString& text){
        if (m_view) {
            m_view->setGridSize(text.toDouble());
        }
    });
    topToolbar->addWidget(gridCombo);

    // Zoom & View controls (match schematic editor experience)
    topToolbar->addSeparator();

    QAction* topZoomInAct = topToolbar->addAction(QIcon(":/icons/view_zoom_in.svg"), "Zoom In");
    connect(topZoomInAct, &QAction::triggered, this, &MainWindow::onZoomIn);

    QAction* topZoomOutAct = topToolbar->addAction(QIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    connect(topZoomOutAct, &QAction::triggered, this, &MainWindow::onZoomOut);

    QAction* topFitAct = topToolbar->addAction(QIcon(":/icons/view_fit.svg"), "Fit All");
    connect(topFitAct, &QAction::triggered, this, &MainWindow::onZoomFit);

    QAction* topCompAct = topToolbar->addAction(QIcon(":/icons/view_zoom_components.svg"), "Zoom Components");
    connect(topCompAct, &QAction::triggered, this, &MainWindow::onZoomAllComponents);

    QAction* topZoomSelAct = topToolbar->addAction(QIcon(":/icons/view_zoom_selection.svg"), "Zoom Selection");
    connect(topZoomSelAct, &QAction::triggered, this, &MainWindow::onZoomSelection);

    QAction* topZoomAreaAct = topToolbar->addAction(QIcon(":/icons/tool_zoom_area.svg"), "Zoom Area");
    topZoomAreaAct->setToolTip("Drag a rectangle to zoom in (Z)");
    connect(topZoomAreaAct, &QAction::triggered, this, [this]() {
        if (m_view) {
            m_view->setCurrentTool("Zoom Area");
        }
    });

    topToolbar->addSeparator();

    QAction* top3DAct = topToolbar->addAction(QIcon(":/icons/tool_3d.svg"), "3D View");
    top3DAct->setToolTip("Open PCB 3D Preview (Alt+3)");
    connect(top3DAct, &QAction::triggered, this, &MainWindow::onToggle3DView);

    // ─── Options Toolbar (Context Settings) ──────────────────────────────────
    m_optionsToolbar = new QToolBar("Tool Settings", this);
    m_optionsToolbar->setObjectName("OptionsToolbar");
    m_optionsToolbar->setIconSize(QSize(18, 18));
    m_optionsToolbar->setMovable(false);
    m_optionsToolbar->setFixedHeight(40);
    m_optionsToolbar->setStyleSheet(
        "QToolBar#OptionsToolbar {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #252528, stop:1 #1e1e20);"
        "  border-bottom: 1px solid #333336;"
        "  spacing: 12px;"
        "  padding-left: 10px;"
        "}"
        "QLabel { color: #aaaaaa; font-weight: 500; font-size: 11px; }"
        "QComboBox, QSpinBox, QDoubleSpinBox {"
        "  background: #2d2d30;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 4px;"
        "  padding: 2px 6px;"
        "  color: #eeeeee;"
        "}"
    );
    addToolBar(Qt::TopToolBarArea, m_optionsToolbar);
    insertToolBarBreak(m_optionsToolbar);

    // ─── Layout Toolbar (Alignment & Distribution) ──────────────────────────
    QToolBar *layoutToolbar = addToolBar("Layout");
    layoutToolbar->setObjectName("LayoutToolbar");
    layoutToolbar->setIconSize(QSize(20, 20));
    layoutToolbar->setMovable(false);
    layoutToolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, layoutToolbar);
    layoutToolbar->setStyleSheet(
        "QToolBar#LayoutToolbar {"
        "  background-color: #1a1a1c;"
        "  border-right: 1px solid #101010;"
        "  padding: 8px 6px;"
        "  spacing: 8px;"
        "}"
        "QToolBar#LayoutToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#LayoutToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
    );
    auto addAlignAct = [this, layoutToolbar](const QString& text, const QString& tooltip, auto slot) {
        QAction* act = layoutToolbar->addAction(createPCBIcon(text), text);
        act->setToolTip(tooltip);
        connect(act, &QAction::triggered, this, slot);
    };

    addAlignAct("Align Left", "Align Left", &MainWindow::onAlignLeft);
    addAlignAct("Align Right", "Align Right", &MainWindow::onAlignRight);
    addAlignAct("Align Top", "Align Top", &MainWindow::onAlignTop);
    addAlignAct("Align Bottom", "Align Bottom", &MainWindow::onAlignBottom);
    
    layoutToolbar->addSeparator();

    addAlignAct("Center X", "Center Horizontal", &MainWindow::onAlignCenterX);
    addAlignAct("Center Y", "Center Vertical", &MainWindow::onAlignCenterY);
    
    layoutToolbar->addSeparator();

    addAlignAct("Distribute H", "Distribute Horizontally", &MainWindow::onDistributeH);
    addAlignAct("Distribute V", "Distribute Vertically", &MainWindow::onDistributeV);
}

void MainWindow::ensureRightBottomDockTabs() {
    if (!m_layerDock || !m_propertiesDock || !m_drcDock || !m_geminiDock) {
        return;
    }

    // Force these docks into the right tab stack so the core panels are always present.
    const QList<QDockWidget*> requiredDocks = {m_layerDock, m_propertiesDock, m_drcDock, m_geminiDock};
    for (QDockWidget* dock : requiredDocks) {
        if (!dock) continue;
        if (dock->isFloating()) {
            dock->setFloating(false);
        }
        addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->show();
    }

    tabifyDockWidget(m_layerDock, m_propertiesDock);
    tabifyDockWidget(m_layerDock, m_drcDock);
    tabifyDockWidget(m_layerDock, m_geminiDock);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::South);
    m_layerDock->raise();
}

void MainWindow::createDockWidgets() {
    // === Layer Dock ===
    m_layerDock = new QDockWidget("Layers", this);
    m_layerDock->setObjectName("LayerDock");
    m_layerPanel = new PCBLayerPanel(m_layerDock);

    QWidget* layerContainer = new QWidget(this);
    QVBoxLayout* layerLayout = new QVBoxLayout(layerContainer);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    layerLayout->setSpacing(0);

    m_selectionFilter = new SelectionFilterWidget(this);
    connect(m_selectionFilter, &SelectionFilterWidget::filterChanged, this, &MainWindow::onFilterChanged);

    layerLayout->addWidget(m_layerPanel, 1);
    layerLayout->addWidget(m_selectionFilter);

    m_layerDock->setWidget(layerContainer);
    addDockWidget(Qt::RightDockWidgetArea, m_layerDock);

    connect(m_layerPanel, &PCBLayerPanel::activeLayerChanged, 
            this, &MainWindow::onActiveLayerChanged);
    connect(m_layerPanel, &PCBLayerPanel::layerVisibilityChanged,
            this, [this](int, bool) { updateGrid(); });

    // === DRC Dock ===
    m_drcDock = new QDockWidget("Design Rule Check", this);
    m_drcDock->setObjectName("DRCDock");
    m_drcPanel = new PCBDRCPanel(m_drcDock);
    m_drcPanel->setScene(m_scene);
    m_drcDock->setWidget(m_drcPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_drcDock);

    connect(m_drcPanel, &PCBDRCPanel::violationSelected,
            this, &MainWindow::onDRCViolationSelected);

    // === Properties Dock ===
    m_propertiesDock = new QDockWidget("Properties", this);
    m_propertiesDock->setObjectName("PropertiesDock");
    m_propertyEditor = new Flux::PCBPropertyEditor();
    m_propertiesDock->setWidget(m_propertyEditor);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    connect(m_propertyEditor, &Flux::PCBPropertyEditor::propertyChanged, this, &MainWindow::onPropertyChanged);

    // === Gemini Assistant Dock ===
    m_geminiDock = new QDockWidget("✨ Gemini Assistant", this);
    m_geminiDock->setObjectName("GeminiDock");
    m_geminiDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_geminiPanel = new GeminiPanel(m_scene, this);
    m_geminiPanel->setMode("pcb");
    m_geminiPanel->setUndoStack(m_undoStack);
    connect(m_geminiPanel, &GeminiPanel::snippetGenerated, this, &MainWindow::onSnippetGenerated);
    m_geminiDock->setWidget(m_geminiPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_geminiDock);

    // === Tabify All Right Docks ===
    ensureRightBottomDockTabs();

    // === Left Side: Library ===
    m_libraryDock = new QDockWidget("Component Library", this);
    m_libraryDock->setObjectName("LibraryDock");
    m_componentsPanel = new PCBComponentsWidget(this);
    m_libraryDock->setWidget(m_componentsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_libraryDock);

    connect(m_componentsPanel, &PCBComponentsWidget::footprintSelected, this, [this](const QString& fpName){
        m_view->setCurrentTool("Component");
        PCBTool* current = m_view->currentTool();
        if (PCBComponentTool* compTool = dynamic_cast<PCBComponentTool*>(current)) {
             compTool->setComponentType(fpName);
             statusBar()->showMessage("Selected component: " + fpName, 3000);
        }

        if (m_toolActions.contains("Component")) {
             m_toolActions["Component"]->setChecked(true);
        }
    });

    connect(m_componentsPanel, &PCBComponentsWidget::footprintCreated, this, &MainWindow::onOpenFootprintEditor);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this](){
        if (!m_scene) return;
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        QList<PCBItem*> pItems;
        for (auto* item : selected) {
            if (auto* pi = dynamic_cast<PCBItem*>(item)) {
                if (dynamic_cast<PCBItem*>(pi->parentItem()) == nullptr || pi->itemType() == PCBItem::PadType) {
                    pItems.append(pi);
                }
            }
        }

        if (!pItems.isEmpty()) {
            m_propertyEditor->setPCBItems(pItems);
            if (m_propertiesDock) {
                m_propertiesDock->show();
                m_propertiesDock->raise();
            }
        } else {
            m_propertyEditor->clear();
        }

        if (m_netHighlightEnabled) {
            applyNetHighlighting();
        }
    });
}

void MainWindow::createStatusBar() {
    m_coordLabel = new QLabel("📍 X: 0.0mm  Y: 0.0mm");
    m_coordLabel->setMinimumWidth(180);
    m_coordLabel->setStyleSheet("QLabel { padding: 4px 12px; font-weight: 500; }");
    
    // Grid Size Selector
    m_gridCombo = new QComboBox();
    m_gridCombo->addItem("Grid: 0.01mm", 0.01);
    m_gridCombo->addItem("Grid: 0.05mm", 0.05);
    m_gridCombo->addItem("Grid: 0.1mm", 0.1);
    m_gridCombo->addItem("Grid: 0.25mm", 0.25);
    m_gridCombo->addItem("Grid: 0.5mm", 0.5);
    m_gridCombo->addItem("Grid: 1.0mm", 1.0);
    m_gridCombo->addItem("Grid: 1.27mm", 1.27);
    m_gridCombo->addItem("Grid: 2.54mm", 2.54);
    m_gridCombo->addItem("Grid: 5.0mm", 5.0);
    m_gridCombo->setCurrentIndex(7); // Default to 2.54mm
    m_gridCombo->setToolTip("Select Snap Grid Size");
    m_gridCombo->setStyleSheet(
        "QComboBox { border: none; padding: 2px 10px; background: transparent; font-weight: 500; min-width: 120px; }"
        "QComboBox:hover { background: #2d2d30; }"
        "QComboBox::drop-down { border: none; }"
    );
    
    connect(m_gridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index){
        double size = m_gridCombo->itemData(index).toDouble();
        if (m_view) m_view->setGridSize(size);
    });

    m_layerLabel = new QLabel("⚡ Layer: Top Copper");
    m_layerLabel->setStyleSheet("QLabel { padding: 4px 12px; }");

    statusBar()->addWidget(m_coordLabel);
    statusBar()->addWidget(createStatusSeparator());
    statusBar()->addPermanentWidget(m_gridCombo);
    statusBar()->addWidget(createStatusSeparator());
    statusBar()->addPermanentWidget(m_layerLabel);
    statusBar()->addWidget(createStatusSeparator());

    QPushButton* themeBtn = new QPushButton("🎨 Theme");
    themeBtn->setFlat(true);
    themeBtn->setCursor(Qt::PointingHandCursor);
    themeBtn->setStyleSheet("QPushButton { color: #a1a1aa; font-weight: bold; border: none; padding: 0 5px; } QPushButton:hover { color: white; }");
    connect(themeBtn, &QPushButton::clicked, this, []() {
        auto& tm = ThemeManager::instance();
        if (tm.currentTheme()->type() == PCBTheme::Engineering) tm.setTheme(PCBTheme::Dark);
        else if (tm.currentTheme()->type() == PCBTheme::Dark) tm.setTheme(PCBTheme::Light);
        else tm.setTheme(PCBTheme::Engineering);
    });
    statusBar()->addPermanentWidget(themeBtn);
}

QWidget* MainWindow::createStatusSeparator() {
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet("QFrame { color: #3f3f46; margin: 4px 0px; }");
    return separator;
}

void MainWindow::onToolSelected() {
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) return;
    QString toolName = action->data().toString();
    if (toolName.isEmpty()) return;
    m_view->setCurrentTool(toolName);
    
    // Update properties editor with tool settings if no selection
    if (m_scene->selectedItems().isEmpty()) {
        m_propertyEditor->setPCBTool(m_view->currentTool());
    }
    
    if (toolName == "Pad") statusBar()->showMessage("Click to place pads", 3000);
    else if (toolName == "Select") statusBar()->showMessage("Select and move items", 3000);
    else {
        auto& registry = PCBToolRegistry::instance();
        PCBTool* tool = registry.getTool(toolName);
        if (tool) statusBar()->showMessage(tool->tooltip(), 3000);
    }
}

void MainWindow::onNewProject() {
    if (!m_scene) return;
    
    // Check for unsaved changes (omitted for brevity, assume user confirmed)
    
    PCBRatsnestManager::instance().clearRatsnest();
    m_scene->clear();
    m_undoStack->clear();
    m_currentFilePath.clear();
    setWindowTitle("Viora EDA - PCB Editor [untitled.pcb]");
    statusBar()->showMessage("New PCB Project Created", 5000);
}

bool MainWindow::openFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;
    
    if (PCBFileIO::loadPCB(m_scene, filePath)) {
        m_currentFilePath = filePath;
        setWindowTitle("Viora EDA - PCB Editor [" + QFileInfo(filePath).fileName() + "]");
        statusBar()->showMessage("Loaded PCB: " + filePath, 5000);
        
        return true;
    } else {
        statusBar()->showMessage("Error loading PCB: " + PCBFileIO::lastError(), 5000);
        return false;
    }
}

void MainWindow::setProjectContext(const QString& projectName, const QString& projectDir) {
    m_projectName = projectName;
    m_projectDir = projectDir;
    
    // Auto-derive file path from project if not set
    if (!projectName.isEmpty() && !projectDir.isEmpty() && m_currentFilePath.isEmpty()) {
        QString derivedPath = projectDir + "/" + projectName + ".pcb";
        m_currentFilePath = derivedPath;
        setWindowTitle(QString("Viora EDA - PCB Editor [%1.pcb]").arg(projectName));
        
        // Auto-load if file exists
        if (QFile::exists(m_currentFilePath)) {
            openFile(m_currentFilePath);
        }
    }
}

void MainWindow::onOpenProject() {
    QString filePath = QFileDialog::getOpenFileName(this, "Open PCB", "", 
        "Viora EDA PCB (*.pcb);;KiCad PCB (*.kicad_pcb);;Altium PCB (*.PcbDoc);;All Files (*)");
    if (!filePath.isEmpty()) {
        openFile(filePath);
    }
}

void MainWindow::onSaveProject() {
    // If we have a project context but no file path yet, derive it
    if (m_currentFilePath.isEmpty() && !m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        m_currentFilePath = m_projectDir + "/" + m_projectName + ".pcb";
    }
    
    if (m_currentFilePath.isEmpty()) {
        onSaveProjectAs();
        return;
    }
    
    if (PCBFileIO::savePCB(m_scene, m_currentFilePath)) {
        setWindowTitle("Viora EDA - PCB Editor [" + QFileInfo(m_currentFilePath).fileName() + "]");
        statusBar()->showMessage("Saved PCB: " + m_currentFilePath, 5000);
    } else {
        statusBar()->showMessage("Error saving PCB: " + PCBFileIO::lastError(), 5000);
    }
}

void MainWindow::onSaveProjectAs() {
    // Default to project-derived name if available
    QString defaultPath;
    if (!m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        defaultPath = m_projectDir + "/" + m_projectName + ".pcb";
    } else if (!m_currentFilePath.isEmpty()) {
        defaultPath = m_currentFilePath;
    } else {
        defaultPath = "untitled.pcb";
    }

    QString filePath = QFileDialog::getSaveFileName(this, "Save PCB As", defaultPath, 
        "Viora EDA PCB (*.pcb);;KiCad PCB (*.kicad_pcb);;Altium PCB (*.PcbDoc);;All Files (*)");
        
    if (!filePath.isEmpty()) {
        // Ensure extension
        QFileInfo fi(filePath);
        if (fi.suffix().isEmpty()) filePath += ".pcb";
        
        m_currentFilePath = filePath;
        onSaveProject(); // Call save logic
    }
}
void MainWindow::onZoomIn() {
    if (m_view) m_view->scale(1.2, 1.2);
}
void MainWindow::onZoomOut() {
    if (m_view) m_view->scale(1/1.2, 1/1.2);
}
void MainWindow::onZoomFit() {
    if (!m_view || !m_scene) return;
    QRectF bounds = m_scene->itemsBoundingRect();
    if (!bounds.isValid()) return;
    QRectF fitRect = bounds.adjusted(-50, -50, 50, 50);
    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });
}

void MainWindow::onZoomAllComponents() {
    if (!m_scene || !m_view) return;

    QRectF rect;
    int count = 0;
    for (auto* item : m_scene->items()) {
        if (item->type() == PCBItem::ComponentType) {
            rect = rect.united(item->sceneBoundingRect());
            count++;
        }
    }

    if (!rect.isValid() || count == 0) {
        statusBar()->showMessage("No components found to zoom to.", 2000);
        return;
    }

    // Add comfortable margin
    QRectF fitRect = rect.adjusted(-10, -10, 10, 10); // PCB units are mm usually

    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });

    statusBar()->showMessage(QString("Zoomed to %1 components").arg(count), 2000);
}

void MainWindow::onZoomSelection() {
    if (!m_view || !m_scene) return;

    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        statusBar()->showMessage("No items selected to zoom to.", 2000);
        return;
    }

    QRectF rect;
    for (auto* item : selected) {
        rect = rect.united(item->sceneBoundingRect());
    }

    if (!rect.isValid()) return;

    const qreal minSize = 50.0; // PCB uses mm-scale units
    if (rect.width() < minSize) {
        qreal d = (minSize - rect.width()) / 2.0;
        rect.adjust(-d, 0, d, 0);
    }
    if (rect.height() < minSize) {
        qreal d = (minSize - rect.height()) / 2.0;
        rect.adjust(0, -d, 0, d);
    }

    QRectF fitRect = rect.adjusted(-15, -15, 15, 15);
    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });

    statusBar()->showMessage(
        QString("Zoomed to %1 selected item(s)").arg(selected.size()), 2000);
}

void MainWindow::onActiveLayerChanged(int layerId) {
    if (m_layerLabel) {
        QString colorStr = (layerId == 0) ? "#ef4444" : "#3b82f6";
        m_layerLabel->setText(QString("⚡ Layer: %1").arg(layerId == 0 ? "Top Copper" : "Bottom Copper"));
        m_layerLabel->setStyleSheet(QString("QLabel { padding: 4px 12px; color: %1; }").arg(colorStr));
    }
    
    // Sync active tool with the newly selected layer
    if (m_view && m_view->currentTool()) {
        m_view->currentTool()->setToolProperty("Active Layer", layerId);
    }
}

void MainWindow::updateCoordinates(QPointF pos) {
    if (m_coordLabel)
        m_coordLabel->setText(QString("📍 X: %1mm  Y: %2mm").arg(pos.x(), 0, 'f', 2).arg(pos.y(), 0, 'f', 2));
}

void MainWindow::onRunDRC() {
    if (m_drcDock) {
        m_drcDock->show();
        m_drcDock->raise();
    }
    if (m_drcPanel) m_drcPanel->runCheck();
}

void MainWindow::onRunCourtyardValidation() {
    if (!m_scene) return;

    QList<ComponentItem*> comps;
    for (QGraphicsItem* item : m_scene->items()) {
        if (ComponentItem* c = dynamic_cast<ComponentItem*>(item)) {
            if (c->isVisible()) comps.append(c);
        }
    }

    QList<ComponentItem*> offenders;
    int overlapPairs = 0;
    for (int i = 0; i < comps.size(); ++i) {
        for (int j = i + 1; j < comps.size(); ++j) {
            ComponentItem* a = comps[i];
            ComponentItem* b = comps[j];
            if (a->layer() != b->layer()) continue; // opposite side allowed
            if (!a->sceneBoundingRect().intersects(b->sceneBoundingRect())) continue;
            overlapPairs++;
            if (!offenders.contains(a)) offenders.append(a);
            if (!offenders.contains(b)) offenders.append(b);
        }
    }

    m_scene->clearSelection();
    for (ComponentItem* c : offenders) c->setSelected(true);

    if (overlapPairs == 0) {
        statusBar()->showMessage("Courtyard Validation: no overlaps found.", 3000);
        return;
    }

    if (m_view && !offenders.isEmpty()) {
        m_view->centerOn(offenders.first());
    }
    statusBar()->showMessage(
        QString("Courtyard Validation: %1 overlap pair(s), %2 component(s) selected.")
            .arg(overlapPairs)
            .arg(offenders.size()),
        5000);
}

void MainWindow::onCreateLinearArray() {
    if (!m_scene) return;

    QList<PCBItem*> seeds;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (PCBItem* p = dynamic_cast<PCBItem*>(item)) {
            if (dynamic_cast<PCBItem*>(p->parentItem()) == nullptr) {
                seeds.append(p);
            }
        }
    }
    if (seeds.isEmpty()) {
        statusBar()->showMessage("Linear Array: select at least one top-level item.", 3000);
        return;
    }

    bool ok = false;
    int copies = QInputDialog::getInt(this, "Linear Array", "Number of copies:", 3, 1, 1000, 1, &ok);
    if (!ok || copies <= 0) return;

    double dx = QInputDialog::getDouble(this, "Linear Array", "Step X (mm):", 5.0, -10000.0, 10000.0, 3, &ok);
    if (!ok) return;
    double dy = QInputDialog::getDouble(this, "Linear Array", "Step Y (mm):", 0.0, -10000.0, 10000.0, 3, &ok);
    if (!ok) return;

    QList<PCBItem*> newItems;
    newItems.reserve(seeds.size() * copies);

    for (int i = 1; i <= copies; ++i) {
        const QPointF offset(dx * i, dy * i);
        for (PCBItem* seed : seeds) {
            PCBItem* clone = seed->clone();
            if (!clone) continue;
            clone->setPos(seed->pos() + offset);
            newItems.append(clone);
        }
    }

    if (newItems.isEmpty()) {
        statusBar()->showMessage("Linear Array: no items created.", 3000);
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(new PCBAddItemsCommand(m_scene, newItems));
    } else {
        for (PCBItem* item : newItems) m_scene->addItem(item);
    }
    statusBar()->showMessage(QString("Linear Array: created %1 item(s).").arg(newItems.size()), 4000);
}

void MainWindow::onCreateCircularArray() {
    if (!m_scene) return;

    QList<PCBItem*> seeds;
    for (QGraphicsItem* item : m_scene->selectedItems()) {
        if (PCBItem* p = dynamic_cast<PCBItem*>(item)) {
            if (dynamic_cast<PCBItem*>(p->parentItem()) == nullptr) {
                seeds.append(p);
            }
        }
    }
    if (seeds.isEmpty()) {
        statusBar()->showMessage("Circular Array: select at least one top-level item.", 3000);
        return;
    }

    bool ok = false;
    int copies = QInputDialog::getInt(this, "Circular Array", "Number of copies:", 6, 1, 1000, 1, &ok);
    if (!ok || copies <= 0) return;

    double stepDeg = QInputDialog::getDouble(this, "Circular Array", "Angle step (deg):", 30.0, -360.0, 360.0, 3, &ok);
    if (!ok) return;

    // Default center at selected items' bounding center.
    QRectF selBounds;
    for (PCBItem* s : seeds) selBounds = selBounds.united(s->sceneBoundingRect());
    QPointF center = selBounds.center();

    double cx = QInputDialog::getDouble(this, "Circular Array", "Center X (mm):", center.x(), -100000.0, 100000.0, 3, &ok);
    if (!ok) return;
    double cy = QInputDialog::getDouble(this, "Circular Array", "Center Y (mm):", center.y(), -100000.0, 100000.0, 3, &ok);
    if (!ok) return;
    center = QPointF(cx, cy);

    bool rotateWithArray = (QMessageBox::question(
        this, "Circular Array",
        "Rotate copied items by the same array angle?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);

    QList<PCBItem*> newItems;
    newItems.reserve(seeds.size() * copies);

    for (int i = 1; i <= copies; ++i) {
        const double angleDeg = stepDeg * i;
        const double rad = angleDeg * M_PI / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);

        for (PCBItem* seed : seeds) {
            PCBItem* clone = seed->clone();
            if (!clone) continue;

            QPointF v = seed->pos() - center;
            QPointF vr(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
            clone->setPos(center + vr);

            if (rotateWithArray) {
                clone->setRotation(seed->rotation() + angleDeg);
            }

            newItems.append(clone);
        }
    }

    if (newItems.isEmpty()) {
        statusBar()->showMessage("Circular Array: no items created.", 3000);
        return;
    }

    if (m_undoStack) {
        m_undoStack->push(new PCBAddItemsCommand(m_scene, newItems));
    } else {
        for (PCBItem* item : newItems) m_scene->addItem(item);
    }
    statusBar()->showMessage(QString("Circular Array: created %1 item(s).").arg(newItems.size()), 4000);
}

void MainWindow::onPanelizeBoard() {
    if (!m_scene) return;

    QList<PCBItem*> seeds;
    QRectF boardBounds;

    for (QGraphicsItem* item : m_scene->items()) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem) continue;
        if (dynamic_cast<PCBItem*>(pcbItem->parentItem()) != nullptr) continue;
        if (!pcbItem->isVisible()) continue;
        if (dynamic_cast<RatsnestItem*>(pcbItem) != nullptr) continue;

        seeds.append(pcbItem);
        boardBounds = boardBounds.united(pcbItem->sceneBoundingRect());
    }

    if (seeds.isEmpty()) {
        statusBar()->showMessage("Panelize Board: nothing to panelize.", 3000);
        return;
    }
    if (!boardBounds.isValid() || boardBounds.isEmpty()) {
        statusBar()->showMessage("Panelize Board: invalid board bounds.", 3000);
        return;
    }

    bool ok = false;
    int rows = QInputDialog::getInt(this, "Panelize Board", "Rows:", 2, 1, 100, 1, &ok);
    if (!ok) return;
    int cols = QInputDialog::getInt(this, "Panelize Board", "Columns:", 2, 1, 100, 1, &ok);
    if (!ok) return;
    if (rows == 1 && cols == 1) {
        statusBar()->showMessage("Panelize Board: rows/columns are both 1; nothing to create.", 3000);
        return;
    }

    const double defaultSpacing = 5.0;
    double spacingX = QInputDialog::getDouble(
        this, "Panelize Board", "Spacing X (mm):", defaultSpacing, 0.0, 100000.0, 3, &ok);
    if (!ok) return;
    double spacingY = QInputDialog::getDouble(
        this, "Panelize Board", "Spacing Y (mm):", defaultSpacing, 0.0, 100000.0, 3, &ok);
    if (!ok) return;

    const double pitchX = std::max(0.001, boardBounds.width() + spacingX);
    const double pitchY = std::max(0.001, boardBounds.height() + spacingY);

    const bool addRails = (QMessageBox::question(
        this, "Panelize Board",
        "Add panel frame rails (edge-cuts frame)?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);

    double railWidth = 5.0;
    if (addRails) {
        railWidth = QInputDialog::getDouble(
            this, "Panelize Board", "Rail width (mm):", 5.0, 0.5, 100000.0, 3, &ok);
        if (!ok) return;
    }

    const bool addToolingHoles = (QMessageBox::question(
        this, "Panelize Board",
        "Add 4 tooling holes?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    double toolingDiameter = 3.0;
    double toolingDrill = 2.0;
    if (addToolingHoles) {
        toolingDiameter = QInputDialog::getDouble(
            this, "Panelize Board", "Tooling hole diameter (mm):", 3.0, 0.2, 100000.0, 3, &ok);
        if (!ok) return;
        toolingDrill = QInputDialog::getDouble(
            this, "Panelize Board", "Tooling hole drill (mm):", 2.0, 0.1, toolingDiameter, 3, &ok);
        if (!ok) return;
    }

    const bool addFiducials = (QMessageBox::question(
        this, "Panelize Board",
        "Add 3 global fiducials?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    double fidDiameter = 1.0;
    if (addFiducials) {
        fidDiameter = QInputDialog::getDouble(
            this, "Panelize Board", "Fiducial copper diameter (mm):", 1.0, 0.1, 100000.0, 3, &ok);
        if (!ok) return;
    }

    const bool addTabsWithMouseBites = (QMessageBox::question(
        this, "Panelize Board",
        "Add breakaway tabs with mouse-bite holes between boards?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    int tabsPerSeam = 2;
    double tabWidth = 4.0;
    double biteDiameter = 0.5;
    double biteDrill = 0.4;
    double bitePitch = 0.9;
    bool addRoutedTabCuts = true;
    if (addTabsWithMouseBites) {
        tabsPerSeam = QInputDialog::getInt(
            this, "Panelize Board", "Tabs per seam:", 2, 1, 20, 1, &ok);
        if (!ok) return;
        tabWidth = QInputDialog::getDouble(
            this, "Panelize Board", "Tab width (mm):", 4.0, 0.5, 100000.0, 3, &ok);
        if (!ok) return;
        biteDiameter = QInputDialog::getDouble(
            this, "Panelize Board", "Mouse-bite hole diameter (mm):", 0.5, 0.1, 10.0, 3, &ok);
        if (!ok) return;
        biteDrill = QInputDialog::getDouble(
            this, "Panelize Board", "Mouse-bite drill (mm):", 0.4, 0.05, biteDiameter, 3, &ok);
        if (!ok) return;
        bitePitch = QInputDialog::getDouble(
            this, "Panelize Board", "Mouse-bite hole pitch (mm):", 0.9, 0.1, 50.0, 3, &ok);
        if (!ok) return;
        addRoutedTabCuts = (QMessageBox::question(
            this, "Panelize Board",
            "Add routed seam cuts with tab bridges?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes);
    }

    QList<PCBItem*> newItems;
    newItems.reserve(seeds.size() * (rows * cols - 1) + 1024);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (r == 0 && c == 0) continue; // Keep original board as first panel slot.

            const QPointF offset(c * pitchX, r * pitchY);
            for (PCBItem* seed : seeds) {
                PCBItem* clone = seed->clone();
                if (!clone) continue;
                clone->setPos(seed->pos() + offset);
                newItems.append(clone);
            }
        }
    }

    if (newItems.isEmpty()) {
        statusBar()->showMessage("Panelize Board: no copies created.", 3000);
        return;
    }

    QRectF panelBounds = boardBounds.united(
        boardBounds.translated((cols - 1) * pitchX, (rows - 1) * pitchY));
    QRectF frameBounds = addRails
        ? panelBounds.adjusted(-railWidth, -railWidth, railWidth, railWidth)
        : panelBounds;

    if (addRails) {
        const double edgeWidth = 0.2;
        TraceItem* top = new TraceItem(frameBounds.topLeft(), frameBounds.topRight(), edgeWidth);
        TraceItem* right = new TraceItem(frameBounds.topRight(), frameBounds.bottomRight(), edgeWidth);
        TraceItem* bottom = new TraceItem(frameBounds.bottomRight(), frameBounds.bottomLeft(), edgeWidth);
        TraceItem* left = new TraceItem(frameBounds.bottomLeft(), frameBounds.topLeft(), edgeWidth);
        top->setLayer(PCBLayerManager::EdgeCuts);
        right->setLayer(PCBLayerManager::EdgeCuts);
        bottom->setLayer(PCBLayerManager::EdgeCuts);
        left->setLayer(PCBLayerManager::EdgeCuts);
        newItems.append(top);
        newItems.append(right);
        newItems.append(bottom);
        newItems.append(left);
    }

    if (addToolingHoles) {
        const double margin = std::max(2.0, railWidth * 0.5);
        const QPointF tl(frameBounds.left() + margin, frameBounds.top() + margin);
        const QPointF tr(frameBounds.right() - margin, frameBounds.top() + margin);
        const QPointF br(frameBounds.right() - margin, frameBounds.bottom() - margin);
        const QPointF bl(frameBounds.left() + margin, frameBounds.bottom() - margin);
        const QList<QPointF> corners = {tl, tr, br, bl};

        int idx = 1;
        for (const QPointF& p : corners) {
            ViaItem* hole = new ViaItem(p, toolingDiameter);
            hole->setName(QString("TOOL_%1").arg(idx++));
            hole->setDrillSize(toolingDrill);
            hole->setStartLayer(PCBLayerManager::TopCopper);
            hole->setEndLayer(PCBLayerManager::BottomCopper);
            hole->setLayer(PCBLayerManager::TopCopper);
            hole->setNetName(QString());
            newItems.append(hole);
        }
    }

    if (addFiducials) {
        const double margin = std::max(4.0, railWidth);
        const QPointF tl(frameBounds.left() + margin, frameBounds.top() + margin);
        const QPointF tr(frameBounds.right() - margin, frameBounds.top() + margin);
        const QPointF bl(frameBounds.left() + margin, frameBounds.bottom() - margin);
        const QList<QPointF> fidPts = {tl, tr, bl};

        int idx = 1;
        for (const QPointF& p : fidPts) {
            PadItem* fid = new PadItem(p, fidDiameter);
            fid->setName(QString("FID_%1").arg(idx++));
            fid->setPadShape("Round");
            fid->setLayer(PCBLayerManager::TopCopper);
            fid->setNetName(QString());
            newItems.append(fid);
        }
    }

    int mouseBiteCount = 0;
    int routedCutCount = 0;
    if (addTabsWithMouseBites && (rows > 1 || cols > 1)) {
        auto tabCentersAlongSpan = [&](double start, double length) {
            QList<double> centers;
            for (int t = 0; t < tabsPerSeam; ++t) {
                const double frac = static_cast<double>(t + 1) / static_cast<double>(tabsPerSeam + 1);
                centers.append(start + frac * length);
            }
            return centers;
        };

        auto addMouseBiteChain = [&](const QPointF& center, bool alongX) {
            const int holeCount = std::max(2, static_cast<int>(std::floor(tabWidth / bitePitch)) + 1);
            const double step = (holeCount > 1) ? (tabWidth / static_cast<double>(holeCount - 1)) : 0.0;
            const double start = -tabWidth * 0.5;

            for (int i = 0; i < holeCount; ++i) {
                QPointF p = center;
                const double d = start + i * step;
                if (alongX) {
                    p.setX(center.x() + d);
                } else {
                    p.setY(center.y() + d);
                }

                ViaItem* bite = new ViaItem(p, biteDiameter);
                bite->setName(QString("MB_%1").arg(mouseBiteCount + 1));
                bite->setDrillSize(biteDrill);
                bite->setLayer(PCBLayerManager::Drills);
                bite->setStartLayer(PCBLayerManager::TopCopper);
                bite->setEndLayer(PCBLayerManager::BottomCopper);
                bite->setNetName(QString());
                newItems.append(bite);
                ++mouseBiteCount;
            }
        };

        auto addRoutedSeamSegments = [&](double fixedCoord, double spanStart, double spanLen, bool alongX) {
            if (!addRoutedTabCuts) return;
            const double minSegLen = 0.2;
            QList<QPair<double, double>> blocked;
            const QList<double> centers = tabCentersAlongSpan(spanStart, spanLen);
            for (double c : centers) {
                blocked.append({c - tabWidth * 0.5, c + tabWidth * 0.5});
            }
            std::sort(blocked.begin(), blocked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            double cursor = spanStart;
            const double spanEnd = spanStart + spanLen;
            for (const auto& interval : blocked) {
                const double a = std::max(spanStart, interval.first);
                const double b = std::min(spanEnd, interval.second);
                if (a > cursor && (a - cursor) >= minSegLen) {
                    TraceItem* seg = alongX
                        ? new TraceItem(QPointF(cursor, fixedCoord), QPointF(a, fixedCoord), 0.2)
                        : new TraceItem(QPointF(fixedCoord, cursor), QPointF(fixedCoord, a), 0.2);
                    seg->setLayer(PCBLayerManager::EdgeCuts);
                    newItems.append(seg);
                    ++routedCutCount;
                }
                cursor = std::max(cursor, b);
            }
            if (spanEnd > cursor && (spanEnd - cursor) >= minSegLen) {
                TraceItem* seg = alongX
                    ? new TraceItem(QPointF(cursor, fixedCoord), QPointF(spanEnd, fixedCoord), 0.2)
                    : new TraceItem(QPointF(fixedCoord, cursor), QPointF(fixedCoord, spanEnd), 0.2);
                seg->setLayer(PCBLayerManager::EdgeCuts);
                newItems.append(seg);
                ++routedCutCount;
            }
        };

        // Vertical seams (between columns): chains run along Y at multiple tab Y positions.
        if (cols > 1) {
            for (int r = 0; r < rows; ++r) {
                const double rowTop = boardBounds.top() + r * pitchY;
                const QList<double> yTabs = tabCentersAlongSpan(rowTop, boardBounds.height());
                for (int c = 1; c < cols; ++c) {
                    const double seamX = boardBounds.right() + (c - 1) * pitchX + spacingX * 0.5;
                    for (double cy : yTabs) {
                        addMouseBiteChain(QPointF(seamX, cy), false);
                    }
                    addRoutedSeamSegments(seamX, rowTop, boardBounds.height(), false);
                }
            }
        }

        // Horizontal seams (between rows): chains run along X at multiple tab X positions.
        if (rows > 1) {
            for (int c = 0; c < cols; ++c) {
                const double colLeft = boardBounds.left() + c * pitchX;
                const QList<double> xTabs = tabCentersAlongSpan(colLeft, boardBounds.width());
                for (int r = 1; r < rows; ++r) {
                    const double seamY = boardBounds.bottom() + (r - 1) * pitchY + spacingY * 0.5;
                    for (double cx : xTabs) {
                        addMouseBiteChain(QPointF(cx, seamY), true);
                    }
                    addRoutedSeamSegments(seamY, colLeft, boardBounds.width(), true);
                }
            }
        }
    }

    if (m_undoStack) {
        m_undoStack->push(new PCBAddItemsCommand(m_scene, newItems));
    } else {
        for (PCBItem* item : newItems) m_scene->addItem(item);
    }

    statusBar()->showMessage(
        QString("Panelize Board: created %1 board copy/copies (%2 new item(s), mouse-bites: %3, routed cuts: %4).")
            .arg(rows * cols - 1)
            .arg(newItems.size())
            .arg(mouseBiteCount)
            .arg(routedCutCount),
        5000);
}

void MainWindow::onDRCViolationSelected(QPointF location) {
    if (m_view) m_view->centerOn(location);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_undoStack->index() != 0) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Changes?",
            "The PCB layout has unsaved changes. Do you want to save before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Save) {
            onSaveProject();
            event->accept();
        } else if (reply == QMessageBox::Discard) {
            event->accept();
        } else {
            event->ignore();
            return;
        }
    }

    // Save UI State
    ConfigManager::instance().saveWindowState("PCBEditor", saveGeometry(), saveState());
    
    event->accept();
}

void MainWindow::onBoardSetup() {
    BoardSetupDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_layerPanel) m_layerPanel->refreshLayers();
        PCBRatsnestManager::instance().update();
        statusBar()->showMessage("Board stackup updated.", 3000);
    }
}

void MainWindow::onGenerateGerbers() {
    GerberExportDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString outDir = dlg.outputDirectory();
        GerberExportSettings settings;
        settings.outputDirectory = outDir;

        int successCount = 0;
        for (int layerId : dlg.selectedLayers()) {
            PCBLayer* layer = PCBLayerManager::instance().layer(layerId);
            if (!layer) continue;

            QString safeName = layer->name().replace(" ", "_");
            QString filePath = outDir + "/" + safeName + ".gbr";
            
            if (GerberExporter::exportLayer(m_scene, layerId, filePath, settings)) {
                successCount++;
            }
        }

        // Generate Drill file
        QString drillPath = outDir + "/Drills.drl";
        GerberExporter::generateDrillFile(m_scene, drillPath);

        QMessageBox::information(this, "Export Complete", 
            QString("Successfully generated %1 Gerber files and 1 Drill file in:\n%2").arg(successCount).arg(outDir));
    }
}

void MainWindow::onExportPDF() {
    QString path = QFileDialog::getSaveFileName(this, "Export PDF", "Board.pdf", "PDF Files (*.pdf)");
    if (path.isEmpty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QRectF source = m_scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    QRectF target = painter.viewport();
    m_scene->render(&painter, target, source, Qt::KeepAspectRatio);
    painter.end();
    statusBar()->showMessage("Exported PDF to " + path, 3000);
}

void MainWindow::onExportSVG() {
    QString path = QFileDialog::getSaveFileName(this, "Export SVG", "Board.svg", "SVG Files (*.svg)");
    if (path.isEmpty()) return;

    QRectF rect = m_scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);

    QSvgGenerator generator;
    generator.setFileName(path);
    generator.setSize(rect.size().toSize());
    generator.setViewBox(rect);
    generator.setTitle("Viora EDA Export");

    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, rect, rect);
    painter.end();
    statusBar()->showMessage("Exported SVG to " + path, 3000);
}

void MainWindow::onExportImage() {
    QString path = QFileDialog::getSaveFileName(this, "Export Image", "Board.png", "Images (*.png *.jpg)");
    if (path.isEmpty()) return;

    QRectF rect = m_scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    qreal scale = 4.0;
    QImage image(rect.size().toSize() * scale, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    painter.translate(-rect.topLeft());
    m_scene->render(&painter, rect, rect);
    painter.end();

    image.save(path);
    statusBar()->showMessage("Exported Image to " + path, 3000);
}

void MainWindow::onExportAssemblyDrawing() {
    QString path = QFileDialog::getSaveFileName(this, "Export Assembly Drawing", "Assembly_Drawing.pdf", "PDF Files (*.pdf)");
    if (path.isEmpty()) return;

    QRectF boardRect = m_scene->itemsBoundingRect().adjusted(-10, -10, 10, 10);
    if (!boardRect.isValid() || boardRect.isEmpty()) {
        statusBar()->showMessage("Assembly export failed: empty board.", 3000);
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);

    QPainter p(&printer);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(p.viewport(), Qt::white);

    const QRectF target = p.viewport().adjusted(30, 30, -30, -30);
    const double sx = target.width() / boardRect.width();
    const double sy = target.height() / boardRect.height();
    const double s = std::min(sx, sy);

    p.translate(target.center());
    p.scale(s, s);
    p.translate(-boardRect.center());

    // Board frame.
    p.setPen(QPen(QColor(40, 40, 40), 0.15));
    p.setBrush(Qt::NoBrush);
    p.drawRect(boardRect);

    // Components: outline + reference designator.
    QFont labelFont("Arial", 6);
    p.setFont(labelFont);
    p.setPen(QPen(Qt::black, 0.12));
    for (QGraphicsItem* item : m_scene->items()) {
        ComponentItem* comp = dynamic_cast<ComponentItem*>(item);
        if (!comp) continue;
        QRectF r = comp->mapRectToScene(comp->boundingRect());
        p.drawRect(r);
        const QString ref = comp->name().isEmpty() ? QString("U?") : comp->name();
        p.drawText(r.center() + QPointF(0.4, 0.4), ref);
    }

    p.end();
    statusBar()->showMessage("Exported assembly drawing to " + path, 4000);
}

void MainWindow::onExportIPC2581() {
    QString path = QFileDialog::getSaveFileName(this, "Export IPC-2581", "Board.ipc2581.xml", "IPC-2581 XML (*.xml)");
    if (path.isEmpty()) return;
    QString err;
    if (!ManufacturingExporter::exportIPC2581(m_scene, path, &err)) {
        QMessageBox::warning(this, "IPC-2581 Export Failed", err.isEmpty() ? "Failed to export IPC-2581." : err);
        return;
    }
    statusBar()->showMessage("Exported IPC-2581 to " + path, 4000);
}

void MainWindow::onExportODBpp() {
    QString outDir = QFileDialog::getExistingDirectory(this, "Export ODB++ Package", QString());
    if (outDir.isEmpty()) return;
    QString err;
    if (!ManufacturingExporter::exportODBppPackage(m_scene, outDir, &err)) {
        QMessageBox::warning(this, "ODB++ Export Failed", err.isEmpty() ? "Failed to export ODB++ package." : err);
        return;
    }
    statusBar()->showMessage("Exported ODB++ package to " + outDir, 4000);
}

void MainWindow::onExportSTEP() {
    QString path = QFileDialog::getSaveFileName(this, "Export STEP", "Board.step", "STEP Files (*.step *.stp)");
    if (path.isEmpty()) return;
    QString err;
    if (!MCADExporter::exportSTEPWireframe(m_scene, path, &err)) {
        QMessageBox::warning(this, "STEP Export Failed", err.isEmpty() ? "Failed to export STEP." : err);
        return;
    }
    statusBar()->showMessage("Exported STEP to " + path, 3000);
}

void MainWindow::onExportIGES() {
    QString path = QFileDialog::getSaveFileName(this, "Export IGES", "Board.igs", "IGES Files (*.igs *.iges)");
    if (path.isEmpty()) return;
    QString err;
    if (!MCADExporter::exportIGESWireframe(m_scene, path, &err)) {
        QMessageBox::warning(this, "IGES Export Failed", err.isEmpty() ? "Failed to export IGES." : err);
        return;
    }
    statusBar()->showMessage("Exported IGES to " + path, 3000);
}

void MainWindow::onSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        auto& config = ConfigManager::instance();
        if (m_view) m_view->setSnapToGrid(config.snapToGrid());
        applyTheme();
        statusBar()->showMessage("Global settings applied.", 3000);
    }
}

void MainWindow::onViaStitching() {
    // 1. Find selected copper pour
    CopperPourItem* selectedPour = nullptr;
    for (auto* gItem : m_scene->selectedItems()) {
        if (auto* pour = dynamic_cast<CopperPourItem*>(gItem)) {
            selectedPour = pour;
            break;
        }
    }

    if (!selectedPour) {
        statusBar()->showMessage("Error: Please select a copper pour first.", 3000);
        return;
    }

    // 2. Open settings dialog
    ViaStitchingDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    // 3. Generate Grid
    QRectF bounds = selectedPour->polygon().boundingRect();
    QPolygonF poly = selectedPour->polygon();
    double stepX = dlg.gridSpacingX();
    double stepY = dlg.gridSpacingY();
    const QString netName = dlg.netName().trimmed().isEmpty() ? selectedPour->netName() : dlg.netName().trimmed();
    const int startLayer = dlg.startLayer();
    const int endLayer = dlg.endLayer();
    const bool microvia = dlg.microviaMode();
    
    QList<PCBItem*> newVias;
    PCBDRC drc;
    double minClearance = drc.rules().minClearance();

    m_undoStack->beginMacro("Via Stitching");

    for (double x = bounds.left() + stepX/2; x < bounds.right(); x += stepX) {
        for (double y = bounds.top() + stepY/2; y < bounds.bottom(); y += stepY) {
            QPointF pos(x, y);
            
            // Check if inside polygon
            if (!poly.containsPoint(pos, Qt::OddEvenFill)) continue;

            // Check DRC clearance
            ViaItem* tempVia = new ViaItem(pos, dlg.viaDiameter());
            tempVia->setDrillSize(dlg.viaDrill());
            tempVia->setNetName(netName);
            tempVia->setStartLayer(startLayer);
            tempVia->setEndLayer(endLayer);
            tempVia->setLayer(startLayer);
            tempVia->setMicrovia(microvia);

            // Skip duplicates on same location/net.
            bool duplicate = false;
            for (QGraphicsItem* existing : m_scene->items(QRectF(pos.x() - 0.05, pos.y() - 0.05, 0.1, 0.1))) {
                ViaItem* existingVia = dynamic_cast<ViaItem*>(existing);
                if (!existingVia) continue;
                if (QLineF(existingVia->scenePos(), pos).length() <= 0.05 &&
                    existingVia->netName() == netName &&
                    existingVia->startLayer() == startLayer &&
                    existingVia->endLayer() == endLayer &&
                    existingVia->isMicrovia() == microvia) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                delete tempVia;
                continue;
            }
            
            // Fast bounding box check for obstacles
            QRectF searchRect = tempVia->sceneBoundingRect().adjusted(-minClearance, -minClearance, minClearance, minClearance);
            bool collision = false;
            for (auto* obstacle : m_scene->items(searchRect)) {
                PCBItem* pItem = dynamic_cast<PCBItem*>(obstacle);
                if (!pItem || pItem == selectedPour) continue;
                
                QPointF dummy;
                if (drc.checkItemClearance(tempVia, pItem, minClearance, dummy)) {
                    collision = true;
                    break;
                }
            }

            if (!collision) {
                m_scene->addItem(tempVia);
                tempVia->setNetName(netName);
                newVias.append(tempVia);
            } else {
                delete tempVia;
            }
        }
    }

    if (!newVias.isEmpty()) {
        m_undoStack->push(new PCBAddItemsCommand(m_scene, newVias));
        statusBar()->showMessage(QString("Successfully generated %1 stitching vias.").arg(newVias.size()), 5000);
    } else {
        statusBar()->showMessage("No vias could be placed without DRC violations.", 3000);
    }

    m_undoStack->endMacro();
    PCBRatsnestManager::instance().update();
}

void MainWindow::onOpenFootprintEditor() {
    FootprintEditor* editor = new FootprintEditor(this);
    connect(editor, &FootprintEditor::footprintSaved, this, [this](const FootprintDefinition& def) {
        auto libs = FootprintLibraryManager::instance().libraries();
        if (!libs.isEmpty()) {
            libs.first()->saveFootprint(def);
        }
        if (m_componentsPanel) m_componentsPanel->populate();
    });
    editor->show();
}

void MainWindow::onOpenGerberViewer() {
    GerberViewerWindow* viewer = new GerberViewerWindow();
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
}

void MainWindow::onOpenGeminiAI() {
    GeminiDialog* dialog = new GeminiDialog(m_scene, this);
    dialog->show();
}

void MainWindow::handleIncomingECO() {
    if (SyncManager::instance().hasPendingECO()) {
        const SyncManager::ECOTarget target = SyncManager::instance().pendingECOTarget();
        if (target == SyncManager::ECOTarget::Schematic) return;
        ECOPackage pkg = SyncManager::instance().pendingECO();
        applyECO(pkg);
        SyncManager::instance().clearPendingECO();
    }
}

void MainWindow::applyECO(const ECOPackage& package) {
    if (!m_scene) return;

    statusBar()->showMessage("🔄 Applying ECO from Schematic...", 3000);
    auto& lib = FootprintLibraryManager::instance();
    auto resolveFootprintName = [&lib](const QString& rawName) -> QString {
        const QString trimmed = rawName.trimmed();
        if (trimmed.isEmpty()) return QString();
        if (lib.hasFootprint(trimmed)) return trimmed;
        if (trimmed.contains(':')) {
            const QString tail = trimmed.section(':', -1).trimmed();
            if (!tail.isEmpty() && lib.hasFootprint(tail)) return tail;
        }
        if (trimmed.contains('/')) {
            const QString tail = trimmed.section('/', -1).trimmed();
            if (!tail.isEmpty() && lib.hasFootprint(tail)) return tail;
        }
        return trimmed;
    };
    
    QMap<QString, ComponentItem*> existingComps;
    for (auto* item : m_scene->items()) {
        if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
            existingComps[comp->name()] = comp;
        }
    }

    int newCount = 0;
    QPointF gridStart(0, 0);
    int row = 0, col = 0;
    QList<ComponentItem*> newItems;
    QSet<QString> unresolvedFootprints;
    QMap<QString, QMap<QString, QString>> componentPinPadMappings;

    for (const auto& ecoComp : package.components) {
        componentPinPadMappings[ecoComp.reference] = ecoComp.pinPadMapping;
    }

    for (const auto& ecoComp : package.components) {
        if (existingComps.contains(ecoComp.reference)) {
            ComponentItem* existingComp = existingComps.value(ecoComp.reference);
            const QString newFootprint = resolveFootprintName(ecoComp.footprint);
            if (!newFootprint.isEmpty() && !lib.hasFootprint(newFootprint)) {
                unresolvedFootprints.insert(newFootprint);
            }
            if (!newFootprint.isEmpty() && existingComp->componentType() != newFootprint) {
                existingComp->setComponentType(newFootprint);
            }
            existingComp->setName(ecoComp.reference);
            existingComp->setValue(ecoComp.value);
        } else {
            // Space items by 20mm instead of 150mm
            QPointF pos = gridStart + QPointF(col * 20, row * 20);
            const QString newFootprint = resolveFootprintName(ecoComp.footprint);
            if (!newFootprint.isEmpty() && !lib.hasFootprint(newFootprint)) {
                unresolvedFootprints.insert(newFootprint);
            }
            ComponentItem* newComp = new ComponentItem(pos, newFootprint);
            newComp->setName(ecoComp.reference);
            newComp->setValue(ecoComp.value);
            m_scene->addItem(newComp);
            existingComps[ecoComp.reference] = newComp;
            newItems.append(newComp);
            newCount++;
            col++;
            if (col > 5) { col = 0; row++; }
        }
    }
    
    // Cache pads for deterministic pin->pad mapping and clear stale net assignments.
    QMap<ComponentItem*, QList<PadItem*>> componentPads;
    for (auto it = existingComps.begin(); it != existingComps.end(); ++it) {
        ComponentItem* c = it.value();
        QList<PadItem*> pads;
        for (QGraphicsItem* child : c->childItems()) {
            if (PadItem* p = dynamic_cast<PadItem*>(child)) {
                p->setNetName(QString());
                pads.append(p);
            }
        }
        std::sort(pads.begin(), pads.end(), [](PadItem* a, PadItem* b) {
            const bool aNamed = !a->name().trimmed().isEmpty();
            const bool bNamed = !b->name().trimmed().isEmpty();
            if (aNamed != bNamed) return aNamed; // Prefer named pads first

            bool aNumOk = false, bNumOk = false;
            int aNum = a->name().toInt(&aNumOk);
            int bNum = b->name().toInt(&bNumOk);
            if (aNumOk && bNumOk && aNum != bNum) return aNum < bNum;
            if (aNumOk != bNumOk) return aNumOk;

            if (!qFuzzyCompare(a->pos().x() + 1.0, b->pos().x() + 1.0)) {
                return a->pos().x() < b->pos().x();
            }
            return a->pos().y() < b->pos().y();
        });
        componentPads[c] = pads;
    }

    int netCount = 0;
    for (const auto& net : package.nets) {
        for (const auto& pin : net.pins) {
            if (!existingComps.contains(pin.componentRef)) continue;
            ComponentItem* c = existingComps[pin.componentRef];
            const QMap<QString, QString> pinPadMap = componentPinPadMappings.value(pin.componentRef);

            PadItem* targetPad = nullptr;
            const QList<PadItem*>& pads = componentPads[c];
            QString requestedPadName = pin.pinName.trimmed();
            if (pinPadMap.contains(pin.pinName) && !pinPadMap.value(pin.pinName).trimmed().isEmpty()) {
                requestedPadName = pinPadMap.value(pin.pinName).trimmed();
            }

            // 1) Strict named match first
            for (PadItem* p : pads) {
                if (p->name().trimmed() == requestedPadName) {
                    targetPad = p;
                    break;
                }
            }

            // 2) Fallback for unnamed footprints: numeric pin maps to sorted unnamed pad index
            if (!targetPad) {
                bool pinNumOk = false;
                int pinNum = requestedPadName.toInt(&pinNumOk);
                if (pinNumOk && pinNum > 0) {
                    QList<PadItem*> unnamedPads;
                    for (PadItem* p : pads) {
                        if (p->name().trimmed().isEmpty()) unnamedPads.append(p);
                    }
                    if (pinNum <= unnamedPads.size()) {
                        targetPad = unnamedPads[pinNum - 1];
                    }
                }
            }

            if (targetPad) {
                targetPad->setNetName(net.name);
            }
        }
        netCount++;
    }

    statusBar()->showMessage(QString("✅ ECO Applied: %1 new parts, %2 active nets").arg(newCount).arg(netCount), 5000);
    if (!unresolvedFootprints.isEmpty()) {
        QStringList unresolvedList;
        for (const QString& fp : unresolvedFootprints) unresolvedList.append(fp);
        unresolvedList.sort();
        statusBar()->showMessage(
            QString("ECO applied, but %1 footprint(s) were not found in PCB libraries: %2")
                .arg(unresolvedFootprints.size())
                .arg(unresolvedList.join(", ")),
            8000
        );
    }
    
    // Zoom to fit new items if any
    if (!newItems.isEmpty() && m_view) {
        m_view->centerOn(newItems.first());
    }

    PCBRatsnestManager::instance().update();
}

void MainWindow::onPropertyChanged(const QString& name, const QVariant& value) {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        // If no items are selected, check if we are editing tool properties
        if (m_view && m_view->currentTool()) {
            m_view->currentTool()->setToolProperty(name, value);
        }
        return;
    }
    
    m_undoStack->beginMacro(QString("Change PCB Property: %1").arg(name));

    for (QGraphicsItem* gItem : selected) {
        PCBItem* item = dynamic_cast<PCBItem*>(gItem);
        if (!item) continue;

        QVariant oldValue;
        bool found = false;

        if (name == "ID") { /* Read-only */ }
        else if (name == "Name") { oldValue = item->name(); found = true; }
        else if (name == "Net") { oldValue = item->netName(); found = true; }
        else if (name == "Layer") { oldValue = item->layer(); found = true; }
        else if (name == "Height (mm)") { oldValue = item->height(); found = true; }
        else if (name == "3D Model Path") { oldValue = item->modelPath(); found = true; }
        else if (name == "3D Model Scale") { oldValue = item->modelScale(); found = true; }
        else if (name == "Locked") { oldValue = item->isLocked(); found = true; }
        else if (name == "Width (mm)") {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                oldValue = trace->width();
                found = true;
            }
        }
        else if (name == "Start X (mm)" || name == "Start Y (mm)") {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                oldValue = (name == "Start X (mm)") ? trace->startPoint().x() : trace->startPoint().y();
                found = true;
            }
        }
        else if (name == "End X (mm)" || name == "End Y (mm)") {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                oldValue = (name == "End X (mm)") ? trace->endPoint().x() : trace->endPoint().y();
                found = true;
            }
        }
        else if (name == "Position X (mm)" || name == "Position Y (mm)") {
            oldValue = (name == "Position X (mm)") ? item->pos().x() : item->pos().y();
            found = true;
        }
        else if (name == "Rotation (deg)") {
            oldValue = item->rotation();
            found = true;
        }
        else if (selected.size() == 1) {
            if (name == "Pad Shape") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->padShape();
                    found = true;
                }
            }
            else if (name == "Size X (mm)" || name == "Size Y (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = (name == "Size X (mm)") ? pad->size().width() : pad->size().height();
                    found = true;
                }
            }
            else if (name == "Diameter (mm)") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->diameter();
                    found = true;
                }
            }
            else if (name == "Drill Size (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->drillSize();
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->drillSize();
                    found = true;
                }
            }
            else if (name == "Via Start Layer") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->startLayer();
                    found = true;
                }
            }
            else if (name == "Via End Layer") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->endLayer();
                    found = true;
                }
            }
            else if (name == "Microvia") {
                if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->isMicrovia();
                    found = true;
                }
            }
            else if (name == "Mask Expansion Mode") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->maskExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->maskExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                }
            }
            else if (name == "Mask Expansion (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->maskExpansion();
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->maskExpansion();
                    found = true;
                }
            }
            else if (name == "Paste Expansion Mode") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->pasteExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->pasteExpansionOverrideEnabled() ? "Custom" : "Board";
                    found = true;
                }
            }
            else if (name == "Paste Expansion (mm)") {
                if (PadItem* pad = dynamic_cast<PadItem*>(item)) {
                    oldValue = pad->pasteExpansion();
                    found = true;
                } else if (ViaItem* via = dynamic_cast<ViaItem*>(item)) {
                    oldValue = via->pasteExpansion();
                    found = true;
                }
            }
            else if (name == "Clearance (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->clearance();
                    found = true;
                }
            }
            else if (name == "Priority") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->priority();
                    found = true;
                }
            }
            else if (name == "Remove Islands") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->removeIslands();
                    found = true;
                }
            }
            else if (name == "Min Island Width (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->minWidth();
                    found = true;
                }
            }
            else if (name == "Filled") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->filled();
                    found = true;
                }
            }
            else if (name == "Pour Type") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = static_cast<int>(pour->pourType());
                    found = true;
                }
            }
            else if (name == "Hatch Width (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->hatchWidth();
                    found = true;
                }
            }
            else if (name == "Solid") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->isSolid();
                    found = true;
                }
            }
            else if (name == "Use Thermal Reliefs") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->useThermalReliefs();
                    found = true;
                }
            }
            else if (name == "Thermal Spoke Width (mm)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->thermalSpokeWidth();
                    found = true;
                }
            }
            else if (name == "Thermal Spoke Count") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->thermalSpokeCount();
                    found = true;
                }
            }
            else if (name == "Thermal Angle (deg)") {
                if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) {
                    oldValue = pour->thermalSpokeAngleDeg();
                    found = true;
                }
            }
        }

        if (found && oldValue != value) {
            if (m_undoStack) {
                m_undoStack->push(new PCBPropertyCommand(m_scene, item, name, oldValue, value));
            } else {
                // Manual fallback
                if (name == "Name") item->setName(value.toString());
                else if (name == "Net") item->setNetName(value.toString());
                else if (name == "Layer") item->setLayer(value.toInt());
                else if (name == "Height (mm)") item->setHeight(value.toDouble());
                else if (name == "3D Model Path") item->setModelPath(value.toString());
                else if (name == "3D Model Scale") item->setModelScale(value.toDouble());
                else if (name == "Locked") item->setLocked(value.toBool());
                else if (name == "Width (mm)") {
                    if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) trace->setWidth(value.toDouble());
                }
                else if (name == "Position X (mm)") item->setX(value.toDouble());
                else if (name == "Position Y (mm)") item->setY(value.toDouble());
                else if (name == "Rotation (deg)") item->setRotation(value.toDouble());
                else if (name == "Pad Shape") {
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setPadShape(value.toString());
                }
                else if (name == "Diameter (mm)") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setDiameter(value.toDouble());
                }
                else if (name == "Via Start Layer") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setStartLayer(value.toInt());
                }
                else if (name == "Via End Layer") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setEndLayer(value.toInt());
                }
                else if (name == "Microvia") {
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setMicrovia(value.toBool() || value.toString() == "True");
                }
                else if (name == "Mask Expansion Mode") {
                    const bool custom = value.toString().compare("Custom", Qt::CaseInsensitive) == 0;
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setMaskExpansionOverrideEnabled(custom);
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setMaskExpansionOverrideEnabled(custom);
                }
                else if (name == "Mask Expansion (mm)") {
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setMaskExpansion(value.toDouble());
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setMaskExpansion(value.toDouble());
                }
                else if (name == "Paste Expansion Mode") {
                    const bool custom = value.toString().compare("Custom", Qt::CaseInsensitive) == 0;
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setPasteExpansionOverrideEnabled(custom);
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setPasteExpansionOverrideEnabled(custom);
                }
                else if (name == "Paste Expansion (mm)") {
                    if (PadItem* pad = dynamic_cast<PadItem*>(item)) pad->setPasteExpansion(value.toDouble());
                    if (ViaItem* via = dynamic_cast<ViaItem*>(item)) via->setPasteExpansion(value.toDouble());
                }
                else if (name == "Use Thermal Reliefs") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setUseThermalReliefs(value.toBool() || value.toString() == "True");
                }
                else if (name == "Priority") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setPriority(qRound(value.toDouble()));
                }
                else if (name == "Remove Islands") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setRemoveIslands(value.toBool() || value.toString() == "True");
                }
                else if (name == "Min Island Width (mm)") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setMinWidth(value.toDouble());
                }
                else if (name == "Filled") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setFilled(value.toBool() || value.toString() == "True");
                }
                else if (name == "Thermal Spoke Width (mm)") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setThermalSpokeWidth(value.toDouble());
                }
                else if (name == "Thermal Spoke Count") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setThermalSpokeCount(value.toInt());
                }
                else if (name == "Thermal Angle (deg)") {
                    if (CopperPourItem* pour = dynamic_cast<CopperPourItem*>(item)) pour->setThermalSpokeAngleDeg(value.toDouble());
                }
            }
            
            // Sync tool default values with the last edited item properties
            if (m_view && m_view->currentTool()) {
                if (name == "Width (mm)") {
                    m_view->currentTool()->setToolProperty("Trace Width (mm)", value);
                    updateOptionsBar("Trace");
                } else if (name == "Layer") {
                    m_view->currentTool()->setToolProperty("Active Layer", value);
                    updateOptionsBar("Trace");
                }
            }
        }
        item->update();
    }

    m_undoStack->endMacro();
}

void MainWindow::onFilterChanged() {
    if (!m_scene || !m_selectionFilter) return;

    bool componentsEnabled = m_selectionFilter->isFilterEnabled("Symbols"); 
    bool tracesEnabled = m_selectionFilter->isFilterEnabled("Traces");
    bool padsEnabled = m_selectionFilter->isFilterEnabled("Pads/Vias");
    bool poursEnabled = m_selectionFilter->isFilterEnabled("Pours");
    bool ratsnestEnabled = m_selectionFilter->isFilterEnabled("Ratsnest");
    bool activeLayerOnly = m_selectionFilter->isFilterEnabled("Active Layer");
    const int activeLayer = PCBLayerManager::instance().activeLayerId();

    for (QGraphicsItem* item : m_scene->items()) {
        PCBItem* pItem = dynamic_cast<PCBItem*>(item);
        if (!pItem) continue;

        bool selectable = true;
        QString type = pItem->itemTypeName();

        if (type == "Component") selectable = componentsEnabled;
        else if (type == "Trace") selectable = tracesEnabled;
        else if (type == "Pad" || type == "Via") selectable = padsEnabled;
        else if (type == "CopperPour") selectable = poursEnabled;
        else if (type == "Ratsnest") selectable = ratsnestEnabled;

        if (selectable && activeLayerOnly) {
            if (ViaItem* via = dynamic_cast<ViaItem*>(pItem)) {
                selectable = via->spansLayer(activeLayer);
            } else if (type == "Ratsnest") {
                selectable = true; // airwires are net-level guides
            } else {
                selectable = (pItem->layer() == activeLayer);
            }
        }

        pItem->setFlag(QGraphicsItem::ItemIsSelectable, selectable);
        
        if (!selectable && pItem->isSelected()) {
            pItem->setSelected(false);
        }
    }
}


void MainWindow::onOpenCommandPalette() {
    CommandPalette* palette = new CommandPalette(this);
    palette->setPlaceholderText("Search footprints, nets, or run PCB commands...");

    // 1. Add all menu actions
    for (auto menu : menuBar()->findChildren<QMenu*>()) {
        for (auto action : menu->actions()) {
            if (!action->text().isEmpty() && !action->isSeparator()) {
                palette->addAction(action);
            }
        }
    }

    // 2. Add all items in the PCB
    for (auto gItem : m_scene->items()) {
        if (auto pItem = dynamic_cast<PCBItem*>(gItem)) {
            QString name = pItem->name();
            if (name.isEmpty() && dynamic_cast<ComponentItem*>(pItem)) {
                // ...
            }
            
            if (auto comp = dynamic_cast<ComponentItem*>(pItem)) {
                PaletteResult res;
                res.title = QString("%1 (%2)").arg(comp->name(), comp->componentType());
                res.description = QString("Footprint: %1 on Layer %2").arg(comp->componentType()).arg(comp->layer());
                res.icon = QIcon(":/icons/comp_ic.svg");
                res.action = [this, comp]() {
                    m_view->centerOn(comp);
                    m_scene->clearSelection();
                    comp->setSelected(true);
                };
                palette->addResult(res);
            } else if (auto trace = dynamic_cast<TraceItem*>(pItem)) {
                if (!trace->netName().isEmpty()) {
                    PaletteResult res;
                    res.title = QString("Net: %1").arg(trace->netName());
                    res.description = QString("Trace on Layer %1").arg(trace->layer());
                    res.icon = QIcon(":/icons/tool_trace.svg");
                    res.action = [this, trace]() {
                        m_view->centerOn(trace);
                        m_scene->clearSelection();
                        trace->setSelected(true);
                    };
                    palette->addResult(res);
                }
            }
        }
    }

    palette->show();
}

void MainWindow::onAlignLeft() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignLeft));
        statusBar()->showMessage("Aligned Left", 2000);
    }
}

void MainWindow::onAlignRight() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignRight));
        statusBar()->showMessage("Aligned Right", 2000);
    }
}

void MainWindow::onAlignTop() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignTop));
        statusBar()->showMessage("Aligned Top", 2000);
    }
}

void MainWindow::onAlignBottom() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignBottom));
        statusBar()->showMessage("Aligned Bottom", 2000);
    }
}

void MainWindow::onAlignCenterX() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignCenterX));
        statusBar()->showMessage("Aligned Center X", 2000);
    }
}

void MainWindow::onAlignCenterY() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 1) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::AlignCenterY));
        statusBar()->showMessage("Aligned Center Y", 2000);
    }
}

void MainWindow::onDistributeH() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 2) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::DistributeH));
        statusBar()->showMessage("Distributed Horizontally", 2000);
    }
}

void MainWindow::onDistributeV() {
    QList<PCBItem*> items;
    for (auto* it : m_scene->selectedItems()) {
        if (auto* pi = dynamic_cast<PCBItem*>(it)) items.append(pi);
    }
    if (items.size() > 2) {
        m_undoStack->push(new PCBAlignItemCommand(m_scene, items, PCBAlignItemCommand::DistributeV));
        statusBar()->showMessage("Distributed Vertically", 2000);
    }
}

void MainWindow::onRotate() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    // Use PCBCommands.cpp implementation
    // For now, simple rotation logic
    for (auto* item : selected) {
        item->setRotation(item->rotation() + 90);
    }
    statusBar()->showMessage("Rotated selected items", 2000);
}

void MainWindow::onMirror() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    for (auto* item : selected) {
        // Toggle horizontal flip using scale transformation
        item->setTransform(QTransform().scale(-1, 1), true);
    }
    statusBar()->showMessage("Mirrored selected items", 2000);
}

void MainWindow::onDeleteSelection() {
    if (!m_scene) return;

    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QSet<PCBItem*> itemsToDelete;
    for (QGraphicsItem* it : selected) {
        // Find the top-most selectable PCBItem
        PCBItem* pcbItem = nullptr;
        QGraphicsItem* current = it;
        while (current) {
            if (PCBItem* candidate = dynamic_cast<PCBItem*>(current)) {
                if (candidate->flags() & QGraphicsItem::ItemIsSelectable) {
                    pcbItem = candidate;
                }
            }
            current = current->parentItem();
        }

        if (pcbItem && !pcbItem->isLocked()) {
            itemsToDelete.insert(pcbItem);
        } else if (!pcbItem) {
            // If it's a raw shape not wrapped in PCBItem (rare but possible), 
            // we'll still try to handle it if it's selectable.
            if (it->flags() & QGraphicsItem::ItemIsSelectable) {
                m_scene->removeItem(it);
                delete it;
            }
        }
    }

    // Filter out items whose parents are also being deleted
    QList<PCBItem*> finalItems;
    for (PCBItem* item : itemsToDelete) {
        bool parentInSet = false;
        QGraphicsItem* p = item->parentItem();
        while (p) {
            if (PCBItem* pi = dynamic_cast<PCBItem*>(p)) {
                if (itemsToDelete.contains(pi)) {
                    parentInSet = true;
                    break;
                }
            }
            p = p->parentItem();
        }
        if (!parentInSet) {
            finalItems.append(item);
        }
    }

    if (finalItems.isEmpty()) return;

    m_scene->clearSelection();

    if (m_undoStack) {
        m_undoStack->push(new PCBRemoveItemCommand(m_scene, finalItems));
    } else {
        for (PCBItem* item : finalItems) {
            m_scene->removeItem(item);
            delete item;
        }
    }

    statusBar()->showMessage(QString("Deleted %1 item(s)").arg(finalItems.size()), 2000);
    m_view->update();
    if (m_propertyEditor) m_propertyEditor->clear();
}

void MainWindow::clearNetHighlighting() {
    if (!m_scene) return;
    for (QGraphicsItem* item : m_scene->items()) {
        if (dynamic_cast<PCBItem*>(item) || dynamic_cast<RatsnestItem*>(item)) {
            item->setOpacity(1.0);
        }
    }
    m_highlightedNet.clear();
}

void MainWindow::applyNetHighlighting() {
    if (!m_scene) return;

    QString targetNet;
    const QList<QGraphicsItem*> selected = m_scene->selectedItems();
    for (QGraphicsItem* gi : selected) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(gi);
        if (!pcb) continue;
        if (!pcb->netName().isEmpty() && pcb->netName() != "No Net") {
            targetNet = pcb->netName();
            break;
        }
        // Component-level fallback: infer net from pads.
        for (QGraphicsItem* child : pcb->childItems()) {
            if (PCBItem* cp = dynamic_cast<PCBItem*>(child)) {
                if (!cp->netName().isEmpty() && cp->netName() != "No Net") {
                    targetNet = cp->netName();
                    break;
                }
            }
        }
        if (!targetNet.isEmpty()) break;
    }

    if (targetNet.isEmpty()) {
        clearNetHighlighting();
        statusBar()->showMessage("Select an item with a net to highlight", 2000);
        return;
    }

    m_highlightedNet = targetNet;

    for (QGraphicsItem* gi : m_scene->items()) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(gi);
        RatsnestItem* air = dynamic_cast<RatsnestItem*>(gi);
        if (!pcb && !air) continue;

        bool match = false;
        if (pcb) {
            if (pcb->netName() == targetNet) {
                match = true;
            } else if (pcb->itemType() == PCBItem::ComponentType) {
                for (QGraphicsItem* child : pcb->childItems()) {
                    if (PCBItem* cp = dynamic_cast<PCBItem*>(child)) {
                        if (cp->netName() == targetNet) {
                            match = true;
                            break;
                        }
                    }
                }
            }
        }

        // RatsnestItem currently has no persistent netName field; keep visible while dimming others.
        if (air) match = true;

        gi->setOpacity(match ? 1.0 : 0.18);
    }

    statusBar()->showMessage(QString("Highlighting net: %1").arg(targetNet), 2000);
}

void MainWindow::onBringToFront() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    qreal maxZ = -999999;
    for (auto* item : m_scene->items()) maxZ = qMax(maxZ, item->zValue());
    
    for (auto* item : selected) item->setZValue(maxZ + 1);
}

void MainWindow::onSendToBack() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    qreal minZ = 999999;
    for (auto* item : m_scene->items()) minZ = qMin(minZ, item->zValue());
    
    for (auto* item : selected) item->setZValue(minZ - 1);
}

QIcon MainWindow::createPCBIcon(const QString& name) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QColor color = Qt::white;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->textColor();
    }
    QPen pen(color, 2);
    painter.setPen(pen);

    if (name == "Align Left") {
        painter.drawLine(4, 4, 4, 28);
        painter.drawRect(6, 8, 10, 4);
        painter.drawRect(6, 16, 20, 4);
    } else if (name == "Align Right") {
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(18, 8, 10, 4);
        painter.drawRect(8, 16, 20, 4);
    } else if (name == "Align Top") {
        painter.drawLine(4, 4, 28, 4);
        painter.drawRect(8, 6, 4, 10);
        painter.drawRect(16, 6, 4, 20);
    } else if (name == "Align Bottom") {
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(8, 18, 4, 10);
        painter.drawRect(16, 8, 4, 20);
    } else if (name == "Center X") {
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(16, 2, 16, 30);
        painter.setPen(pen);
        painter.drawRect(10, 8, 12, 4);
        painter.drawRect(6, 18, 20, 4);
    } else if (name == "Center Y") {
        painter.setPen(QPen(color, 1, Qt::DashLine));
        painter.drawLine(2, 16, 30, 16);
        painter.setPen(pen);
        painter.drawRect(8, 10, 4, 12);
        painter.drawRect(18, 6, 4, 20);
    } else if (name == "Distribute H") {
        painter.drawLine(4, 4, 4, 28);
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(10, 12, 4, 8);
        painter.drawRect(18, 12, 4, 8);
    } else if (name == "Distribute V") {
        painter.drawLine(4, 4, 28, 4);
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(12, 10, 8, 4);
        painter.drawRect(12, 18, 8, 4);
    } else {
        painter.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1));
    }

    return QIcon(pixmap);
}

void MainWindow::updateOptionsBar(const QString& toolName) {
    if (!m_optionsToolbar) return;
    
    m_optionsToolbar->clear();
    
    QLabel* title = new QLabel("<b>" + toolName.toUpper() + " SETTINGS:</b>  ");
    title->setStyleSheet("color: #ec4899; margin-right: 5px;"); // Pink accent for PCB
    m_optionsToolbar->addWidget(title);

    if (toolName == "Trace") {
        PCBTool* tool = m_view->currentTool();
        double currentWidth = tool->toolProperties().value("Trace Width (mm)", 0.25).toDouble();
        int currentLayer = tool->toolProperties().value("Active Layer", 0).toInt();

        m_optionsToolbar->addWidget(new QLabel("Width: "));
        QDoubleSpinBox* widthSpin = new QDoubleSpinBox();
        widthSpin->setRange(0.05, 10.0);
        widthSpin->setSingleStep(0.05);
        widthSpin->setValue(currentWidth);
        widthSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(widthSpin);
        
        connect(widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val){
            if (m_view && m_view->currentTool()) {
                m_view->currentTool()->setToolProperty("Trace Width (mm)", val);
                // If we have items selected, apply to them too
                if (!m_scene->selectedItems().isEmpty()) {
                    onPropertyChanged("Width (mm)", val);
                }
            }
        });
        
        m_optionsToolbar->addSeparator();
        
        m_optionsToolbar->addWidget(new QLabel("Layer: "));
        QComboBox* layerCombo = new QComboBox();
        layerCombo->addItems({"Top Copper", "Bottom Copper"});
        layerCombo->setCurrentIndex(currentLayer);
        m_optionsToolbar->addWidget(layerCombo);
        
        connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx){
            onActiveLayerChanged(idx);
            if (m_layerPanel) m_layerPanel->selectLayer(idx);
            // If we have items selected, apply to them too
            if (!m_scene->selectedItems().isEmpty()) {
                onPropertyChanged("Layer", idx);
            }
        });
        
        m_optionsToolbar->addSeparator();
        
        m_optionsToolbar->addWidget(new QLabel("Mode: "));
        QComboBox* modeCombo = new QComboBox();
        modeCombo->addItems({"45 Degree", "90 Degree", "Free"});
        m_optionsToolbar->addWidget(modeCombo);

        m_optionsToolbar->addSeparator();
        
        QCheckBox* shoveCheck = new QCheckBox("Shove Traces");
        bool isShoveEnabled = tool->toolProperties().value("Enable Shoving", true).toBool();
        shoveCheck->setChecked(isShoveEnabled);
        m_optionsToolbar->addWidget(shoveCheck);
        connect(shoveCheck, &QCheckBox::toggled, this, [tool](bool checked){
            tool->setToolProperty("Enable Shoving", checked);
        });
    } 
    else if (toolName == "Pad") {
        m_optionsToolbar->addWidget(new QLabel("Size X: "));
        QDoubleSpinBox* sx = new QDoubleSpinBox();
        sx->setRange(0.1, 10.0);
        sx->setValue(1.5);
        m_optionsToolbar->addWidget(sx);
        
        m_optionsToolbar->addWidget(new QLabel("Size Y: "));
        QDoubleSpinBox* sy = new QDoubleSpinBox();
        sy->setRange(0.1, 10.0);
        sy->setValue(1.5);
        m_optionsToolbar->addWidget(sy);
        
        m_optionsToolbar->addSeparator();
        
        m_optionsToolbar->addWidget(new QLabel("Shape: "));
        QComboBox* shape = new QComboBox();
        shape->addItems({"Rect", "Round", "Oval"});
        m_optionsToolbar->addWidget(shape);
    }
    else if (toolName == "Via") {
        m_optionsToolbar->addWidget(new QLabel("Diameter: "));
        QDoubleSpinBox* d = new QDoubleSpinBox();
        d->setRange(0.1, 5.0);
        d->setValue(0.6);
        m_optionsToolbar->addWidget(d);
        
        m_optionsToolbar->addWidget(new QLabel("Drill: "));
        QDoubleSpinBox* dr = new QDoubleSpinBox();
        dr->setRange(0.1, 5.0);
        dr->setValue(0.3);
        m_optionsToolbar->addWidget(dr);
    }
    else if (toolName == "Length Tuning") {
        PCBTool* tool = m_view->currentTool();
        double currentTarget = tool->toolProperties().value("Target Length (mm)", 50.0).toDouble();
        double currentAmp = tool->toolProperties().value("Amplitude (mm)", 2.0).toDouble();

        m_optionsToolbar->addWidget(new QLabel("Target: "));
        QDoubleSpinBox* targetSpin = new QDoubleSpinBox();
        targetSpin->setRange(1.0, 1000.0);
        targetSpin->setValue(currentTarget);
        targetSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(targetSpin);
        connect(targetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
            tool->setToolProperty("Target Length (mm)", val);
        });

        m_optionsToolbar->addSeparator();

        m_optionsToolbar->addWidget(new QLabel("Amplitude: "));
        QDoubleSpinBox* ampSpin = new QDoubleSpinBox();
        ampSpin->setRange(0.1, 20.0);
        ampSpin->setValue(currentAmp);
        ampSpin->setSuffix(" mm");
        m_optionsToolbar->addWidget(ampSpin);
        connect(ampSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
            tool->setToolProperty("Amplitude (mm)", val);
        });
    }
    else if (toolName == "Rectangle" || toolName == "Polygon Pour") {
        PCBTool* tool = m_view->currentTool();
        int currentLayer = tool->toolProperties().value("Active Layer", 0).toInt();

        m_optionsToolbar->addWidget(new QLabel("Layer: "));
        QComboBox* layerCombo = new QComboBox();
        
        // Add all available layers
        for (const auto& layer : PCBLayerManager::instance().layers()) {
            layerCombo->addItem(layer.name(), layer.id());
        }
        
        // Set current selection
        int initialIdx = layerCombo->findData(currentLayer);
        if (initialIdx != -1) layerCombo->setCurrentIndex(initialIdx);
        
        m_optionsToolbar->addWidget(layerCombo);
        
        connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, tool, layerCombo](int index){
            int layerId = layerCombo->itemData(index).toInt();
            tool->setToolProperty("Active Layer", layerId);
            
            // Also sync active layer global state if user expects it to follow
            onActiveLayerChanged(layerId);
            if (m_layerPanel) m_layerPanel->selectLayer(layerId);
        });

        if (toolName == "Polygon Pour") {
            m_optionsToolbar->addSeparator();
            m_optionsToolbar->addWidget(new QLabel("Net: "));
            QLineEdit* netEdit = new QLineEdit(tool->toolProperties().value("Net Name", "GND").toString());
            netEdit->setMaximumWidth(80);
            m_optionsToolbar->addWidget(netEdit);
            connect(netEdit, &QLineEdit::textChanged, this, [tool](const QString& text){
                tool->setToolProperty("Net Name", text);
            });

            m_optionsToolbar->addSeparator();
            m_optionsToolbar->addWidget(new QLabel("Clearance: "));
            QDoubleSpinBox* clearSpin = new QDoubleSpinBox();
            clearSpin->setRange(0.05, 5.0);
            clearSpin->setSingleStep(0.05);
            clearSpin->setValue(tool->toolProperties().value("Clearance (mm)", 0.3).toDouble());
            m_optionsToolbar->addWidget(clearSpin);
            connect(clearSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [tool](double val){
                tool->setToolProperty("Clearance (mm)", val);
            });
        }
    }
    else if (toolName == "Select") {
        m_optionsToolbar->addWidget(new QLabel("Select footprints and traces to edit properties."));
    }
    else {
        m_optionsToolbar->addWidget(new QLabel("Ready."));
    }

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_optionsToolbar->addWidget(spacer);
}

void MainWindow::onToggle3DView() {
    if (!m_3dWindow) {
        m_3dWindow = new PCB3DWindow(m_scene, this);
        m_3dWindow->setAttribute(Qt::WA_DeleteOnClose, false); // Keep it alive for toggling
        connect(m_3dWindow, &PCB3DWindow::componentPicked, this, [this](const QUuid& id) {
            if (!m_scene || !m_view) return;

            ComponentItem* hit = nullptr;
            for (QGraphicsItem* item : m_scene->items()) {
                if (ComponentItem* comp = dynamic_cast<ComponentItem*>(item)) {
                    if (comp->id() == id) {
                        hit = comp;
                        break;
                    }
                }
            }
            if (!hit) return;

            m_scene->clearSelection();
            hit->setSelected(true);
            m_view->centerOn(hit);
            m_view->setFocus();

            const QString refDes = hit->name().trimmed();
            const QString netName = hit->netName().trimmed();
            if (!refDes.isEmpty() || !netName.isEmpty()) {
                SyncManager::instance().pushCrossProbe(refDes, netName);
            }

            if (statusBar()) {
                statusBar()->showMessage(
                    QString("3D cross-probe: %1").arg(refDes.isEmpty() ? hit->componentType() : refDes),
                    2500);
            }
        });
    }
    
    if (m_3dWindow->isVisible()) {
        m_3dWindow->hide();
    } else {
        m_3dWindow->show();
        m_3dWindow->raise();
        m_3dWindow->activateWindow();
        m_3dWindow->updateView();
    }
}

#include "trace_item.h"
#include "via_item.h"
#include "component_item.h"
#include "pad_item.h"
#include <QDoubleSpinBox>
#include <QLineEdit>

void MainWindow::updatePropertyBar() {
    if (!m_propertyBar || !m_scene) return;

    m_propertyBar->clear();
    QList<QGraphicsItem*> selected = m_scene->selectedItems();

    if (selected.isEmpty()) {
        m_propertyBar->addWidget(new QLabel(" NO SELECTION"));
        return;
    }

    if (selected.size() == 1) {
        PCBItem* pItem = dynamic_cast<PCBItem*>(selected.first());
        if (!pItem) return;

        if (pItem->itemType() == PCBItem::TraceType) {
            TraceItem* trace = static_cast<TraceItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" TRACE: "));

            m_propertyBar->addWidget(new QLabel(" Width:"));
            QDoubleSpinBox* wSpin = new QDoubleSpinBox();
            wSpin->setRange(0.1, 10.0);
            wSpin->setSingleStep(0.05);
            wSpin->setValue(trace->width());
            connect(wSpin, &QDoubleSpinBox::valueChanged, this, [this, trace](double val) {
                onPropertyChanged("Width (mm)", val);
            });
            m_propertyBar->addWidget(wSpin);

            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(trace->netName());
            netEdit->setMaximumWidth(100);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, trace, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);

            m_propertyBar->addWidget(new QLabel(" Layer:"));
            QComboBox* layerCombo = new QComboBox();
            for (const auto& layer : PCBLayerManager::instance().layers()) {
                layerCombo->addItem(layer.name(), layer.id());
            }
            layerCombo->setCurrentIndex(layerCombo->findData(trace->layer()));
            connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, trace, layerCombo](int index){
                onPropertyChanged("Layer", layerCombo->itemData(index).toInt());
            });
            m_propertyBar->addWidget(layerCombo);

        } else if (pItem->itemType() == PCBItem::ViaType) {
            ViaItem* via = static_cast<ViaItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" VIA: "));

            m_propertyBar->addWidget(new QLabel(" Outer:"));
            QDoubleSpinBox* dSpin = new QDoubleSpinBox();
            dSpin->setRange(0.2, 5.0);
            dSpin->setValue(via->diameter());
            connect(dSpin, &QDoubleSpinBox::valueChanged, this, [this, via](double val) {
                onPropertyChanged("Diameter (mm)", val);
            });
            m_propertyBar->addWidget(dSpin);

            m_propertyBar->addWidget(new QLabel(" Drill:"));
            QDoubleSpinBox* drSpin = new QDoubleSpinBox();
            drSpin->setRange(0.1, 4.0);
            drSpin->setValue(via->drillSize());
            connect(drSpin, &QDoubleSpinBox::valueChanged, this, [this, via](double val) {
                onPropertyChanged("Drill Size (mm)", val);
            });
            m_propertyBar->addWidget(drSpin);

            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(via->netName());
            netEdit->setMaximumWidth(100);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, via, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);

        } else if (pItem->itemType() == PCBItem::ComponentType) {
            ComponentItem* comp = static_cast<ComponentItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" COMPONENT: "));

            m_propertyBar->addWidget(new QLabel(" Ref:"));
            QLineEdit* refEdit = new QLineEdit(comp->name());
            refEdit->setMaximumWidth(80);
            connect(refEdit, &QLineEdit::editingFinished, this, [this, comp, refEdit]() {
                onPropertyChanged("Name", refEdit->text());
            });
            m_propertyBar->addWidget(refEdit);

            m_propertyBar->addWidget(new QLabel(" Rot:"));
            QDoubleSpinBox* rotSpin = new QDoubleSpinBox();
            rotSpin->setRange(-360, 360);
            rotSpin->setValue(comp->rotation());
            connect(rotSpin, &QDoubleSpinBox::valueChanged, this, [this, comp](double val) {
                onPropertyChanged("Rotation (deg)", val);
            });
            m_propertyBar->addWidget(rotSpin);

            m_propertyBar->addWidget(new QLabel(" Layer:"));
            QComboBox* layerCombo = new QComboBox();
            layerCombo->addItem("Top", 0);
            layerCombo->addItem("Bottom", 1);
            layerCombo->setCurrentIndex(comp->layer() == 0 ? 0 : 1);
            connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, comp, layerCombo](int index){
                onPropertyChanged("Layer", layerCombo->itemData(index).toInt());
            });
            m_propertyBar->addWidget(layerCombo);

        } else if (pItem->itemType() == PCBItem::CopperPourType) {
            CopperPourItem* pour = static_cast<CopperPourItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(" SHAPE/POUR: "));

            m_propertyBar->addWidget(new QLabel(" Layer:"));
            QComboBox* layerCombo = new QComboBox();
            for (const auto& layer : PCBLayerManager::instance().layers()) {
                layerCombo->addItem(layer.name(), layer.id());
            }
            layerCombo->setCurrentIndex(layerCombo->findData(pour->layer()));
            connect(layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, pour, layerCombo](int index){
                onPropertyChanged("Layer", layerCombo->itemData(index).toInt());
            });
            m_propertyBar->addWidget(layerCombo);

            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(pour->netName());
            netEdit->setMaximumWidth(80);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, pour, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);

            m_propertyBar->addWidget(new QLabel(" Clearance:"));
            QDoubleSpinBox* cSpin = new QDoubleSpinBox();
            cSpin->setRange(0.0, 5.0);
            cSpin->setSingleStep(0.05);
            cSpin->setValue(pour->clearance());
            connect(cSpin, &QDoubleSpinBox::valueChanged, this, [this, pour](double val) {
                onPropertyChanged("Clearance (mm)", val);
            });
            m_propertyBar->addWidget(cSpin);

        } else if (pItem->itemType() == PCBItem::PadType) {
            PadItem* pad = static_cast<PadItem*>(pItem);
            m_propertyBar->addWidget(new QLabel(QString(" PAD (%1)").arg(pad->padShape())));
            m_propertyBar->addWidget(new QLabel(" Net:"));
            QLineEdit* netEdit = new QLineEdit(pad->netName());
            netEdit->setMaximumWidth(100);
            connect(netEdit, &QLineEdit::editingFinished, this, [this, pad, netEdit]() {
                onPropertyChanged("Net", netEdit->text());
            });
            m_propertyBar->addWidget(netEdit);
        }
    } else {
        m_propertyBar->addWidget(new QLabel(QString(" MULTI-SELECTION (%1 items)").arg(selected.size())));
    }
}

void MainWindow::onSnippetGenerated(const QString& jsonSnippet) {
    if (!m_scene || !m_api) return;

    QJsonDocument doc = QJsonDocument::fromJson(jsonSnippet.toUtf8());
    if (!doc.isArray() && !doc.isObject()) return;

    if (doc.isObject() && doc.object().contains("commands")) {
        m_undoStack->beginMacro("AI Generated Changes");
        m_api->executeBatch(doc.object()["commands"].toArray());
        m_undoStack->endMacro();
        statusBar()->showMessage("Executed AI generated PCB commands", 3000);
    }
}

void MainWindow::onImportNetlist() {
    NetlistImportDialog* importDialog = new NetlistImportDialog(this);
    connect(importDialog, &NetlistImportDialog::importRequested, this, [this](const ECOPackage& pkg) {
        // Apply the imported netlist to the PCB
        applyECO(pkg);
    });

    if (importDialog->exec() == QDialog::Accepted) {
        statusBar()->showMessage("Netlist import completed", 3000);
    }
}

void MainWindow::onExportPickPlace() {
    PickPlaceExportDialog* dlg = new PickPlaceExportDialog(this);

    // Wire up the export to use the actual scene
    connect(dlg, &QDialog::accepted, this, [this, dlg]() {
        QString path = dlg->outputPath();
        if (path.isEmpty()) return;

        auto opts = dlg->options();
        QString err;
        bool ok = ManufacturingExporter::exportPickPlace(m_scene, path, opts, &err);
        if (!ok) {
            QMessageBox::warning(this, "Export Failed", err.isEmpty() ? "Unknown error." : err);
            return;
        }
        statusBar()->showMessage("Pick and Place exported to " + path, 4000);
    });

    dlg->exec();
}

void MainWindow::onAutoRoute() {
    if (!m_scene) {
        QMessageBox::warning(this, "No PCB Scene", "Open or create a PCB board first.");
        return;
    }

    AutoRouterDialog* dlg = new AutoRouterDialog(m_scene, this);
    dlg->exec();
}

void MainWindow::onLengthMatching() {
    if (!m_scene) {
        QMessageBox::warning(this, "No PCB Scene", "Open or create a PCB board first.");
        return;
    }

    LengthMatchingDialog* dlg = new LengthMatchingDialog(m_scene, this);
    dlg->exec();
}
