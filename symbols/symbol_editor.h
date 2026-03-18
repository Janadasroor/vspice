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

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

class QAbstractGraphicsShapeItem;
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
    void onNewSymbol();
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
    void createPinTable();
    void updatePinTable();
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
    
    // Pin Table Helpers
    QList<int> selectedPinRows() const;
    void applyPinEditsToRows(const QList<int>& rows, const std::function<void(SymbolPrimitive&)>& edit, const QString& label);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

    // Current editing state
    enum Tool { Select, Line, Rect, Circle, Arc, Text, Pin, Polygon, Erase, ZoomArea, Anchor, Bezier, Image };
    Tool m_currentTool = Select;
    
    // UI Components
    QGraphicsScene* m_scene = nullptr;
    SymbolEditorView* m_view = nullptr;
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
    
    // Library Browser
    QLineEdit* m_libSearchEdit = nullptr;
    QTreeWidget* m_libraryTree = nullptr;
    QTableWidget* m_pinTable = nullptr;
    QListWidget* m_srcList = nullptr;
    QGraphicsView* m_libPreviewView = nullptr;
    QGraphicsScene* m_libPreviewScene = nullptr;
    class GeminiPanel* m_aiPanel = nullptr;
    
    // Bulk Pin Edits
    QComboBox* m_pinBulkOrientation = nullptr;
    QComboBox* m_pinBulkType = nullptr;

    // Wizard
    class QSpinBox* m_pinCountSpin = nullptr;
    class QDoubleSpinBox* m_pinSpacingSpin = nullptr;
    class QDoubleSpinBox* m_bodyWidthSpin = nullptr;
    class QComboBox* m_wizardStyleCombo = nullptr;
    
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
