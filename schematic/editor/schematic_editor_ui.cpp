// schematic_editor_ui.cpp
// Menu bar, toolbar, dock widgets, status bar creation for SchematicEditor

#include "schematic_editor.h"
#include "schematic_file_io.h"
#include "../analysis/schematic_erc.h"
#include "theme_manager.h"
#include "../../python/gemini_panel.h"
#include "../../core/config_manager.h"
#include "schematic_commands.h"
#include "spice_directive_classifier.h"
#include "../dialogs/spice_mean_dialog.h"
#include "../dialogs/spice_step_dialog.h"
#include "../../symbols/models/symbol_definition.h"
#include "../items/generic_component_item.h"
#include "../ui/schematic_components_widget.h"
#include "../ui/schematic_hierarchy_panel.h"
#include "../ui/simulation_panel.h"
#include "../ui/logic_analyzer_window.h"
#include "../../ui/source_control_panel.h"
#include "../ui/schematic_minimap.h"
#include "../../symbols/symbol_library.h"
#include "../dialogs/simulation_debugger_dialog.h"
#include "../dialogs/circuit_template_gallery.h"
#include <QTreeWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include "../dialogs/spice_directive_dialog.h"
#include "../../simulator/bridge/sim_manager.h"
#include "../tools/schematic_zoom_area_tool.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;
#include "../items/schematic_item.h"
#include "../items/smart_signal_item.h"
#include "../ui/logic_editor_panel.h"
#include "../items/schematic_waveform_marker.h"
#include "../../simulator/bridge/sim_schematic_bridge.h"
#include "../../ui/source_control_panel.h"

#include <QMenuBar>
#include <QMenu>
#include <QActionGroup>
#include <QComboBox>
#include <QPainter>
#include <QFrame>
#include <QLabel>
#include <QListWidget>
#include <QStatusBar>
#include <QGraphicsItem>
#include <QDir>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QTimer>
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QTemporaryFile>
#include <QTextStream>
#include <QPointer>
#include <array>
#include "../io/netlist_to_schematic.h"
#include "../../core/ws_server.h"

void SchematicEditor::createMenuBar() {
    // Hide traditional menu bar for modern UI
    menuBar()->hide();
}

QIcon SchematicEditor::getThemeIcon(const QString& path) {
    QIcon icon(path);
    if (!ThemeManager::theme()) return icon;

    // List of icons that should keep their original multi-color design
    static const QStringList multiColorIcons = {
        "probe", "ammeter", "voltmeter", "power_meter", "scissor", "n-v-probe", "p-v-probe"
    };

    bool isMultiColor = false;
    for (const auto& tag : multiColorIcons) {
        if (path.contains(tag, Qt::CaseInsensitive)) {
            isMultiColor = true;
            break;
        }
    }

    if (isMultiColor) {
        return icon;
    }

    // Tint monochrome icons for the active theme so they remain visible on both
    // light and dark backgrounds.
    QPixmap pixmap = icon.pixmap(QSize(32, 32));
    if (pixmap.isNull()) return icon;

    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), ThemeManager::theme()->textColor());
    painter.end();
    return QIcon(pixmap);
}

void SchematicEditor::refreshOscilloscopeDockContent() {
    if (!m_oscilloscopeDock || !m_simulationPanel) return;

    const bool showFullPanel = ConfigManager::instance()
                                   .toolProperty("SimulationPanel", "showFullPanelInDock", false)
                                   .toBool();
    QWidget* targetWidget = showFullPanel
        ? static_cast<QWidget*>(m_simulationPanel)
        : m_simulationPanel->getOscilloscopeContainer();

    if (!targetWidget) {
        targetWidget = m_simulationPanel;
    }

    if (m_oscilloscopeDock->widget() == targetWidget) {
        return;
    }

    m_oscilloscopeDock->setWidget(targetWidget);
}

// Helper to create simple programmatic icons for components/tools
QIcon SchematicEditor::createComponentIcon(const QString& name) {
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Use theme color if possible, else fallback
    QColor color = Qt::white;
    if (ThemeManager::theme()) {
        color = ThemeManager::theme()->textColor();
    }
    QPen pen(color, 2);
    painter.setPen(pen);

    if (name == "Select") {
        return getThemeIcon(":/icons/tool_select.svg");
    } else if (name == "Zoom Area") {
        return getThemeIcon(":/icons/tool_zoom_area.svg");
    } else if (name == "Zoom Components") {
        return getThemeIcon(":/icons/view_zoom_components.svg");
    } else if (name == "Sync") {
        return getThemeIcon(":/icons/tool_sync.svg");
    } else if (name == "Leave Sheet") {
        painter.drawLine(8, 16, 24, 16); // Arrow shaft
        painter.drawLine(8, 16, 16, 8);  // Arrow head top
        painter.drawLine(8, 16, 16, 24); // Arrow head bottom
        painter.drawRect(22, 10, 4, 12); // Vertical bar (door/up)
    } else if (name == "Wire") {
        return getThemeIcon(":/icons/tool_wire.svg");
    } else if (name == "Probe" || name == "Voltage Probe" || name == "Current Probe" || name == "Power Probe" || name == "Logic Probe" || name == "Simulator") {
        QString iconPath = ":/icons/tool_probe.svg";
        if (name == "Voltage Probe") iconPath = ":/icons/tool_voltage_probe.svg";
        else if (name == "Current Probe") iconPath = ":/icons/tool_current_probe.svg";
        else if (name == "Power Probe") iconPath = ":/icons/tool_power_probe.svg";
        return getThemeIcon(iconPath);
    } else if (name == "Bus") {
        return getThemeIcon(":/icons/tool_bus.svg");
    } else if (name == "Bus Entry") {
        return getThemeIcon(":/icons/tool_bus_entry.svg");
    } else if (name == "No-Connect") {
        return getThemeIcon(":/icons/tool_no_connect.svg");
    } else if (name == "Scissors" || name == "Erase") {
        return getThemeIcon(":/icons/tool_scissors.svg");
    } else if (name == "Resistor" || name == "Resistor (US)" || name == "Resistor (IEC)") {
        return getThemeIcon(":/icons/comp_resistor.svg");
    } else if (name == "Capacitor" || name == "Capacitor (Non-Polar)" || name == "Capacitor (Polarized)") {
        return getThemeIcon(":/icons/comp_capacitor.svg");
    } else if (name == "Inductor") {
        return getThemeIcon(":/icons/comp_inductor.svg");
    } else if (name == "Diode") {
        return getThemeIcon(":/icons/comp_diode.svg");
    } else if (name == "Transistor" || name == "NPN Transistor" || name == "PNP Transistor" || name == "NMOS Transistor" || name == "PMOS Transistor") {
        return getThemeIcon(":/icons/comp_transistor.svg");
    } else if (name == "IC" || name == "RAM") {
        return getThemeIcon(":/icons/comp_ic.svg");
    } else if (name == "GND") {
        return getThemeIcon(":/icons/comp_gnd.svg");
    } else if (name == "VCC" || name == "VDD" || name == "VSS" || name == "VBAT") {
        return getThemeIcon(":/icons/comp_vcc.svg");
    } else if (name == "Net Label" || name == "Global Label") {
        return getThemeIcon(":/icons/tool_net_label.svg");
    } else if (name == "Sheet" || name == "Hierarchical Port") {
        return getThemeIcon(name == "Sheet" ? ":/icons/tool_sheet.svg" : ":/icons/tool_hierarchical_port.svg");
    } else if (name == "Spice Directive") {
        return getThemeIcon(":/icons/tool_spice_directive.svg");
    } else if (name == "BV" || name == "BI") {
        return getThemeIcon(":/icons/comp_bv.svg");
    } else if (name == "Rectangle") {
        return getThemeIcon(":/icons/tool_rect.svg");
    } else if (name == "Circle") {
        return getThemeIcon(":/icons/tool_circle.svg");
    } else if (name == "Line") {
        return getThemeIcon(":/icons/tool_line.svg");
    } else if (name == "Polygon") {
        return getThemeIcon(":/icons/tool_polygon.svg");
    } else if (name == "Bezier") {
        return getThemeIcon(":/icons/tool_bezier.svg");
    } else if (name == "Text") {
        return getThemeIcon(":/icons/tool_text.svg");
    } else if (name == "Voltmeter (DC)" || name == "Voltmeter (AC)") {
        return getThemeIcon(":/icons/tool_voltmeter.svg");
    } else if (name == "Ammeter (DC)" || name == "Ammeter (AC)") {
        return getThemeIcon(":/icons/tool_ammeter.svg");
    } else if (name == "Wattmeter" || name == "Power Meter") {
        return getThemeIcon(":/icons/tool_power_meter.svg");
    } else if (name == "Frequency Counter") {
        painter.drawEllipse(4, 4, 24, 24);
        painter.setFont(QFont("Arial", 11, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "Hz");
    } else if (name == "Oscilloscope Instrument") {
        return getThemeIcon(":/icons/tool_oscilloscope.svg");
    } else if (name == "Annotate") {
        painter.setFont(QFont("Inter", 10, QFont::Bold));
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : QColor("#3b82f6"), 2));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "1..N");
        painter.drawRect(2, 2, 28, 28);
    } else if (name == "ERC") {
        painter.setFont(QFont("Inter", 10, QFont::Bold));
        painter.setPen(QPen(QColor("#10b981"), 2)); // Green for success/check
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "ERC");
        painter.drawRect(2, 2, 28, 28);
    } else if (name == "Rotate CW") {
        return getThemeIcon(":/icons/tool_rotate.svg");
    } else if (name == "Rotate CCW") {
        return getThemeIcon(":/icons/tool_rotate_ccw.svg");
    } else if (name == "Flip H") {
        return getThemeIcon(":/icons/flip_h.svg");
    } else if (name == "Flip V") {
        return getThemeIcon(":/icons/flip_v.svg");
    } else if (name == "Front") {
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(4, 4, 16, 16);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(12, 12, 16, 16);
    } else if (name == "Back") {
        painter.drawRect(4, 4, 16, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(12, 12, 16, 16);
    } else if (name == "Align Left") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 2));
        painter.drawLine(4, 4, 4, 28);
        painter.setPen(QPen(color, 1));
        painter.drawRect(6, 8, 10, 4);
        painter.drawRect(6, 16, 20, 4);
    } else if (name == "Align Right") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 2));
        painter.drawLine(28, 4, 28, 28);
        painter.setPen(QPen(color, 1));
        painter.drawRect(18, 8, 10, 4);
        painter.drawRect(8, 16, 20, 4);
    } else if (name == "Align Top") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 2));
        painter.drawLine(4, 4, 28, 4);
        painter.setPen(QPen(color, 1));
        painter.drawRect(8, 6, 4, 10);
        painter.drawRect(16, 6, 4, 20);
    } else if (name == "Align Bottom") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 2));
        painter.drawLine(4, 28, 28, 28);
        painter.setPen(QPen(color, 1));
        painter.drawRect(8, 18, 4, 10);
        painter.drawRect(16, 8, 4, 20);
    } else if (name == "Center X") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 1, Qt::DashLine));
        painter.drawLine(16, 2, 16, 30);
        painter.setPen(QPen(color, 1));
        painter.drawRect(10, 8, 12, 4);
        painter.drawRect(6, 18, 20, 4);
    } else if (name == "Center Y") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 1, Qt::DashLine));
        painter.drawLine(2, 16, 30, 16);
        painter.setPen(QPen(color, 1));
        painter.drawRect(8, 10, 4, 12);
        painter.drawRect(18, 6, 4, 20);
    } else if (name == "Distribute H") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 1, Qt::SolidLine));
        painter.drawLine(4, 4, 4, 28);
        painter.drawLine(28, 4, 28, 28);
        painter.drawRect(10, 12, 4, 8);
        painter.drawRect(18, 12, 4, 8);
    } else if (name == "Distribute V") {
        painter.setPen(QPen(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color, 1, Qt::SolidLine));
        painter.drawLine(4, 4, 28, 4);
        painter.drawLine(4, 28, 28, 28);
        painter.drawRect(12, 10, 8, 4);
        painter.drawRect(12, 18, 8, 4);
    } else if (name == "Search") {
        return getThemeIcon(":/icons/tool_search.svg");
    } else if (name == "Panel Sidebar Left") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(6, 8, 6, 16);
    } else if (name == "Panel Bottom") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(6, 18, 20, 6);
    } else if (name == "Panel Sidebar Right") {
        painter.drawRect(6, 8, 20, 16);
        painter.setBrush(ThemeManager::theme() ? ThemeManager::theme()->accentColor() : color);
        painter.drawRect(20, 8, 6, 16);
    } else if (name == "Breadcrumb Sep") {
        painter.drawLine(10, 8, 22, 16);
        painter.drawLine(22, 16, 10, 24);
    } else {
        // Fallback: draw first letter
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1));
    }

    return QIcon(pixmap);
}

QIcon SchematicEditor::createItemPreviewIcon(SchematicItem* item) {
    if (!item) return QIcon();
    
    QRectF rect = item->boundingRect();
    if (rect.isEmpty()) return createComponentIcon(item->itemTypeName());
    
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Scale and center the item
    qreal margin = 8.0;
    qreal availableSize = 64.0 - 2.0 * margin;
    qreal scale = qMin(availableSize / rect.width(), availableSize / rect.height());
    
    // Safety check for infinite or zero scale
    if (scale <= 0 || scale > 1000) scale = 1.0;

    painter.translate(32, 32);
    painter.scale(scale, scale);
    painter.translate(-rect.center().x(), -rect.center().y());
    
    QStyleOptionGraphicsItem opt;
    item->paint(&painter, &opt, nullptr);
    
    return QIcon(pixmap);
}

void SchematicEditor::createToolBar() {
    // ─── Main Toolbar ────────────────────────────────────────────────────────
    QToolBar *mainToolbar = addToolBar("Main");
    mainToolbar->setObjectName("MainToolbar");
    mainToolbar->setIconSize(QSize(18, 18));
    mainToolbar->setMovable(false);
    mainToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mainToolbar->setMinimumHeight(28);
    
    // 1. CREATE MODERN HAMBURGER MENU BUTTON
    QToolButton* menuBtn = new QToolButton(this);
    menuBtn->setObjectName("MainMenuButton");
    menuBtn->setText("Menu");
    // Simple 3-line hamburger icon
    QPixmap menuPix(24, 24);
    menuPix.fill(Qt::transparent);
    QPainter p(&menuPix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ThemeManager::theme()->textColor(), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(4, 6, 20, 6);
    p.drawLine(4, 12, 20, 12);
    p.drawLine(4, 18, 20, 18);
    menuBtn->setIcon(QIcon(menuPix));
    menuBtn->setPopupMode(QToolButton::InstantPopup);
    menuBtn->setStyleSheet(QString("QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 4px; } "
                           "QToolButton:hover { background-color: %1; } "
                           "QToolButton::menu-indicator { image: none; }")
                           .arg((ThemeManager::theme()->type() == PCBTheme::Light) ? "#e9ecef" : "#3c3c3c"));

    QMenu* mainAppMenu = new QMenu(menuBtn);
    mainAppMenu->setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");
    
    // File Menu
    QMenu* fileMenu = mainAppMenu->addMenu("&File");
    fileMenu->addAction(createComponentIcon("New"), "New Schematic", QKeySequence::New, this, &SchematicEditor::onNewSchematic);
    fileMenu->addAction(createComponentIcon("Open"), "Open Schematic...", QKeySequence::Open, this, &SchematicEditor::onOpenSchematic);
    QAction* openTemplateAct = fileMenu->addAction(createComponentIcon("Open"), "Open Template...");
    connect(openTemplateAct, &QAction::triggered, this, [this]() {
        CircuitTemplateGallery dlg(m_projectDir, this);
        if (dlg.exec() == QDialog::Accepted) {
            auto tpl = dlg.selectedTemplate();
            if (!tpl.filePath.isEmpty()) {
                if (m_isModified) {
                    QMessageBox::StandardButton reply = QMessageBox::question(
                        this, "Unsaved Changes",
                        "Do you want to save changes before loading a template?",
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
                    );
                    if (reply == QMessageBox::Save) {
                        onSaveSchematic();
                        if (m_isModified) return;
                    } else if (reply == QMessageBox::Cancel) {
                        return;
                    }
                }
                m_navigationStack.clear();
                openFile(tpl.filePath);
                m_isModified = true;
                statusBar()->showMessage(QString("Loaded template: %1").arg(tpl.name), 5000);
            }
        }
    });
    fileMenu->addAction(createComponentIcon("Open"), "Import ASC File...", QKeySequence(), this, &SchematicEditor::onImportAscFile);
    
    QMenu* importSubcktMenu = fileMenu->addMenu("Import SPICE Subcircuit");
    importSubcktMenu->addAction(createComponentIcon("Open"), "Import from Text/Paste...", QKeySequence(), this, &SchematicEditor::onImportSpiceSubcircuit);
    QAction* importSubcktFileAct = importSubcktMenu->addAction(createComponentIcon("Open"), "Import from File...");
    connect(importSubcktFileAct, &QAction::triggered, this, [this]() {
        const QString projectDir = m_projectDir.isEmpty() ? QFileInfo(m_currentFilePath).absolutePath() : m_projectDir;
        const QString filePath = QFileDialog::getOpenFileName(this, "Import SPICE Subcircuit File",
            projectDir, "SPICE Files (*.cir *.lib *.sub *.sp);;All Files (*)");
        if (!filePath.isEmpty()) {
            onImportSpiceSubcircuitFile(filePath);
        }
    });
    QAction* importSubcktLibAct = importSubcktMenu->addAction(createComponentIcon("Open"), "Import from Library...");
    connect(importSubcktLibAct, &QAction::triggered, this, [this]() {
        onImportSpiceSubcircuitFile("");
    });
    
    fileMenu->addAction(createComponentIcon("Save"), "Save Schematic", QKeySequence::Save, this, &SchematicEditor::onSaveSchematic);
    fileMenu->addSeparator();
    fileMenu->addAction(createComponentIcon("New Symbol"), "Create New Symbol", QKeySequence(), this, &SchematicEditor::onOpenSymbolEditor);
    fileMenu->addAction(createComponentIcon("New Symbol"), "Create New Symbol from Schematic", QKeySequence(), this, &SchematicEditor::onCreateSymbolFromSchematic);
    fileMenu->addSeparator();
    QMenu* exportMenu = fileMenu->addMenu("Export");
    exportMenu->addAction("Export as PDF", QKeySequence(), this, &SchematicEditor::onExportPDF);
    exportMenu->addAction("Export as SVG", QKeySequence(), this, &SchematicEditor::onExportSVG);
    exportMenu->addAction("Export as Image", QKeySequence(), this, &SchematicEditor::onExportImage);
    exportMenu->addSeparator();
    exportMenu->addAction("Export AI JSON...", QKeySequence(), this, &SchematicEditor::onExportAISchematic);
    fileMenu->addSeparator();
    fileMenu->addAction(createComponentIcon("Exit"), "Exit", QKeySequence::Quit, this, &QWidget::close);

    // Edit Menu
    QMenu* editMenu = mainAppMenu->addMenu("&Edit");
    QAction* menuUndoAct = m_undoStack->createUndoAction(this);
    menuUndoAct->setShortcut(QKeySequence());  // Toolbar owns Ctrl+Z shortcut
    editMenu->addAction(menuUndoAct);
    QAction* menuRedoAct = m_undoStack->createRedoAction(this);
    menuRedoAct->setShortcut(QKeySequence());  // Toolbar owns Ctrl+Shift+Z shortcut
    editMenu->addAction(menuRedoAct);
    editMenu->addSeparator();
    editMenu->addAction(getThemeIcon(":/icons/tool_scissors.svg"), "Cut", QKeySequence::Cut, this, &SchematicEditor::onCut);
    editMenu->addAction(getThemeIcon(":/icons/tool_duplicate.svg"), "Copy", QKeySequence::Copy, this, &SchematicEditor::onCopy);
    editMenu->addAction(getThemeIcon(":/icons/tool_generic.svg"), "Paste", QKeySequence::Paste, this, &SchematicEditor::onPaste);
    editMenu->addSeparator();
    QAction* deleteAction = editMenu->addAction(getThemeIcon(":/icons/tool_delete.svg"), "Delete", QKeySequence(), this, &SchematicEditor::onDelete);
    deleteAction->setShortcut(QKeySequence());
    editMenu->addAction("Select All", QKeySequence::SelectAll, this, &SchematicEditor::onSelectAll);
    editMenu->addSeparator();
    editMenu->addAction(getThemeIcon(":/icons/tool_search.svg"), "Find and Replace...", QKeySequence::Find, this, &SchematicEditor::onOpenFindReplace);
    editMenu->addSeparator();
    QAction* batchEditAction = editMenu->addAction(getThemeIcon(":/icons/tool_edit.svg"), "Batch Edit Values...", this, &SchematicEditor::onBatchEdit);
    batchEditAction->setShortcut(QKeySequence("Ctrl+E"));
    batchEditAction->setShortcutContext(Qt::ApplicationShortcut);
    batchEditAction->setToolTip("Edit values of multiple selected components simultaneously");

    // View Menu
    QMenu* viewMenu = mainAppMenu->addMenu("&View");
    viewMenu->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In", QKeySequence::ZoomIn, this, &SchematicEditor::onZoomIn);
    viewMenu->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out", QKeySequence::ZoomOut, this, &SchematicEditor::onZoomOut);
    viewMenu->addAction(getThemeIcon(":/icons/view_fit.svg"), "Fit All", QKeySequence("F"), this, &SchematicEditor::onZoomFit);
    viewMenu->addAction(getThemeIcon(":/icons/view_zoom_components.svg"), "Zoom to Components", QKeySequence("Alt+F"), this, &SchematicEditor::onZoomAllComponents);
    viewMenu->addSeparator();
    viewMenu->addAction(getThemeIcon(":/icons/toolbar_new.png"), "Show Netlist", QKeySequence("Ctrl+G"), this, &SchematicEditor::onOpenNetlistEditor);
    m_showDetailedLogAction = viewMenu->addAction("Show Detailed Log");
    m_showDetailedLogAction->setShortcut(QKeySequence("Ctrl+L"));
    connect(m_showDetailedLogAction, &QAction::triggered, this, [this]() {
        if (m_simulationPanel) m_simulationPanel->showDetailedLog();
    });
    if (m_showDetailedLogAction) {
        m_showDetailedLogAction->setEnabled(m_workspaceTabs->currentWidget() == m_simulationPanel);
    }

    viewMenu->addSeparator();

    QAction* crosshairAct = viewMenu->addAction("Show Crosshair");
    crosshairAct->setCheckable(true);
    crosshairAct->setChecked(m_view->isCrosshairEnabled());
    connect(crosshairAct, &QAction::toggled, this, [this](bool checked) {
        if (m_view) m_view->setShowCrosshair(checked);
    });

    m_toggleMiniMapAction = viewMenu->addAction("Show Mini-map");
    m_toggleMiniMapAction->setCheckable(true);
    m_toggleMiniMapAction->setShortcut(QKeySequence("Ctrl+M"));
    connect(m_toggleMiniMapAction, &QAction::toggled, this, &SchematicEditor::onToggleMiniMap);
    
    m_toggleHeatmapAction = viewMenu->addAction("Show Thermal Heatmap");
    m_toggleHeatmapAction->setCheckable(true);
    m_toggleHeatmapAction->setShortcut(QKeySequence("Ctrl+H"));
    connect(m_toggleHeatmapAction, &QAction::toggled, this, [this](bool checked) {
        if (m_view) m_view->setHeatmapEnabled(checked);
    });

    QMenu* gridStyleMenu = viewMenu->addMenu("Grid Style");
    QActionGroup* gridStyleGroup = new QActionGroup(this);
    
    QAction* linesAct = gridStyleMenu->addAction("Lines");
    linesAct->setCheckable(true);
    linesAct->setActionGroup(gridStyleGroup);
    linesAct->setChecked(m_view->gridStyle() == SchematicView::Lines);
    connect(linesAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setGridStyle(SchematicView::Lines);
    });

    QAction* pointsAct = gridStyleMenu->addAction("Points");
    pointsAct->setCheckable(true);
    pointsAct->setActionGroup(gridStyleGroup);
    pointsAct->setChecked(m_view->gridStyle() == SchematicView::Points);
    connect(pointsAct, &QAction::triggered, this, [this]() {
        if (m_view) m_view->setGridStyle(SchematicView::Points);
    });

    viewMenu->addSeparator();
    
    QMenu* panelsMenu = viewMenu->addMenu("Panels");
    auto addToggle = [&](QDockWidget* d, const QString& t) {
        if (!d) return;
        QAction* a = d->toggleViewAction();
        a->setText(t);
        panelsMenu->addAction(a);
    };
    addToggle(m_componentDock, "Component Library");
    addToggle(m_geminiDock, "Gemini Assistant");
    addToggle(m_hierarchyDock, "Sheet Hierarchy");
    addToggle(m_ercDock, "ERC Results");
    addToggle(m_oscilloscopeDock, "Analog Oscilloscope");
    addToggle(m_sourceControlDock, "Source Control");


    // Simulation Menu
    QMenu* simMenu = mainAppMenu->addMenu("&Simulation");
    m_runSimMenuAction = simMenu->addAction(createComponentIcon("Simulator"), "Run Simulation", QKeySequence("F8"), this, &SchematicEditor::onRunSimulation);
    m_stopSimMenuAction = simMenu->addAction(getThemeIcon(":/icons/tool_delete.svg"), "Stop Simulation");
    m_stopSimMenuAction->setShortcut(QKeySequence("Shift+F8"));
    connect(m_stopSimMenuAction, &QAction::triggered, this, [this]() {
        if (m_simulationPanel) {
            m_simulationPanel->cancelPendingRun();
        }
        SimManager::instance().stopAll();
    });
    simMenu->addSeparator();
    simMenu->addAction(getThemeIcon(":/icons/tool_gear.svg"), "Simulation Setup...", QKeySequence(), this, &SchematicEditor::onOpenSimulationSetup);

    // Tools Menu
    QMenu* toolsMenu = mainAppMenu->addMenu("&Tools");
    toolsMenu->addAction(createComponentIcon("Annotate"), "Annotate Components", QKeySequence(), this, &SchematicEditor::onAnnotate);
    toolsMenu->addAction(createComponentIcon("ERC"), "Run ERC Checker", QKeySequence("F7"), this, &SchematicEditor::onRunERC);
    toolsMenu->addAction("Configure ERC Rules...", QKeySequence(), this, &SchematicEditor::onOpenERCRulesConfig);
    toolsMenu->addAction("Design Rule Editor...", QKeySequence(), this, &SchematicEditor::onOpenDesignRuleEditor);
    toolsMenu->addAction("Clear ERC Exclusions", QKeySequence(), this, &SchematicEditor::onClearErcExclusions);
    toolsMenu->addAction("Bus Aliases...", QKeySequence(), this, &SchematicEditor::onOpenBusAliasesManager);
    toolsMenu->addAction(createComponentIcon("Netlist"), "Netlist Editor", QKeySequence(), this, &SchematicEditor::onOpenNetlistEditor);
    toolsMenu->addSeparator();

    if (ConfigManager::instance().isFeatureEnabled("pcb_tools", false)) {
        QAction* syncAction = toolsMenu->addAction(createComponentIcon("Sync"), "🔄 Update PCB from Schematic...", QKeySequence("Ctrl+Shift+U"), this, &SchematicEditor::onSendToPCB);
        syncAction->setToolTip("Generate ECO and push changes to the PCB editor");
    }

    QMenu* importSubcktToolsMenu = toolsMenu->addMenu(getThemeIcon(":/icons/tool_spice_directive.svg"), "Import SPICE Subcircuit");
    importSubcktToolsMenu->addAction("Import from Text/Paste...", QKeySequence(), this, &SchematicEditor::onImportSpiceSubcircuit);
    QAction* importSubcktToolsFileAct = importSubcktToolsMenu->addAction("Import from File...");
    connect(importSubcktToolsFileAct, &QAction::triggered, this, [this]() {
        const QString projectDir = m_projectDir.isEmpty() ? QFileInfo(m_currentFilePath).absolutePath() : m_projectDir;
        const QString filePath = QFileDialog::getOpenFileName(this, "Import SPICE Subcircuit File",
            projectDir, "SPICE Files (*.cir *.lib *.sub *.sp);;All Files (*)");
        if (!filePath.isEmpty()) {
            onImportSpiceSubcircuitFile(filePath);
        }
    });
    QAction* importSubcktToolsLibAct = importSubcktToolsMenu->addAction("Import from Library...");
    connect(importSubcktToolsLibAct, &QAction::triggered, this, [this]() {
        onImportSpiceSubcircuitFile("");
    });
    
    toolsMenu->addAction(getThemeIcon(":/icons/tool_gear.svg"), "SPICE Model Architect", QKeySequence(), this, &SchematicEditor::onOpenModelArchitect);
    toolsMenu->addSeparator();
    
    QAction* askModeAct = toolsMenu->addAction("Ask Co-Pilot (Mode)");
    connect(askModeAct, &QAction::triggered, this, [this](bool checked) {
        if (m_geminiPanel) {
            m_geminiPanel->setMode(checked ? "ask" : "schematic");
            if (m_geminiDock) m_geminiDock->show();
        }
    });
    askModeAct->setCheckable(true);
    askModeAct->setToolTip("Toggle AI mode to general circuit explanation/review.");
    
    toolsMenu->addSeparator();
    toolsMenu->addAction(getThemeIcon(":/icons/tool_search.svg"), "Command Palette", QKeySequence("Ctrl+K"), this, &SchematicEditor::onOpenCommandPalette);
    
    QAction* openCompAct = toolsMenu->addAction(getThemeIcon(":/icons/comp_ic.svg"), "Place Component...", QKeySequence("A"), this, &SchematicEditor::onOpenComponentBrowser);
    openCompAct->setToolTip("Open component browser and search (A)");

    // Settings (top-level, above Help)
    mainAppMenu->addSeparator();
    mainAppMenu->addAction(getThemeIcon(":/icons/tool_gear.svg"), "Settings...", QKeySequence(), this, &SchematicEditor::onSettings);

    QMenu* helpMenu = mainAppMenu->addMenu("&Help");
    helpMenu->addAction(createComponentIcon("About"), "About viospice", QKeySequence(), this, &SchematicEditor::onAbout);
    helpMenu->addAction("Help & Guides", QKeySequence::HelpContents, this, &SchematicEditor::onShowHelp);
    helpMenu->addAction("Developer Documentation", QKeySequence("Ctrl+Shift+F1"), this, &SchematicEditor::onShowDeveloperHelp);
    helpMenu->addSeparator();
    helpMenu->addAction("Project Health Audit...", QKeySequence(), this, &SchematicEditor::onProjectAudit);

    menuBtn->setMenu(mainAppMenu);
    mainToolbar->addWidget(menuBtn);

    mainToolbar->addSeparator();

    // Custom "New File" icon (File + Plus)
    QPixmap newFilePix(48, 48); // High DPI
    newFilePix.fill(Qt::transparent);
    {
        QPainter p(&newFilePix);
        p.setRenderHint(QPainter::Antialiasing);
        QColor iconColor = ThemeManager::theme()->textColor();
        p.setPen(QPen(iconColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        // Draw file shape
        QPolygonF filePoly;
        filePoly << QPointF(12, 8) << QPointF(28, 8) << QPointF(36, 16) << QPointF(36, 40) << QPointF(12, 40);
        p.drawPolygon(filePoly);
        p.drawLine(28, 8, 28, 16);
        p.drawLine(28, 16, 36, 16);

        // Draw plus mark
        p.setPen(QPen(ThemeManager::theme()->accentColor(), 4.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(20, 28, 28, 28);
        p.drawLine(24, 24, 24, 32);
    }
    QToolButton* newFileBtn = new QToolButton(this);
    newFileBtn->setObjectName("NewSchematicButton");
    newFileBtn->setIcon(QIcon(newFilePix));
    newFileBtn->setToolTip("New Schematic (Ctrl+N)");
    connect(newFileBtn, &QToolButton::clicked, this, &SchematicEditor::onNewSchematic);
    mainToolbar->addWidget(newFileBtn);

    // Custom "Open File" icon (Folder)
    QPixmap openFilePix(48, 48);
    openFilePix.fill(Qt::transparent);
    {
        QPainter p(&openFilePix);
        p.setRenderHint(QPainter::Antialiasing);
        QColor iconColor = ThemeManager::theme()->textColor();
        p.setPen(QPen(iconColor, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        
        // Draw folder shape
        QPolygonF folderPoly;
        folderPoly << QPointF(8, 16) << QPointF(20, 16) << QPointF(24, 12) << QPointF(40, 12) << QPointF(40, 36) << QPointF(8, 36);
        p.drawPolygon(folderPoly);
        p.drawLine(8, 20, 40, 20);
    }
    QToolButton* openFileBtn = new QToolButton(this);
    openFileBtn->setObjectName("OpenSchematicButton");
    openFileBtn->setIcon(QIcon(openFilePix));
    openFileBtn->setToolTip("Open Schematic (Ctrl+O)");
    connect(openFileBtn, &QToolButton::clicked, this, &SchematicEditor::onOpenSchematic);
    mainToolbar->addWidget(openFileBtn);

    mainToolbar->addSeparator();

    // Quick Undo button (Primary Ctrl+Z handler)
    QAction* undoAct = m_undoStack->createUndoAction(this);
    undoAct->setIcon(getThemeIcon(":/icons/undo.svg"));
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setToolTip("Undo last action (Ctrl+Z)");
    mainToolbar->addAction(undoAct);

    mainToolbar->addSeparator();

    mainToolbar->setStyleSheet(
        QString("QToolBar#MainToolbar {"
        "  background-color: %1;"
        "  border-bottom: 1px solid %2;"
        "  padding: 2px 6px;"
        "  spacing: 2px;"
        "}"
        "QToolBar#MainToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 2px;"
        "  color: %3;"
        "}"
        "QToolBar#MainToolbar QToolButton:hover {"
        "  background-color: %4;"
        "}"
        "QToolBar#MainToolbar QToolButton:checked, QToolBar#MainToolbar QToolButton:pressed {"
        "  background-color: %5;"
        "  color: white;"
        "}")
        .arg(ThemeManager::theme()->windowBackground().name())
        .arg(ThemeManager::theme()->panelBorder().name())
        .arg(ThemeManager::theme()->textColor().name())
        .arg((ThemeManager::theme()->type() == PCBTheme::Light) ? "#e9ecef" : "#3c3c3c")
        .arg(ThemeManager::theme()->accentColor().name())
    );

    // Command Palette / Search
    QAction* openPaletteAct = mainToolbar->addAction(getThemeIcon(":/icons/tool_search.svg"), "Search (Ctrl+K)");
    openPaletteAct->setShortcut(QKeySequence("Ctrl+K"));
    connect(openPaletteAct, &QAction::triggered, this, &SchematicEditor::onOpenCommandPalette);

    // Place Component Browser
    QAction* openBrowserAct = mainToolbar->addAction(getThemeIcon(":/icons/comp_ic.svg"), "Place Component (A)");
    openBrowserAct->setShortcut(QKeySequence("A"));
    connect(openBrowserAct, &QAction::triggered, this, &SchematicEditor::onOpenComponentBrowser);

    mainToolbar->addSeparator();

    // Zoom & View
    QAction* zoomInAct = mainToolbar->addAction(getThemeIcon(":/icons/view_zoom_in.svg"), "Zoom In");
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, this, &SchematicEditor::onZoomIn);

    QAction* zoomFitAct = mainToolbar->addAction(getThemeIcon(":/icons/view_fit.svg"), "Zoom to Fit");
    zoomFitAct->setShortcut(QKeySequence("F"));
    connect(zoomFitAct, &QAction::triggered, this, &SchematicEditor::onZoomFit);
    
    QAction* zoomOutAct = mainToolbar->addAction(getThemeIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, this, &SchematicEditor::onZoomOut);

    mainToolbar->addSeparator();
    
    QToolButton* heatmapBtn = new QToolButton(this);
    heatmapBtn->setDefaultAction(m_toggleHeatmapAction);
    heatmapBtn->setText("Heatmap");
    // Simple fire/flame icon using primitives if no SVG
    QPixmap heatPix(24, 24);
    heatPix.fill(Qt::transparent);
    {
        QPainter p(&heatPix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor("#f97316")); // Orange
        p.setPen(Qt::NoPen);
        p.drawEllipse(6, 12, 12, 10);
        p.drawEllipse(8, 6, 8, 12);
    }
    m_toggleHeatmapAction->setIcon(QIcon(heatPix));
    mainToolbar->addWidget(heatmapBtn);

    mainToolbar->addSeparator();

    // Breadcrumbs
    m_breadcrumbWidget = new QWidget();
    QHBoxLayout* bcLayout = new QHBoxLayout(m_breadcrumbWidget);
    bcLayout->setContentsMargins(5, 0, 5, 0);
    bcLayout->setSpacing(2);
    mainToolbar->addWidget(m_breadcrumbWidget);
    updateBreadcrumbs();
    
    mainToolbar->addSeparator();
    
    QLabel* filterLabel = new QLabel(" Filter: ");
    filterLabel->setStyleSheet("font-size: 11px; color: #6b7280;");
    mainToolbar->addWidget(filterLabel);

    QComboBox* filterCombo = new QComboBox();
    filterCombo->addItems({"All", "Components Only", "Wires Only"});
    filterCombo->setToolTip("Selection Filter");
    filterCombo->setStyleSheet(
        "QComboBox { background: #ffffff; border: 1px solid #d1d5db; border-radius: 4px; padding: 2px 4px; font-size: 11px; min-width: 100px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: url(:/icons/arrow_down.svg); width: 10px; }"
    );
    connect(filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_view->setSelectionFilter(static_cast<SchematicView::SelectionFilter>(index));
        statusBar()->showMessage(QString("Selection filter: %1").arg(
            index == 0 ? "All" : (index == 1 ? "Components" : "Wires")), 2000);
    });
    mainToolbar->addWidget(filterCombo);

    mainToolbar->addSeparator();

    // Manipulation (Instant)
    QAction* rotateAct = mainToolbar->addAction(getThemeIcon(":/icons/tool_rotate.svg"), "Rotate");
    connect(rotateAct, &QAction::triggered, [this]() {
         if (m_scene && !m_scene->selectedItems().isEmpty()) {
             QList<SchematicItem*> items;
             for (auto* it : m_scene->selectedItems()) {
                 if (auto* si = dynamic_cast<SchematicItem*>(it)) items.append(si);
             }
             if (!items.isEmpty()) {
                 m_undoStack->push(new RotateItemCommand(m_scene, items, 90));
             }
         }
    });

    mainToolbar->addSeparator();

    QAction* ercAct = mainToolbar->addAction(createComponentIcon("ERC"), "Run ERC (F7)");
    connect(ercAct, &QAction::triggered, this, &SchematicEditor::onRunERC);

    mainToolbar->addSeparator();

    // Professional Simulation Control Group
    QAction* setupSimAct = mainToolbar->addAction(getThemeIcon(":/icons/tool_gear.svg"), "Simulation Setup...");
    setupSimAct->setToolTip("Configure Simulation Analysis (Transient, AC, DC)");
    connect(setupSimAct, &QAction::triggered, this, &SchematicEditor::onOpenSimulationSetup);

    mainToolbar->addSeparator();

    // Professional Simulation Control Group (LTspice style: Run/Pause toggle + Stop)
    QWidget* simGroup = new QWidget();
    QHBoxLayout* simLayout = new QHBoxLayout(simGroup);
    simLayout->setContentsMargins(0, 0, 0, 0);
    simLayout->setSpacing(4);

    m_runSimToolbarAction = new QAction(getThemeIcon(":/icons/tool_run.svg"), "Run Simulation (F8)", this);
    m_runSimToolbarAction->setShortcut(QKeySequence("F8"));
    m_runSimToolbarAction->setToolTip("Run Analysis (F8)");
    connect(m_runSimToolbarAction, &QAction::triggered, this, &SchematicEditor::onRunSimulation);
    
    QToolButton* mainBtn = new QToolButton();
    mainBtn->setDefaultAction(m_runSimToolbarAction);
    mainBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    mainBtn->setIconSize(QSize(24, 24));
    mainBtn->setStyleSheet("QToolButton { background-color: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; } "
                          "QToolButton:hover { background-color: rgba(0, 0, 0, 0.05); border-color: #cbd5e1; }");
    simLayout->addWidget(mainBtn);

    m_stopSimToolbarAction = new QAction(getThemeIcon(":/icons/tool_stop.svg"), "Stop", this);
    m_stopSimToolbarAction->setShortcut(QKeySequence("Shift+F8"));
    connect(m_stopSimToolbarAction, &QAction::triggered, this, [this]() {
        if (m_simulationPanel) {
            m_simulationPanel->cancelPendingRun();
        }
        SimManager::instance().stopAll();
    });
    
    QToolButton* stopBtn = new QToolButton();
    stopBtn->setDefaultAction(m_stopSimToolbarAction);
    stopBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopBtn->setIconSize(QSize(24, 24));
    stopBtn->setStyleSheet("QToolButton { background-color: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; } "
                           "QToolButton:hover { background-color: rgba(0, 0, 0, 0.05); border-color: #cbd5e1; }");
    simLayout->addWidget(stopBtn);
    
    // Store stop widget for visibility control
    m_simControlSubGroup = stopBtn;
    mainToolbar->addWidget(simGroup);

    // Initial state: hide Stop button
    m_simControlSubGroup->setVisible(false);

    updateSimulationUiState(m_simulationRunning);

    updateSimulationUiState(m_simulationRunning);

    // --- PANEL TOGGLES (VS CODE STYLE) ---
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    mainToolbar->addWidget(spacer);

    auto addPanelToggle = [&](const QString& iconName, const QString& tooltip, auto slot) {
        QToolButton* btn = new QToolButton(this);
        btn->setIcon(createComponentIcon(iconName));
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setChecked(true); // Assuming visible by default
        connect(btn, &QToolButton::clicked, this, slot);
        mainToolbar->addWidget(btn);
        return btn;
    };

    addPanelToggle("Panel Sidebar Left", "Toggle Left Sidebar", &SchematicEditor::onToggleLeftSidebar);
    addPanelToggle("Panel Bottom", "Toggle Bottom Panel", &SchematicEditor::onToggleBottomPanel);
    addPanelToggle("Panel Sidebar Right", "Toggle Right Sidebar", &SchematicEditor::onToggleRightSidebar);

    mainToolbar->addSeparator();

    // ─── Property Bar (Dynamic Selection Properties) ─────────────────────────

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
    
    // Default state
    updatePropertyBar();

    // ─── Schematic Tools Toolbar (Wiring & Placement) ────────────────────────
    QToolBar *schToolbar = addToolBar("Schematic Tools");
    schToolbar->setObjectName("SchematicToolbar");
    schToolbar->setIconSize(QSize(22, 22));
    schToolbar->setMovable(false);
    schToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    schToolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, schToolbar);
    schToolbar->setStyleSheet(
        "QToolBar#SchematicToolbar {"
        "  background-color: #1e1e1e;"
        "  border-right: 1px solid #3c3c3c;"
        "  padding: 6px 4px;"
        "  spacing: 2px;"
        "}"
        "QToolBar#SchematicToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#SchematicToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#SchematicToolbar QToolButton:checked, QToolBar#SchematicToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "  color: white;"
        "}"
        "QToolBar#SchematicToolbar::extension {"
        "  image: url(:/icons/chevron_down.svg);"
        "  background-color: #2d2d30;"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "  margin: 2px;"
        "}"
        "QToolBar#SchematicToolbar::extension:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #6a6a6a;"
        "}"
        "QToolBar#SchematicToolbar QToolButton#qt_toolbar_ext_button {"
        "  background-color: #2d2d30;"
        "  border: 1px solid #6a6a6a;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 2px;"
        "}"
        "QToolBar#SchematicToolbar QToolButton#qt_toolbar_ext_button:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #8a8a8a;"
        "}"
        "QToolButton#qt_toolbar_ext_button {"
        "  background-color: #2d2d30;"
        "  border: 1px solid #6a6a6a;"
        "  border-radius: 4px;"
        "  min-height: 24px;"
        "  min-width: 24px;"
        "}"
        "QToolButton#qt_toolbar_ext_button:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #8a8a8a;"
        "}"
    );

    // ─── Layout Toolbar (Alignment & Distribution) ──────────────────────────
    QToolBar *layoutToolbar = addToolBar("Layout");
    layoutToolbar->setObjectName("LayoutToolbar");
    layoutToolbar->setIconSize(QSize(20, 20));
    layoutToolbar->setMovable(false);
    layoutToolbar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, layoutToolbar);
    layoutToolbar->setStyleSheet(schToolbar->styleSheet().replace("SchematicToolbar", "LayoutToolbar"));
    
    // Add grid size to Main Toolbar
    mainToolbar->addSeparator();
    mainToolbar->addWidget(new QLabel("  Grid: "));
    auto* gridCombo = new QComboBox();
    gridCombo->addItems({"1.0", "2.5", "5.0", "10.0", "25.0", "50.0"});
    gridCombo->setCurrentText(QString::number(m_view ? m_view->gridSize() : 10.0, 'f', 1));
    gridCombo->setFixedWidth(60);
    connect(gridCombo, &QComboBox::currentTextChanged, this, [this](const QString& text){
        if (m_view) {
            m_view->setGridSize(text.toDouble());
        }
    });
    mainToolbar->addWidget(gridCombo);

    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true);

    auto addSchTool = [&](const QString& toolName, const QString& label, const QString& iconName, const QString& shortcut = "") {
        QIcon icon = createComponentIcon(toolName);
        
        if (icon.isNull() && !iconName.isEmpty()) {
            if (iconName.startsWith("tool_") || iconName.startsWith("comp_")) {
                icon = getThemeIcon(QString(":/icons/%1.svg").arg(iconName));
            }
        }
        
        if (icon.isNull() && !iconName.isEmpty()) {
            icon = getThemeIcon(QString(":/icons/%1.svg").arg(iconName));
        }

        QAction* action = schToolbar->addAction(icon, label);
        action->setCheckable(true);
        action->setData(toolName);
        if (!shortcut.isEmpty()) {
            action->setShortcut(QKeySequence(shortcut));
            action->setToolTip(label + " (" + shortcut + ")");
        } else {
            action->setToolTip(label);
        }
        toolGroup->addAction(action);
        m_toolActions[toolName] = action;
        connect(action, &QAction::triggered, this, &SchematicEditor::onToolSelected);
        return action;
    };

    // Wiring Tools
    addSchTool("Hand", "Pan (Hand Tool) [H]", "tool_select", "H"); // Using tool_select icon until hand icon is added
    addSchTool("Select", "Select", "tool_select", "Esc");
    addSchTool("Probe", "Probe Signal", "tool_probe", "K");
    addSchTool("Voltage Probe", "Voltage Probe", "tool_voltage_probe", "Shift+K");
    addSchTool("Current Probe", "Current Probe", "tool_current_probe", "Alt+K");
    addSchTool("Power Probe", "Power Probe", "tool_power_probe", "Ctrl+Shift+P");
    addSchTool("Spice Directive", "SPICE Directive (.op)", "tool_spice_directive", "P");
    addSchTool("BV", "Arbitrary Behavioral Source", "comp_bv", "B");
    addSchTool("Scissors", "Delete (Scissors Tool)", "tool_scissors", "F5");
    addSchTool("Zoom Area", "Zoom to Area", "tool_zoom_area", "Z");
    addSchTool("Wire", "Place Wire", "tool_wire", "W");
    addSchTool("Bus", "Place Bus", "tool_bus", "Shift+B");
    addSchTool("Bus Entry", "Place Bus Entry", "tool_bus_entry", "");
    addSchTool("Net Label", "Place Net Label (Local)", "tool_net_label", "N");
    addSchTool("Global Label", "Place Global Label", "tool_global_label", "Ctrl+L");
    addSchTool("Hierarchical Port", "Place Hierarchical Port", "tool_hierarchical_port", "H");
    addSchTool("Sheet", "Place Hierarchical Sheet", "tool_sheet", "Shift+S");
    addSchTool("No-Connect", "No-Connect Flag", "tool_no_connect", "X");
    addSchTool("GND", "Place Power GND", "comp_gnd", "G");
    addSchTool("VCC", "Place Power VCC", "comp_vcc");
    
    schToolbar->addSeparator();

    // Simulation Instruments
    addSchTool("Voltmeter (DC)", "Place Voltmeter (DC)", "tool_voltmeter");
    addSchTool("Voltmeter (AC)", "Place Voltmeter (AC)", "tool_voltmeter");
    addSchTool("Ammeter (DC)", "Place Ammeter (DC)", "tool_ammeter");
    addSchTool("Ammeter (AC)", "Place Ammeter (AC)", "tool_ammeter");
    addSchTool("Wattmeter", "Place Wattmeter", "tool_power_meter");
    addSchTool("Power Meter", "Place Power Meter", "tool_power_meter");
    addSchTool("Frequency Counter", "Place Frequency Counter", "tool_meter");
    addSchTool("Logic Probe", "Place Logic Probe", "tool_probe");
    addSchTool("Oscilloscope Instrument", "Place Oscilloscope", "tool_oscilloscope");

    schToolbar->addSeparator();

    // Fast Components
    addSchTool("Resistor", "Place Resistor", "comp_resistor", "R");
    addSchTool("Inductor", "Place Inductor", "comp_inductor", "L");
    addSchTool("Capacitor", "Place Capacitor", "comp_capacitor", "C");
    addSchTool("Diode", "Place Diode", "comp_diode", "D");
    addSchTool("Transistor", "Place Transistor", "comp_transistor", "Q");
    addSchTool("IC", "Place IC", "comp_ic", "U");
    addSchTool("RAM", "Place RAM Module", "comp_ram", "M");
    addSchTool("Gate_AND", "Place AND Gate", "comp_ic");
    addSchTool("Gate_OR", "Place OR Gate", "comp_ic");
    addSchTool("Gate_XOR", "Place XOR Gate", "comp_ic");
    addSchTool("Gate_NAND", "Place NAND Gate", "comp_ic");
    addSchTool("Gate_NOR", "Place NOR Gate", "comp_ic");
    addSchTool("Gate_NOT", "Place NOT Gate", "comp_ic");
    addSchTool("Switch", "Place Switch", "comp_switch", "S");
    addSchTool("Voltage Source (DC)", "Place DC Voltage Source", "comp_voltage_source", "V");

    // Set default tool
    if (m_toolActions.contains("Select")) {
        m_toolActions["Select"]->setChecked(true);
    }

    QToolButton* moreBtn = new QToolButton(this);
    moreBtn->setObjectName("MoreToolsButton");
    moreBtn->setIcon(getThemeIcon(":/icons/chevron_down.svg"));
    moreBtn->setToolTip("More...");
    moreBtn->setPopupMode(QToolButton::InstantPopup);
    QMenu* moreMenu = new QMenu(moreBtn);
    connect(moreMenu, &QMenu::aboutToShow, this, [this, moreMenu]() {
        moreMenu->clear();
        QStringList ordered = {
            "Select", "Probe", "Voltage Probe", "Current Probe", "Power Probe",
            "Zoom Area", "Wire", "Bus", "Bus Entry", "Net Label", "Global Label",
            "Hierarchical Port", "Sheet", "No-Connect", "GND", "VCC",
            "Voltmeter (DC)", "Voltmeter (AC)", "Ammeter (DC)", "Ammeter (AC)",
            "Wattmeter", "Power Meter", "Frequency Counter", "Logic Probe", "Oscilloscope Instrument",
            "Resistor", "Capacitor", "Diode", "Transistor", "IC", "RAM",
            "Gate_AND", "Gate_OR", "Gate_XOR", "Gate_NAND", "Gate_NOR", "Gate_NOT"
        };
        for (const QString& key : ordered) {
            QAction* a = m_toolActions.value(key, nullptr);
            if (!a) continue;
            moreMenu->addAction(a->icon(), a->text(), [a]() { a->trigger(); });
        }
    });
    moreBtn->setMenu(moreMenu);
    moreBtn->setStyleSheet(
        "QToolButton {"
        "  background-color: #2d2d30;"
        "  border: 1px solid #6a6a6a;"
        "  border-radius: 4px;"
        "  min-height: 24px;"
        "  min-width: 24px;"
        "  padding: 4px;"
        "}"
        "QToolButton:hover {"
        "  background-color: #3c3c3c;"
        "  border-color: #8a8a8a;"
        "}"
    );
    // Keep this button near the top so it stays visible on small screens.
    if (QAction* anchor = m_toolActions.value("Zoom Area", nullptr)) {
        schToolbar->insertWidget(anchor, moreBtn);
    } else {
        schToolbar->addWidget(moreBtn);
    }

    // Make overflow button explicit/visible when toolbar has hidden actions.
    QTimer::singleShot(0, this, [this, schToolbar]() {
        if (QToolButton* extBtn = schToolbar->findChild<QToolButton*>("qt_toolbar_ext_button")) {
            extBtn->setToolTip("More...");
            extBtn->setIcon(getThemeIcon(":/icons/chevron_down.svg"));
            extBtn->setIconSize(QSize(12, 12));
            extBtn->setAutoRaise(false);
            extBtn->show();
            extBtn->raise();
        }
    });
}

void SchematicEditor::ensureGeminiPanelInitialized() {
    if (m_geminiPanel || !m_geminiDock) {
        return;
    }

    auto* geminiScroll = qobject_cast<QScrollArea*>(m_geminiDock->widget());
    if (!geminiScroll) {
        return;
    }

    if (QWidget* oldWidget = geminiScroll->takeWidget()) {
        oldWidget->deleteLater();
    }

    m_geminiPanel = new GeminiPanel(m_scene, this);
    for (int i = 0; i < m_workspaceTabs->count(); ++i) {
        if (auto* v = qobject_cast<SchematicView*>(m_workspaceTabs->widget(i))) {
            v->setGeminiPanel(m_geminiPanel);
        }
    }

    m_geminiPanel->setNetManager(m_netManager);
    m_geminiPanel->setUndoStack(m_undoStack);

    connect(m_geminiPanel, &GeminiPanel::runSimulationRequested, this, &SchematicEditor::onRunSimulation);
    connect(m_geminiPanel, &GeminiPanel::runERCRequested, this, &SchematicEditor::onRunERC);
    connect(m_geminiPanel, &GeminiPanel::importSubcircuitRequested, this, &SchematicEditor::onImportSpiceSubcircuitFile);
    connect(m_geminiPanel, &GeminiPanel::checkpointRequested, this, &SchematicEditor::onCheckpointRequested);
    connect(m_geminiPanel, &GeminiPanel::rewindRequested, this, &SchematicEditor::onRewindRequested);
    connect(m_geminiPanel, &GeminiPanel::togglePanelRequested, this, [this](const QString& pName) {
        QString n = pName.toLower();
        if (n.contains("left")) onToggleLeftSidebar();
        else if (n.contains("right") || n.contains("ai")) onToggleRightSidebar();
        else if (n.contains("bottom") || n.contains("result") || n.contains("sim")) onToggleBottomPanel();
    });
    connect(m_geminiPanel, &GeminiPanel::itemsHighlighted, this, &SchematicEditor::onItemsHighlighted);
    connect(m_geminiPanel, &GeminiPanel::snippetGenerated, this, [this](const QString& jsonSnippet) {
        QPointF pos;
        if (m_view) {
            pos = m_view->mapToScene(m_view->viewport()->rect().center());
        }
        onSnippetGenerated(jsonSnippet, pos);
    });
    connect(m_geminiPanel, &GeminiPanel::netlistGenerated, this, [this](const QString& netlist) {
        if (!m_scene) return;
        QTemporaryFile tempFile;
        if (tempFile.open()) {
            QTextStream out(&tempFile);
            out << netlist;
            tempFile.close();

            m_scene->clear();
            NetlistToSchematic::convertToScene(tempFile.fileName(), m_scene);

            if (m_netManager) {
                m_netManager->updateNets(m_scene);
            }
            if (m_view) {
                m_scene->setSceneRect(m_scene->itemsBoundingRect().marginsAdded(QMarginsF(50, 50, 50, 50)));
                m_view->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
            }
            statusBar()->showMessage("AI Schematic generated successfully!", 5000);
        }
    });

    m_geminiPanel->setContextProvider([this]() {
        QJsonObject ctx = SchematicFileIO::serializeSceneToJson(m_scene);

        QJsonArray ercArray;
        for (const auto& v : getErcViolations()) {
            QJsonObject vObj;
            vObj["severity"] = v.severity == ERCViolation::Critical ? "Critical" : (v.severity == ERCViolation::Error ? "Error" : "Warning");
            vObj["message"] = v.message;
            if (v.item) vObj["item"] = v.item->reference();
            if (!v.netName.isEmpty()) vObj["net"] = v.netName;
            ercArray.append(vObj);
        }
        ctx["erc_violations"] = ercArray;

        QJsonArray symbolsArray;
        for (SymbolLibrary* lib : SymbolLibraryManager::instance().libraries()) {
            for (const QString& sym : lib->symbolNames()) {
                symbolsArray.append(sym);
            }
        }
        ctx["available_symbols"] = symbolsArray;

        return QString::fromUtf8(QJsonDocument(ctx).toJson(QJsonDocument::Compact));
    });

    geminiScroll->setWidget(m_geminiPanel);
}

void SchematicEditor::createDockWidgets() {
    // Configure Dock Corners so bottom dock doesn't stretch across the entire width
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // === Component Library Dock ===
    m_componentDock = new QDockWidget("Components", this);
    m_componentDock->setObjectName("ComponentDock");
    m_componentDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    m_componentsPanel = new SchematicComponentsWidget(this);

    connect(m_componentsPanel, &SchematicComponentsWidget::toolSelected, [this](const QString& toolName) {
        if (!toolName.isEmpty()) {
            m_view->setCurrentTool(toolName);
            statusBar()->showMessage("" + toolName + " tool selected", 2000);
        }
    });

    connect(m_componentsPanel, &SchematicComponentsWidget::symbolCreated, [this](const QString& symbolName) {
        statusBar()->showMessage("Symbol '" + symbolName + "' created", 3000);
    });

    connect(m_componentsPanel, &SchematicComponentsWidget::symbolPlacementRequested, this, [this](const SymbolDefinition& symbol) {
        QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
        auto* item = new GenericComponentItem(symbol);
        item->setPos(center);
        m_undoStack->push(new AddItemCommand(m_scene, item));
        statusBar()->showMessage("Placed symbol: " + symbol.name(), 3000);
    });

    connect(m_componentsPanel, &SchematicComponentsWidget::modelAssignmentRequested, this, &SchematicEditor::onAssignModel);

    m_componentDock->setWidget(m_componentsPanel);
    m_componentDock->setMinimumWidth(260);
    m_componentDock->setMaximumWidth(400);
    addDockWidget(Qt::LeftDockWidgetArea, m_componentDock);

    // === Project Explorer Dock ===
    m_projectExplorerDock = new QDockWidget("Explorer", this);
    m_projectExplorerDock->setObjectName("ProjectExplorerDock");
    m_projectExplorerDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    
    m_projectExplorer = new ProjectExplorerWidget(this);
    
    connect(m_projectExplorer, &ProjectExplorerWidget::fileDoubleClicked, this, &SchematicEditor::openFile);
    
    m_projectExplorerDock->setWidget(m_projectExplorer);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerDock);
    tabifyDockWidget(m_componentDock, m_projectExplorerDock);
    m_projectExplorerDock->raise(); // Show explorer by default

    // === ERC Results Dock ===
    m_ercDock = new QDockWidget("ERC Results", this);
    m_ercDock->setObjectName("ERCDock");
    m_ercDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    
    QWidget* ercContainer = new QWidget(this);
    QVBoxLayout* ercLayout = new QVBoxLayout(ercContainer);
    ercLayout->setContentsMargins(4, 4, 4, 4);

    m_ercList = new QListWidget();
    if (ThemeManager::theme()) {
        m_ercList->setStyleSheet(QString("QListWidget { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; }")
            .arg(ThemeManager::theme()->panelBackground().name())
            .arg(ThemeManager::theme()->textColor().name())
            .arg(ThemeManager::theme()->panelBorder().name()));
    }
    ercLayout->addWidget(m_ercList);

    QHBoxLayout* ercControlLayout = new QHBoxLayout();
    QPushButton* ignoreSelectedBtn = new QPushButton("Ignore Selected");
    QPushButton* clearIgnoredBtn = new QPushButton("Clear Ignored");
    ercControlLayout->addWidget(ignoreSelectedBtn);
    ercControlLayout->addWidget(clearIgnoredBtn);
    ercControlLayout->addStretch();
    ercLayout->addLayout(ercControlLayout);

    QPushButton* aiFixBtn = new QPushButton("Ask Gemini for Fixes ✨");
    if (ThemeManager::theme() && ThemeManager::theme()->type() == PCBTheme::Light) {
        aiFixBtn->setStyleSheet("background-color: #f5f3ff; border: 1px solid #c084fc; color: #6b21a8; font-weight: bold; padding: 6px; border-radius: 6px;");
    } else {
        aiFixBtn->setStyleSheet("background-color: #3d2b3d; border: 1px solid #6b21a8; color: #f5d0fe; font-weight: bold; padding: 6px; border-radius: 6px;");
    }
    ercLayout->addWidget(aiFixBtn);

    QScrollArea* ercScroll = new QScrollArea(this);
    ercScroll->setWidget(ercContainer);
    ercScroll->setWidgetResizable(true);
    ercScroll->setFrameShape(QFrame::NoFrame);
    m_ercDock->setWidget(ercScroll);
    addDockWidget(Qt::RightDockWidgetArea, m_ercDock);
    m_ercDock->show(); // Show by default as ERC is required

    connect(aiFixBtn, &QPushButton::clicked, this, [this]() {
        if (!m_geminiPanel) return;
        
        QString violations;
        for (int i = 0; i < m_ercList->count(); ++i) {
            violations += "- " + m_ercList->item(i)->text() + "\n";
        }
        
        if (violations.isEmpty()) return;

        m_geminiDock->show();
        m_geminiDock->raise();
        m_geminiPanel->askPrompt("I have the following ERC (Electrical Rules Check) violations in my schematic:\n\n" + violations + "\nCan you explain why these are happening and suggest FluxScript or schematic changes to fix them?");
    });

    m_ercList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_ercList, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_ercList->itemAt(pos);
        if (!item) return;

        QMenu menu;
        menu.addAction("Ignore This Violation", this, &SchematicEditor::onIgnoreSelectedErc);
        menu.addAction("Clear All Exclusions", this, &SchematicEditor::onClearErcExclusions);
        menu.addSeparator();
        menu.addAction("Copy Message", [item]() {
            QApplication::clipboard()->setText(item->text());
        });
        menu.exec(m_ercList->mapToGlobal(pos));
    });

    connect(m_ercList, &QListWidget::itemDoubleClicked, this, &SchematicEditor::onIssueItemDoubleClicked);
    connect(ignoreSelectedBtn, &QPushButton::clicked, this, &SchematicEditor::onIgnoreSelectedErc);
    connect(clearIgnoredBtn, &QPushButton::clicked, this, &SchematicEditor::onClearErcExclusions);
    
    // === Gemini AI Dock ===
    m_geminiDock = new QDockWidget("viospice AI Co-Pilot", this);
    m_geminiDock->setObjectName("GeminiDock");
    m_geminiDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    QScrollArea* geminiScroll = new QScrollArea(this);
    geminiScroll->setWidgetResizable(true);
    geminiScroll->setFrameShape(QFrame::NoFrame);
    geminiScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (ThemeManager::theme()) {
        geminiScroll->setStyleSheet(QString("QScrollArea { background-color: %1; border: none; }").arg(ThemeManager::theme()->panelBackground().name()));
    }
    auto* geminiPlaceholder = new QLabel("AI panel will initialize when opened.", geminiScroll);
    geminiPlaceholder->setAlignment(Qt::AlignCenter);
    geminiPlaceholder->setWordWrap(true);
    if (ThemeManager::theme()) {
        geminiPlaceholder->setStyleSheet(QString("color: %1; padding: 24px;")
            .arg(ThemeManager::theme()->textSecondary().name()));
    }
    geminiScroll->setWidget(geminiPlaceholder);
    m_geminiDock->setWidget(geminiScroll);
    addDockWidget(Qt::LeftDockWidgetArea, m_geminiDock);
    connect(m_geminiDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_allowGeminiDockInit) {
            ensureGeminiPanelInitialized();
        }
    });

    // Tabify docks so they don't crush each other
    tabifyDockWidget(m_componentDock, m_geminiDock);
    m_componentDock->raise();

    // === Sheet Hierarchy Dock ===
    m_hierarchyDock = new QDockWidget("Sheet Hierarchy", this);
    m_hierarchyDock->setObjectName("HierarchyDock");
    m_hierarchyDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);

    QWidget* hierarchyContainer = new QWidget(this);
    QVBoxLayout* hierarchyLayout = new QVBoxLayout(hierarchyContainer);
    hierarchyLayout->setContentsMargins(0, 0, 0, 0);
    hierarchyLayout->setSpacing(0);

    // Toolbar row at top
    QWidget* hierHeader = new QWidget();
    QHBoxLayout* hierHeaderLayout = new QHBoxLayout(hierHeader);
    hierHeaderLayout->setContentsMargins(6, 4, 6, 4);
    hierHeaderLayout->setSpacing(4);

    QLabel* hierTitle = new QLabel("Sheets");
    if (ThemeManager::theme()) {
        hierTitle->setStyleSheet(QString("color: %1; font-size: 11px; font-weight: bold;").arg(ThemeManager::theme()->textSecondary().name()));
    }
    hierHeaderLayout->addWidget(hierTitle, 1);

    QToolButton* hierRefreshBtn = new QToolButton();
    hierRefreshBtn->setIcon(getThemeIcon(":/icons/view_fit.svg"));
    hierRefreshBtn->setToolTip("Refresh hierarchy");
    hierRefreshBtn->setIconSize(QSize(14, 14));
    hierRefreshBtn->setStyleSheet("QToolButton { background: transparent; border: none; }");
    hierHeaderLayout->addWidget(hierRefreshBtn);

    QToolButton* hierCollapseBtn = new QToolButton();
    hierCollapseBtn->setToolTip("Collapse all");
    hierCollapseBtn->setText("−");
    hierCollapseBtn->setStyleSheet(QString("QToolButton { background: transparent; border: none; color: %1; font-size: 14px; }")
        .arg(ThemeManager::theme() ? ThemeManager::theme()->textSecondary().name() : "#888"));
    hierHeaderLayout->addWidget(hierCollapseBtn);

    hierarchyLayout->addWidget(hierHeader);

    // Separator line
    QFrame* hierSep = new QFrame();
    hierSep->setFrameShape(QFrame::HLine);
    if (ThemeManager::theme()) {
        hierSep->setStyleSheet(QString("background-color: %1;").arg(ThemeManager::theme()->panelBorder().name()));
    }
    hierSep->setFixedHeight(1);
    hierarchyLayout->addWidget(hierSep);

    // Tree widget
    m_hierarchyTree = new QTreeWidget();
    m_hierarchyTree->setHeaderHidden(true);
    m_hierarchyTree->setRootIsDecorated(true);
    m_hierarchyTree->setAnimated(true);
    m_hierarchyTree->setIndentation(16);
    m_hierarchyTree->setIconSize(QSize(16, 16));
    m_hierarchyTree->setUniformRowHeights(true);
    m_hierarchyTree->setContextMenuPolicy(Qt::CustomContextMenu);
    
    if (ThemeManager::theme()) {
        QString bg = ThemeManager::theme()->panelBackground().name();
        QString fg = ThemeManager::theme()->textColor().name();
        QString selBg = ThemeManager::theme()->accentColor().name();
        QString hoverBg = (ThemeManager::theme()->type() == PCBTheme::Light) ? "#f1f5f9" : "#2d2d30";
        
        m_hierarchyTree->setStyleSheet(QString(R"(
            QTreeWidget {
                background-color: %1;
                color: %2;
                border: none;
                font-size: 12px;
                outline: none;
            }
            QTreeWidget::item {
                padding: 4px 6px;
                border-radius: 4px;
            }
            QTreeWidget::item:selected {
                background-color: %3;
                color: #ffffff;
            }
            QTreeWidget::item:hover:!selected {
                background-color: %4;
            }
            QTreeWidget::branch:has-children:!has-siblings:closed,
            QTreeWidget::branch:closed:has-children:has-siblings {
                image: url(:/icons/chevron_right.svg);
            }
            QTreeWidget::branch:open:has-children:!has-siblings,
            QTreeWidget::branch:open:has-children:has-siblings {
                image: url(:/icons/chevron_down.svg);
            }
        )").arg(bg, fg, selBg, hoverBg));
    }
    hierarchyLayout->addWidget(m_hierarchyTree, 1);

    m_hierarchyPanel = new SchematicHierarchyPanel(this);
    m_hierarchyDock->setWidget(m_hierarchyPanel);
    m_hierarchyDock->setMinimumWidth(200);
    addDockWidget(Qt::LeftDockWidgetArea, m_hierarchyDock);

    // -- Connect signals --
    connect(m_hierarchyTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem* item, int) {
        if (item) {
            QString fileName = item->data(0, Qt::UserRole).toString();
            if (!fileName.isEmpty()) openFile(fileName);
        }
    });

    connect(m_hierarchyTree, &QTreeWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_hierarchyTree->itemAt(pos);
        if (!item) return;
        
        QMenu menu(this);
        menu.addAction("Open Sheet", [this, item]() {
            QString fileName = item->data(0, Qt::UserRole).toString();
            if (!fileName.isEmpty()) openFile(fileName);
        });
        
        menu.exec(m_hierarchyTree->mapToGlobal(pos));
    });

    connect(hierRefreshBtn, &QToolButton::clicked, this, &SchematicEditor::refreshHierarchyPanel);
    connect(hierCollapseBtn, &QToolButton::clicked, m_hierarchyTree, &QTreeWidget::collapseAll);

    // === Simulation Window ===
    m_componentDock->raise(); // Show components by default

    // === Source Control Dock ===
    m_sourceControlDock = new QDockWidget("Source Control", this);
    m_sourceControlDock->setObjectName("SourceControlDock");
    m_sourceControlDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_sourceControlPanel = new SourceControlPanel(this);
    m_sourceControlDock->setWidget(m_sourceControlPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_sourceControlDock);
    tabifyDockWidget(m_ercDock, m_sourceControlDock);
    m_sourceControlDock->raise();

    if (ThemeManager::theme()) {
        m_sourceControlDock->setStyleSheet(QString(
            "QDockWidget { border: none; }"
            "QDockWidget::title { background: %1; color: %2; padding: 6px; border-bottom: 1px solid %3; font-weight: bold; }"
        ).arg(ThemeManager::theme()->panelBackground().name(),
              ThemeManager::theme()->textColor().name(),
              ThemeManager::theme()->panelBorder().name()));
    }


    // Stack the left docks
    tabifyDockWidget(m_componentDock, m_hierarchyDock);
    tabifyDockWidget(m_hierarchyDock, m_geminiDock);
    m_componentDock->raise();

    // === Oscilloscope Dock ===
    m_oscilloscopeDock = new QDockWidget("Analog Oscilloscope", this);
    m_oscilloscopeDock->setObjectName("AnalogOscilloscopeDock");
    m_oscilloscopeDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    m_oscilloscopeDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    if (ThemeManager::theme()) {        m_oscilloscopeDock->setStyleSheet(QString(
            "QDockWidget { border: none; }"
            "QDockWidget::title { background: %1; color: %2; padding: 6px; border-bottom: 1px solid %3; font-weight: bold; }"
        ).arg(ThemeManager::theme()->panelBackground().name(), 
              ThemeManager::theme()->textColor().name(),
              ThemeManager::theme()->panelBorder().name()));
    }
    
    m_oscilloscopeDock->setMinimumHeight(300);
    m_oscilloscopeDock->setMinimumWidth(0);
    m_oscilloscopeDock->hide();
    addDockWidget(Qt::BottomDockWidgetArea, m_oscilloscopeDock);

    connect(m_oscilloscopeDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_view) {
            m_view->setProbingEnabled(visible);
        }
    });

    // Initialize Simulation Panel (but don't add to tabs yet) so Oscilloscope is available
    if (m_scene && m_netManager) {
        m_simulationPanel = new SimulationPanel(m_scene, m_netManager, m_projectDir, this);
        m_simulationPanel->setEditor(this);
        
        SimulationPanel::AnalysisConfig pCfg;
        pCfg.type = m_simConfig.type;
        pCfg.stop = m_simConfig.stop;
        pCfg.step = m_simConfig.step;
        pCfg.transientSteady = m_simConfig.transientSteady;
        pCfg.steadyStateTol = m_simConfig.steadyStateTol;
        pCfg.steadyStateDelay = m_simConfig.steadyStateDelay;
        pCfg.fStart = m_simConfig.fStart;
        pCfg.fStop = m_simConfig.fStop;
        pCfg.pts = m_simConfig.pts;
        pCfg.rfPort1Source = m_simConfig.rfPort1Source;
        pCfg.rfPort2Node = m_simConfig.rfPort2Node;
        pCfg.rfZ0 = m_simConfig.rfZ0;
        pCfg.commandText = m_simConfig.commandText;
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

        refreshOscilloscopeDockContent();
    }
}

void SchematicEditor::createStatusBar() {
    statusBar()->setStyleSheet(
        "QStatusBar { background: #f5f5f5; border-top: 1px solid #d1d5db; }"
        "QStatusBar QLabel { padding: 0 8px; color: #111827; }"
        "QStatusBar::item { border: none; }"
    );

    // Coordinate display with icon
    m_coordLabel = new QLabel("X: 0.00  Y: 0.00 mm");
    m_coordLabel->setMinimumWidth(200);
    statusBar()->addWidget(m_coordLabel);

    m_netLabel = new QLabel("Net: (none)");
    m_netLabel->setMinimumWidth(150);
    statusBar()->addWidget(m_netLabel);

    m_remoteLabel = new QLabel("Remote: Off");
    m_remoteLabel->setMinimumWidth(180);
    m_remoteLabel->setStyleSheet("color: #6b7280;"); // Muted gray when off
    statusBar()->addWidget(m_remoteLabel);

    m_agentStatusBtn = new QToolButton();
    m_agentStatusBtn->setText(" Agents: 0");
    m_agentStatusBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_agentStatusBtn->setStyleSheet("QToolButton { border: none; padding: 0 4px; color: #6b7280; font-size: 11px; } QToolButton:hover { background: rgba(0,0,0,0.05); }");
    connect(m_agentStatusBtn, &QToolButton::clicked, this, &SchematicEditor::onShowAgentList);
    statusBar()->addWidget(m_agentStatusBtn);
#if VIOSPICE_HAS_QT_WEBSOCKETS
    auto* agentStatusTimer = new QTimer(this);
    agentStatusTimer->setInterval(1000);
    connect(agentStatusTimer, &QTimer::timeout, this, &SchematicEditor::updateAgentStatus);
    agentStatusTimer->start();
    QTimer::singleShot(0, this, &SchematicEditor::updateAgentStatus);
#endif

    // Separator
    QFrame* sep1 = new QFrame();
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFixedWidth(1);
    sep1->setStyleSheet("QFrame { background: #d1d5db; margin: 3px 6px; }");
    statusBar()->addWidget(sep1);

    // Grid display
    m_gridLabel = new QLabel("Grid: 10mil");
    statusBar()->addPermanentWidget(m_gridLabel);

    // Separator
    QFrame* sep2 = new QFrame();
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedWidth(1);
    sep2->setStyleSheet("QFrame { background: #d1d5db; margin: 3px 6px; }");
    statusBar()->addPermanentWidget(sep2);

    // Page size indicator
    QLabel* pageLabel = new QLabel("Page: " + m_currentPageSize);
    statusBar()->addPermanentWidget(pageLabel);

    // Separator
    QFrame* sep3 = new QFrame();
    sep3->setFrameShape(QFrame::VLine);
    sep3->setFixedWidth(1);
    sep3->setStyleSheet("QFrame { background: #d1d5db; margin: 3px 6px; }");
    statusBar()->addPermanentWidget(sep3);

    // Layer indicator
    m_layerLabel = new QLabel("Layer: Schematic");
    statusBar()->addPermanentWidget(m_layerLabel);

    // Theme Switcher
    QPushButton* themeBtn = new QPushButton("Theme");
    themeBtn->setFlat(true);
    themeBtn->setCursor(Qt::PointingHandCursor);
    themeBtn->setStyleSheet("QPushButton { color: #374151; font-weight: 600; border: none; padding: 0 6px; } QPushButton:hover { color: #111827; }");
    connect(themeBtn, &QPushButton::clicked, this, []() {
        auto& tm = ThemeManager::instance();
        if (tm.currentTheme()->type() == PCBTheme::Engineering) tm.setTheme(PCBTheme::Dark);
        else if (tm.currentTheme()->type() == PCBTheme::Dark) tm.setTheme(PCBTheme::Light);
        else tm.setTheme(PCBTheme::Engineering);
    });
    statusBar()->addPermanentWidget(themeBtn);

    // Ready message
    statusBar()->showMessage("Ready - Select a component or tool to begin", 5000);
}

void SchematicEditor::createDrawingToolbar() {
    QToolBar *drawToolbar = addToolBar("Drawing Tools");
    addToolBar(Qt::RightToolBarArea, drawToolbar);
    drawToolbar->setObjectName("DrawingToolbar");
    drawToolbar->setIconSize(QSize(24, 24));
    drawToolbar->setMovable(false);
    drawToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    drawToolbar->setOrientation(Qt::Vertical);
    
    // Consistent styling with left toolbars
    drawToolbar->setStyleSheet(
        "QToolBar#DrawingToolbar {"
        "  background-color: #1e1e1e;"
        "  border-left: 1px solid #3c3c3c;"
        "  padding: 6px 4px;"
        "  spacing: 2px;"
        "  min-width: 42px;"
        "}"
        "QToolBar#DrawingToolbar QToolButton {"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 4px;"
        "  padding: 5px;"
        "  margin: 1px 2px;"
        "  color: #cccccc;"
        "}"
        "QToolBar#DrawingToolbar QToolButton:hover {"
        "  border-color: #555;"
        "  background-color: #3c3c3c;"
        "}"
        "QToolBar#DrawingToolbar QToolButton:checked, QToolBar#DrawingToolbar QToolButton:pressed {"
        "  background-color: #094771;"
        "  border-color: #094771;"
        "  color: white;"
        "}"
    );

    // Attempt to join the existing exclusive group
    QActionGroup* group = nullptr;
    if (m_toolActions.contains("Select")) {
        group = m_toolActions["Select"]->actionGroup();
    }
    if (!group) {
        group = new QActionGroup(this);
        group->setExclusive(true);
    }

    auto addTool = [&](const QString& toolName, const QString& label) {
        QIcon icon = createComponentIcon(toolName); // Use our programmatic icons
        
        QAction* action = drawToolbar->addAction(icon, label);
        action->setCheckable(true);
        action->setData(toolName);
        action->setToolTip(label);
        
        group->addAction(action);
        m_toolActions[toolName] = action;
        
        connect(action, &QAction::triggered, this, &SchematicEditor::onToolSelected);
        return action;
    };

    addTool("Rectangle", "Draw Rectangle");
    addTool("Circle", "Draw Circle");
    addTool("Line", "Draw Line");
    addTool("Polygon", "Draw Polygon");
    addTool("Bezier", "Draw Bezier Curve");
    addTool("Text", "Add Text");
    addTool("Net Label", "Place Net Label (Local)");
    addTool("Scissors", "Scissors Items (F5)")->setShortcut(QKeySequence("F5"));
    addTool("Spice Directive", "SPICE Directive (S)")->setShortcut(QKeySequence("S"));

    drawToolbar->addSeparator();

    // ─── Manipulation Tools (Not exclusive group, these are actions) ───────
    auto addManipAction = [&](const QString& iconName, const QString& tooltip, auto slot) {
        QAction* action = drawToolbar->addAction(createComponentIcon(iconName), tooltip);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    addManipAction("Rotate CW", "Rotate Clockwise (90°) (Ctrl+R)", &SchematicEditor::onRotateCW)->setShortcut(QKeySequence("Ctrl+R"));
    addManipAction("Rotate CCW", "Rotate Counter-Clockwise (90°) (Ctrl+Shift+R)", &SchematicEditor::onRotateCCW)->setShortcut(QKeySequence("Ctrl+Shift+R"));
    
    drawToolbar->addSeparator();
    
    addManipAction("Flip H", "Flip Horizontal (H)", &SchematicEditor::onFlipHorizontal)->setShortcut(QKeySequence("H"));
    addManipAction("Flip V", "Flip Vertical (Shift+V)", &SchematicEditor::onFlipVertical)->setShortcut(QKeySequence("Shift+V"));

    drawToolbar->addSeparator();

    addManipAction("Front", "Bring to Front", &SchematicEditor::onBringToFront);
    addManipAction("Back", "Send to Back", &SchematicEditor::onSendToBack);

    drawToolbar->addSeparator();

    addManipAction("Align Left", "Align Left", &SchematicEditor::onAlignLeft);
    addManipAction("Align Right", "Align Right", &SchematicEditor::onAlignRight);
    addManipAction("Align Top", "Align Top", &SchematicEditor::onAlignTop);
    addManipAction("Align Bottom", "Align Bottom", &SchematicEditor::onAlignBottom);
    addManipAction("Center X", "Align Center X", &SchematicEditor::onAlignCenterX);
    addManipAction("Center Y", "Align Center Y", &SchematicEditor::onAlignCenterY);
}

#include "../items/oscilloscope_item.h"

void SchematicEditor::updateSimulationUiState(bool running, const QString& statusMessage) {
    if (m_view) m_view->setSimulationRunning(running);
    m_simulationRunning = running;
    
    // Primary Action (Run/Pause toggle)
    if (m_runSimToolbarAction) {
        if (running && !m_simPaused) {
            m_runSimToolbarAction->setIcon(getThemeIcon(":/icons/tool_pause.svg"));
            m_runSimToolbarAction->setText("Pause Simulation");
            m_runSimToolbarAction->setToolTip("Pause current simulation");
        } else if (m_simPaused) {
            m_runSimToolbarAction->setIcon(getThemeIcon(":/icons/tool_run.svg"));
            m_runSimToolbarAction->setText("Resume Simulation");
            m_runSimToolbarAction->setToolTip("Resume current simulation");
        } else {
            m_runSimToolbarAction->setIcon(getThemeIcon(":/icons/tool_run.svg"));
            m_runSimToolbarAction->setText("Run Simulation (F8)");
            m_runSimToolbarAction->setToolTip("Run Analysis (F8)");
        }
        // Button remains enabled so it can toggle between Run and Pause
        m_runSimToolbarAction->setEnabled(true);
    }

    if (m_runSimMenuAction) m_runSimMenuAction->setEnabled(!running || m_simPaused);
    if (m_stopSimMenuAction) m_stopSimMenuAction->setEnabled(running);
    
    // Stop button visibility
    if (m_simControlSubGroup) m_simControlSubGroup->setVisible(running);

    if (!statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage, running ? 0 : 3000);
    }
}

void SchematicEditor::onSimulationPaused(bool paused) {
    m_simPaused = paused;
    updateSimulationUiState(m_simulationRunning, paused ? "Simulation paused." : "Simulation resumed.");
}

void SchematicEditor::connectSimulationSignals() {
    auto& sim = SimManager::instance();

    connect(&sim, &SimManager::simulationStarted, this, [this]() {
        m_simulationRunning = true;
        updateSimulationUiState(true, "Simulation running...");
        
        // Find and switch to Simulation Tab
        for (int i = 0; i < m_workspaceTabs->count(); ++i) {
            if (qobject_cast<SimulationPanel*>(m_workspaceTabs->widget(i))) {
                m_workspaceTabs->setCurrentIndex(i);
                break;
            }
        }
    });

    connect(&sim, &SimManager::simulationFinished, this, [this](const SimResults&) {
        if (m_simConfig.type != SimAnalysisType::RealTime) {
            m_simulationRunning = false;
            updateSimulationUiState(false, "Simulation finished.");
        }
    });

    connect(&sim, &SimManager::simulationStopped, this, [this]() {
        m_simPaused = false;
        m_simulationRunning = false;
        updateSimulationUiState(false, "Simulation stopped.");
    });

    connect(&sim, &SimManager::simulationPaused, this, &SchematicEditor::onSimulationPaused);

    connect(&sim, &SimManager::errorOccurred, this, [this](const QString& message) {
        m_simulationRunning = false;
        updateSimulationUiState(false, "Simulation error.");
        statusBar()->showMessage(QString("Simulation error: %1").arg(message), 5000);
        appendSimulationIssue(message);
    });
}

void SchematicEditor::appendSimulationIssue(const QString& message) {
    if (!m_ercList) return;

    const auto target = SimSchematicBridge::extractDiagnosticTarget(message);
    QListWidgetItem* item = new QListWidgetItem(QString("[SIM] %1").arg(message));
    item->setData(Qt::UserRole + 1, "simulation");
    item->setData(Qt::UserRole + 2, static_cast<int>(target.type));
    item->setData(Qt::UserRole + 3, target.id);
    item->setForeground(QColor("#f59e0b"));
    item->setFont(QFont("Inter", 9, QFont::Bold));
    m_ercList->addItem(item);

    if (m_ercDock) {
        m_ercDock->show();
        m_ercDock->raise();
    }
}

void SchematicEditor::navigateToSimulationTarget(const QString& targetType, const QString& targetId) {
    if (targetId.trimmed().isEmpty()) return;

    if (targetType == "component") {
        if (!findAndSelectInScene(m_scene, targetId)) {
            navigateAndSelectHierarchical(targetId);
        }
        return;
    }

    if (targetType == "net") {
        if (m_netManager) {
            const auto conns = m_netManager->getConnections(targetId);
            if (!conns.isEmpty()) {
                m_view->centerOn(conns.first().connectionPoint);
            }
        }
    }
}

void SchematicEditor::onIssueItemDoubleClicked(QListWidgetItem* item) {
    if (!item || !m_view) return;

    if (item->data(Qt::UserRole + 1).toString() == "simulation") {
        const int t = item->data(Qt::UserRole + 2).toInt();
        const QString id = item->data(Qt::UserRole + 3).toString();
        if (t == static_cast<int>(SimSchematicBridge::DiagnosticTarget::Type::Component)) {
            navigateToSimulationTarget("component", id);
            return;
        }
        if (t == static_cast<int>(SimSchematicBridge::DiagnosticTarget::Type::Net)) {
            navigateToSimulationTarget("net", id);
            return;
        }
    }

    QPointF pos = item->data(Qt::UserRole).toPointF();
    if (!pos.isNull()) {
        m_view->centerOn(pos);
    }
}

void SchematicEditor::onOpenSimulationSetup() {
    SimulationSetupDialog dlg(this);
    dlg.setConfig(m_simConfig);
    if (dlg.exec() == QDialog::Accepted) {
        m_simConfig = dlg.getConfig();

        if (m_simulationPanel) {
            SimulationPanel::AnalysisConfig pCfg;
            pCfg.type = m_simConfig.type;
            pCfg.stop = m_simConfig.stop;
            pCfg.step = m_simConfig.step;
            pCfg.transientSteady = m_simConfig.transientSteady;
            pCfg.steadyStateTol = m_simConfig.steadyStateTol;
            pCfg.steadyStateDelay = m_simConfig.steadyStateDelay;
            pCfg.fStart = m_simConfig.fStart;
            pCfg.fStop = m_simConfig.fStop;
            pCfg.pts = m_simConfig.pts;
            pCfg.rfPort1Source = m_simConfig.rfPort1Source;
            pCfg.rfPort2Node = m_simConfig.rfPort2Node;
            pCfg.rfZ0 = m_simConfig.rfZ0;
            pCfg.commandText = m_simConfig.commandText;
            m_simulationPanel->setAnalysisConfig(pCfg);

            // Sync schematic directive with the command text from dialog
            if (!m_simConfig.commandText.isEmpty()) {
                m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
            }
        }

        statusBar()->showMessage("Simulation parameters updated.", 3000);
    }
}

void SchematicEditor::applyDirectiveText(SchematicSpiceDirectiveItem* item, const QString& newText) {
    if (!item) return;
    const QString trimmed = newText.trimmed();
    if (trimmed.isEmpty() || item->text() == trimmed) return;
    if (m_undoStack && m_scene) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, item, "Text", item->text(), trimmed));
    } else {
        item->setText(trimmed);
        item->update();
    }
}

SchematicSpiceDirectiveItem* SchematicEditor::resolveDirectiveItemForEdit(const QString& currentCommand) const {
    SchematicSpiceDirectiveItem* directiveItem = qobject_cast<SchematicSpiceDirectiveItem*>(sender());
    if (!directiveItem && m_scene) {
        for (auto* gi : m_scene->selectedItems()) {
            if (auto* selectedDirective = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
                directiveItem = selectedDirective;
                break;
            }
        }
    }
    if (!directiveItem && m_scene) {
        for (auto* gi : m_scene->items()) {
            if (auto* candidate = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
                if (candidate->text().trimmed() == currentCommand.trimmed()) {
                    directiveItem = candidate;
                    break;
                }
            }
        }
    }
    return directiveItem;
}

bool SchematicEditor::editDirectiveWithSimulationSetup(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    QPointer<SchematicSpiceDirectiveItem> safeDirective(directiveItem);
    m_simConfig.commandText = currentCommand;

    SimulationSetupDialog dlg(this);
    dlg.setConfig(m_simConfig);
    if (dlg.exec() != QDialog::Accepted) {
        return true;
    }

    m_simConfig = dlg.getConfig();

    if (m_simulationPanel) {
        SimulationPanel::AnalysisConfig pCfg;
        pCfg.type = m_simConfig.type;
        pCfg.stop = m_simConfig.stop;
        pCfg.step = m_simConfig.step;
        pCfg.transientSteady = m_simConfig.transientSteady;
        pCfg.steadyStateTol = m_simConfig.steadyStateTol;
        pCfg.steadyStateDelay = m_simConfig.steadyStateDelay;
        pCfg.fStart = m_simConfig.fStart;
        pCfg.fStop = m_simConfig.fStop;
        pCfg.pts = m_simConfig.pts;
        pCfg.rfPort1Source = m_simConfig.rfPort1Source;
        pCfg.rfPort2Node = m_simConfig.rfPort2Node;
        pCfg.rfZ0 = m_simConfig.rfZ0;
        pCfg.commandText = m_simConfig.commandText;
        m_simulationPanel->setAnalysisConfig(pCfg);
    }

    if (!m_simConfig.commandText.isEmpty()) {
        if (safeDirective && safeDirective->scene() == m_scene) {
            applyDirectiveText(safeDirective, m_simConfig.commandText);
        } else if (m_simulationPanel) {
            m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
        }
    }
    statusBar()->showMessage("Simulation directive updated.", 3000);
    return true;
}

bool SchematicEditor::editDirectiveWithMeanDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    QPointer<SchematicSpiceDirectiveItem> safeDirective(directiveItem);
    SpiceMeanDialog dlg(currentCommand, this);
    if (dlg.exec() != QDialog::Accepted) {
        return true;
    }

    const QString newCommand = dlg.commandText();
    if (safeDirective && safeDirective->scene() == m_scene) {
        applyDirectiveText(safeDirective, newCommand);
    } else if (m_simulationPanel) {
        m_simulationPanel->updateSchematicDirectiveFromCommand(newCommand);
    }
    statusBar()->showMessage("SPICE .mean directive updated.", 3000);
    return true;
}

bool SchematicEditor::editDirectiveWithStepDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    QPointer<SchematicSpiceDirectiveItem> safeDirective(directiveItem);
    SpiceStepDialog dlg(currentCommand, m_scene, this);
    if (dlg.exec() != QDialog::Accepted) {
        return true;
    }

    const QString newCommand = dlg.commandText();
    if (safeDirective && safeDirective->scene() == m_scene) {
        applyDirectiveText(safeDirective, newCommand);
    } else if (m_simulationPanel) {
        m_simulationPanel->updateSchematicDirectiveFromCommand(newCommand);
    }
    statusBar()->showMessage("SPICE .step directive updated.", 3000);
    return true;
}

bool SchematicEditor::editDirectiveWithGenericDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem) {
    QPointer<SchematicSpiceDirectiveItem> safeDirective(directiveItem);
    if (safeDirective && safeDirective->scene() == m_scene) {
        SpiceDirectiveDialog dlg(safeDirective, m_undoStack, m_scene, this);
        if (dlg.exec() == QDialog::Accepted) {
            statusBar()->showMessage("SPICE directive updated.", 3000);
        }
        return true;
    }

    m_simConfig.commandText = currentCommand;
    SimulationSetupDialog dlg(this);
    dlg.setConfig(m_simConfig);
    if (dlg.exec() == QDialog::Accepted) {
        m_simConfig = dlg.getConfig();
        if (!m_simConfig.commandText.isEmpty() && m_simulationPanel) {
            m_simulationPanel->updateSchematicDirectiveFromCommand(m_simConfig.commandText);
        }
    }
    return true;
}

void SchematicEditor::onEditSimulationFromDirective(const QString& currentCommand) {
    SchematicSpiceDirectiveItem* directiveItem = resolveDirectiveItemForEdit(currentCommand);

    const SpiceDirectiveClassification classification = SpiceDirectiveClassifier::classify(currentCommand);

    struct EditRoute {
        SpiceDirectiveEditTarget target;
        bool (SchematicEditor::*handler)(const QString&, SchematicSpiceDirectiveItem*);
    };

    static const std::array<EditRoute, 4> routes = {{
        {SpiceDirectiveEditTarget::SimulationSetup, &SchematicEditor::editDirectiveWithSimulationSetup},
        {SpiceDirectiveEditTarget::MeanDialog, &SchematicEditor::editDirectiveWithMeanDialog},
        {SpiceDirectiveEditTarget::StepDialog, &SchematicEditor::editDirectiveWithStepDialog},
        {SpiceDirectiveEditTarget::GenericDirective, &SchematicEditor::editDirectiveWithGenericDialog},
    }};

    for (const EditRoute& route : routes) {
        if (route.target == classification.target) {
            (this->*route.handler)(currentCommand, directiveItem);
            return;
        }
    }

    editDirectiveWithGenericDialog(currentCommand, directiveItem);
}

void SchematicEditor::onRunSimulation() {
    if (!m_scene || !m_netManager) {
        updateSimulationUiState(false, "Simulation unavailable: scene or net manager is not ready.");
        return;
    }

    if (m_simulationRunning) {
        onPauseSimulation(); // Toggle pause/resume if already running
        return;
    }

    // Auto-save pending Smart Signal code edits before building simulator netlist.
    if (m_logicEditorPanel) {
        m_logicEditorPanel->flushEdits();
    }

    // Auto-annotate if duplicate references exist (prevents netlist collisions like V1/V1/V1).
    {
        QMap<QString, QList<SchematicItem*>> refs;
        for (auto* gi : m_scene->items()) {
            auto* si = dynamic_cast<SchematicItem*>(gi);
            if (!si) continue;
            const int t = si->itemType();
            if (t == SchematicItem::WireType ||
                t == SchematicItem::LabelType ||
                t == SchematicItem::NetLabelType ||
                t == SchematicItem::JunctionType ||
                t == SchematicItem::NoConnectType ||
                t == SchematicItem::BusType ||
                t == SchematicItem::SheetType ||
                t == SchematicItem::HierarchicalPortType) {
                continue;
            }
            const QString ref = si->reference().trimmed();
            if (ref.isEmpty()) continue;
            refs[ref.toUpper()].append(si);
        }

        auto isValidMultiUnitPack = [](const QList<SchematicItem*>& items) -> bool {
            if (items.size() <= 1) return false;

            QString identity;
            int totalUnits = 0;
            QSet<int> usedUnits;

            for (SchematicItem* si : items) {
                auto* gc = dynamic_cast<GenericComponentItem*>(si);
                if (!gc) return false;

                const SymbolDefinition sym = gc->symbol();
                const QString sid = sym.symbolId().trimmed();
                const QString key = (sid.isEmpty() ? sym.name().trimmed() : sid).toLower();
                if (key.isEmpty()) return false;

                if (identity.isEmpty()) identity = key;
                if (identity != key) return false;

                totalUnits = qMax(totalUnits, sym.unitCount());
                const int u = gc->unit();
                if (u <= 0) return false;
                usedUnits.insert(u);
            }

            return totalUnits > 1 && usedUnits.size() == items.size() && usedUnits.size() <= totalUnits;
        };

        bool hasDup = false;
        for (auto it = refs.constBegin(); it != refs.constEnd(); ++it) {
            if (it.value().size() <= 1) continue;
            if (isValidMultiUnitPack(it.value())) continue;
            hasDup = true;
            break;
        }
        if (hasDup) {
            onAnnotate();
        }
    }

    m_simulationRunning = true;
    updateSimulationUiState(true, "Starting simulation...");
    
    // Force UI update to show Pause/Stop buttons before heavy netlist building blocks the main thread
    qApp->processEvents();

    // Netlist generation now happens in the SimulationPanel background worker.
    // Avoid blocking the UI with a full net rebuild here.

    if (m_oscilloscopeDock && m_simulationPanel) {
        refreshOscilloscopeDockContent();
        m_oscilloscopeDock->setFloating(false);
        m_oscilloscopeDock->show();
    }

    if (m_simulationPanel) {
        m_simulationPanel->setTargetScene(m_scene, m_netManager, m_projectDir, true);
    }
    
    // Ensure all logic analyzer windows are ready.
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
        m_laWindows[id]->activateWindow();
    }

    // Clear previous results so failed runs don't show stale data
    if (m_simulationPanel) {
        m_simulationPanel->clearResults();
    }

    // Route probes and hardware oscilloscopes to the bottom dock simulation panel
    QStringList probedNets;
    for (auto* item : m_scene->items()) {
        if (auto* m = dynamic_cast<SchematicWaveformMarker*>(item)) {
            probedNets << m->netName();
            if (m_simulationPanel) {
                m_simulationPanel->addProbe(m->netName());
            }
        } else if (auto* sItem = dynamic_cast<SchematicItem*>(item)) {
            const QString typeName = sItem->itemTypeName().toLower();
            const QString ref = sItem->reference().toLower();
            if (typeName.contains("oscilloscope") || ref.startsWith("osc")) {
                const QStringList oscNets = resolveConnectedInstrumentNets(sItem);
                for (const QString& net : oscNets) {
                    probedNets << net;
                    if (m_simulationPanel) m_simulationPanel->addProbe(net);
                }
            }
        }
    }
    
    // 1. Preflight check and Debugger (TODO: Move this to a background thread)
    // We'll skip the heavy preflightCheck on the UI thread to prevent the freeze.
    /*
    SimNetlist netlist;
    QStringList diagnostics = SimManager::instance().preflightCheck(m_scene, m_netManager, netlist);
    */

    if (m_simulationPanel) {
        const auto cfg = m_simulationPanel->getAnalysisConfig();
        m_simConfig.type = cfg.type;
        m_simConfig.stop = cfg.stop;
        m_simConfig.step = cfg.step;
        m_simConfig.transientSteady = cfg.transientSteady;
        m_simConfig.steadyStateTol = cfg.steadyStateTol;
        m_simConfig.steadyStateDelay = cfg.steadyStateDelay;
        m_simConfig.fStart = cfg.fStart;
        m_simConfig.fStop = cfg.fStop;
        m_simConfig.pts = cfg.pts;
        m_simConfig.rfPort1Source = cfg.rfPort1Source;
        m_simConfig.rfPort2Node = cfg.rfPort2Node;
        m_simConfig.rfZ0 = cfg.rfZ0;
        m_simConfig.commandText = cfg.commandText;
    }

    SimAnalysisConfig config;
    if (m_simConfig.type == SimAnalysisType::Transient) {
        config.type = SimAnalysisType::Transient;
        config.tStart = 0;
        config.tStop = m_simConfig.stop;
        config.tStep = m_simConfig.step;
        config.transientStopAtSteadyState = m_simConfig.transientSteady;
        config.transientSteadyStateTol = m_simConfig.steadyStateTol;
        config.transientSteadyStateDelay = m_simConfig.steadyStateDelay;
        config.transientStorageMode = SimTransientStorageMode::AutoDecimate;
        config.transientMaxStoredPoints = 50000;
    } else if (m_simConfig.type == SimAnalysisType::OP) {
        config.type = SimAnalysisType::OP;
    } else if (m_simConfig.type == SimAnalysisType::AC) {
        config.type = SimAnalysisType::AC;
        config.fStart = m_simConfig.fStart > 0.0 ? m_simConfig.fStart : 10.0;
        config.fStop = m_simConfig.fStop > 0.0 ? m_simConfig.fStop : 1e6;
        config.fPoints = m_simConfig.pts > 0 ? m_simConfig.pts : 10;
    } else if (m_simConfig.type == SimAnalysisType::SParameter) {
        config.type = SimAnalysisType::SParameter;
        config.fStart = m_simConfig.fStart > 0.0 ? m_simConfig.fStart : 10.0;
        config.fStop = m_simConfig.fStop > 0.0 ? m_simConfig.fStop : 1e6;
        config.fPoints = m_simConfig.pts > 0 ? m_simConfig.pts : 10;
        config.rfPort1Source = m_simConfig.rfPort1Source.toStdString();
        config.rfPort2Node = m_simConfig.rfPort2Node.toStdString();
        config.rfZ0 = m_simConfig.rfZ0 > 0.0 ? m_simConfig.rfZ0 : 50.0;
    }

    // 2. Trigger Engine via Ngspice backend asynchronously.
    updateSimulationUiState(true, "Generating netlist...");
    QString netlist = SimManager::instance().generateNetlist(m_scene, m_netManager, config, m_projectDir);
    
    updateSimulationUiState(true, "Starting simulation...");
    SimManager::instance().runNgspiceSimulation(netlist, config);
}

void SchematicEditor::onPauseSimulation() {
    if (!m_simulationRunning) {
        onRunSimulation();
        return;
    }

    SimManager::instance().pauseSimulation(!m_simPaused);
}

void SchematicEditor::updateBreadcrumbs() {
    if (!m_breadcrumbWidget || !m_breadcrumbWidget->layout()) return;
    
    // Clear layout
    QLayoutItem* item;
    while ((item = m_breadcrumbWidget->layout()->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
    
    // Add "Root"
    QPushButton* rootBtn = new QPushButton("ROOT");
    rootBtn->setFlat(true);
    rootBtn->setCursor(Qt::PointingHandCursor);
    rootBtn->setStyleSheet("QPushButton { color: #94a3b8; font-family: 'Inter'; font-size: 10px; font-weight: bold; border: none; padding: 2px 5px; } "
                          "QPushButton:hover { color: #f8fafc; background: rgba(255,255,255,0.1); border-radius: 4px; }");
    m_breadcrumbWidget->layout()->addWidget(rootBtn);
    
    connect(rootBtn, &QPushButton::clicked, this, [this]() {
        if (m_navigationStack.isEmpty()) return;
        QString rootPath = m_navigationStack.first();
        m_navigationStack.clear();
        openFile(rootPath);
        updateBreadcrumbs();
    });

    // Add path segments
    for (int i = 0; i < m_navigationStack.size(); ++i) {
        QLabel* sep = new QLabel(""); // Modern arrow glyph if font supports it, or just >
        sep->setStyleSheet("color: #475569; font-size: 10px;");
        m_breadcrumbWidget->layout()->addWidget(sep);
        
        QString path = m_navigationStack[i];
        QFileInfo info(path);
        QPushButton* btn = new QPushButton(info.baseName().toUpper());
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { color: #38bdf8; font-family: 'Inter'; font-size: 10px; border: none; padding: 2px 5px; } "
                          "QPushButton:hover { color: #7dd3fc; background: rgba(56,189,248,0.1); border-radius: 4px; }");
        m_breadcrumbWidget->layout()->addWidget(btn);
        
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            QString targetPath = m_navigationStack[i];
            while (m_navigationStack.size() > i) {
                m_navigationStack.removeLast();
            }
            openFile(targetPath);
            updateBreadcrumbs();
        });
    }
    
    // Current File
    if (!m_currentFilePath.isEmpty()) {
        QLabel* sep = new QLabel("");
        sep->setStyleSheet("color: #475569; font-size: 10px;");
        m_breadcrumbWidget->layout()->addWidget(sep);
        
        QFileInfo info(m_currentFilePath);
        QLabel* label = new QLabel(info.baseName().toUpper());
        label->setStyleSheet("color: #ec4899; font-family: 'Inter'; font-size: 10px; font-weight: bold; padding: 2px 5px; background: rgba(236,72,153,0.1); border-radius: 4px;");
        m_breadcrumbWidget->layout()->addWidget(label);
    }

    // Sync the hierarchy tree with the current navigation state
    refreshHierarchyPanel();
}

void SchematicEditor::updateAgentStatus() {
    if (!m_agentStatusBtn) return;
    m_agentStatusBtn->show();
    int count = 0;
#if VIOSPICE_HAS_QT_WEBSOCKETS
    if (auto* ws = WsServer::instance()) {
        count = ws->connectedClients().size();
    }
#endif

    m_agentStatusBtn->setText(QString(" Agents: %1").arg(count));
    m_agentStatusBtn->setIcon(QIcon());
    const QString color = count > 0 ? "#059669" : "#6b7280";
    m_agentStatusBtn->setStyleSheet(
        QString("QToolButton { border: none; padding: 0 4px; color: %1; font-size: 11px; } "
                "QToolButton:hover { background: rgba(0,0,0,0.05); }").arg(color));
}

void SchematicEditor::onShowAgentList() {
#if VIOSPICE_HAS_QT_WEBSOCKETS
    if (!m_agentStatusBtn) return;
    auto* ws = WsServer::instance();

    QMenu menu(this);
    menu.setStyleSheet(ThemeManager::theme() ? ThemeManager::theme()->widgetStylesheet() : "");

    if (!ws) {
        menu.addAction("Agent bridge not running")->setEnabled(false);
    } else {
        const auto clients = ws->connectedClients();
        if (clients.isEmpty()) {
            menu.addAction("No active agents")->setEnabled(false);
        } else {
            menu.addSection(QString("%1 Active Agent%2").arg(clients.size()).arg(clients.size() > 1 ? "s" : ""));
            for (const auto& client : clients) {
                QString label = client.name;
                if (label.isEmpty()) {
                    label = "Unknown Agent";
                }
                menu.addAction(label + " [" + client.address + "]")->setEnabled(false);
            }
        }
    }

    menu.exec(m_agentStatusBtn->mapToGlobal(QPoint(0, m_agentStatusBtn->height())));
#endif
}
