// schematic_editor.cpp
// Core SchematicEditor: constructor, destructor, canvas setup, and view/tool handlers.
//
// This file is the main entry point for SchematicEditor. The implementation is
// split across multiple files for maintainability:
//   - schematic_editor.cpp        : Core setup, constructor, view/tool handlers
//   - schematic_editor_ui.cpp     : Menu bar, toolbar, dock widgets, status bar
//   - schematic_editor_theme.cpp  : Theme styling, grid, page frame rendering
//   - schematic_editor_actions.cpp: Clipboard, selection, undo/redo, properties
//   - schematic_editor_file.cpp   : File I/O, export, ERC, netlist, settings

#include "schematic_editor.h"
#include "remote_display_server.h"
#include "../../symbols/symbol_editor.h"
#include "../../symbols/symbol_library.h"
#include "library_index.h"
#include "../../ui/spice_model_architect.h"
#include "schematic_api.h"
#include "schematic_commands.h"
#include "schematic_tool_registry_builtin.h"
#include "schematic_item_registry.h"
#include "../tools/schematic_component_tool.h"
#include "../items/schematic_spice_directive_item.h"
#include "../ui/simulation_setup_dialog.h"
#include "../ui/simulation_panel.h"
#include "../ui/schematic_minimap.h"
#include "schematic_item.h"
#include "../items/schematic_item_selection_utils.h"
#include "schematic_page_item.h"
#include <QDir>

static SymbolLibrary* ensureDefaultUserSymbolLibrary() {
    auto& mgr = SymbolLibraryManager::instance();
    for (SymbolLibrary* lib : mgr.libraries()) {
        if (lib && !lib->isBuiltIn()) return lib;
    }
    const QString baseDir = QDir::homePath() + "/ViospiceLib/sym";
    QDir().mkpath(baseDir);
    auto* lib = new SymbolLibrary("User", false);
    lib->setPath(QDir(baseDir).filePath("user.sclib"));
    mgr.addLibrary(lib);
    return lib;
}
#include "schematic_connectivity.h"
#include "../analysis/schematic_erc.h"
#include "theme_manager.h"
#include "net_manager.h"
#include "schematic_layout_optimizer.h"
#include "config_manager.h"
#include <QTimer>
#include <QMessageBox>
#include <QCloseEvent>
#include "../items/schematic_sheet_item.h"
#include "../items/simulation_overlay_item.h"
#include "../items/schematic_waveform_marker.h"
#include "../tools/schematic_probe_tool.h"
#include "../../simulator/core/sim_results.h"
#include "../ui/simulation_panel.h"
#include "../ui/logic_analyzer_window.h"
#include "../ui/logic_editor_panel.h"
#include "../items/smart_signal_item.h"

#include <QApplication>
#include <QFileInfo>
#include <QEvent>
#include "../../ui/source_control_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsRectItem>
#include <QWheelEvent>
#include <QScrollBar>
#include <QLineF>
#include <QStatusBar>
#include <QDir>

// ─── Constructor / Destructor ────────────────────────────────────────────────

#include "sync_manager.h"
#include "ws_server.h"
#include "schematic_item.h"
#include "schematic/dialogs/oscilloscope_properties_dialog.h"
#include "schematic_menu_registry.h"

SchematicEditor::SchematicEditor(QWidget *parent)
    : QMainWindow(parent),
      m_workspaceTabs(nullptr),
      m_scene(nullptr),
      m_view(nullptr),
      m_netManager(nullptr),
      m_layoutOptimizer(nullptr),
      m_pageFrame(nullptr),
      m_currentPageSize("A4"),
      m_componentDock(nullptr),
      m_projectExplorerDock(nullptr),
      m_libraryDock(nullptr),
      m_componentsPanel(nullptr),
      m_projectExplorer(nullptr),
      m_libraryList(nullptr),
      m_ercDock(nullptr),
      m_ercPanel(nullptr),
      m_ercList(nullptr),
      m_simulationPanel(nullptr),
      m_toggleHeatmapAction(nullptr),
      m_geminiDock(nullptr),
      m_geminiPanel(nullptr),
      m_logicEditorPanel(nullptr),
      m_sourceControlDock(nullptr),
      m_sourceControlPanel(nullptr),
      m_oscilloscopeDock(nullptr),
      m_breadcrumbWidget(nullptr),
      m_propertyBar(nullptr),
      m_runSimMenuAction(nullptr),
      m_stopSimMenuAction(nullptr),
      m_runSimToolbarAction(nullptr),
      m_pauseSimToolbarAction(nullptr),
      m_stopSimToolbarAction(nullptr),
      m_showDetailedLogAction(nullptr),
      m_simControlSubGroup(nullptr),
      m_coordLabel(nullptr),
      m_gridLabel(nullptr),
      m_layerLabel(nullptr),
      m_netLabel(nullptr),
      m_remoteLabel(nullptr),
      m_isModified(false),
      m_simulationRunning(false),
      m_simPaused(false),
      m_showVoltageOverlays(true),
      m_showCurrentOverlays(true),
      m_mouseFollowPlacementActive(false),
      m_isSaving(false),
      m_undoStack(new QUndoStack(this)),
      m_api(new SchematicAPI(nullptr, m_undoStack, this)),
      m_updatingProperties(false),
      m_hierarchyDock(nullptr),
      m_hierarchyPanel(nullptr),
      m_hierarchyTree(nullptr),
      m_ercRules(SchematicERCRules::defaultRules()),
      m_quickOpenDialog(nullptr),
      m_miniMap(nullptr),
      m_toggleMiniMapAction(nullptr)
{
    setWindowTitle("viospice - Schematic Editor");
    setMinimumSize(640, 480);
    resize(1024, 720);
    setObjectName("SchematicEditor");
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    // Register all built-in schematic tools and items
    SchematicToolRegistryBuiltIn::registerBuiltInTools();
    SchematicItemRegistry::registerBuiltInItems();
    
    // Initialize context menu actions
    SchematicMenuRegistry::instance().initializeDefaultActions();

    m_workspaceTabs = new QTabWidget(this);
    m_workspaceTabs->setTabsClosable(true);
    m_workspaceTabs->setMovable(true);
    m_workspaceTabs->setDocumentMode(true); // VS Code / Modern look

    connect(m_workspaceTabs, &QTabWidget::currentChanged, this, &SchematicEditor::onTabChanged);
    connect(m_workspaceTabs, &QTabWidget::tabCloseRequested, this, &SchematicEditor::onTabCloseRequested);

    // Setup tab enhancements
    setupTabShortcuts();
    setupTabBarSignals();
    setupTabContextMenu();

    // Core setup
    createStatusBar();
    setupCanvas();
    createDockWidgets();
    createMenuBar();
    createToolBar();
    createDrawingToolbar();
    connectSimulationSignals();
    updateSimulationUiState(false);

    // --- Remote Display Server ---
    auto& remoteServer = RemoteDisplayServer::instance();
    remoteServer.start();
    if (remoteServer.isRunning()) {
        m_remoteLabel->setText("Remote: " + remoteServer.serverUrl());
        m_remoteLabel->setStyleSheet("color: #059669; font-weight: bold;"); // Green
    }

#if VIOSPICE_HAS_QT_WEBSOCKETS
    connect(&remoteServer, &RemoteDisplayServer::clientConnected, this, [this](const QString& addr) {
        statusBar()->showMessage("Remote Display Connected: " + addr, 3000);
    });
    connect(&remoteServer, &RemoteDisplayServer::clientDisconnected, this, [this](const QString& addr) {
        statusBar()->showMessage("Remote Display Disconnected: " + addr, 3000);
    });

    // Connect to WsServer for remote file updates (VioCode sync)
    if (auto* ws = WsServer::instance()) {
        connect(ws, &WsServer::remoteFileUpdated, this, &SchematicEditor::onRemoteFileUpdated);
        connect(ws, &WsServer::clientConnected, this, &SchematicEditor::updateAgentStatus, Qt::QueuedConnection);
        connect(ws, &WsServer::clientDisconnected, this, &SchematicEditor::updateAgentStatus, Qt::QueuedConnection);
    }
#endif

    // Initial Mini-map state (hidden by default)
    if (m_toggleMiniMapAction) m_toggleMiniMapAction->setChecked(false);

    // Restore UI State after docks/toolbars are created
    bool restoredWindowState = false;
    {
        QByteArray geom = ConfigManager::instance().windowGeometry("SchematicEditor");
        QByteArray state = ConfigManager::instance().windowState("SchematicEditor");
        if (!geom.isEmpty()) restoreGeometry(geom);
        if (!state.isEmpty()) {
            restoredWindowState = restoreState(state);
        }
    }
    // Re-apply dock nesting and corner rules after restoring state to avoid overlaps.
    setDockNestingEnabled(true);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setDockOptions(QMainWindow::AllowTabbedDocks | QMainWindow::ForceTabbedDocks);
    // Only enforce default docking layout when we did not restore a prior layout.
    if (!restoredWindowState && m_ercDock && m_sourceControlDock) {
        addDockWidget(Qt::RightDockWidgetArea, m_ercDock);
        addDockWidget(Qt::RightDockWidgetArea, m_sourceControlDock);
        tabifyDockWidget(m_ercDock, m_sourceControlDock);
        m_sourceControlDock->raise();
    }

    // Allow Gemini dock visibility to be restored from saved state if it exists.
    // The panel itself will still only initialize when the dock is actually shown.
    m_allowGeminiDockInit = true;
    if (m_geminiDock && m_geminiDock->isVisible()) {
        ensureGeminiPanelInitialized();
    }
    
    // Theme and grid
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &SchematicEditor::applyTheme);
    applyTheme();

    // Set default tool
    qDebug() << "DBG: before setCurrentTool, m_view=" << m_view;
    if (m_view) m_view->setCurrentTool("Select");
    qDebug() << "DBG: after setCurrentTool";

    // Smart Cross-Probing from PCB
    connect(&SyncManager::instance(), &SyncManager::crossProbeReceived, this, &SchematicEditor::onCrossProbeReceived);
    
    // ECO / Netlist Synchronization from PCB or Reverse Engineering
    connect(&SyncManager::instance(), &SyncManager::ecoAvailable, this, &SchematicEditor::handleIncomingECO);
    qDebug() << "DBG: after signal connections";

    // Auto-Save Setup
    if (ConfigManager::instance().autoSaveEnabled()) {
        QTimer* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &SchematicEditor::onSaveSchematic);
        timer->start(ConfigManager::instance().autoSaveInterval() * 60000);
    }
    qDebug() << "DBG: after auto-save";

    // Restore Session
    QStringList openFiles = ConfigManager::instance().toolProperty("SchematicEditor", "openFiles").toStringList();
    qDebug() << "DBG: session openFiles=" << openFiles;
    if (!openFiles.isEmpty()) {
        for (const QString& path : openFiles) {
            if (QFile::exists(path)) openFile(path);
        }
        int activeIdx = ConfigManager::instance().toolProperty("SchematicEditor", "activeTabIndex", 0).toInt();
        if (activeIdx >= 0 && activeIdx < m_workspaceTabs->count()) {
            m_workspaceTabs->setCurrentIndex(activeIdx);
        }
    }
    qDebug() << "DBG: constructor done";
}

SchematicEditor::~SchematicEditor() {
    if (m_undoStack) {
        m_undoStack->disconnect(this);
        m_undoStack->clear(); // Ensure commands are deleted while scene and NetManager are still alive
    }
    for (auto* win : m_laWindows) delete win;
}

void SchematicEditor::closeEvent(QCloseEvent* event) {
    if (!m_undoStack->isClean() || m_isModified) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Save Changes?",
            "The schematic has unsaved changes. Do you want to save before closing?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Save) {
            onSaveSchematic();
            event->accept();
        } else if (reply == QMessageBox::Discard) {
            event->accept();
        } else {
            event->ignore();
            return; // Don't save window state if we're not closing
        }
    }

    // Save Session State
    QStringList openFiles;
    for (int i = 0; i < m_workspaceTabs->count(); ++i) {
        QWidget* w = m_workspaceTabs->widget(i);
        QString path = w->property("filePath").toString();
        if (!path.isEmpty()) openFiles.append(path);
    }
    ConfigManager::instance().setToolProperty("SchematicEditor", "openFiles", openFiles);
    ConfigManager::instance().setToolProperty("SchematicEditor", "activeTabIndex", m_workspaceTabs->currentIndex());

    // Save UI State
    ConfigManager::instance().saveWindowState("SchematicEditor", saveGeometry(), saveState());

    // Commands keep raw pointers to scene/items. Clear history while scenes are still alive.
    if (m_undoStack) {
        m_undoStack->disconnect(this);
        m_undoStack->clear();
    }
    
    disconnect(&ThemeManager::instance(), nullptr, this, nullptr);
    disconnect(&SyncManager::instance(), nullptr, this, nullptr);
    disconnect(&SimManager::instance(), nullptr, this, nullptr);
    
    event->accept();
}

bool SchematicEditor::event(QEvent* event) {
    if (m_mouseFollowPlacementActive && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke && ke->key() == Qt::Key_Escape) {
            endMouseFollowPlacement(true);
            statusBar()->showMessage("Placement canceled", 1200);
            return true;
        }
    }
    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::ApplicationActivate) {
        SourceControlManager::instance().scheduleRefresh();
    }
    return QMainWindow::event(event);
}

bool SchematicEditor::eventFilter(QObject* watched, QEvent* event) {
    // Handle tab bar right-click for context menu
    if (watched == m_workspaceTabs->tabBar()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent && mouseEvent->button() == Qt::RightButton) {
                showTabContextMenu(mouseEvent->pos());
                return true;
            }
        }
    }

    if (handlePlacementModeEvent(watched, event)) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool SchematicEditor::handlePlacementModeEvent(QObject* watched, QEvent* event) {
    if (!m_mouseFollowPlacementActive || !m_view) return false;

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent) return true;

        if (keyEvent->key() == Qt::Key_Escape) {
            endMouseFollowPlacement(true);
            statusBar()->showMessage("Placement canceled", 1200);
            return true;
        }

        QString statusMessage;
        if (keyEvent->key() == Qt::Key_R && (keyEvent->modifiers() & Qt::ControlModifier)) {
            const SchematicTool::TransformAction action = (keyEvent->modifiers() & Qt::ShiftModifier)
                ? SchematicTool::TransformAction::RotateCCW
                : SchematicTool::TransformAction::RotateCW;
            if (applyMouseFollowTransformAction(action, &statusMessage)) {
                statusBar()->showMessage(statusMessage, 1200);
                return true;
            }
        }

        if (keyEvent->key() == Qt::Key_H &&
            !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (applyMouseFollowTransformAction(SchematicTool::TransformAction::FlipHorizontal, &statusMessage)) {
                statusBar()->showMessage(statusMessage, 1200);
                return true;
            }
        }

        if (keyEvent->key() == Qt::Key_V &&
            (keyEvent->modifiers() & Qt::ShiftModifier) &&
            !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            if (applyMouseFollowTransformAction(SchematicTool::TransformAction::FlipVertical, &statusMessage)) {
                statusBar()->showMessage(statusMessage, 1200);
                return true;
            }
        }

        return false;
    }

    if (event->type() != QEvent::MouseButtonPress ||
        (watched != m_view->viewport() && watched != m_view)) {
        return false;
    }

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    if (!mouseEvent) return false;

    if (mouseEvent->button() == Qt::LeftButton) {
        endMouseFollowPlacement(false);
        statusBar()->showMessage("Placement confirmed", 1200);
        return true;
    }
    if (mouseEvent->button() == Qt::RightButton) {
        endMouseFollowPlacement(true);
        statusBar()->showMessage("Placement canceled", 1200);
        return true;
    }

    return false;
}

QList<SchematicItem*> SchematicEditor::selectedSchematicItems() const {
    return topLevelSelectedSchematicItems(m_scene);
}

bool SchematicEditor::handleTransformAction(SchematicTool::TransformAction action) {
    QString statusMessage;
    if (applyMouseFollowTransformAction(action, &statusMessage) ||
        applySelectedItemsTransformAction(action, &statusMessage) ||
        applyCurrentToolTransformAction(action)) {
        if (!statusMessage.isEmpty()) {
            statusBar()->showMessage(statusMessage, 2000);
        }
        return true;
    }
    return false;
}

bool SchematicEditor::applyMouseFollowTransformAction(SchematicTool::TransformAction action, QString* statusMessage) {
    if (!m_mouseFollowPlacementActive) return false;

    switch (action) {
    case SchematicTool::TransformAction::RotateCW:
        m_mouseFollowRotation += 90;
        break;
    case SchematicTool::TransformAction::RotateCCW:
        m_mouseFollowRotation -= 90;
        break;
    case SchematicTool::TransformAction::FlipHorizontal:
        m_mouseFollowFlippedH = !m_mouseFollowFlippedH;
        applyPlacementTransforms();
        if (statusMessage) *statusMessage = m_mouseFollowFlippedH ? "Flipped Horizontally" : "Unflipped Horizontally";
        return true;
    case SchematicTool::TransformAction::FlipVertical:
        m_mouseFollowFlippedV = !m_mouseFollowFlippedV;
        applyPlacementTransforms();
        if (statusMessage) *statusMessage = m_mouseFollowFlippedV ? "Flipped Vertically" : "Unflipped Vertically";
        return true;
    }

    m_mouseFollowRotation = fmod(m_mouseFollowRotation, 360);
    if (m_mouseFollowRotation < 0) m_mouseFollowRotation += 360;
    applyPlacementTransforms();
    if (statusMessage) *statusMessage = QString("Rotation: %1°").arg(m_mouseFollowRotation);
    return true;
}

bool SchematicEditor::applyCurrentToolTransformAction(SchematicTool::TransformAction action) {
    if (!m_view || !m_view->currentTool()) return false;

    return m_view->currentTool()->applyTransformAction(action);
}

bool SchematicEditor::applySelectedItemsTransformAction(SchematicTool::TransformAction action, QString* statusMessage) {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.isEmpty()) return false;

    SchematicItem::TransformAction itemAction;
    switch (action) {
    case SchematicTool::TransformAction::RotateCW:
        itemAction = SchematicItem::TransformAction::RotateCW;
        if (statusMessage) *statusMessage = "Rotated 90° CW";
        break;
    case SchematicTool::TransformAction::RotateCCW:
        itemAction = SchematicItem::TransformAction::RotateCCW;
        if (statusMessage) *statusMessage = "Rotated 90° CCW";
        break;
    case SchematicTool::TransformAction::FlipHorizontal:
        itemAction = SchematicItem::TransformAction::FlipHorizontal;
        if (statusMessage) *statusMessage = "Flipped Horizontally";
        break;
    case SchematicTool::TransformAction::FlipVertical:
        itemAction = SchematicItem::TransformAction::FlipVertical;
        if (statusMessage) *statusMessage = "Flipped Vertically";
        break;
    }

    if (QUndoCommand* command = createItemTransformCommand(m_scene, items, itemAction)) {
        m_undoStack->push(command);
        return true;
    }
    return false;
}

void SchematicEditor::saveActionShortcuts(const QList<QAction*>& actions, QMap<QAction*, QKeySequence>& storage) {
    storage.clear();
    for (QAction* action : actions) {
        if (!action) continue;
        storage[action] = action->shortcut();
        action->setShortcut(QKeySequence());
    }
}

void SchematicEditor::saveMatchingActionShortcuts(const QList<QAction*>& actions,
                                                  const QSet<QKeySequence>& shortcuts,
                                                  QMap<QAction*, QKeySequence>& storage) {
    storage.clear();
    for (QAction* action : actions) {
        if (!action) continue;
        const QKeySequence shortcut = action->shortcut();
        if (shortcut.isEmpty() || !shortcuts.contains(shortcut)) continue;
        storage[action] = shortcut;
        action->setShortcut(QKeySequence());
    }
}

void SchematicEditor::restoreActionShortcuts(QMap<QAction*, QKeySequence>& storage) {
    for (auto it = storage.begin(); it != storage.end(); ++it) {
        if (it.key()) {
            it.key()->setShortcut(it.value());
        }
    }
    storage.clear();
}

void SchematicEditor::updateComponentToolShortcutState() {
    const QSet<QKeySequence> transformShortcuts = {
        QKeySequence("H"),
        QKeySequence("Shift+V"),
        QKeySequence("Ctrl+R"),
        QKeySequence("Ctrl+Shift+R")
    };
    const bool isComponentTool = m_view && dynamic_cast<SchematicComponentTool*>(m_view->currentTool()) != nullptr;
    if (isComponentTool && m_componentToolManipShortcuts.isEmpty()) {
        saveMatchingActionShortcuts(m_manipActions, transformShortcuts, m_componentToolManipShortcuts);
        saveMatchingActionShortcuts(m_toolActions.values(), transformShortcuts, m_componentToolToolShortcuts);
        return;
    }

    if (!isComponentTool &&
        (!m_componentToolManipShortcuts.isEmpty() || !m_componentToolToolShortcuts.isEmpty())) {
        restoreActionShortcuts(m_componentToolManipShortcuts);
        restoreActionShortcuts(m_componentToolToolShortcuts);
    }
}

// ─── Canvas Setup ────────────────────────────────────────────────────────────

void SchematicEditor::setupCanvas() {
    setCentralWidget(m_workspaceTabs);
    
    // Initial schematic
    addSchematicTab("Schematic 1");

    m_layoutOptimizer = new SchematicLayoutOptimizer(this);

    // UI state restore moved to constructor after docks/toolbars are created
}

void SchematicEditor::addSchematicTab(const QString& name) {
    auto* view = new SchematicView(this);
    auto* scene = new QGraphicsScene(view);
    scene->setSceneRect(-5000, -5000, 10000, 10000);
    view->setScene(scene);
    view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFocusPolicy(Qt::StrongFocus);

    auto* netManager = new NetManager(view);
    view->setNetManager(netManager);
    view->setUndoStack(m_undoStack);

    connect(m_undoStack, &QUndoStack::indexChanged, this, &SchematicEditor::onUndoStackIndexChanged);

    connect(view, &SchematicView::pageTitleBlockDoubleClicked, this, &SchematicEditor::onEditTitleBlock);
    connect(view, &SchematicView::coordinatesChanged, this, &SchematicEditor::updateCoordinates);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this, scene, view]() {
        if (m_scene == scene) {
            onSelectionChanged();
            QList<SchematicItem*> selected;
            for (auto* it : scene->selectedItems()) {
                if (auto* si = dynamic_cast<SchematicItem*>(it)) selected.append(si);
            }
            if (!selected.isEmpty()) runLiveERC(selected);
            else view->clearLiveERCMarkers();
        }
    });
    connect(view, &SchematicView::itemDoubleClicked, this, &SchematicEditor::onItemDoubleClicked);
    connect(view, &SchematicView::componentTextLabelDoubleClicked, this, &SchematicEditor::onComponentTextLabelDoubleClicked);
    connect(view, &SchematicView::itemPlaced, this, &SchematicEditor::onItemPlaced);
    connect(view, &SchematicView::itemSelectionDoubleClicked, this, &SchematicEditor::onSelectionDoubleClicked);
    connect(view, &SchematicView::editSimulationDirective, this, &SchematicEditor::onEditSimulationFromDirective);
    connect(view, &SchematicView::syncSheetRequested, this, [this, scene](SchematicSheetItem* sheet) {
        if (sheet) {
            sheet->updatePorts(m_projectDir);
            SchematicConnectivity::updateVisualConnections(scene);
            statusBar()->showMessage("Synchronized pins for: " + sheet->sheetName(), 3000);
        }
    });
    connect(view, &SchematicView::runLiveERC, this, &SchematicEditor::runLiveERC);
    connect(view, &SchematicView::toolChanged, this, [this](const QString& toolName) {
        if (m_toolActions.contains(toolName)) {
            m_toolActions[toolName]->setChecked(true);
        }
        // When a component placement tool is active, disable H/Shift+V QAction shortcuts
        // so they reach the component tool's keyPressEvent instead of being consumed by
        // the flip actions (which require a selection and do nothing for preview items).
        updateComponentToolShortcutState();
    });

    connect(view, &SchematicView::netProbed, this, [this](const QString& netName) {
        if (m_simulationPanel) {
            if (m_simulationPanel->hasProbe(netName)) {
                m_simulationPanel->onClearFocusedPaneProbes();
            }
            m_simulationPanel->addProbe(netName);
            statusBar()->showMessage("Probed signal: " + netName, 3000);
        }
    });

    connect(view, &SchematicView::snippetDropped, this, [this](const QString& json, const QPointF& pos) {
        onSnippetGenerated(json, pos);
    });

    connect(view, &SchematicView::netlistDropped, this, [this](const QString& netlist, const QPointF& pos) {
        // Implementation for netlist drop at pos
        // For now, let's just use the existing netlist handling but maybe offset it
        statusBar()->showMessage("AI Netlist dropped at position", 3000);
        // (Logic to handle netlist at pos if possible)
    });

    int idx = m_workspaceTabs->addTab(view, getThemeIcon(":/icons/comp_ic.svg"), name);
    m_workspaceTabs->setCurrentIndex(idx);

    // Reset simulation config for new schematics
    m_simConfigured = false;

    // Initial page frame for this new sheet
    m_view = view;
    m_scene = scene;
    m_netManager = netManager;
    m_pageFrame = nullptr; // Let updatePageFrame create it for this new scene
    qDebug() << "[SchematicEditor] Updating page frame...";
    updatePageFrame();

    // Create or update Logic Editor (standalone IDE)
    qDebug() << "[SchematicEditor] Initializing Logic Editor Panel...";
    if (!m_logicEditorPanel) {
        qDebug() << "[SchematicEditor] Creating NEW LogicEditorPanel...";
        m_logicEditorPanel = new LogicEditorPanel(scene, netManager, this);
        connect(m_logicEditorPanel, &LogicEditorPanel::closed, this, [this]() {
            if (m_logicEditorPanel) m_logicEditorPanel->setTargetBlock(nullptr);
        });
    } else {
        qDebug() << "[SchematicEditor] Updating EXISTING LogicEditorPanel scene...";
        m_logicEditorPanel->setScene(scene, netManager);
    }
    qDebug() << "[SchematicEditor] Logic Editor Panel ready.";

    if (m_geminiPanel) {
        view->setGeminiPanel(m_geminiPanel);
    }

    if (m_api) m_api->setScene(m_scene);
    
    view->setFocus();
}

void SchematicEditor::onTabChanged(int index) {
    if (index < 0) {
        m_view = nullptr;
        m_scene = nullptr;
        m_netManager = nullptr;
        m_pageFrame = nullptr;
        m_currentFilePath.clear();
        if (m_geminiPanel) {
            m_geminiPanel->setScene(nullptr);
            m_geminiPanel->setNetManager(nullptr);
            m_geminiPanel->setProjectFilePath(QString());
        }
        updateBreadcrumbs();
        refreshHierarchyPanel();
        return;
    }
    
    QWidget* current = m_workspaceTabs->widget(index);
    if (auto* view = qobject_cast<SchematicView*>(current)) {
        m_view = view;
        m_scene = view->scene();
        m_netManager = view->netManager();
        m_currentFilePath = view->property("filePath").toString();

        if (m_simulationPanel) {
            m_simulationPanel->setTargetScene(m_scene, m_netManager, m_projectDir, true);

            // Sync editor config with the newly restored tab state
            const auto panelCfg = m_simulationPanel->getAnalysisConfig();
            m_simConfig.type = panelCfg.type;
            m_simConfig.stop = panelCfg.stop;
            m_simConfig.step = panelCfg.step;
            m_simConfig.transientSteady = panelCfg.transientSteady;
            m_simConfig.steadyStateTol = panelCfg.steadyStateTol;
            m_simConfig.steadyStateDelay = panelCfg.steadyStateDelay;
            m_simConfig.fStart = panelCfg.fStart;
            m_simConfig.fStop = panelCfg.fStop;
            m_simConfig.pts = panelCfg.pts;
            m_simConfig.rfPort1Source = panelCfg.rfPort1Source;
            m_simConfig.rfPort2Node = panelCfg.rfPort2Node;
            m_simConfig.rfZ0 = panelCfg.rfZ0;
            m_simConfig.commandText = panelCfg.commandText;
            // If the tab has a saved file path, consider it configured; otherwise keep the flag as-is
            if (!m_currentFilePath.isEmpty()) {
                m_simConfigured = true;
            }
        }
        if (m_geminiPanel) {
            m_geminiPanel->setScene(m_scene);
            m_geminiPanel->setNetManager(m_netManager);
            m_geminiPanel->setProjectFilePath(m_currentFilePath);
        }
        
        if (m_api) m_api->setScene(m_scene);
        
        // Find page frame in this scene
        m_pageFrame = nullptr;
        for (auto* item : m_scene->items()) {
            if (auto* pf = dynamic_cast<SchematicPageItem*>(item)) {
                m_pageFrame = pf;
                break;
            }
        }
        
        if (m_logicEditorPanel) m_logicEditorPanel->setScene(m_scene, m_netManager);
        
        // Update Mini-map if visible
        if (m_miniMap && m_miniMap->isVisible()) {
            m_miniMap->setParent(m_view);
            m_miniMap->setScene(m_scene);
            m_miniMap->updateViewportRect();
            m_miniMap->show();
            m_miniMap->raise();
            
            // Re-connect transformation signal
            connect(m_view, &SchematicView::transformationChanged, m_miniMap, &SchematicMiniMap::updateViewportRect, Qt::UniqueConnection);

            // Reposition
            int x = m_view->viewport()->width() - m_miniMap->width() - 20;
            int y = m_view->viewport()->height() - m_miniMap->height() - 20;
            m_miniMap->move(x, y);
        }

        onSelectionChanged();
        updateBreadcrumbs();
        refreshHierarchyPanel();
        updateCoordinates(m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos())));
    } else if (QString(current->metaObject()->className()) == "SymbolEditor") {
        // Contextually disable schematic docks/toolbars if needed
        if (m_geminiPanel) {
            m_geminiPanel->setScene(nullptr);
            m_geminiPanel->setNetManager(nullptr);
            m_geminiPanel->setProjectFilePath(QString());
        }
    }

    if (m_showDetailedLogAction) {
        m_showDetailedLogAction->setEnabled(current == m_simulationPanel);
    }
}

void SchematicEditor::closeTab(int index) {
    if (index < 0) return;
    QWidget* w = m_workspaceTabs->widget(index);

    if (w == m_simulationPanel) {
        m_workspaceTabs->removeTab(index);
        return; // Don't delete the simulation panel, just hide the tab
    }

    // LogicEditor guard: if we are closing the scene it's currently editing
    if (auto* view = qobject_cast<SchematicView*>(w)) {
        // Global undo commands reference item pointers from this scene.
        // Clearing prevents stale-pointer access after scene/tab teardown.
        if (m_undoStack) {
            m_undoStack->clear();
        }

        if (m_logicEditorPanel) {
            // If the logic IDE is editing an item in this scene, we MUST flush it
            m_logicEditorPanel->flushEdits();
            // If the logic IDE's active scene is the one we're closing, null it out
            if (m_scene == view->scene()) {
                m_logicEditorPanel->setScene(nullptr, nullptr);
            }
        }
        
        // Keep raw pointers for state cleanup before removing the tab.
        QGraphicsScene* scene = view->scene();

        // Clean up saved oscilloscope state for this tab
        if (m_simulationPanel && scene) {
            m_simulationPanel->removeTabState(scene);
        }

        // Save to closed tabs history before removing
        QString filePath = view->property("filePath").toString();
        if (!filePath.isEmpty()) {
            ClosedTabInfo info;
            info.filePath = filePath;
            info.scrollX = view->horizontalScrollBar()->value();
            info.scrollY = view->verticalScrollBar()->value();
            info.zoomLevel = view->transform().m11(); // Extract scale factor

            m_closedTabsHistory.append(info);

            // Limit history size
            while (m_closedTabsHistory.size() > MAX_CLOSED_TABS_HISTORY) {
                m_closedTabsHistory.removeFirst();
            }
        }

        if (auto* scene = view->scene()) {
            if (m_simulationPanel) m_simulationPanel->removeTabState(scene);
        }
        m_workspaceTabs->removeTab(index);
        
        if (m_scene == scene) {
            m_scene = nullptr;
            m_view = nullptr;
            m_netManager = nullptr;
            m_pageFrame = nullptr;
            m_currentFilePath.clear();
        }

        // View owns scene/net manager, so deleting view tears down tab resources safely.
        view->deleteLater();
    } else {
        m_workspaceTabs->removeTab(index);
        w->deleteLater();
    }

    if (m_workspaceTabs->count() == 0) {
        m_view = nullptr;
        m_scene = nullptr;
        m_netManager = nullptr;
        m_pageFrame = nullptr;
        m_currentFilePath.clear();
        updateBreadcrumbs();
        refreshHierarchyPanel();
    }
}
void SchematicEditor::onTabCloseRequested(int index) {
    closeTab(index);
}

bool SchematicEditor::canReuseTab(int index) const {
    if (index < 0 || index >= m_workspaceTabs->count()) return false;
    
    QWidget* w = m_workspaceTabs->widget(index);
    auto* view = qobject_cast<SchematicView*>(w);
    if (!view) return false;

    // Is it the initial "Schematic 1"?
    if (m_workspaceTabs->tabText(index) != "Schematic 1") return false;

    // Does it have a file path?
    if (!view->property("filePath").toString().isEmpty()) return false;

    // Is the scene actually empty (except for the page frame)?
    QGraphicsScene* scene = view->scene();
    if (scene) {
        // Find all items except the page frame
        int itemCount = 0;
        for (auto* item : scene->items()) {
            if (!dynamic_cast<SchematicPageItem*>(item)) {
                itemCount++;
            }
        }
        if (itemCount > 0) return false;
    }

    return true;
}

// ─── View / Zoom Handlers ───────────────────────────────────────────────────

void SchematicEditor::onZoomIn() {
    m_view->scale(1.2, 1.2);
}

void SchematicEditor::onZoomOut() {
    m_view->scale(1.0/1.2, 1.0/1.2);
}

void SchematicEditor::onZoomFit() {
    if (!m_scene || !m_view) return;

    QRectF itemsRect;
    bool hasItems = false;
    for (auto* item : m_scene->items()) {
        if (item == m_pageFrame) continue;
        
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->isSubItem()) continue;
            
            itemsRect = itemsRect.united(item->sceneBoundingRect());
            hasItems = true;
        }
    }

    QRectF fitRect;
    if (hasItems) {
        fitRect = itemsRect.adjusted(-100, -100, 100, 100);
    } else if (m_pageFrame) {
        fitRect = m_pageFrame->sceneBoundingRect().adjusted(-50, -50, 50, 50);
    } else {
        return;
    }

    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
        if (m_miniMap) m_miniMap->updateViewportRect();
    });
}

void SchematicEditor::onZoomAllComponents() {
    if (!m_scene || !m_view) return;

    QRectF rect;
    int count = 0;
    for (auto* item : m_scene->items()) {
        SchematicItem* si = dynamic_cast<SchematicItem*>(item);
        if (!si) continue;

        // Skip non-component items
        SchematicItem::ItemType type = si->itemType();
        if (type == SchematicItem::WireType || 
            type == SchematicItem::LabelType || 
            type == SchematicItem::NetLabelType ||
            type == SchematicItem::JunctionType ||
            type == SchematicItem::BusType ||
            type == SchematicItem::NoConnectType ||
            si->isSubItem()) {
            continue;
        }

        rect = rect.united(item->sceneBoundingRect());
        count++;
    }

    if (!rect.isValid() || count == 0) {
        statusBar()->showMessage("No components found to zoom to.", 2000);
        return;
    }

    // Add comfortable margin
    QRectF fitRect = rect.adjusted(-100, -100, 100, 100);

    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });

    statusBar()->showMessage(QString("Zoomed to %1 components").arg(count), 2000);
}

void SchematicEditor::onZoomSelection() {
    if (!m_scene || !m_view) return;

    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        statusBar()->showMessage("No items selected to zoom to.", 2000);
        return;
    }

    QRectF rect;
    for (auto* item : selected) {
        // Skip child sub-items (e.g. draggable labels) — use parent's rect instead
        SchematicItem* si = dynamic_cast<SchematicItem*>(item);
        if (si && si->isSubItem()) continue;

        rect = rect.united(item->sceneBoundingRect());
    }

    // If only sub-items were selected, fall back to all selected items
    if (rect.isNull()) {
        for (auto* item : selected) {
            rect = rect.united(item->sceneBoundingRect());
        }
    }

    if (!rect.isValid()) {
        statusBar()->showMessage("Cannot determine bounds of selection.", 2000);
        return;
    }

    // Enforce a minimum zoom area so single-pin items don't over-zoom
    const qreal minSize = 200.0;
    if (rect.width() < minSize) {
        qreal d = (minSize - rect.width()) / 2.0;
        rect.adjust(-d, 0, d, 0);
    }
    if (rect.height() < minSize) {
        qreal d = (minSize - rect.height()) / 2.0;
        rect.adjust(0, -d, 0, d);
    }

    // Add comfortable margin
    QRectF fitRect = rect.adjusted(-60, -60, 60, 60);

    // Defer fitInView so Qt finishes any pending layout/update events first.
    // This is necessary especially with MinimalViewportUpdate mode.
    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
    });

    statusBar()->showMessage(
        QString("Zoomed to %1 selected item(s)").arg(selected.size()), 2000);
}

void SchematicEditor::onZoomArea() {
    m_view->setCurrentTool("Zoom Area");
}

void SchematicEditor::onPageSizeChanged(const QString& size) {
    m_currentPageSize = size;
    updatePageFrame();
    statusBar()->showMessage("Page size changed to " + size, 2000);
}

void SchematicEditor::updateCoordinates(QPointF pos) {
    QString unit = m_view->property("currentUnit").toString();
    if (unit.isEmpty()) unit = "mm";

    double factor = 1.0;
    if (unit == "mil") factor = 39.3701;
    else if (unit == "in") factor = 0.0393701;

    m_coordLabel->setText(QString("X: %1  Y: %2 %3")
        .arg(pos.x() * factor, 0, 'f', unit == "mm" ? 2 : 1)
        .arg(pos.y() * factor, 0, 'f', unit == "mm" ? 2 : 1)
        .arg(unit));

    if (m_netManager) {
        QString netName = m_netManager->findNetAtPoint(pos);
        if (netName.isEmpty()) {
            m_netLabel->setText("Net: (none)");
        } else {
            m_netLabel->setText("Net: " + netName);
        }
    }

    if (m_mouseFollowPlacementActive && m_view && m_scene && !m_mouseFollowItems.isEmpty()) {
        const QPointF target = m_view->snapToGridOrPin(pos).point;
        const QPointF delta = target - m_mouseFollowAnchor;
        for (int i = 0; i < m_mouseFollowItems.size(); ++i) {
            SchematicItem* item = m_mouseFollowItems[i];
            if (!item || item->scene() != m_scene) continue;
            // Preserve the transform by resetting position only
            item->setPos(m_mouseFollowOriginalPositions.value(i) + delta);
        }
    }
}

void SchematicEditor::beginMouseFollowPlacement(const QList<SchematicItem*>& items, const QString& actionLabel) {
    if (!m_view || !m_scene || items.isEmpty()) return;

    endMouseFollowPlacement(false);

    QRectF bounds;
    bool hasBounds = false;
    for (SchematicItem* item : items) {
        if (!item || item->scene() != m_scene) continue;
        bounds = hasBounds ? bounds.united(item->sceneBoundingRect()) : item->sceneBoundingRect();
        hasBounds = true;
    }
    if (!hasBounds) return;

    const QPointF anchor = bounds.center();
    m_mouseFollowItems.clear();
    m_mouseFollowOriginalPositions.clear();
    m_mouseFollowOriginalRotations.clear();
    m_mouseFollowAnchor = anchor;
    m_mouseFollowFlippedH = false;
    m_mouseFollowFlippedV = false;
    m_mouseFollowRotation = 0;
    for (SchematicItem* item : items) {
        if (!item || item->scene() != m_scene) continue;
        m_mouseFollowItems.append(item);
        m_mouseFollowOriginalPositions.append(item->pos());
        m_mouseFollowOriginalRotations.append(item->rotation());
    }
    if (m_mouseFollowItems.isEmpty()) return;

    m_mouseFollowActionLabel = actionLabel;
    m_mouseFollowPlacementActive = true;
    qApp->installEventFilter(this);
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    m_view->viewport()->setFocusPolicy(Qt::StrongFocus);
    m_view->setFocus(Qt::OtherFocusReason);
    m_view->viewport()->setFocus(Qt::OtherFocusReason);

    // Temporarily clear only the shortcuts that conflict with placement controls.
    const QSet<QKeySequence> placementToolShortcuts = {
        QKeySequence("Esc"),
        QKeySequence("H"),
        QKeySequence("Shift+V"),
        QKeySequence("Ctrl+R"),
        QKeySequence("Ctrl+Shift+R")
    };
    const QSet<QKeySequence> placementManipShortcuts = {
        QKeySequence("H"),
        QKeySequence("Shift+V"),
        QKeySequence("Ctrl+R"),
        QKeySequence("Ctrl+Shift+R")
    };
    saveMatchingActionShortcuts(m_toolActions.values(), placementToolShortcuts, m_savedToolShortcuts);
    saveMatchingActionShortcuts(m_manipActions, placementManipShortcuts, m_savedManipShortcuts);

    const QPointF cursorScene = m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos()));
    const QPointF target = m_view->snapToGridOrPin(cursorScene).point;
    const QPointF delta = target - m_mouseFollowAnchor;
    for (int i = 0; i < m_mouseFollowItems.size(); ++i) {
        if (SchematicItem* item = m_mouseFollowItems[i]) {
            item->setPos(m_mouseFollowOriginalPositions[i] + delta);
        }
    }

    statusBar()->showMessage(QString("%1: move mouse to preview, left click to place, Esc to finish")
                             .arg(actionLabel), 5000);
}

void SchematicEditor::applyPlacementTransforms() {
    for (int i = 0; i < m_mouseFollowItems.size(); ++i) {
        SchematicItem* item = m_mouseFollowItems[i];
        if (!item || item->scene() != m_scene) continue;

        // Get item center for transform origin
        QRectF rect = item->boundingRect();
        QPointF center = rect.center();

        // Build complete transform around item center: original rotation + placement rotation + flip
        qreal baseRotation = m_mouseFollowOriginalRotations.value(i, 0);
        QTransform t;
        t.translate(center.x(), center.y());
        t.rotate(baseRotation + m_mouseFollowRotation);
        if (m_mouseFollowFlippedH) t.scale(-1, 1);
        if (m_mouseFollowFlippedV) t.scale(1, -1);
        t.translate(-center.x(), -center.y());
        
        item->setTransform(t, false); // Apply combined transform
    }
}

void SchematicEditor::endMouseFollowPlacement(bool cancel) {
    if (!m_mouseFollowPlacementActive) return;
    QList<SchematicItem*> activeItems = m_mouseFollowItems;

    restoreActionShortcuts(m_savedToolShortcuts);
    restoreActionShortcuts(m_savedManipShortcuts);
    updateComponentToolShortcutState();

    if (m_view) {
        m_view->removeEventFilter(this);
        m_view->viewport()->removeEventFilter(this);
    }
    qApp->removeEventFilter(this);
    m_mouseFollowPlacementActive = false;
    m_mouseFollowItems.clear();
    m_mouseFollowOriginalPositions.clear();
    m_mouseFollowAnchor = QPointF();
    m_mouseFollowActionLabel.clear();

    if (cancel && m_scene && m_undoStack && !activeItems.isEmpty()) {
        QList<SchematicItem*> toRemove;
        for (SchematicItem* item : activeItems) {
            if (item && item->scene() == m_scene) {
                toRemove.append(item);
            }
        }
        if (!toRemove.isEmpty()) {
            m_undoStack->push(new RemoveItemCommand(m_scene, toRemove));
        }
    }
}

void SchematicEditor::cancelPlacementMode() {
    if (m_mouseFollowPlacementActive) {
        endMouseFollowPlacement(true);
        statusBar()->showMessage("Placement canceled", 1200);
    }
}

// ─── Tool Selection ─────────────────────────────────────────────────────────

void SchematicEditor::onToolSelected() {
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString toolName = action->data().toString();
    if (toolName.isEmpty()) return;

    if (toolName == "Hand") {
        m_view->setHandToolActive(true);
        statusBar()->showMessage("Hand tool active: Click and drag to pan (H to toggle)", 3000);
        return;
    } else {
        m_view->setHandToolActive(false);
    }

    m_view->setCurrentTool(toolName);
    if (m_view) {
        m_view->setFocusPolicy(Qt::StrongFocus);
        if (m_view->viewport()) {
            m_view->viewport()->setFocusPolicy(Qt::StrongFocus);
            m_view->viewport()->setFocus(Qt::OtherFocusReason);
        }
        m_view->setFocus(Qt::OtherFocusReason);
    }

    if (qobject_cast<SchematicProbeTool*>(m_view->currentTool()) ||
        toolName == "Oscilloscope Instrument" ||
        toolName == "Voltmeter (DC)" ||
        toolName == "Voltmeter (AC)" ||
        toolName == "Ammeter (DC)" ||
        toolName == "Ammeter (AC)" ||
        toolName == "Wattmeter" ||
        toolName == "Power Meter" ||
        toolName == "Frequency Counter" ||
        toolName == "Logic Probe") {
        ensureProbeToolConnected();
        statusBar()->showMessage("Interactive Instrument / Probe active", 5000);
    } else if (toolName == "Select") {
        statusBar()->showMessage("Select and move items", 3000);
    } else {
        statusBar()->showMessage(toolName + " tool active", 3000);
    }
}

void SchematicEditor::ensureProbeToolConnected() {
    SchematicProbeTool* probeTool = qobject_cast<SchematicProbeTool*>(m_view ? m_view->currentTool() : nullptr);
    if (probeTool && m_simulationPanel) {
        // Ensure net data is fully updated before probing
        if (m_netManager && m_scene) {
            m_netManager->updateNets(m_scene);
        }
        
        connect(probeTool, &SchematicProbeTool::signalProbed,
                m_simulationPanel, &SimulationPanel::addProbe, Qt::UniqueConnection);
        connect(probeTool, &SchematicProbeTool::signalUnprobed,
                m_simulationPanel, &SimulationPanel::removeProbe, Qt::UniqueConnection);
        connect(probeTool, &SchematicProbeTool::signalDifferentialProbed,
                m_simulationPanel, &SimulationPanel::addDifferentialProbe, Qt::UniqueConnection);
        connect(probeTool, &SchematicProbeTool::signalClearAllProbes,
                this, &SchematicEditor::onClearAllProbeMarkers, Qt::UniqueConnection);
        connect(probeTool, &SchematicProbeTool::signalClearFocusedPaneProbes,
                m_simulationPanel, &SimulationPanel::onClearFocusedPaneProbes, Qt::UniqueConnection);
    }
}

// ─── Layout Optimization ────────────────────────────────────────────────────

void SchematicEditor::onOptimizeLayout() {
    if (m_layoutOptimizer && m_scene) {
        m_layoutOptimizer->optimizeLayout(m_scene);
        m_view->update();
        statusBar()->showMessage("Layout optimization completed", 3000);
    }
}

void SchematicEditor::onApplyOrthogonalRouting() {
    if (m_layoutOptimizer && m_scene) {
        m_layoutOptimizer->applyOrthogonalRouting(m_scene);
        m_view->update();
        statusBar()->showMessage("Orthogonal routing applied", 3000);
    }
}

void SchematicEditor::onMinimizeCrossings() {
    if (m_layoutOptimizer && m_scene) {
        m_layoutOptimizer->minimizeWireCrossings(m_scene);
        m_view->update();
        statusBar()->showMessage("Wire crossings minimized", 3000);
    }
}

void SchematicEditor::onSwitchToEngineeringTheme() {
    PCBTheme* engineeringTheme = new PCBTheme(PCBTheme::Engineering);
    ThemeManager::instance().setTheme(engineeringTheme);
    applyTheme();
    statusBar()->showMessage("Switched to engineering theme", 3000);
}

void SchematicEditor::onApplyAIStyleTransfer(const QString& stylePreset, const QString& customInstructions) {
    if (m_geminiPanel && !m_currentFilePath.isEmpty()) {
        // Trigger AI style transfer through Gemini panel
        QString prompt = QString("Apply %1 style transfer to this schematic.%2")
            .arg(stylePreset)
            .arg(customInstructions.isEmpty() ? QString() : " Additional instructions: " + customInstructions);
        
        m_geminiPanel->askPrompt(prompt, true);
        statusBar()->showMessage(QString("Applying %1 style transfer...").arg(stylePreset), 5000);
    } else if (!m_scene) {
        QMessageBox::warning(this, "Style Transfer", "No schematic open for style transfer.");
    } else {
        // Fallback: apply basic style changes directly
        statusBar()->showMessage(QString("AI style transfer ready: %1").arg(stylePreset), 3000);
    }
}

#include "../items/wire_item.h"
#include <QDoubleSpinBox>
#include <QLineEdit>

void SchematicEditor::updatePropertyBar() {
    if (!m_propertyBar || !m_scene) return;

    m_propertyBar->clear();
    QList<QGraphicsItem*> selected = m_scene->selectedItems();

    if (selected.isEmpty()) {
        return;
    }

    if (selected.size() == 1) {
        QGraphicsItem* item = selected.first();
        SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem) return;

        if (sItem->itemType() == SchematicItem::WireType) {
            WireItem* wire = dynamic_cast<WireItem*>(sItem);
            if (!wire) return;
            m_propertyBar->addWidget(new QLabel(" WIRE: "));
            
            // Width
            m_propertyBar->addWidget(new QLabel(" Width:"));
            QDoubleSpinBox* wSpin = new QDoubleSpinBox();
            wSpin->setRange(0.5, 10.0);
            wSpin->setSingleStep(0.25);
            wSpin->setValue(wire->pen().widthF());
            connect(wSpin, &QDoubleSpinBox::valueChanged, this, [this, wire](double val) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, wire, "Width", wire->pen().widthF(), val));
            });
            m_propertyBar->addWidget(wSpin);

            // Style
            m_propertyBar->addWidget(new QLabel(" Style:"));
            QComboBox* styleCombo = new QComboBox();
            styleCombo->addItems({"Solid", "Dash", "Dot"});
            styleCombo->setCurrentText(wire->pen().style() == Qt::SolidLine ? "Solid" : (wire->pen().style() == Qt::DashLine ? "Dash" : "Dot"));
            connect(styleCombo, &QComboBox::currentTextChanged, this, [this, wire](const QString& style) {
                m_undoStack->push(new ChangePropertyCommand(m_scene, wire, "Line Style", "", style));
            });
            m_propertyBar->addWidget(styleCombo);

        } else if (sItem->itemType() == SchematicItem::ComponentType || sItem->itemType() == SchematicItem::HierarchicalPortType) {
            m_propertyBar->addWidget(new QLabel(" ITEM: "));
            
            // Reference
            m_propertyBar->addWidget(new QLabel(" Ref:"));
            QLineEdit* refEdit = new QLineEdit(sItem->reference());
            refEdit->setMaximumWidth(80);
            connect(refEdit, &QLineEdit::editingFinished, this, [this, sItem, refEdit]() {
                if (refEdit->text() != sItem->reference()) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, sItem, "reference", sItem->reference(), refEdit->text(), m_projectDir));
                }
            });
            m_propertyBar->addWidget(refEdit);

            // Value
            m_propertyBar->addWidget(new QLabel(" Value:"));
            QLineEdit* valEdit = new QLineEdit(sItem->value());
            valEdit->setMaximumWidth(120);
            connect(valEdit, &QLineEdit::editingFinished, this, [this, sItem, valEdit]() {
                if (valEdit->text() != sItem->value()) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, sItem, "value", sItem->value(), valEdit->text(), m_projectDir));
                }
            });
            m_propertyBar->addWidget(valEdit);

            // Footprint (Quick View)
            if (!sItem->footprint().isEmpty()) {
                m_propertyBar->addWidget(new QLabel(" FP:"));
                QLabel* fpLabel = new QLabel(sItem->footprint());
                fpLabel->setStyleSheet("color: #888; font-style: italic;");
                m_propertyBar->addWidget(fpLabel);
            }
        }
    } else {
        m_propertyBar->addWidget(new QLabel(QString(" MULTI-SELECTION (%1 items)").arg(selected.size())));
    }
}

QStringList SchematicEditor::resolveConnectedInstrumentNets(SchematicItem* instrument) const {
    if (!instrument || !m_netManager) return {};

    QStringList nets;
    QSet<QString> seen;
    const qreal pinTolerance = 2.0;

    for (const QPointF& pinLocal : instrument->connectionPoints()) {
        const QPointF pinScene = instrument->mapToScene(pinLocal);
        const QString net = m_netManager->findNetAtPoint(pinScene);
        if (net.isEmpty() || seen.contains(net)) continue;

        const QList<NetConnection> conns = m_netManager->getConnections(net);

        bool pinBelongsToInstrument = false;
        for (const auto& conn : conns) {
            if (conn.item != instrument) continue;
            if (QLineF(conn.connectionPoint, pinScene).length() <= pinTolerance) {
                pinBelongsToInstrument = true;
                break;
            }
        }
        if (!pinBelongsToInstrument) continue;

        const bool hasWire = !m_netManager->getWiresInNet(net).isEmpty();
        bool hasOtherConnection = false;
        for (const auto& conn : conns) {
            if (conn.item != instrument ||
                QLineF(conn.connectionPoint, pinScene).length() > pinTolerance) {
                hasOtherConnection = true;
                break;
            }
        }

        // Skip dangling isolated pins to avoid plotting phantom channels.
        if (!hasWire && !hasOtherConnection) continue;

        seen.insert(net);
        nets.append(net);
    }

    return nets;
}

void SchematicEditor::onSimulationResultsReady(const SimResults& results) {
    if (!m_scene) return;

    if (results.analysisType == SimAnalysisType::RealTime) {
        // Convert Std maps to QMap for SchematicItem interface
        QMap<QString, double> nodeVoltages;
        for (const auto& [name, val] : results.nodeVoltages) nodeVoltages[name.c_str()] = val;        
        QMap<QString, double> currents;
        for (const auto& [name, val] : results.branchCurrents) currents[name.c_str()] = val;

        // Propagate state to all items (for LEDs, active labels, etc)
        for (auto* item : m_scene->items()) {
            if (auto* si = dynamic_cast<SchematicItem*>(item)) {
                si->setSimState(nodeVoltages, currents);
            }
        }
        
        // Show only volatile overlays (no history)
        showSimulationResults(results);
        return;
    }

    if (m_netManager) m_netManager->updateNets(m_scene);

    // --- Logic Analyzers ---
    for (auto* item : m_scene->items()) {
        auto* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem) continue;
        const QString typeName = sItem->itemTypeName().toLower();
        if (!typeName.contains("logicanalyzer") && !typeName.contains("logic analyzer")) continue;

        QString id = sItem->id().toString();
        if (id.isEmpty()) id = QString("LA_%1").arg(reinterpret_cast<quintptr>(sItem), 0, 16);

        if (!m_laWindows.contains(id)) {
            auto* win = new LogicAnalyzerWindow("Logic Analyzer - " + sItem->reference(), this);
            win->setInstrumentId(id);
            connect(win, &LogicAnalyzerWindow::windowClosing, this, [this](const QString& windowId) {
                m_laWindows.remove(windowId);
            });
            m_laWindows[id] = win;
        }

        const QStringList nets = resolveConnectedInstrumentNets(sItem);
        m_laWindows[id]->setChannels(nets);
        m_laWindows[id]->show();
        m_laWindows[id]->raise();
    }

    // 1. Convert maps for easier propagation
    QMap<QString, double> voltages;
    for (const auto& [name, val] : results.nodeVoltages) voltages[QString::fromStdString(name)] = val;
    
    QMap<QString, double> currents;
    for (const auto& [name, val] : results.branchCurrents) currents[QString::fromStdString(name)] = val;

    // 2. Update all scene items (LEDs, Switches, Markers)
    for (auto* item : m_scene->items()) {
        if (auto* sItem = dynamic_cast<SchematicItem*>(item)) {
            sItem->setSimState(voltages, currents);
        } else if (auto* marker = dynamic_cast<SchematicWaveformMarker*>(item)) {
            QString target = marker->netName();
            QString kind = marker->kind();
            
            for (const auto& wave : results.waveforms) {
                QString waveName = QString::fromStdString(wave.name);
                bool match = false;
                if (kind == "V") match = (waveName == "V(" + target + ")" || waveName == target);
                else if (kind == "I") match = (waveName == "I(" + target + ")");

                if (match) {
                    marker->updateData(QVector<double>(wave.xData.begin(), wave.xData.end()),
                                     QVector<double>(wave.yData.begin(), wave.yData.end()));
                    break;
                }
            }
        }
    }
    
    // 3. Show static overlays (Voltages/Currents at operating point)
    showSimulationResults(results);

    // 4. Update all active instrument windows
    for (auto* win : m_laWindows) {
        win->updateData(results);
    }

    for (auto* win : m_oscilloscopeWindows) {
        win->updateResults(results, m_netManager);
    }

    if (m_view) {
        const bool showByDock = (m_oscilloscopeDock && m_oscilloscopeDock->isVisible());
        m_view->setProbingEnabled(showByDock);
    }
}

void SchematicEditor::onOscilloscopeWindowClosing(const QUuid& id) {
    m_oscilloscopeWindows.remove(id);
}

void SchematicEditor::onOscilloscopeConfigChanged(const QUuid& id, const OscilloscopeItem::Config& cfg) {
    if (!m_scene) return;
    for (auto* item : m_scene->items()) {
        if (auto* osc = dynamic_cast<OscilloscopeItem*>(item)) {
            if (osc->id() == id) {
                osc->setConfig(cfg);
                break;
            }
        }
    }
}

void SchematicEditor::onOscilloscopePropertiesRequested(const QUuid& id) {
    if (!m_scene) return;
    for (auto* it : m_scene->items()) {
        if (auto* osc = dynamic_cast<OscilloscopeItem*>(it)) {
            if (osc->id() == id) {
                OscilloscopePropertiesDialog dlg(osc, m_undoStack, m_scene, this);
                dlg.exec();
                break;
            }
        }
    }
}

void SchematicEditor::openOscilloscopeWindow(SchematicItem* item) {
    if (!item) return;
    QUuid id = item->id();
    if (m_oscilloscopeWindows.contains(id)) {
        m_oscilloscopeWindows[id]->show();
        m_oscilloscopeWindows[id]->raise();
        return;
    }

    auto* win = new OscilloscopeWindow(id, item->reference(), this);
    m_oscilloscopeWindows[id] = win;
    
    connect(win, &OscilloscopeWindow::windowClosing, this, &SchematicEditor::onOscilloscopeWindowClosing);
    connect(win, &OscilloscopeWindow::configChanged, this, &SchematicEditor::onOscilloscopeConfigChanged);
    connect(win, &OscilloscopeWindow::propertiesRequested, this, &SchematicEditor::onOscilloscopePropertiesRequested);
    
    // For specific OscilloscopeItem, we can synchronize config if needed
    if (auto* osc = dynamic_cast<OscilloscopeItem*>(item)) {
        // Initial config sync
        // win->setConfig(osc->config()); 
    }

    // Auto-close window if the item is deleted from the schematic
    connect(item, &QObject::destroyed, win, &QWidget::close);

    win->show();
}

void SchematicEditor::onTimeTravelSnapshot(double t, const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& currents) {
    if (!m_scene) return;
    
    // Update all items with the snapshot state (LEDs, etc)
    for (auto* item : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            si->setSimState(nodeVoltages, currents);
        }
    }
    
    updateSimulationOverlays(nodeVoltages, currents);

    statusBar()->showMessage(QString("Time-Travel: %1 s").arg(t, 0, 'g', 4), 2000);
    }
void SchematicEditor::runLiveERC(const QList<SchematicItem*>& items) {
    if (!m_scene || !m_netManager || !m_view || items.isEmpty()) return;

    m_netManager->updateNets(m_scene);
    auto violations = SchematicERC::runLive(m_scene, items, m_netManager, m_ercRules);
    m_view->showLiveERCMarkers(violations);
}

void SchematicEditor::clearSimulationOverlays() {
    if (!m_scene) return;
    QList<QGraphicsItem*> toDelete;
    for (auto* item : m_scene->items()) {
        if (dynamic_cast<SimulationOverlayItem*>(item)) {
            toDelete.append(item);
        }
    }
    for (auto* item : toDelete) {
        m_scene->removeItem(item);
        delete item;
    }
}

void SchematicEditor::onOverlayVisibilityChanged(bool showVoltage, bool showCurrent) {
    m_showVoltageOverlays = showVoltage;
    m_showCurrentOverlays = showCurrent;
}

void SchematicEditor::onClearSimulationOverlays() {
    clearSimulationOverlays();
    
    if (m_view) {
        bool showByDock = (m_oscilloscopeDock && m_oscilloscopeDock->isVisible());
        m_view->setProbingEnabled(showByDock);
    }
    
    if (statusBar()) {
        statusBar()->showMessage("Simulation overlays cleared.", 3000);
    }
}

void SchematicEditor::onClearAllProbeMarkers() {
    if (!m_scene) return;
    QList<QGraphicsItem*> toDelete;
    for (auto* item : m_scene->items()) {
        if (dynamic_cast<SchematicWaveformMarker*>(item)) {
            toDelete.append(item);
        } else if (item->data(0).toString() == "probe_dot") {
            toDelete.append(item);
        }
    }
    for (auto* item : toDelete) {
        m_scene->removeItem(item);
        delete item;
    }
    // Also notify SimulationPanel if it was a global clear
    if (m_simulationPanel) m_simulationPanel->clearAllProbesPreserveX();
}

void SchematicEditor::removeProbeMarkerBySignalName(const QString& signalName) {
    if (!m_scene || signalName.isEmpty()) return;
    
    QString kind = "V", net = signalName;
    if (signalName.startsWith("V(", Qt::CaseInsensitive) && signalName.endsWith(")")) {
        QString core = signalName.mid(2, signalName.length() - 3);
        if (core.contains(",")) return;
        net = core; kind = "V";
    } else if (signalName.startsWith("I(", Qt::CaseInsensitive) && signalName.endsWith(")")) {
        net = signalName.mid(2, signalName.length() - 3); kind = "I";
    } else if (signalName.startsWith("P(", Qt::CaseInsensitive) && signalName.endsWith(")")) {
        net = signalName.mid(2, signalName.length() - 3); kind = "P";
    }

    const QString key = (kind + ":" + net).toUpper();
    QList<QGraphicsItem*> toDelete;
    for (auto* item : m_scene->items()) {
        if (auto* marker = dynamic_cast<SchematicWaveformMarker*>(item)) {
            if ((marker->kind() + ":" + marker->netName()).toUpper() == key) {
                toDelete.append(marker);
            }
        } else if (item->data(0).toString() == "probe_dot" && item->data(1).toString().toUpper() == key) {
            toDelete.append(item);
        }
    }
    for (auto* item : toDelete) {
        m_scene->removeItem(item);
        delete item;
    }
}

void SchematicEditor::showSimulationResults(const SimResults& results) {
    QMap<QString, double> nodeVoltages;
    for (const auto& [name, val] : results.nodeVoltages) nodeVoltages[QString::fromStdString(name)] = val;
    
    QMap<QString, double> currents;
    for (const auto& [name, val] : results.branchCurrents) currents[QString::fromStdString(name)] = val;

    if (m_view) {
        m_view->setSimulationResults(nodeVoltages, currents);
        m_view->setLastSimResults(&results);
    }
    updateSimulationOverlays(nodeVoltages, currents);
}

void SchematicEditor::updateSimulationOverlays(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& currents) {
    if (!m_scene) return;

    // 1. Clear existing simulation overlays
    clearSimulationOverlays();

    if (nodeVoltages.empty() && currents.empty()) return;

    // 2. Add Voltage Overlays
    if (m_showVoltageOverlays) {
        for (auto it = nodeVoltages.begin(); it != nodeVoltages.end(); ++it) {
            QString netName = it.key();
            double val = it.value();
            if (netName == "0") continue; // Ground is obvious

            // Find a representative point for this net
            QPointF overlayPos;
            bool found = false;

            for (auto* item : m_scene->items()) {
                if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
                    // If it's a wire or label, we can use its position
                    if (sItem->itemType() == SchematicItem::LabelType || sItem->itemType() == SchematicItem::NetLabelType) {
                        if (sItem->value() == netName) {
                            overlayPos = sItem->scenePos() + QPointF(0, -20);
                            found = true;
                            break;
                        }
                    }
                }
            }

            if (!found && m_netManager) {
                // Fallback: Use first connection point of the net
                auto conns = m_netManager->getConnections(netName);
                if (!conns.isEmpty()) {
                    overlayPos = conns.first().connectionPoint + QPointF(10, -10);
                    found = true;
                }
            }

            if (found) {
                QString text = QString::number(val, 'f', 2) + " V";
                auto* overlay = new SimulationOverlayItem(text, overlayPos, SimulationOverlayItem::Voltage);
                m_scene->addItem(overlay);
            }
        }
    }

    // 3. Add Current Overlays
    if (m_showCurrentOverlays) {
        for (auto it = currents.begin(); it != currents.end(); ++it) {
            QString branchName = it.key();
            double val = it.value();
            // branchName is usually the component reference (e.g., "R1")

            for (auto* item : m_scene->items()) {
                if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
                    if (sItem->reference() == branchName) {
                        QString text = QString::number(val * 1000.0, 'f', 2) + " mA";
                        auto* overlay = new SimulationOverlayItem(text, sItem->scenePos() + QPointF(0, 20), SimulationOverlayItem::Current);
                        m_scene->addItem(overlay);
                        break;
                    }
                }
            }
        }
    }
}

void SchematicEditor::openSymbolEditorWindow(const QString& name, const SymbolDefinition& preBuiltDef) {
    SymbolEditor* editor;
    const bool autoSaveToLibrary = preBuiltDef.isValid();
    if (preBuiltDef.isValid()) {
        editor = new SymbolEditor(preBuiltDef, nullptr);
    } else {
        editor = new SymbolEditor(nullptr);
    }
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->setWindowTitle("Symbol Editor - " + name);
    QString projectKey = m_projectDir;
    if (projectKey.isEmpty() && !m_currentFilePath.isEmpty()) {
        projectKey = QFileInfo(m_currentFilePath).absolutePath();
    }
    editor->setProjectKey(projectKey);
    
    // When a symbol is saved in the editor, optionally auto-save to default library
    connect(editor, &SymbolEditor::symbolSaved, this, [this, autoSaveToLibrary](const SymbolDefinition& sym) {
        if (autoSaveToLibrary) {
            SymbolDefinition fixed = sym;
            const QString rawPath = fixed.modelPath().trimmed();
            if (!rawPath.isEmpty() && !QFileInfo(rawPath).isAbsolute()) {
                const QString fallback = QDir(QDir::homePath() + "/ViospiceLib/sub").filePath(rawPath);
                if (QFileInfo::exists(fallback)) fixed.setModelPath(fallback);
            }

            SymbolLibrary* lib = ensureDefaultUserSymbolLibrary();
            if (lib) {
                lib->addSymbol(fixed);
                if (lib->save()) {
                    LibraryIndex::instance().addSymbol(fixed.name(), lib->name(), fixed.category());
                }
            }
        }
        if (m_componentsPanel) m_componentsPanel->populate();
    });

    // Support "Place in Schematic" directly from the editor
    connect(editor, &SymbolEditor::placeInSchematicRequested, this, &SchematicEditor::onPlaceSymbolInSchematic);
    editor->show();
}

void SchematicEditor::addModelArchitectTab() {
    auto* architect = new SpiceModelArchitect(this);
    int idx = m_workspaceTabs->addTab(architect, getThemeIcon(":/icons/tool_gear.svg"), "SPICE Architect");
    m_workspaceTabs->setCurrentIndex(idx);
}

void SchematicEditor::addSimulationTab(const QString& name) {
    if (!m_scene || !m_netManager || !m_simulationPanel) return;

    if (m_oscilloscopeDock) {
        refreshOscilloscopeDockContent();
        m_oscilloscopeDock->setFloating(false);
        m_oscilloscopeDock->show();
    }

    int idx = m_workspaceTabs->addTab(m_simulationPanel, getThemeIcon(":/icons/tool_oscilloscope.svg"), name);
    m_workspaceTabs->setCurrentIndex(idx);
}

void SchematicEditor::addImageTab(const QString& filePath) {
    auto* preview = new ImagePreviewPanel(this);
    if (preview->loadImage(filePath)) {
        preview->setProperty("filePath", filePath);
        int idx = m_workspaceTabs->addTab(preview, getThemeIcon(":/icons/toolbar_file.png"), QFileInfo(filePath).fileName());
        m_workspaceTabs->setCurrentIndex(idx);
        statusBar()->showMessage(QString("Opened image: %1").arg(filePath), 3000);
    } else {
        delete preview;
        QMessageBox::warning(this, "Open Image", "Failed to load image: " + filePath);
    }
}

void SchematicEditor::onToggleLeftSidebar() {
    bool visible = false;
    if (m_componentDock && m_componentDock->isVisible()) visible = true;
    else if (m_projectExplorerDock && m_projectExplorerDock->isVisible()) visible = true;
    else if (m_geminiDock && m_geminiDock->isVisible()) visible = true;
    else if (m_hierarchyDock && m_hierarchyDock->isVisible()) visible = true;

    if (m_componentDock) m_componentDock->setVisible(!visible);
    if (m_projectExplorerDock) m_projectExplorerDock->setVisible(!visible);
    if (m_geminiDock) m_geminiDock->setVisible(!visible);
    if (m_hierarchyDock) m_hierarchyDock->setVisible(!visible);
    ConfigManager::instance().saveWindowState("SchematicEditor", saveGeometry(), saveState());
}

void SchematicEditor::onToggleBottomPanel() {
    if (m_oscilloscopeDock) {
        refreshOscilloscopeDockContent();
        m_oscilloscopeDock->setVisible(!m_oscilloscopeDock->isVisible());
    }
    ConfigManager::instance().saveWindowState("SchematicEditor", saveGeometry(), saveState());
}

void SchematicEditor::onToggleRightSidebar() {
    bool visible = false;
    if (m_ercDock && m_ercDock->isVisible()) visible = true;
    else if (m_sourceControlDock && m_sourceControlDock->isVisible()) visible = true;

    if (m_ercDock) m_ercDock->setVisible(!visible);
    if (m_sourceControlDock) m_sourceControlDock->setVisible(!visible);
    ConfigManager::instance().saveWindowState("SchematicEditor", saveGeometry(), saveState());
}
void SchematicEditor::onToggleMiniMap(bool visible) {
    if (visible) {
        if (!m_miniMap) {
            m_miniMap = new SchematicMiniMap(m_view, m_view); // Make it a child of the current view
            m_miniMap->setFixedSize(220, 160);
        }
        
        // Ensure it's child of the current active view
        if (m_view && m_miniMap->parent() != m_view) {
            m_miniMap->setParent(m_view);
        }

        m_miniMap->setScene(m_scene);
        m_miniMap->updateViewportRect();
        m_miniMap->show();
        m_miniMap->raise();
        
        // Position at bottom-right of viewport
        if (m_view) {
            int x = m_view->viewport()->width() - m_miniMap->width() - 20;
            int y = m_view->viewport()->height() - m_miniMap->height() - 20;
            m_miniMap->move(x, y);
        }

        if (m_view) {
            connect(m_view, &SchematicView::transformationChanged, m_miniMap, &SchematicMiniMap::updateViewportRect, Qt::UniqueConnection);
        }
    } else {
        if (m_miniMap) m_miniMap->hide();
    }
}
