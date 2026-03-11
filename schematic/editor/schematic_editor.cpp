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
#include "../../symbols/symbol_editor.h"
#include "../../ui/spice_model_architect.h"
#include "schematic_api.h"
#include "schematic_commands.h"
#include "schematic_tool_registry_builtin.h"
#include "schematic_item_registry.h"
#include "schematic_item.h"
#include "schematic_page_item.h"
#include "schematic_connectivity.h"
#include "../analysis/schematic_erc.h"
#include "theme_manager.h"
#include "flux/core/net_manager.h"
#include "schematic_layout_optimizer.h"
#include "../../core/config_manager.h"
#include <QTimer>
#include <QMessageBox>
#include <QCloseEvent>
#include "../items/schematic_sheet_item.h"
#include "../items/simulation_overlay_item.h"
#include "../items/schematic_waveform_marker.h"
#include "../tools/schematic_probe_tool.h"
#include "../../simulator/core/sim_engine.h"
#include "../ui/simulation_panel.h"
#include "../ui/instrument_window.h"
#include "../ui/logic_analyzer_window.h"
#include "../ui/logic_editor_panel.h"
#include "../items/smart_signal_item.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsRectItem>
#include <QWheelEvent>
#include <QScrollBar>
#include <QLineF>
#include <QStatusBar>
#include <QDir>

// ─── Constructor / Destructor ────────────────────────────────────────────────

#include "../../core/sync_manager.h"
#include "schematic_item.h"
#include "schematic_menu_registry.h"

SchematicEditor::SchematicEditor(QWidget *parent)
    : QMainWindow(parent),
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
      m_ercList(nullptr),
      m_simulationPanel(nullptr),
      m_geminiDock(nullptr),
      m_scriptDock(nullptr),
      m_scriptPanel(nullptr),
      m_logicEditorPanel(nullptr),
      m_breadcrumbWidget(nullptr),
      m_runSimMenuAction(nullptr),
      m_stopSimMenuAction(nullptr),
      m_runSimToolbarAction(nullptr),
      m_stopSimToolbarAction(nullptr),
      m_coordLabel(nullptr),
      m_gridLabel(nullptr),
      m_layerLabel(nullptr),
      m_isModified(false),
      m_simulationRunning(false),
      m_showVoltageOverlays(true),
      m_showCurrentOverlays(true),
      m_undoStack(new QUndoStack(this)),
      m_api(new SchematicAPI(nullptr, m_undoStack, this)),
      m_updatingProperties(false),
      m_hierarchyDock(nullptr),
      m_hierarchyTree(nullptr),
      m_ercRules(SchematicERCRules::defaultRules())
{
    setWindowTitle("Viora EDA - Schematic Editor");
    setMinimumSize(640, 480);
    resize(1024, 720);
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

    // Core setup
    createStatusBar();
    setupCanvas();
    createDockWidgets();
    createMenuBar();
    createToolBar();
    createDrawingToolbar();
    connectSimulationSignals();
    updateSimulationUiState(false);
    applyTheme();

    // Set default tool
    m_view->setCurrentTool("Select");

    // Smart Cross-Probing from PCB
    connect(&SyncManager::instance(), &SyncManager::crossProbeReceived, this, &SchematicEditor::onCrossProbeReceived);
    
    // ECO / Netlist Synchronization from PCB or Reverse Engineering
    connect(&SyncManager::instance(), &SyncManager::ecoAvailable, this, &SchematicEditor::handleIncomingECO);

    // Auto-Save Setup
    if (ConfigManager::instance().autoSaveEnabled()) {
        QTimer* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &SchematicEditor::onSaveSchematic);
        timer->start(ConfigManager::instance().autoSaveInterval() * 60000);
    }
}

SchematicEditor::~SchematicEditor() {
    for (auto* win : m_instrumentWindows) delete win;
    for (auto* win : m_laWindows) delete win;
}

void SchematicEditor::closeEvent(QCloseEvent* event) {
    if (m_undoStack->index() != 0 || m_isModified) {
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

    // Save UI State
    ConfigManager::instance().saveWindowState("SchematicEditor", saveGeometry(), saveState());
    
    event->accept();
}

// ─── Canvas Setup ────────────────────────────────────────────────────────────

void SchematicEditor::setupCanvas() {
    setCentralWidget(m_workspaceTabs);
    
    // Initial schematic
    addSchematicTab("Schematic 1");

    m_layoutOptimizer = new SchematicLayoutOptimizer(this);

    // Restore UI State
    QByteArray geom = ConfigManager::instance().windowGeometry("SchematicEditor");
    QByteArray state = ConfigManager::instance().windowState("SchematicEditor");
    if (!geom.isEmpty()) restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

void SchematicEditor::addSchematicTab(const QString& name) {
    auto* scene = new QGraphicsScene(this);
    scene->setSceneRect(-5000, -5000, 10000, 10000);
    
    auto* view = new SchematicView(this);
    view->setScene(scene);
    view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    view->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFocusPolicy(Qt::StrongFocus);

    auto* netManager = new NetManager(this);
    view->setNetManager(netManager);
    view->setUndoStack(m_undoStack);

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
    connect(view, &SchematicView::itemSelectionDoubleClicked, this, &SchematicEditor::onSelectionDoubleClicked);
    connect(view, &SchematicView::syncSheetRequested, this, [this, scene](SchematicSheetItem* sheet) {
        if (sheet) {
            sheet->updatePorts(m_projectDir);
            SchematicConnectivity::updateVisualConnections(scene);
            statusBar()->showMessage("Synchronized pins for: " + sheet->sheetName(), 3000);
        }
    });
    connect(view, &SchematicView::runLiveERC, this, &SchematicEditor::runLiveERC);

    int idx = m_workspaceTabs->addTab(view, getThemeIcon(":/icons/file_flux_sch.png"), name);
    m_workspaceTabs->setCurrentIndex(idx);

    // Initial page frame for this new sheet
    m_view = view;
    m_scene = scene;
    m_netManager = netManager;
    m_pageFrame = nullptr; // Let updatePageFrame create it for this new scene
    updatePageFrame();

    // Create Logic Editor (each schematic gets one potentially, or shared)
    // For now, keep shared logic editor but update its scene
    if (!m_logicEditorPanel) {
        m_logicEditorPanel = new LogicEditorPanel(scene, netManager, this);
        connect(m_logicEditorPanel, &LogicEditorPanel::closed, this, [this]() {
            m_logicEditorPanel->setTargetBlock(nullptr);
        });
    }

    if (m_api) m_api->setScene(m_scene);
    
    view->setFocus();
}

void SchematicEditor::onTabChanged(int index) {
    if (index < 0) return;
    
    QWidget* current = m_workspaceTabs->widget(index);
    if (auto* view = qobject_cast<SchematicView*>(current)) {
        m_view = view;
        m_scene = view->scene();
        m_netManager = view->netManager();
        if (m_api) m_api->setScene(m_scene);
        
        // Find page frame in this scene
        for (auto* item : m_scene->items()) {
            if (auto* pf = dynamic_cast<SchematicPageItem*>(item)) {
                m_pageFrame = pf;
                break;
            }
        }
        
        if (m_logicEditorPanel) m_logicEditorPanel->setScene(m_scene, m_netManager);
        
        onSelectionChanged();
        updateCoordinates(m_view->mapToScene(m_view->mapFromGlobal(QCursor::pos())));
    } else if (QString(current->metaObject()->className()) == "SymbolEditor") {
        // Contextually disable schematic docks/toolbars if needed
    }
}

void SchematicEditor::closeTab(int index) {
    if (index < 0) return;
    QWidget* w = m_workspaceTabs->widget(index);

    if (w == m_simulationPanel) {
        m_simulationPanel = nullptr;
    }

    // Don't close the last schematic if you want to keep one always open,
    // or handle "no tabs" state.
    m_workspaceTabs->removeTab(index);
    w->deleteLater();

    if (m_workspaceTabs->count() == 0) {
        m_view = nullptr;
        m_scene = nullptr;
        m_netManager = nullptr;
        m_pageFrame = nullptr;
    }
}
void SchematicEditor::onTabCloseRequested(int index) {
    closeTab(index);
}

// ─── View / Zoom Handlers ───────────────────────────────────────────────────

void SchematicEditor::onZoomIn() {
    m_view->scale(1.2, 1.2);
}

void SchematicEditor::onZoomOut() {
    m_view->scale(1.0/1.2, 1.0/1.2);
}

void SchematicEditor::onZoomFit() {
    if (!m_view) return;
    QRectF fitRect;
    if (m_pageFrame) {
        fitRect = m_pageFrame->sceneBoundingRect().adjusted(-50, -50, 50, 50);
    } else if (m_scene && m_scene->itemsBoundingRect().isValid()) {
        fitRect = m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);
    } else {
        return;
    }
    QTimer::singleShot(0, m_view, [this, fitRect]() {
        m_view->fitInView(fitRect, Qt::KeepAspectRatio);
        m_view->viewport()->update();
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
}

// ─── Tool Selection ─────────────────────────────────────────────────────────

void SchematicEditor::onToolSelected() {
    QAction *action = qobject_cast<QAction*>(sender());
    if (!action) return;

    QString toolName = action->data().toString();
    if (toolName.isEmpty()) return;

    m_view->setCurrentTool(toolName);

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
            WireItem* wire = static_cast<WireItem*>(sItem);
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
        // Update all open instruments with live data
        for (auto* win : m_instrumentWindows.values()) win->updateData(results);
        
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

    // Ensure oscilloscope windows exist even when simulation is started from the
    // 0. Proactively find/create instrument windows for any oscilloscopes on the sheet.
    bool foundHardwareScope = false;
    for (auto* item : m_scene->items()) {
        auto* sItem = dynamic_cast<SchematicItem*>(item);
        if (!sItem) continue;
        const QString typeName = sItem->itemTypeName().toLower();
        const QString ref = sItem->reference().toLower();
        
        if (!typeName.contains("oscilloscope") && !ref.startsWith("osc")) continue;

        foundHardwareScope = true;
        QString id = sItem->id().toString();
        if (id.isEmpty()) id = QString("OSC_%1").arg(reinterpret_cast<quintptr>(sItem), 0, 16);

        if (!m_instrumentWindows.contains(id)) {
            auto* win = new InstrumentWindow("Oscilloscope - " + sItem->reference(), this);
            win->setInstrumentId(id);
            m_instrumentWindows[id] = win;
        }

        const QStringList nets = resolveConnectedInstrumentNets(sItem);
        m_instrumentWindows[id]->setChannels(nets);
        m_instrumentWindows[id]->show();
        m_instrumentWindows[id]->raise();
    }

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
            m_laWindows[id] = win;
        }

        const QStringList nets = resolveConnectedInstrumentNets(sItem);
        m_laWindows[id]->setChannels(nets);
        m_laWindows[id]->show();
        m_laWindows[id]->raise();
    }

    // Support system-probes viewer if no hardware scope is placed
    QStringList probedNets;
    for (auto* item : m_scene->items()) {
        if (auto* m = dynamic_cast<SchematicWaveformMarker*>(item)) probedNets << m->netName();
    }
    if (!probedNets.isEmpty() && !foundHardwareScope) {
        QString id = "SYSTEM_PROBE_SCOPE";
        if (!m_instrumentWindows.contains(id)) {
            auto* win = new InstrumentWindow("Waveform Viewer (Probes)", this);
            win->setInstrumentId(id);
            m_instrumentWindows[id] = win;
        }
        m_instrumentWindows[id]->setChannels(probedNets);
        m_instrumentWindows[id]->show();
        m_instrumentWindows[id]->raise();
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
    for (auto* win : m_instrumentWindows) {
        win->updateData(results);
    }
    for (auto* win : m_laWindows) {
        win->updateData(results);
    }
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
    
    // Update all active instrument windows with the time cursor
    for (auto* win : m_instrumentWindows) {
        win->setTimeCursor(t);
    }
    
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
    for (auto* item : m_scene->items()) {
        if (dynamic_cast<SimulationOverlayItem*>(item)) {
            m_scene->removeItem(item);
            delete item;
        }
    }
}

void SchematicEditor::onOverlayVisibilityChanged(bool showVoltage, bool showCurrent) {
    m_showVoltageOverlays = showVoltage;
    m_showCurrentOverlays = showCurrent;
}

void SchematicEditor::onClearSimulationOverlays() {
    clearSimulationOverlays();
    if (statusBar()) {
        statusBar()->showMessage("Simulation overlays cleared.", 3000);
    }
}

void SchematicEditor::showSimulationResults(const SimResults& results) {
    QMap<QString, double> nodeVoltages;
    for (const auto& [name, val] : results.nodeVoltages) nodeVoltages[QString::fromStdString(name)] = val;
    
    QMap<QString, double> currents;
    for (const auto& [name, val] : results.branchCurrents) currents[QString::fromStdString(name)] = val;

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

void SchematicEditor::openSymbolEditorWindow(const QString& name) {
    auto* editor = new SymbolEditor(nullptr); // Standalone window
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->setWindowTitle("Symbol Editor - " + name);
    
    // When a symbol is saved in the editor, we should refresh our library browser
    connect(editor, &SymbolEditor::symbolSaved, this, [this](const SymbolDefinition&) {
        if (m_componentsPanel) m_componentsPanel->populate();
    });

    // Support "Place in Schematic" directly from the editor
    connect(editor, &SymbolEditor::placeInSchematicRequested, this, &SchematicEditor::onPlaceSymbolInSchematic);

    editor->show();
    editor->raise();
    editor->activateWindow();
}

void SchematicEditor::addModelArchitectTab() {
    auto* architect = new SpiceModelArchitect(this);
    int idx = m_workspaceTabs->addTab(architect, getThemeIcon(":/icons/tool_gear.svg"), "SPICE Architect");
    m_workspaceTabs->setCurrentIndex(idx);
}

void SchematicEditor::addSimulationTab(const QString& name) {
    if (!m_scene || !m_netManager) return;

    m_simulationPanel = new SimulationPanel(m_scene, m_netManager, m_projectDir, this);

    SimulationPanel::AnalysisConfig pCfg;
    pCfg.type = m_simConfig.type;
    pCfg.stop = m_simConfig.stop;
    pCfg.step = m_simConfig.step;
    pCfg.fStart = m_simConfig.fStart;
    pCfg.fStop = m_simConfig.fStop;
    pCfg.pts = m_simConfig.pts;
    m_simulationPanel->setAnalysisConfig(pCfg);

    connect(m_simulationPanel, &SimulationPanel::resultsReady, this, &SchematicEditor::onSimulationResultsReady);
    connect(m_simulationPanel, &SimulationPanel::timeSnapshotReady, this, &SchematicEditor::onTimeTravelSnapshot);
    connect(m_simulationPanel, &SimulationPanel::probeRequested, this, [this]() {
        m_view->setCurrentTool("Probe");
        ensureProbeToolConnected();
        statusBar()->showMessage("Click on a net or pin to probe signal", 5000);
    });
    connect(m_simulationPanel, &SimulationPanel::placementToolRequested, this, [this](const QString& toolName) {
        if (!m_view) return;
        m_view->setCurrentTool(toolName);
        if (toolName == "Probe" ||
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
        }
        statusBar()->showMessage(QString("Placement tool active: %1").arg(toolName), 4000);
    });
    connect(m_simulationPanel, &SimulationPanel::simulationTargetRequested, this,
            [this](const QString& type, const QString& id) {
        navigateToSimulationTarget(type, id);
        if (m_view) m_view->setFocus();
        statusBar()->showMessage(QString("Navigated to %1: %2").arg(type, id), 4000);
    });
    connect(m_simulationPanel, &SimulationPanel::overlayVisibilityChanged,
            this, &SchematicEditor::onOverlayVisibilityChanged, Qt::UniqueConnection);
    connect(m_simulationPanel, &SimulationPanel::clearOverlaysRequested,
            this, &SchematicEditor::onClearSimulationOverlays, Qt::UniqueConnection);

    if (m_oscilloscopeDock) {
        m_oscilloscopeDock->setWidget(m_simulationPanel->getOscilloscopeContainer());
        m_oscilloscopeDock->setFloating(false);
        m_oscilloscopeDock->show();
    }

    int idx = m_workspaceTabs->addTab(m_simulationPanel, getThemeIcon(":/icons/tool_run.svg"), name);
    m_workspaceTabs->setCurrentIndex(idx);
}

void SchematicEditor::onToggleLeftSidebar() {
    bool visible = false;
    if (m_componentDock && m_componentDock->isVisible()) visible = true;
    else if (m_projectExplorerDock && m_projectExplorerDock->isVisible()) visible = true;
    else if (m_geminiDock && m_geminiDock->isVisible()) visible = true;
    else if (m_scriptDock && m_scriptDock->isVisible()) visible = true;

    if (m_componentDock) m_componentDock->setVisible(!visible);
    if (m_projectExplorerDock) m_projectExplorerDock->setVisible(!visible);
    if (m_geminiDock) m_geminiDock->setVisible(!visible);
    if (m_scriptDock) m_scriptDock->setVisible(!visible);
}

void SchematicEditor::onToggleBottomPanel() {
    // Bottom panel currently empty after moving ERC
}

void SchematicEditor::onToggleRightSidebar() {
    bool visible = false;
    if (m_hierarchyDock && m_hierarchyDock->isVisible()) visible = true;
    if (m_ercDock && m_ercDock->isVisible()) visible = true;

    if (m_hierarchyDock) m_hierarchyDock->setVisible(!visible);
    if (m_ercDock) m_ercDock->setVisible(!visible);
}

