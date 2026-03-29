#ifndef SYMBOL_EDITOR_H
#define SYMBOL_EDITOR_H

#include <QMainWindow>
#include <QGraphicsScene>
#include <QToolBar>
#include <QStatusBar>
#include <QToolButton>
#include <QDockWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QPointer>
#include <QTableWidget>
#include <QListWidget>
#include <QTreeWidget>
#include <QTextEdit>
#include <functional>
#include "models/symbol_definition.h"
#include "symbol_editor_view.h"
#include "../python/gemini_panel.h"
#include "ui/symbol_preview_widget.h"

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

class QAbstractGraphicsShapeItem;
class QGraphicsRectItem;
class PropertyEditor;
/**
 * @brief Main window for creating and editing schematic symbols
 */
class SymbolEditor : public QMainWindow {
    Q_OBJECT

public:
    explicit SymbolEditor(QWidget* parent = nullptr);
    explicit SymbolEditor(const SymbolDefinition& symbol, QWidget* parent = nullptr);
    ~SymbolEditor();
    
    SymbolDefinition symbolDefinition() const;
    void setSymbolDefinition(const SymbolDefinition& def);
    void applySymbolDefinition(const SymbolDefinition& def);
    void setProjectKey(const QString& key);

    bool importKicadSymbol(const QString& path, const QString& symbolName = QString());
    bool importLtspiceSymbol(const QString& path);
    bool loadLibrary(const QString& path);
    
signals:
    void symbolSaved(const SymbolDefinition& symbol);
    void placeInSchematicRequested(const SymbolDefinition& symbol);

private slots:
    void onToolSelected();
    void onSave();
    void onSaveToLibrary();
    void onExportVioSym();
    void onRefreshLibraries();
    void onClear();
    void onUndo();
    void onRedo();
    void onDelete();
    void onSelectionChanged();
    void onRectResizeStarted(const QString& corner, QPointF scenePos);
    void onRectResizeUpdated(QPointF scenePos);
    void onRectResizeFinished(QPointF scenePos);
     void onBezierEditPointClicked(QPointF pos);
     void onBezierEditPointDragged(QPointF newPos);
     void updateBezierEditPreview();
    void onNewSymbol();
    void onAIDatasheetImport();
    void onCloneSymbol(class QTreeWidgetItem* item, int column);
    void onRotateCW();
    void onRotateCCW();
    void onFlipH();
    void onFlipV();
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterX();
    void onAlignCenterY();
    void onDistributeH();
    void onDistributeV();
    void onMatchSpacing();
    void onMoveExactly();
    void onAddPrimitiveExact();
    void onSnapToGrid();
    void onPinTable();
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onZoomSelection();
      void onPenPointAdded(QPointF pos);
      void onPenHandleDragged(QPointF handlePos);
      void onPenPointFinished();
      void onPenPathClosed();
      void onPenClicked(QPointF pos, int pointIndex = -1, int handleIndex = -1);
      void onPenDoubleClicked(QPointF pos, int pointIndex = -1);
      void finalizePenPath();
      void clearPenState();
      void updatePenPreview();
    void onCopy();
    void onPaste();
    void onDuplicate();
    void onItemErased(QGraphicsItem* item);
    void onGridSizeChanged(const QString& size);
    void onUnitChanged(int index);
    void onCopyToAlternateStyle();
    void updateCoordinates(QPointF pos);
    void onLibSearchChanged(const QString& text);
    void onAiSymbolGenerated(const QString& json);
    void onWizardGenerate();
    void onWizardSaveTemplate();
    void onWizardTemplateSearchChanged(const QString& text);
    void onWizardApplyTemplate();
    void onImportKicadSymbol();
    void onImportLtspiceSymbol();
    void onImportImage();
    void onManageCustomFields();
    void onBrowseFootprint();
    void onPlaceInSchematic();
    void onRunSRC();
    void onLibraryContextMenu(const QPoint& pos);
    void onCanvasContextMenu(const QPoint& pos);
    void onLibraryItemClicked(class QTreeWidgetItem* item, int column);
    void onPinTableItemChanged(int row, int col);
    void onPropertyChanged(const QString& name, const QVariant& value);
    
    // Pin Management
    void onPinRenumberSequential();
    void onPinApplyOrientation();
    void onPinApplyType();
    void onPinDistributeSelected();
    void onPinSortByNumber();
    void onPinStackSelected();

private:
    void applyTheme();
    void setupUI();
    QIcon getThemeIcon(const QString& path);
    void updatePropertiesPanel();
    void populatePropertiesFor(int index);
    void createMenuBar();
    void createToolBar();
    void rebuildPanelsMenu();
    void tryAutoDetectModelName();
    void createStatusBar();
    void setEditingUnlocked(bool unlocked, const QString& message = QString());
    QString promptForTargetLibrary();
    void connectViewSignals();
    void createSymbolInfoPanel();
    void createLibraryBrowser();
    void createWizardPanel();
    void refreshWizardTemplateList(const QString& query = QString());
    void updateWizardTemplatePreview();
    void createPinTable();
    void updatePinTable();
    void updateSubcktMappingTable();
    QWidget* createSymbolMetadataWidget();
    void openSubcircuitPicker();
    QStringList currentSymbolPinNames() const;
     void populateLibraryTree();
      void updateCodePreview();
      void updatePinPreview(QPointF pos);
      
    // Scene & Visual Helpers
    QColor themeLineColor() const;
    QColor themeTextColor() const;
    QColor themePinLabelColor() const;
    int primitiveIndex(QGraphicsItem* item) const;
    void removeOverlayItems();
    void clearScene();
    void updateOverlayLabels();
    void applyShapeStyle(QAbstractGraphicsShapeItem* shape, const SymbolPrimitive& prim) const;
    QGraphicsItem* buildVisual(const SymbolPrimitive& prim, int index) const;
    void updateVisualForPrimitive(int index, const SymbolPrimitive& prim);
    void updateGuideAnchors();
    void clearResizeHandles();
    void updateResizeHandles();
    
    // Pin Table Helpers
    QList<int> selectedPinRows() const;
    void applyPinEditsToRows(const QList<int>& rows, const std::function<void(SymbolPrimitive&)>& edit, const QString& label);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

    // Current editing state
    enum Tool { Select, Line, Rect, Circle, Arc, Text, Pin, Polygon, Erase, ZoomArea, Anchor, Bezier, Image, Pen };
    Tool m_currentTool = Select;
    
    // Pen tool state - Figma-style bezier path editing
    struct PenPoint {
        QPointF pos;           // Main anchor point
        QPointF handleIn;      // Control handle coming INTO this point (relative)
        QPointF handleOut;     // Control handle going OUT of this point (relative)
        bool smooth;           // Whether handles are locked (smooth curve)
        bool corner;           // True = corner point, False = curve point
    };
    QList<PenPoint> m_penPoints;
    int m_selectedPenPoint = -1;           // Index of selected point
    int m_selectedPenHandle = -1;          // -1=none, 0=in, 1=out (for the selected point)
    int m_selectedPenMidpoint = -1;        // Index of selected midpoint (segment edge point)
    QGraphicsPathItem* m_penPreviewItem = nullptr;
    QList<QGraphicsEllipseItem*> m_penPointMarkers;
    QList<QGraphicsLineItem*> m_penHandleLines;
    QList<QGraphicsEllipseItem*> m_penHandleDots;
    QList<QGraphicsEllipseItem*> m_penMidpointDots;   // Midpoint dots on segment edges
    bool m_penFinalizing = false;  // Guard against double finalization
    QPointF m_penLastClickPos;     // Track for detecting double-click
    double m_penDoubleClickTimeout = 300.0;  // milliseconds
     double m_penLastClickTime = 0.0;
     
     // Pen tool helpers - must be after PenPoint definition
     QPointF calculateBezierPoint(const PenPoint& p1, const PenPoint& p2, qreal t) const;
     
     // Select mode bezier editing - allows editing bezier curves while in Select tool
     int m_editingBezierIndex = -1;                    // Index of bezier primitive being edited (-1 = none)
     struct BezierEditPoint {
         int pointType;  // 0=start, 1=cp1, 2=cp2, 3=end
         QPointF pos;
     };
     QList<BezierEditPoint> m_bezierEditPoints;        // Current bezier edit points for visualization
    QList<QGraphicsEllipseItem*> m_bezierEditMarkers; // Visual edit point markers
    QList<QGraphicsLineItem*> m_bezierEditLines;      // Handle lines
    int m_selectedBezierPoint = -1;                   // Which point is selected for dragging

    // Rectangle resize handles/session
    QList<QGraphicsRectItem*> m_resizeHandles;
    bool m_rectResizeSessionActive = false;
    int m_rectResizePrimIdx = -1;
    QString m_rectResizeCorner;
    QPointF m_rectResizeAnchor;
    QPointF m_resizeLineOtherEnd;
    QPointF m_resizeCircleCenter;
    SymbolDefinition m_rectResizeOldDef;
     
    // UI Components
    QGraphicsScene* m_scene = nullptr;
    SymbolEditorView* m_view = nullptr;
    PropertyEditor* m_propertyEditor = nullptr;
    QToolBar* m_toolbar = nullptr;
    QToolBar* m_leftToolbar = nullptr;
    class QMenu* m_panelsMenu = nullptr;
    QStatusBar* m_statusBar = nullptr;
    QLabel* m_coordLabel = nullptr;
    QLabel* m_gridLabel = nullptr;
    QAction* m_selectAction = nullptr;
    
    // Multi-unit & Styles
    QComboBox* m_unitCombo = nullptr;
    QComboBox* m_styleCombo = nullptr;
    QComboBox* m_colorPresetCombo = nullptr;
    int m_currentUnit = 1; // 0 = shared, 1 = Unit A...
    int m_currentStyle = 0; // 0 = shared, 1 = Standard, 2 = Alternate
    int m_colorPreset = 0; // 0 = Theme, 1..N = editor color presets

    // Symbol info
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_descriptionEdit = nullptr;
    QComboBox* m_categoryCombo = nullptr;
    QLineEdit* m_prefixEdit = nullptr;
    QLineEdit* m_footprintEdit = nullptr;
    QTextEdit* m_codePreview = nullptr;
    QComboBox* m_modelSourceCombo = nullptr;
    QLineEdit* m_modelPathEdit = nullptr;
    QLineEdit* m_modelNameEdit = nullptr;
    QTableWidget* m_subcktMappingTable = nullptr;
    
    // Properties panel
    class QTabWidget* m_propsTabWidget = nullptr;
    QDockWidget* m_propsDock = nullptr;
    
    // Library Browser
    QLineEdit* m_libSearchEdit = nullptr;
    QTreeWidget* m_libraryTree = nullptr;
    QTableWidget* m_pinTable = nullptr;
    QListWidget* m_srcList = nullptr;
    QGraphicsView* m_libPreviewView = nullptr;
    QGraphicsScene* m_libPreviewScene = nullptr;
    class GeminiPanel* m_aiPanel = nullptr;
    SymbolPreviewWidget* m_livePreview = nullptr;
    
    // Bulk Pin Edits
    QComboBox* m_pinBulkOrientation = nullptr;
    QComboBox* m_pinBulkType = nullptr;

    // Wizard
    class QSpinBox* m_pinCountSpin = nullptr;
    class QDoubleSpinBox* m_pinSpacingSpin = nullptr;
    class QDoubleSpinBox* m_bodyWidthSpin = nullptr;
    class QComboBox* m_wizardStyleCombo = nullptr;
    QLineEdit* m_wizardTemplateSearchEdit = nullptr;
    class QComboBox* m_wizardTemplateCombo = nullptr;
    QLabel* m_wizardTemplateInfoLabel = nullptr;
    QLabel* m_wizardTemplateDescLabel = nullptr;
    QGraphicsView* m_wizardPreviewView = nullptr;
    QGraphicsScene* m_wizardPreviewScene = nullptr;
    
    // Internal state
    SymbolDefinition m_symbol;
    QList<SymbolPrimitive> m_copyBuffer;
    QList<QGraphicsItem*> m_drawnItems;
    QList<QGraphicsItem*> m_overlayItems;
    QMap<QString, QAction*> m_toolActions;
    
    // Undo/Redo
    class QUndoStack* m_undoStack = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_deleteAction = nullptr;

    // Drawing state
    QList<QPointF> m_polyPoints;
    QGraphicsItem* m_previewItem = nullptr;
    QString m_previewOrientation = "Right";
    bool m_editingUnlocked = false;
    QString m_targetLibraryName;
    QString m_projectKey;

    friend class AddPrimitiveCommand;
    friend class RemovePrimitiveCommand;
    friend class UpdateSymbolCommand;
};

#endif // SYMBOL_EDITOR_H
