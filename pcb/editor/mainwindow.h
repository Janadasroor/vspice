#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QToolBar>
#include <QDockWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QLabel>
#include <QStatusBar>
#include <QComboBox>
#include <QMap>
#include <QUndoStack>

#include "pcb_view.h"
#include "pcb_component_tool.h"
#include "theme_manager.h"
#include "../ui/pcb_property_editor.h"
#include "../drc/pcb_design_rules_editor.h"
#include "../../core/eco_types.h"

// Forward declarations
class PCBLayerPanel;
class PCBDRCPanel;
class SelectionFilterWidget;

namespace Flux {
    class PCBPropertyEditor;
    class PCBDesignRulesEditor;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    bool openFile(const QString& filePath);
    void setProjectContext(const QString& projectName, const QString& projectDir);
    QString projectName() const { return m_projectName; }
    void handleIncomingECO();

private slots:
    void onToolSelected();
    void onNewProject();
    void onOpenProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomAllComponents();
    void onZoomSelection();
    void onRunDRC();
    void onRunCourtyardValidation();
    void onCreateLinearArray();
    void onCreateCircularArray();
    void onPanelizeBoard();
    void onToggle3DView();
    void onDRCViolationSelected(QPointF location);
    void onActiveLayerChanged(int layerId);
    void updateCoordinates(QPointF pos);
    void onOpenFootprintEditor();
    void onOpenGeminiAI();
    void onOpenGerberViewer();
    void onBoardSetup();
    void onViaStitching();
    void onGenerateGerbers();
    void onExportPDF();
    void onExportSVG();
    void onExportImage();
    void onExportAssemblyDrawing();
    void onExportIPC2581();
    void onExportODBpp();
    void onExportPickPlace();
    void onExportSTEP();
    void onExportIGES();
    void onSettings();
    void onImportNetlist();
    void onPropertyChanged(const QString& name, const QVariant& value);
    void onFilterChanged();
    void onOpenCommandPalette();
    void onAutoRoute();
    void onLengthMatching();
    
    // Alignment slots
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterX();
    void onAlignCenterY();
    void onDistributeH();
    void onDistributeV();
    void onRotate();
    void onMirror();
    void onDeleteSelection();
    void onBringToFront();
    void onSendToBack();
    void updateOptionsBar(const QString& toolName);
    void applyNetHighlighting();
    void clearNetHighlighting();
    void onSnippetGenerated(const QString& jsonSnippet);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QToolBar *m_optionsToolbar;
    QToolBar *m_propertyBar;
    void applyECO(const struct ECOPackage& package);
    void createMenuBar();
    void createToolBar();
    void updatePropertyBar();
    void createDockWidgets();
    void createStatusBar();
    void setupCanvas();
    void applyTheme();
    void updateGrid();
    void updateLayerLabel();
    QWidget* createStatusSeparator();
    QIcon createPCBIcon(const QString& name);
    void ensureRightBottomDockTabs();

    // UI Components
    QGraphicsScene *m_scene;
    PCBView *m_view;

    // Dock widgets
    QDockWidget *m_layerDock;
    QDockWidget *m_propertiesDock;
    QDockWidget *m_libraryDock;
    QDockWidget *m_drcDock;
    QDockWidget *m_rulesDock;
    
    // Panels
    PCBLayerPanel *m_layerPanel;
    PCBDRCPanel *m_drcPanel;
    class PCB3DWindow *m_3dWindow = nullptr;
    Flux::PCBPropertyEditor *m_propertyEditor;
    Flux::PCBDesignRulesEditor *m_rulesEditor;
    class PCBComponentsWidget *m_componentsPanel;

    QDockWidget *m_geminiDock;
    class GeminiPanel* m_geminiPanel;
    class SelectionFilterWidget* m_selectionFilter;

    // Toolbar actions
    QMap<QString, QAction*> m_toolActions;

    // Component tool reference
    PCBComponentTool* m_componentTool;

    // Status bar
    QLabel *m_coordLabel;
    QComboBox *m_gridCombo;
    QLabel *m_layerLabel;

    // Undo Stack
    QUndoStack *m_undoStack;
    class PCBAPI *m_api;

    // File state
    QString m_currentFilePath;

    // Project context
    QString m_projectName;
    QString m_projectDir;

    // Visual net focus
    bool m_netHighlightEnabled = false;
    QString m_highlightedNet;
};

#endif // MAINWINDOW_H
