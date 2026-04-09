#ifndef FOOTPRINT_EDITOR_H
#define FOOTPRINT_EDITOR_H

#include <QDialog>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QToolBar>
#include <QDockWidget>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTreeWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QPointer>
#include <QCheckBox>
#include <QList>
#include <QTabWidget>
#include <QTextEdit>
#include <QListWidget>
#include <QGraphicsRectItem>
#include "../ui/property_editor.h"
#include "models/footprint_definition.h"

#include "ui/footprint_editor_view.h"

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;
using Flux::Model::Footprint3DModel;

class FootprintEditorView;
class QDragEnterEvent;
class QDropEvent;
class PCB3DWindow;

/**
 * @brief Dialog for creating and editing PCB footprints
 */
class FootprintEditor : public QDialog {
    Q_OBJECT

public:
    explicit FootprintEditor(QWidget* parent = nullptr);
    explicit FootprintEditor(const FootprintDefinition& footprint, QWidget* parent = nullptr);
    ~FootprintEditor();
    
    FootprintDefinition footprintDefinition() const;
    void setFootprintDefinition(const FootprintDefinition& def);
    
    enum Tool { 
        Select, 
        Pad, 
        Line, 
        Arc, 
        Rect, 
        Circle, 
        Text,
        ZoomArea,
        Measure,
        Anchor
    };
    
signals:
    void footprintSaved(const FootprintDefinition& footprint);

private slots:
    void onToolSelected();
    void onSave();
    void onSaveToLibrary(); // Save to database/file
    void onClear();
    void onUndo();
    void onRedo();
    void onDelete();
    void onSelectionChanged();
    
    // Zoom tools
    void onZoomIn();
    void onZoomOut();
    void onZoomFit();
    void onOpenWizard();
    
    // Alignment tools
    void onAlignLeft();
    void onAlignRight();
    void onAlignTop();
    void onAlignBottom();
    void onAlignCenterH();
    void onAlignCenterV();
    void onDistributeH();
    void onDistributeV();
    void onMatchSpacing();
    void onMoveExactly();
    void onAddPrimitiveExact();
    
    // Flip tools
    void onFlipHorizontal();
    void onFlipVertical();
    void onCreateMirroredPair();
    
    // Advanced tools
    void onRunDRC();
    void onArrayTool();
    void onPolarGridTool();
    void onConvertToPad();
    void onImportKicadFootprint();
    void onOpen3DPreview();
    void onSetAnchor(QPointF pos);
    
    // Grid / Measure
    void onGridSizeChanged(const QString& size);
    void onMeasure(QPointF p1, QPointF p2);
    void onWizardGenerate();
    void onContextMenu(QPoint pos);
    void onRectResizeStarted(const QString& corner, QPointF scenePos);
    void onRectResizeUpdated(QPointF scenePos);
    void onRectResizeFinished(QPointF scenePos);
    
private:
    void setupUI();
    void createToolBar();
    void createPropertiesPanel();
    void createInfoPanel();
    void updatePropertiesPanel();
    void updatePreview();
    void drawGrid();
    void updateSceneFromDefinition();
    QGraphicsItem* buildVisual(const FootprintPrimitive& prim, int index);
    bool prepareFootprint();
    bool importKicadFootprintFromFile(const QString& path);
    QString resolveModelPathForPreview(const QString& rawPath) const;
    void refreshModelSelector();
    void loadModelToFields(int index);
    void syncCurrentModelFromFields();
    
    // Current editing state
    // Current editing state
    Tool m_currentTool;
    
    // UI Components
    QGraphicsScene* m_scene;
    FootprintEditorView* m_view;
    QToolBar* m_toolbar;
    QToolBar* m_leftToolbar;
    QWidget* m_bottomPanel = nullptr;
    QWidget* m_leftNavigatorPanel = nullptr;
    QWidget* m_rightPanel = nullptr;
    QDockWidget* m_propertiesDock;
    PropertyEditor* m_propertyEditor;
    QTabWidget* m_leftTabWidget = nullptr;
    QTabWidget* m_rightTabWidget = nullptr;
    QTabWidget* m_bottomTabWidget = nullptr;
    QTextEdit* m_codePreview = nullptr;
    QListWidget* m_ruleList = nullptr;
    
    // Footprint info
    QLineEdit* m_nameEdit;
    QLineEdit* m_descriptionEdit;
    QComboBox* m_categoryCombo;
    QComboBox* m_classificationCombo;
    QCheckBox* m_excludeBOMCheck;
    QCheckBox* m_excludePosCheck;
    QCheckBox* m_dnpCheck;
    QCheckBox* m_netTieCheck;
    QLineEdit* m_keywordsEdit;
    
    // 3D Model Settings
    QComboBox* m_modelSelector;
    QPushButton* m_addModelButton;
    QPushButton* m_removeModelButton;
    QLineEdit* m_modelFileEdit;
    QLineEdit* m_modelOffsetX;
    QLineEdit* m_modelOffsetY;
    QLineEdit* m_modelOffsetZ;
    QLineEdit* m_modelRotX;
    QLineEdit* m_modelRotY;
    QLineEdit* m_modelRotZ;
    QLineEdit* m_modelScaleX;
    QLineEdit* m_modelScaleY;
    QLineEdit* m_modelScaleZ;
    QDoubleSpinBox* m_modelOpacitySpin = nullptr;
    QCheckBox* m_modelVisibleCheck = nullptr;
    
    // Internal state
    FootprintDefinition m_footprint;
    QList<QGraphicsItem*> m_drawnItems;
    QMap<QString, QAction*> m_toolActions;
    
    // Undo/Redo
    class QUndoStack* m_undoStack = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    
    // Drawing state
    bool m_isDrawing;
    QPointF m_startPoint;
    QGraphicsItem* m_previewItem;
    QList<QPointF> m_polyPoints;
    QList<QGraphicsRectItem*> m_resizeHandles;
    bool m_rectResizeSessionActive = false;
    int m_rectResizePrimIdx = -1;
    QString m_rectResizeCorner;
    FootprintDefinition m_rectResizeOldDef;
    QPointF m_rectResizeAnchor;
    QPointF m_resizeLineOtherEnd;
    QPointF m_resizeCircleCenter;
    
    // Pad settings
    QString m_currentPadShape;
    void setPadShape(const QString& shape);

    // Layer settings
    FootprintPrimitive::Layer m_activeLayer;
    QComboBox* m_layerCombo;

    // Library Browser
    void createLibraryBrowser();
    void populateLibraryTree();
    QTreeWidget* m_libraryTree;
    QLineEdit* m_libSearchEdit;

    // Wizard UI
    QComboBox* m_wizType;
    QSpinBox* m_wizPins;
    QDoubleSpinBox *m_wizPitch, *m_wizSpan, *m_wizPadW, *m_wizPadH;

    QLabel* m_statusLabel; // Status information
    QCheckBox* m_previewBottomCopperCheck = nullptr;
    QPointer<PCB3DWindow> m_footprint3DWindow;
    QGraphicsScene* m_footprint3DScene = nullptr;
    QString m_lastImportBaseDir;
    QList<Footprint3DModel> m_models3D;
    
    QString getNextPadNumber() const;
    void clearResizeHandles();
    void updateResizeHandles();
    void populatePropertiesFor(int index);
    
private slots:
    void onLibSearchChanged(const QString& text);
    void onLoadFootprint(QTreeWidgetItem* item, int column);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
};

#endif // FOOTPRINT_EDITOR_H
