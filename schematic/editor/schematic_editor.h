#ifndef SCHEMATICEDITOR_H
#define SCHEMATICEDITOR_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QToolBar>
#include <QDockWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLabel>
#include <QStatusBar>
#include <QMap>
#include <QStringList>
#include <QSet>
#include <QUndoStack>
#include <QAction>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include "schematic_view.h"
#include "../ui/simulation_setup_dialog.h"
#include "../ui/schematic_components_widget.h"
#include "../ui/project_explorer_widget.h"
#include "../ui/flux_script_panel.h"
#include "../ui/image_preview_panel.h"
#include "flux/core/net_manager.h"
#include "schematic_layout_optimizer.h"
#include "../analysis/schematic_erc_rules.h"
#include "../items/schematic_page_item.h"
class SchematicView;
class SchematicPageItem;
class SchematicSpiceDirectiveItem;
class NetlistEditor;
class SymbolEditor;
class SpiceModelArchitect;
class SourceControlPanel;
#include "../../symbols/models/symbol_definition.h"
using Flux::Model::SymbolDefinition;

class SchematicEditor : public QMainWindow {
    Q_OBJECT
    friend class SchematicMenuRegistry;

public:
    SchematicEditor(QWidget* parent = nullptr);
    ~SchematicEditor();

    bool openFile(const QString& filePath);
    void setProjectContext(const QString& projectName, const QString& projectDir, const QStringList& workspaceFolders = QStringList());

    void showSimulationResults(const class SimResults& results);
    class NetManager* netManager() const { return m_netManager; }

    // Workspace Tab Management
    void addSchematicTab(const QString& name = "Untitled.sch");
    void openSymbolEditorWindow(const QString& name = "New Symbol",
                                const SymbolDefinition& preBuiltDef = SymbolDefinition());
    void addSimulationTab(const QString& name = "Simulation Results");
    void addModelArchitectTab();
    void addImageTab(const QString& filePath);
    void closeTab(int index);

private slots:
    void onZoomFit();
    void onZoomAllComponents();
    void onZoomSelection();
    void onZoomArea();
    void updatePropertyBar();
    void onPlaceSymbolInSchematic(const class SymbolDefinition& symbol);

    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onToolSelected();
    void onNewSchematic();
    void onOpenSchematic();
    void onImportAscFile();
    void onSaveSchematic();
    void onSaveSchematicAs();
    void onZoomIn();
    void onZoomOut();
    void onPageSizeChanged(const QString& size);
    void onUndo();
    void onRedo();
    void onUndoStackIndexChanged();
    void onDuplicate();
    void onDelete();
    void onCut();
    void onCopy();
    void onPaste();
    void onSelectAll();

private slots:
    // Layout optimization slots
    void onOptimizeLayout();
    void onApplyOrthogonalRouting();
    void onMinimizeCrossings();
    void onSwitchToEngineeringTheme();
    void updateCoordinates(QPointF pos);
    void onSelectionChanged();
    void onItemDoubleClicked(SchematicItem* item);
    void onSelectionDoubleClicked(const QList<SchematicItem*>& items);
    void openItemProperties(SchematicItem* item);
    void onPropertyChanged(const QString& name, const QVariant& value);
    void onAssignModel(const QString& modelName);
    void onRunERC();
    void onAnnotate();
    void onResetAnnotations();
    void onGenerateNetlist();
    void onOpenBOM();
    void onOpenSymbolEditor();
    void onCreateSymbolFromSchematic();
    void onOpenSymbolFieldEditor();
    void onExportPDF();
    void onExportSVG();
    void onExportImage();
    void onExportAISchematic();
    void onSettings();
    void onAbout();
    void onRunSimulation();
    void onOpenSimulationSetup();
    void onEditSimulationFromDirective(const QString& currentCommand);
    void onPauseSimulation();
    void onOpenNetlistEditor();
    void onOpenFluxScript();
    void onOpenGeminiAI();
    void onShowHelp();
    void onShowDeveloperHelp();
    void onProjectAudit();
    void onOpenCommandPalette();
    void onOpenComponentBrowser();
    void onOpenFindReplace();
    void onOpenModelArchitect();
    void onOpenPowerNetsManager();
    void onOpenBusAliasesManager();
    void onOpenERCRulesConfig();
    void onIgnoreSelectedErc();
    void onClearErcExclusions();
    void onLeaveSheet();
    void refreshHierarchyPanel();
    void onEditTitleBlock();
    void onSimulationResultsReady(const class SimResults& results);
    void onSimulationPaused(bool paused);
    void onTimeTravelSnapshot(double t, const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& currents);
    void onOverlayVisibilityChanged(bool showVoltage, bool showCurrent);
    void onClearSimulationOverlays();
    
    // Manipulation slots
    void onRotateCW();
    void onRotateCCW();
    void onFlipHorizontal();
    void onFlipVertical();
    void onBringToFront();
    void onSendToBack();
    
    // UI Phase 2 slots
    void onToggleMiniMap(bool visible);
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterX();
    void onAlignCenterY();
    void onDistributeH();
    void onDistributeV();
    void onCrossProbeReceived(const QString& refDes, const QString& netName);
    void handleIncomingECO();
    bool findAndSelectInScene(QGraphicsScene* scene, const QString& refDes);
    void navigateAndSelectHierarchical(const QString& refDes);
    
    // Panel toggling slots
    void onToggleLeftSidebar();
    void onToggleBottomPanel();
    void onToggleRightSidebar();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool m_updatingProperties;

    void setupCanvas();
    void createMenuBar();
    void createToolBar();
    void createDrawingToolbar();
    void createDockWidgets();
    void ensureProbeToolConnected();
    void createStatusBar();
    QIcon getThemeIcon(const QString& path);
    QIcon createComponentIcon(const QString& name);
    QIcon createItemPreviewIcon(class SchematicItem* item);
    void applyTheme();
    void updateGrid();
    void updatePageFrame();
    void buildHierarchyTree(QTreeWidgetItem* parent, const QString& filePath, int depth);
    void clearSimulationOverlays();
    void connectSimulationSignals();
    void updateSimulationUiState(bool running, const QString& statusMessage = QString());
    void updateSimulationOverlays(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& currents);
    void runLiveERC(const QList<class SchematicItem*>& items);
    QStringList resolveConnectedInstrumentNets(class SchematicItem* instrument) const;
    void appendSimulationIssue(const QString& message);
    void navigateToSimulationTarget(const QString& targetType, const QString& targetId);
    void onIssueItemDoubleClicked(QListWidgetItem* item);
    void updateGeminiProjectEffect();
    void onItemsHighlighted(const QStringList& references);
    void onSnippetGenerated(const QString& jsonSnippet);
    QList<ERCViolation> getErcViolations() const;
    void updateCurrentTabTitleFromFilePath(const QString& filePath);
    SymbolDefinition buildSymbolFromSelection() const;
    void applyDirectiveText(SchematicSpiceDirectiveItem* item, const QString& newText);
    SchematicSpiceDirectiveItem* resolveDirectiveItemForEdit(const QString& currentCommand) const;
    bool editDirectiveWithSimulationSetup(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem);
    bool editDirectiveWithMeanDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem);
    bool editDirectiveWithGenericDialog(const QString& currentCommand, SchematicSpiceDirectiveItem* directiveItem);
    void beginMouseFollowPlacement(const QList<SchematicItem*>& items, const QString& actionLabel);
    void endMouseFollowPlacement(bool cancel);

    // UI Components
    QTabWidget *m_workspaceTabs;
    QGraphicsScene *m_scene;
    SchematicView *m_view;
    NetManager *m_netManager;
    SchematicLayoutOptimizer *m_layoutOptimizer;

    class SchematicPageItem* m_pageFrame;
    QString m_currentPageSize;

    // Dock widgets
    QDockWidget *m_componentDock;
    QDockWidget *m_projectExplorerDock;
    QDockWidget *m_libraryDock;
    QDockWidget *m_hierarchyDock;

    // Panels
    class SchematicComponentsWidget *m_componentsPanel;
    class ProjectExplorerWidget *m_projectExplorer;
    QListWidget *m_libraryList;
    class SchematicHierarchyPanel *m_hierarchyPanel;
    class QTreeWidget *m_hierarchyTree;
    
    QDockWidget *m_ercDock;
    class ERCDiagnosticsPanel *m_ercPanel;
    class QListWidget *m_ercList;

    class SimulationPanel* m_simulationPanel = nullptr;
    SimulationSetupDialog::Config m_simConfig;
    bool m_mouseFollowPlacementActive = false;
    QList<SchematicItem*> m_mouseFollowItems;
    QList<QPointF> m_mouseFollowOffsets;
    QAction *m_toggleHeatmapAction = nullptr;
    QString m_mouseFollowActionLabel;
    QMap<QString, class LogicAnalyzerWindow*> m_laWindows;

    QDockWidget *m_geminiDock;
    class GeminiPanel* m_geminiPanel = nullptr;

    QDockWidget *m_scriptDock;
    Flux::ScriptPanel *m_scriptPanel;
    class LogicEditorPanel *m_logicEditorPanel;

    QDockWidget *m_sourceControlDock;
    SourceControlPanel *m_sourceControlPanel;

    QDockWidget *m_oscilloscopeDock;

    // Title block metadata
    TitleBlockData m_titleBlock;
    QMap<QString, QList<QString>> m_busAliases;
    QSet<QString> m_ercExclusions;
    SchematicERCRules m_ercRules;

    // Toolbars
    QToolBar *m_propertyBar;

    // Toolbar actions
    QMap<QString, QAction*> m_toolActions;
    QMap<QString, QAction*> m_editActions;
    QAction* m_runSimMenuAction;
    QAction* m_stopSimMenuAction;
    QAction* m_runSimToolbarAction;
    QAction* m_pauseSimToolbarAction = nullptr;
    QAction* m_stopSimToolbarAction = nullptr;
    QAction* m_showDetailedLogAction = nullptr;
    QWidget* m_simControlSubGroup = nullptr;

    // Status bar
    QLabel *m_coordLabel;
    QLabel *m_gridLabel;
    QLabel *m_layerLabel;
    QLabel *m_netLabel;

    // File state
    QString m_currentFilePath;
    bool m_isModified;
    bool m_isSaving = false;
    bool m_simulationRunning;
    bool m_simPaused = false;
    bool m_showVoltageOverlays;
    bool m_showCurrentOverlays;

    // Project context
    QString m_projectName;
    QString m_projectDir;

    // UI Phase 2 Components
    class SchematicMiniMap* m_miniMap = nullptr;
    QAction* m_toggleMiniMapAction = nullptr;

    // Undo/Redo
    QUndoStack *m_undoStack;
    class SchematicAPI *m_api;
    
    // Navigation stack for hierarchy
    QStringList m_navigationStack;
    QWidget* m_breadcrumbWidget;
    void updateBreadcrumbs();
    bool canReuseTab(int index) const;
};

#endif // SCHEMATICEDITOR_H
