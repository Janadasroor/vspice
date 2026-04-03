#include "footprint_editor.h"
#include "footprint_library.h"
#include "footprint_commands.h"
#include "kicad_footprint_importer.h"
#include "ui/footprint_wizard_dialog.h"
#include "../core/theme_manager.h"
#include "../core/config_manager.h"
#include "../pcb/ui/pcb_3d_window.h"
#include "../pcb/items/component_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUndoStack>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include <QActionGroup>
#include <QHeaderView>
#include <QInputDialog>
#include <cmath>
#include <algorithm>
#include <QGraphicsTextItem>
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileDialog>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include "items/footprint_primitive_item.h"
#include "analysis/footprint_engine.h"

using namespace Flux::Model;
using namespace Flux::Item;
using namespace Flux::Analysis;

namespace {
QPainterPath makeTrapezoidPath(qreal w, qreal h, qreal deltaX) {
    // deltaX is total top-width reduction (positive narrows top, negative widens top)
    const qreal maxDelta = std::max(0.0, w - 0.05);
    const qreal clamped = std::clamp(deltaX, -maxDelta, maxDelta);
    const qreal halfTop = std::max(0.025, (w - clamped) / 2.0);
    const qreal halfBottom = w / 2.0;
    const qreal halfH = h / 2.0;

    QPainterPath path;
    path.moveTo(-halfBottom, halfH);
    path.lineTo(halfBottom, halfH);
    path.lineTo(halfTop, -halfH);
    path.lineTo(-halfTop, -halfH);
    path.closeSubpath();
    return path;
}

FootprintPrimitive::Layer mirroredTopBottomLayer(FootprintPrimitive::Layer layer) {
    switch (layer) {
        case FootprintPrimitive::Top_Copper: return FootprintPrimitive::Bottom_Copper;
        case FootprintPrimitive::Bottom_Copper: return FootprintPrimitive::Top_Copper;
        case FootprintPrimitive::Top_Silkscreen: return FootprintPrimitive::Bottom_Silkscreen;
        case FootprintPrimitive::Bottom_Silkscreen: return FootprintPrimitive::Top_Silkscreen;
        case FootprintPrimitive::Top_Courtyard: return FootprintPrimitive::Bottom_Courtyard;
        case FootprintPrimitive::Bottom_Courtyard: return FootprintPrimitive::Top_Courtyard;
        case FootprintPrimitive::Top_Fabrication: return FootprintPrimitive::Bottom_Fabrication;
        case FootprintPrimitive::Bottom_Fabrication: return FootprintPrimitive::Top_Fabrication;
        case FootprintPrimitive::Top_SolderMask: return FootprintPrimitive::Bottom_SolderMask;
        case FootprintPrimitive::Bottom_SolderMask: return FootprintPrimitive::Top_SolderMask;
        case FootprintPrimitive::Top_SolderPaste: return FootprintPrimitive::Bottom_SolderPaste;
        case FootprintPrimitive::Bottom_SolderPaste: return FootprintPrimitive::Top_SolderPaste;
        case FootprintPrimitive::Top_Adhesive: return FootprintPrimitive::Bottom_Adhesive;
        case FootprintPrimitive::Bottom_Adhesive: return FootprintPrimitive::Top_Adhesive;
        default: return layer;
    }
}

QList<QPointF> primitiveToPadPolygonPoints(const FootprintPrimitive& prim) {
    QList<QPointF> points;
    if (prim.type == FootprintPrimitive::Rect) {
        QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                 prim.data["width"].toDouble(), prim.data["height"].toDouble());
        r = r.normalized();
        points << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
    } else if (prim.type == FootprintPrimitive::Polygon) {
        const QJsonArray arr = prim.data["points"].toArray();
        for (const auto& v : arr) {
            const QJsonObject o = v.toObject();
            points << QPointF(o["x"].toDouble(), o["y"].toDouble());
        }
    }
    return points;
}

qreal cross2D(const QPointF& o, const QPointF& a, const QPointF& b) {
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

QList<QPointF> convexHull2D(const QList<QPointF>& input) {
    if (input.size() < 3) return input;

    QVector<QPointF> pts = input.toVector();
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
        if (a.x() == b.x()) return a.y() < b.y();
        return a.x() < b.x();
    });
    pts.erase(std::unique(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
        return std::abs(a.x() - b.x()) < 1e-9 && std::abs(a.y() - b.y()) < 1e-9;
    }), pts.end());
    if (pts.size() < 3) return pts.toList();

    QVector<QPointF> lower, upper;
    for (const QPointF& p : pts) {
        while (lower.size() >= 2 && cross2D(lower[lower.size() - 2], lower[lower.size() - 1], p) <= 0.0) {
            lower.removeLast();
        }
        lower.push_back(p);
    }
    for (int i = pts.size() - 1; i >= 0; --i) {
        const QPointF& p = pts[i];
        while (upper.size() >= 2 && cross2D(upper[upper.size() - 2], upper[upper.size() - 1], p) <= 0.0) {
            upper.removeLast();
        }
        upper.push_back(p);
    }

    lower.removeLast();
    upper.removeLast();
    QVector<QPointF> hull = lower + upper;
    return hull.toList();
}
}

// === FootprintEditor ===

FootprintEditor::FootprintEditor(QWidget* parent)
    : QDialog(parent), m_currentTool(Select), m_previewItem(nullptr), m_activeLayer(FootprintPrimitive::Top_Silkscreen)
{
    m_undoStack = new QUndoStack(this);
    setupUI();
}

FootprintEditor::FootprintEditor(const FootprintDefinition& footprint, QWidget* parent)
    : QDialog(parent), m_currentTool(Select), m_footprint(footprint), m_previewItem(nullptr), m_activeLayer(FootprintPrimitive::Top_Silkscreen)
{
    m_undoStack = new QUndoStack(this);
    setupUI();
    setFootprintDefinition(footprint);
    m_currentPadShape = "Rect"; // Default
}

void FootprintEditor::setPadShape(const QString& shape) {
    m_currentPadShape = shape;
}

FootprintEditor::~FootprintEditor() {}

void FootprintEditor::setupUI() {
    setWindowTitle("Footprint Editor - Design Mode");
    resize(1240, 850);
    setWindowFlags(Qt::Window | Qt::WindowMaximizeButtonHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    setAcceptDrops(true);
    
    // Global Dark Style patterned after components-library-panel.html
    setStyleSheet(
        "QDialog { background-color: #2b2b2b; color: #cccccc; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; }"
        "QGroupBox { border: 1px solid #1e1e1e; margin-top: 15px; padding-top: 15px; color: #cccccc; font-size: 13px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 5px; }"
        "QLineEdit, QComboBox, QTreeWidget, QSpinBox, QDoubleSpinBox { background-color: #1e1e1e; border: 1px solid #3c3c3c; padding: 4px 8px; color: #cccccc; selection-background-color: #094771; font-size: 12px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid #007acc; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox::down-arrow { image: url(:/icons/arrow_down.svg); width: 12px; height: 12px; }"
        "QTreeWidget::item { padding: 3px 0; border: none; }"
        "QTreeWidget::item:hover { background-color: #2a2d2e; }"
        "QTreeWidget::item:selected { background-color: #094771; color: white; }"
        "QPushButton { background-color: #2d2d30; border: 1px solid #555; padding: 6px 12px; color: #cccccc; }"
        "QPushButton:hover { background-color: #3c3c3c; }"
        "QPushButton:pressed { background-color: #094771; color: white; }"
        "QToolBar { background-color: #2d2d30; border-bottom: 1px solid #1e1e1e; padding: 8px 10px; spacing: 4px; }"
        "QToolBar#LeftToolBar { border-bottom: none; border-right: 1px solid #1e1e1e; padding: 4px; }"
        "QToolButton { background: transparent; border: 1px solid transparent; padding: 4px; color: #cccccc; }"
        "QToolButton:hover { border-color: #555; background-color: #3c3c3c; }"
        "QToolButton:checked { background-color: #094771; border-color: #094771; color: white; }"
        "QLabel { color: #cccccc; font-size: 13px; }"
        "QScrollBar:vertical { background: #2d2d30; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #555; border-radius: 2px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #666; }"
        "QScrollArea { border: none; background-color: #2b2b2b; }"
    );
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Top Utility ToolBar
    createToolBar();
    mainLayout->addWidget(m_toolbar);
    
    // Create separator
    QFrame* hLine = new QFrame();
    hLine->setFrameShape(QFrame::HLine);
    hLine->setStyleSheet("background-color: #1e1e1e; height: 1px; border: none;");
    mainLayout->addWidget(hLine);
    
    // Main Content (View + Lateral Toolbars + Side Panel)
    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setSpacing(0);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Lateral Drawing Bar
    contentLayout->addWidget(m_leftToolbar);
    
    // -- Viewport Area --
    m_scene = new QGraphicsScene(this);
    m_scene->setSceneRect(-200, -200, 400, 400); // 400x400 mm workspace
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &FootprintEditor::onSelectionChanged);
    
    m_view = new FootprintEditorView(this);
    m_view->setScene(m_scene);
    connect(m_view, &FootprintEditorView::contextMenuRequested, this, &FootprintEditor::onContextMenu);
    contentLayout->addWidget(m_view, 1);
    
    // -- Right Side Configuration Panel --
    QScrollArea* sideScroll = new QScrollArea();
    sideScroll->setFixedWidth(340);
    sideScroll->setWidgetResizable(true);
    
    QWidget* sideWidget = new QWidget();
    sideWidget->setObjectName("SidePanel");
    sideWidget->setStyleSheet("#SidePanel { background-color: #3c3c3c; border-left: 1px solid #1e1e1e; }");
    
    QVBoxLayout* sideLayout = new QVBoxLayout(sideWidget);
    sideLayout->setContentsMargins(15, 15, 15, 15);
    sideLayout->setSpacing(15);
    
    // 1. Identity Group
    QGroupBox* infoGroup = new QGroupBox("Footprint Metadata");
    QFormLayout* infoForm = new QFormLayout(infoGroup);
    infoForm->setSpacing(10);
    infoForm->setContentsMargins(15, 25, 15, 15);
    infoForm->setLabelAlignment(Qt::AlignRight);
    
    createInfoPanel();
    infoForm->addRow("Ref Name", m_nameEdit);
    infoForm->addRow("Desc", m_descriptionEdit);
    infoForm->addRow("Category", m_categoryCombo);
    infoForm->addRow("Class", m_classificationCombo);
    infoForm->addRow("Keywords", m_keywordsEdit);
    infoForm->addRow("", m_excludeBOMCheck);
    infoForm->addRow("", m_excludePosCheck);
    infoForm->addRow("", m_dnpCheck);
    infoForm->addRow("", m_netTieCheck);
    sideLayout->addWidget(infoGroup);
    
    // 3D Model Group
    QGroupBox* modelGroup = new QGroupBox("3D Visualization");
    QFormLayout* modelForm = new QFormLayout(modelGroup);
    modelForm->setSpacing(5);
    modelForm->setContentsMargins(10, 20, 10, 10);

    m_modelSelector = new QComboBox();
    m_addModelButton = new QPushButton("+");
    m_removeModelButton = new QPushButton("-");
    m_addModelButton->setFixedWidth(28);
    m_removeModelButton->setFixedWidth(28);
    QHBoxLayout* modelSelectorLayout = new QHBoxLayout();
    modelSelectorLayout->addWidget(m_modelSelector, 1);
    modelSelectorLayout->addWidget(m_addModelButton);
    modelSelectorLayout->addWidget(m_removeModelButton);
    modelForm->addRow("Models", modelSelectorLayout);
    
    m_modelFileEdit = new QLineEdit();
    m_modelFileEdit->setPlaceholderText("Path to .step / .obj");
    QPushButton* browseBtn = new QPushButton("...");
    browseBtn->setFixedWidth(30);
    connect(browseBtn, &QPushButton::clicked, this, [this](){
        QString file = QFileDialog::getOpenFileName(this, "Select 3D Model", "", "3D Models (*.obj *.wrl *.step *.stp *.igs *.iges)");
        if (!file.isEmpty()) {
            m_modelFileEdit->setText(file);
            syncCurrentModelFromFields();
            refreshModelSelector();
        }
    });
    
    QHBoxLayout* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(m_modelFileEdit);
    fileLayout->addWidget(browseBtn);
    modelForm->addRow("File", fileLayout);
    
    auto createVec3Input = [&](const QString& label, QLineEdit*& x, QLineEdit*& y, QLineEdit*& z, double defVal) {
        x = new QLineEdit(QString::number(defVal)); y = new QLineEdit(QString::number(defVal)); z = new QLineEdit(QString::number(defVal));
        QHBoxLayout* l = new QHBoxLayout();
        l->setSpacing(2);
        x->setPlaceholderText("X"); y->setPlaceholderText("Y"); z->setPlaceholderText("Z");
        l->addWidget(x); l->addWidget(y); l->addWidget(z);
        modelForm->addRow(label, l);
    };
    
    createVec3Input("Offset", m_modelOffsetX, m_modelOffsetY, m_modelOffsetZ, 0.0);
    createVec3Input("Rotation", m_modelRotX, m_modelRotY, m_modelRotZ, 0.0);
    createVec3Input("Scale", m_modelScaleX, m_modelScaleY, m_modelScaleZ, 1.0);
    m_modelOpacitySpin = new QDoubleSpinBox(this);
    m_modelOpacitySpin->setRange(0.0, 1.0);
    m_modelOpacitySpin->setDecimals(2);
    m_modelOpacitySpin->setSingleStep(0.05);
    m_modelOpacitySpin->setValue(1.0);
    modelForm->addRow("Opacity", m_modelOpacitySpin);
    m_modelVisibleCheck = new QCheckBox("Show this model", this);
    m_modelVisibleCheck->setChecked(true);
    modelForm->addRow("Visible", m_modelVisibleCheck);

    auto bindModelField = [this](QLineEdit* edit) {
        connect(edit, &QLineEdit::editingFinished, this, [this]() {
            syncCurrentModelFromFields();
            refreshModelSelector();
        });
    };
    bindModelField(m_modelFileEdit);
    bindModelField(m_modelOffsetX);
    bindModelField(m_modelOffsetY);
    bindModelField(m_modelOffsetZ);
    bindModelField(m_modelRotX);
    bindModelField(m_modelRotY);
    bindModelField(m_modelRotZ);
    bindModelField(m_modelScaleX);
    bindModelField(m_modelScaleY);
    bindModelField(m_modelScaleZ);
    connect(m_modelOpacitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        syncCurrentModelFromFields();
        refreshModelSelector();
    });
    connect(m_modelVisibleCheck, &QCheckBox::toggled, this, [this](bool) {
        syncCurrentModelFromFields();
        refreshModelSelector();
        if (m_footprint3DWindow && m_footprint3DWindow->isVisible()) onOpen3DPreview();
    });
    connect(m_modelVisibleCheck, &QCheckBox::toggled, this, [this](bool) {
        syncCurrentModelFromFields();
        refreshModelSelector();
    });

    connect(m_modelSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        loadModelToFields(index);
    });
    connect(m_addModelButton, &QPushButton::clicked, this, [this]() {
        syncCurrentModelFromFields();
        Footprint3DModel model;
        model.scale = QVector3D(1.0f, 1.0f, 1.0f);
        m_models3D.append(model);
        refreshModelSelector();
        m_modelSelector->setCurrentIndex(m_models3D.size() - 1);
        loadModelToFields(m_modelSelector->currentIndex());
    });
    connect(m_removeModelButton, &QPushButton::clicked, this, [this]() {
        syncCurrentModelFromFields();
        const int idx = m_modelSelector->currentIndex();
        if (idx < 0 || idx >= m_models3D.size()) return;
        m_models3D.removeAt(idx);
        if (m_models3D.isEmpty()) {
            Footprint3DModel model;
            model.scale = QVector3D(1.0f, 1.0f, 1.0f);
            m_models3D.append(model);
        }
        refreshModelSelector();
        const int nextIdx = std::min(idx, int(m_models3D.size()) - 1);
        m_modelSelector->setCurrentIndex(nextIdx);
        loadModelToFields(nextIdx);
    });

    QPushButton* editTransformBtn = new QPushButton("Edit Transform...");
    connect(editTransformBtn, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("3D Model Transform");
        QVBoxLayout* layout = new QVBoxLayout(&dlg);
        QFormLayout* form = new QFormLayout();

        auto makeVecRow = [&](const QString& label, double dx, double dy, double dz,
                              QDoubleSpinBox*& sx, QDoubleSpinBox*& sy, QDoubleSpinBox*& sz) {
            sx = new QDoubleSpinBox(&dlg);
            sy = new QDoubleSpinBox(&dlg);
            sz = new QDoubleSpinBox(&dlg);
            for (QDoubleSpinBox* s : {sx, sy, sz}) {
                s->setRange(-10000.0, 10000.0);
                s->setDecimals(4);
                s->setSingleStep(0.1);
            }
            sx->setValue(dx);
            sy->setValue(dy);
            sz->setValue(dz);
            QWidget* row = new QWidget(&dlg);
            QHBoxLayout* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(4);
            h->addWidget(sx);
            h->addWidget(sy);
            h->addWidget(sz);
            form->addRow(label, row);
        };

        QDoubleSpinBox* offX = nullptr;
        QDoubleSpinBox* offY = nullptr;
        QDoubleSpinBox* offZ = nullptr;
        QDoubleSpinBox* rotX = nullptr;
        QDoubleSpinBox* rotY = nullptr;
        QDoubleSpinBox* rotZ = nullptr;
        QDoubleSpinBox* sclX = nullptr;
        QDoubleSpinBox* sclY = nullptr;
        QDoubleSpinBox* sclZ = nullptr;

        makeVecRow("Offset (mm)",
                   m_modelOffsetX->text().toDouble(), m_modelOffsetY->text().toDouble(), m_modelOffsetZ->text().toDouble(),
                   offX, offY, offZ);
        makeVecRow("Rotation (deg)",
                   m_modelRotX->text().toDouble(), m_modelRotY->text().toDouble(), m_modelRotZ->text().toDouble(),
                   rotX, rotY, rotZ);
        makeVecRow("Scale",
                   m_modelScaleX->text().toDouble(), m_modelScaleY->text().toDouble(), m_modelScaleZ->text().toDouble(),
                   sclX, sclY, sclZ);

        layout->addLayout(form);

        QPushButton* resetBtn = new QPushButton("Reset", &dlg);
        connect(resetBtn, &QPushButton::clicked, &dlg, [offX, offY, offZ, rotX, rotY, rotZ, sclX, sclY, sclZ]() {
            offX->setValue(0.0); offY->setValue(0.0); offZ->setValue(0.0);
            rotX->setValue(0.0); rotY->setValue(0.0); rotZ->setValue(0.0);
            sclX->setValue(1.0); sclY->setValue(1.0); sclZ->setValue(1.0);
        });
        layout->addWidget(resetBtn);

        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(bb);

        if (dlg.exec() != QDialog::Accepted) return;

        const auto toText = [](double v) { return QString::number(v, 'f', 4); };
        m_modelOffsetX->setText(toText(offX->value()));
        m_modelOffsetY->setText(toText(offY->value()));
        m_modelOffsetZ->setText(toText(offZ->value()));
        m_modelRotX->setText(toText(rotX->value()));
        m_modelRotY->setText(toText(rotY->value()));
        m_modelRotZ->setText(toText(rotZ->value()));
        m_modelScaleX->setText(toText(sclX->value()));
        m_modelScaleY->setText(toText(sclY->value()));
        m_modelScaleZ->setText(toText(sclZ->value()));
        syncCurrentModelFromFields();
        refreshModelSelector();

        if (m_footprint3DWindow && m_footprint3DWindow->isVisible()) onOpen3DPreview();
    });
    modelForm->addRow(editTransformBtn);

    QPushButton* open3DPreviewBtn = new QPushButton("Open 3D Preview");
    open3DPreviewBtn->setStyleSheet(
        "QPushButton { background-color: #0d9488; color: white; font-weight: bold; padding: 7px; border-radius: 4px; border: none; }"
        "QPushButton:hover { background-color: #0f766e; }"
        "QPushButton:pressed { background-color: #115e59; }");
    connect(open3DPreviewBtn, &QPushButton::clicked, this, &FootprintEditor::onOpen3DPreview);
    modelForm->addRow(open3DPreviewBtn);

    m_previewBottomCopperCheck = new QCheckBox("Show Bottom Copper in 3D");
    m_previewBottomCopperCheck->setChecked(false); // KiCad-like top view by default for footprint preview
    connect(m_previewBottomCopperCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_footprint3DWindow) m_footprint3DWindow->setShowBottomCopper(on);
    });
    modelForm->addRow(m_previewBottomCopperCheck);

    if (m_models3D.isEmpty()) {
        Footprint3DModel model;
        model.scale = QVector3D(1.0f, 1.0f, 1.0f);
        m_models3D.append(model);
    }
    refreshModelSelector();
    m_modelSelector->setCurrentIndex(0);
    loadModelToFields(0);
    
    sideLayout->addWidget(modelGroup);
    
    // 2. Properties Editor Group
    QGroupBox* propsGroup = new QGroupBox("Primitive Parameters");
    QVBoxLayout* propsLayout = new QVBoxLayout(propsGroup);
    propsLayout->setContentsMargins(5, 25, 5, 5);
    createPropertiesPanel();
    propsLayout->addWidget(m_propertyEditor);
    sideLayout->addWidget(propsGroup);

    // 3. Browser Group
    createLibraryBrowser();
    QGroupBox* libGroup = new QGroupBox("Internal Library");
    QVBoxLayout* libLayout = new QVBoxLayout(libGroup);
    libLayout->setContentsMargins(10, 25, 10, 10);
    libLayout->setSpacing(10);
    libLayout->addWidget(m_libSearchEdit);
    libLayout->addWidget(m_libraryTree);
    sideLayout->addWidget(libGroup);

    // 4. Wizard Group
    QGroupBox* wizGroup = new QGroupBox("Footprint Wizard");
    QFormLayout* wizForm = new QFormLayout(wizGroup);
    wizForm->setContentsMargins(10, 25, 10, 10);
    
    m_wizType = new QComboBox();
    m_wizType->addItems({"DIP", "SOIC", "Passive (0603/0805)", "Passive (TH Axial)"});
    
    m_wizPins = new QSpinBox(); m_wizPins->setRange(2, 100); m_wizPins->setValue(8);
    m_wizPitch = new QDoubleSpinBox(); m_wizPitch->setRange(0.1, 10); m_wizPitch->setValue(2.54);
    m_wizSpan = new QDoubleSpinBox(); m_wizSpan->setRange(1, 100); m_wizSpan->setValue(7.62);
    m_wizPadW = new QDoubleSpinBox(); m_wizPadW->setRange(0.1, 10); m_wizPadW->setValue(1.5);
    m_wizPadH = new QDoubleSpinBox(); m_wizPadH->setRange(0.1, 10); m_wizPadH->setValue(1.5);
    
    QString wizInputStyle = "QSpinBox, QDoubleSpinBox, QComboBox { "
                            "background-color: #2d2d2d; color: #ececec; "
                            "border: 1px solid #3c3c3c; border-radius: 4px; "
                            "padding: 4px; min-height: 24px; }"
                            "QSpinBox::up-button, QDoubleSpinBox::up-button, "
                            "QSpinBox::down-button, QDoubleSpinBox::down-button { "
                            "background-color: #3d3d3d; width: 16px; border-left: 1px solid #3c3c3c; }"
                            "QComboBox::drop-down { border-left: 1px solid #3c3c3c; width: 20px; }";
    
    m_wizType->setStyleSheet(wizInputStyle);
    m_wizPins->setStyleSheet(wizInputStyle);
    m_wizPitch->setStyleSheet(wizInputStyle);
    m_wizSpan->setStyleSheet(wizInputStyle);
    m_wizPadW->setStyleSheet(wizInputStyle);
    m_wizPadH->setStyleSheet(wizInputStyle);

    wizForm->addRow("Type:", m_wizType);
    wizForm->addRow("Pins:", m_wizPins);
    wizForm->addRow("Pitch:", m_wizPitch);
    wizForm->addRow("Row Span:", m_wizSpan);
    wizForm->addRow("Pad W:", m_wizPadW);
    wizForm->addRow("Pad H:", m_wizPadH);

    QPushButton* wizBtn = new QPushButton("Generate & Save");
    wizBtn->setCursor(Qt::PointingHandCursor);
    wizBtn->setStyleSheet("QPushButton { background-color: #059669; color: white; font-weight: bold; padding: 8px; border-radius: 4px; border: none; }"
                          "QPushButton:hover { background-color: #047857; }"
                          "QPushButton:pressed { background-color: #065f46; }");
    connect(wizBtn, &QPushButton::clicked, this, &FootprintEditor::onWizardGenerate);
    wizForm->addRow(wizBtn);
    
    sideLayout->addWidget(wizGroup);
    
    sideLayout->addStretch();
    
    // Bottom Action Bar
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(10);
    
    QPushButton* closeBtn = new QPushButton("Close");
    QPushButton* saveAsBtn = new QPushButton("Clone...");
    QPushButton* saveBtn = new QPushButton("Save All");
    
    saveBtn->setStyleSheet("background-color: #007acc; color: white; border-color: #006bbd;");
    saveAsBtn->setStyleSheet("background-color: #333;");
    closeBtn->setStyleSheet("background-color: #1a1a1a;");

    connect(saveBtn, &QPushButton::clicked, this, &FootprintEditor::onSave);
    connect(saveAsBtn, &QPushButton::clicked, this, &FootprintEditor::onSaveToLibrary);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    actionLayout->addWidget(closeBtn);
    actionLayout->addWidget(saveAsBtn);
    actionLayout->addWidget(saveBtn);
    sideLayout->addLayout(actionLayout);
    
    sideScroll->setWidget(sideWidget);
    contentLayout->addWidget(sideScroll);

    mainLayout->addLayout(contentLayout);
    
    // Status Bar
    m_statusLabel = new QLabel("Ready | Grid: 1.27mm");
    m_statusLabel->setStyleSheet("background-color: #1e1e1e; border-top: 1px solid #3c3c3c; padding: 4px 15px; color: #cccccc; font-size: 11px;");
    mainLayout->addWidget(m_statusLabel);
    
    // Connect View Signals
    connect(m_view, &FootprintEditorView::pointClicked, [this](QPointF pos){
         if (m_currentTool == ZoomArea) {
             m_view->scale(1/1.2, 1/1.2); 
             return;
         }
         
         if (m_currentTool == Pad) {
             QString num = getNextPadNumber();
             QString shape = m_currentPadShape;
             
             QSizeF size(1.5, 1.5);
             if (shape == "Oblong") size = QSizeF(2.0, 1.2);
             
             FootprintPrimitive prim = FootprintPrimitive::createPad(pos, num, shape, size);
             if (shape == "Trapezoid") {
                 prim.data["trapezoid_delta_x"] = 0.6;
             }
             prim.layer = FootprintPrimitive::Top_Copper;
             m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
         } else if (m_currentTool == Text) {
             bool ok;
             QString text = QInputDialog::getText(this, "Add Text", "Text:", QLineEdit::Normal, "Ref", &ok);
             if (ok && !text.isEmpty()) {
                 FootprintPrimitive prim = FootprintPrimitive::createText(text, pos);
                 prim.layer = m_activeLayer;
                 m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
             }
         } else if (m_currentTool == Anchor) {
             onSetAnchor(pos);
             // Revert to select tool after setting anchor
             if (m_view) {
                 m_currentTool = Select;
                 m_view->setCurrentTool(Select);
                 if (m_toolActions.contains("Select")) {
                     m_toolActions["Select"]->setChecked(true);
                 }
             }
         }
    });

    connect(m_view, &FootprintEditorView::drawingFinished, [this](QPointF start, QPointF end){
        if (m_currentTool == ZoomArea) {
             QRectF rect(start, end);
             m_view->fitInView(rect.normalized(), Qt::KeepAspectRatio);
             m_view->setCurrentTool(Select);
             m_currentTool = Select;
             return;
        }
        
        if (m_currentTool == Measure) {
             onMeasure(start, end);
             // Keep measurement tool active as per user request
             return;
        }

        if (m_currentTool == Select || m_currentTool == Pad || m_currentTool == Text) return;
        
        FootprintPrimitive prim;
        if (m_currentTool == Line) {
            prim = FootprintPrimitive::createLine(start, end);
        } else if (m_currentTool == Rect) {
            prim = FootprintPrimitive::createRect(QRectF(start, end).normalized());
        } else if (m_currentTool == Circle) {
            qreal params = QLineF(start, end).length();
            prim = FootprintPrimitive::createCircle(start, params); // Radius
        }
        
        prim.layer = m_activeLayer;

        // Cleanup preview item before updating scene (which clears scene)
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
        
        m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
    });
    
    connect(m_view, &FootprintEditorView::toolCancelled, [this](){
        m_currentTool = Select;
        m_view->setCurrentTool(Select);
        // Find select action and check it
        if (m_toolActions.contains("Select")) {
             m_toolActions["Select"]->setChecked(true);
        }
    });

    connect(m_view, &FootprintEditorView::lineDragged, [this](QPointF start, QPointF end){
        if (m_currentTool == Select || m_currentTool == Pad || m_currentTool == Text) return;
        
        // Remove old preview
        if (m_previewItem) {
            m_scene->removeItem(m_previewItem);
            delete m_previewItem;
            m_previewItem = nullptr;
        }
        
        QPen previewPen(Qt::yellow, 0, Qt::DashLine);
        
        if (m_currentTool == Line) {
            m_previewItem = m_scene->addLine(QLineF(start, end), previewPen);
        } else if (m_currentTool == Rect) {
            m_previewItem = m_scene->addRect(QRectF(start, end).normalized(), previewPen);
        } else if (m_currentTool == Circle) {
            qreal r = QLineF(start, end).length();
            m_previewItem = m_scene->addEllipse(start.x()-r, start.y()-r, r*2, r*2, previewPen);
        }
    });
    
    connect(m_view, &FootprintEditorView::mouseMoved, [this](QPointF pos){
        if (m_statusLabel) {
            m_statusLabel->setText(QString("X: %1 mm  Y: %2 mm | Grid: %3 mm")
                                   .arg(pos.x(), 0, 'f', 2)
                                   .arg(pos.y(), 0, 'f', 2)
                                   .arg(m_view->gridSize(), 0, 'f', 2));
        }
    });

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &FootprintEditor::onSelectionChanged);
}

void FootprintEditor::createInfoPanel() {
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("Footprint Name");
    
    m_descriptionEdit = new QLineEdit();
    m_descriptionEdit->setPlaceholderText("Description");
    
    m_categoryCombo = new QComboBox();
    m_categoryCombo->addItems({"Through-Hole", "SMD", "Connectors", "Discrete", "IC"});
    m_categoryCombo->setEditable(true);

    m_classificationCombo = new QComboBox();
    m_classificationCombo->addItems({"Unspecified", "SMD", "Through-Hole", "Virtual"});

    m_keywordsEdit = new QLineEdit();
    m_keywordsEdit->setPlaceholderText("Keywords, comma-separated");

    m_excludeBOMCheck = new QCheckBox("Exclude from BOM");
    m_excludePosCheck = new QCheckBox("Exclude from Position Files");
    m_dnpCheck = new QCheckBox("DNP (Do Not Populate)");
    m_netTieCheck = new QCheckBox("Net Tie Footprint");
}

void FootprintEditor::createToolBar() {
    // Top ToolBar (Settings, Utilities)
    m_toolbar = new QToolBar("Tools", this);
    m_toolbar->setIconSize(QSize(20, 20));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toolbar->setMovable(false);
    
    // Left ToolBar (Drawing Tools)
    m_leftToolbar = new QToolBar("Drawing", this);
    m_leftToolbar->setObjectName("LeftToolBar");
    m_leftToolbar->setOrientation(Qt::Vertical);
    m_leftToolbar->setIconSize(QSize(22, 22));
    m_leftToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_leftToolbar->setMovable(false);

    QActionGroup* group = new QActionGroup(this);
    group->setExclusive(true);
    
    // Helper to add tool to appropriate toolbar
    auto addTool = [&](QToolBar* bar, const QString& name, Tool tool, const QString& iconFile, const QString& shortcut = "") {
        QAction* action = new QAction(QIcon(":/icons/" + iconFile), name, this);
        action->setData(static_cast<int>(tool));
        action->setCheckable(true);
        if (!shortcut.isEmpty()) {
             action->setShortcut(QKeySequence(shortcut));
             action->setToolTip(name + " (" + shortcut + ")");
        }
        bar->addAction(action);
        group->addAction(action);
        if (tool == Select) action->setChecked(true);
        connect(action, &QAction::triggered, this, &FootprintEditor::onToolSelected);
        m_toolActions[name] = action;
        return action;
    };
    
    // Top Toolbar Items
    addTool(m_toolbar, "Select", Select, "tool_select.svg", "Esc");
    m_toolbar->addSeparator();
    
    m_undoAction = m_toolbar->addAction(QIcon(":/icons/undo.svg"), "Undo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, &FootprintEditor::onUndo);
    
    m_redoAction = m_toolbar->addAction(QIcon(":/icons/redo.svg"), "Redo");
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, &FootprintEditor::onRedo);

    m_toolbar->addSeparator();

    QAction* deleteAction = m_toolbar->addAction(QIcon(":/icons/tool_delete.svg"), "Delete");
    deleteAction->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    connect(deleteAction, &QAction::triggered, this, &FootprintEditor::onDelete);
    this->addAction(deleteAction);
    
    m_toolbar->addSeparator();

    // Alignment Tools
    auto addAlignAct = [&](const QString& text, const QString& tooltip, void (FootprintEditor::*member)()) {
        QAction* act = m_toolbar->addAction(text);
        act->setToolTip(tooltip);
        connect(act, &QAction::triggered, this, member);
    };

    addAlignAct("←", "Align Left",   &FootprintEditor::onAlignLeft);
    addAlignAct("→", "Align Right",  &FootprintEditor::onAlignRight);
    addAlignAct("↑", "Align Top",    &FootprintEditor::onAlignTop);
    addAlignAct("↓", "Align Bottom", &FootprintEditor::onAlignBottom);
    addAlignAct("⬌", "Center H",     &FootprintEditor::onAlignCenterH);
    addAlignAct("⬍", "Center V",     &FootprintEditor::onAlignCenterV);
    addAlignAct("⇥", "Distribute H", &FootprintEditor::onDistributeH);
    addAlignAct("⤒", "Distribute V", &FootprintEditor::onDistributeV);
    addAlignAct("↔", "Match Spacing",&FootprintEditor::onMatchSpacing);
    addAlignAct("move", "Move Exactly", &FootprintEditor::onMoveExactly);

    m_toolbar->addSeparator();

    // Pad Shape Selector Actions (Top)
    QActionGroup* shapeGroup = new QActionGroup(this);
    shapeGroup->setExclusive(true);
    
    auto addShape = [&](const QString& name, const QString& iconFile) {
        QAction* action = m_toolbar->addAction(QIcon(iconFile), name);
        action->setCheckable(true);
        action->setToolTip("Pad Shape: " + name);
        shapeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, name](){ 
            setPadShape(name); 
            // Activate Pad Tool automatically for better UX
            m_currentTool = Pad;
            m_view->setCurrentTool(Pad);
            if (m_toolActions.contains("Pad")) {
                 m_toolActions["Pad"]->setChecked(true);
            }
        });
        if (m_currentPadShape == name) action->setChecked(true);
    };
    
    addShape("Rect", ":/icons/pad_rect.svg");
    addShape("Round", ":/icons/pad_circle.svg");
    addShape("Oblong", ":/icons/pad_oblong.svg");
    addShape("Trapezoid", ":/icons/pad_rect.svg");
    
    m_toolbar->addSeparator();

    // Zoom Tools
    addTool(m_toolbar, "Zoom Area", ZoomArea, "tool_zoom_area.svg", "Z");
    
    QAction* zoomIn = m_toolbar->addAction(QIcon(":/icons/view_zoom_in.svg"), "Zoom In");
    connect(zoomIn, &QAction::triggered, this, &FootprintEditor::onZoomIn);
    
    QAction* zoomOut = m_toolbar->addAction(QIcon(":/icons/view_zoom_out.svg"), "Zoom Out");
    connect(zoomOut, &QAction::triggered, this, &FootprintEditor::onZoomOut);
    
    QAction* zoomFit = m_toolbar->addAction(QIcon(":/icons/view_fit.svg"), "Zoom Fit");
    connect(zoomFit, &QAction::triggered, this, &FootprintEditor::onZoomFit);
    
    QAction* wizardAction = m_toolbar->addAction("🔧 Wizard");
    wizardAction->setToolTip("Footprint Wizard — auto-generate standard packages");
    wizardAction->setShortcut(QKeySequence("Ctrl+W"));
    connect(wizardAction, &QAction::triggered, this, &FootprintEditor::onOpenWizard);
    connect(zoomFit, &QAction::triggered, this, &FootprintEditor::onZoomFit);
    
    m_toolbar->addSeparator();

    // Grid Selector 
    QLabel* gridLabel = new QLabel(" Grid:");
    gridLabel->setStyleSheet("color: #ccc; margin-left: 5px;");
    m_toolbar->addWidget(gridLabel);
    
    QComboBox* gridCombo = new QComboBox();
    gridCombo->addItem("0.1 mm", 0.1);
    gridCombo->addItem("0.25 mm", 0.25);
    gridCombo->addItem("0.5 mm", 0.5);
    gridCombo->addItem("1.0 mm", 1.0);
    gridCombo->addItem("1.27 mm (50mil)", 1.27);
    gridCombo->addItem("2.54 mm (100mil)", 2.54);
    gridCombo->setCurrentText("1.27 mm (50mil)");
    m_toolbar->addWidget(gridCombo);
    
    connect(gridCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this, gridCombo](int index){
        onGridSizeChanged(gridCombo->itemData(index).toString());
    });

    m_toolbar->addSeparator();

    // Layer Selector
    QLabel* layerLabel = new QLabel(" Layer:");
    layerLabel->setStyleSheet("color: #ccc; margin-left: 5px;");
    m_toolbar->addWidget(layerLabel);

    m_layerCombo = new QComboBox();
    m_layerCombo->addItem("Top Silkscreen", FootprintPrimitive::Top_Silkscreen);
    m_layerCombo->addItem("Top Courtyard", FootprintPrimitive::Top_Courtyard);
    m_layerCombo->addItem("Top Fabrication", FootprintPrimitive::Top_Fabrication);
    m_layerCombo->addItem("Top Copper", FootprintPrimitive::Top_Copper);
    m_layerCombo->addItem("Bottom Copper", FootprintPrimitive::Bottom_Copper);
    m_layerCombo->addItem("Bottom Silkscreen", FootprintPrimitive::Bottom_Silkscreen);
    m_layerCombo->addItem("Top Solder Mask", FootprintPrimitive::Top_SolderMask);
    m_layerCombo->addItem("Bottom Solder Mask", FootprintPrimitive::Bottom_SolderMask);
    m_layerCombo->addItem("Top Solder Paste", FootprintPrimitive::Top_SolderPaste);
    m_layerCombo->addItem("Bottom Solder Paste", FootprintPrimitive::Bottom_SolderPaste);
    m_layerCombo->addItem("Top Adhesive", FootprintPrimitive::Top_Adhesive);
    m_layerCombo->addItem("Bottom Adhesive", FootprintPrimitive::Bottom_Adhesive);
    m_layerCombo->addItem("Bottom Courtyard", FootprintPrimitive::Bottom_Courtyard);
    m_layerCombo->addItem("Bottom Fabrication", FootprintPrimitive::Bottom_Fabrication);
    m_layerCombo->addItem("Inner Copper 1", FootprintPrimitive::Inner_Copper_1);
    m_layerCombo->addItem("Inner Copper 2", FootprintPrimitive::Inner_Copper_2);
    m_layerCombo->addItem("Inner Copper 3", FootprintPrimitive::Inner_Copper_3);
    m_layerCombo->addItem("Inner Copper 4", FootprintPrimitive::Inner_Copper_4);
    m_layerCombo->setCurrentIndex(0);
    m_toolbar->addWidget(m_layerCombo);

    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){
        m_activeLayer = static_cast<FootprintPrimitive::Layer>(m_layerCombo->itemData(index).toInt());
    });

    m_toolbar->addSeparator();

    QAction* snapAction = m_toolbar->addAction(QIcon(":/icons/snap_grid.svg"), "Snap");
    snapAction->setCheckable(true);
    snapAction->setChecked(true);
    snapAction->setToolTip("Toggle Grid Snapping (S)");
    snapAction->setShortcut(QKeySequence("S"));
    connect(snapAction, &QAction::toggled, this, [this](bool checked){
        if (m_view) m_view->setSnapToGrid(checked);
    });
    
    m_toolbar->addSeparator();
    
    // Alignment / Orientation
    QAction* rotate = m_toolbar->addAction(QIcon(":/icons/tool_rotate.svg"), "Rotate");
    rotate->setShortcut(QKeySequence("Ctrl+R"));
    this->addAction(rotate);
    connect(rotate, &QAction::triggered, this, [this](){
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        for(auto item : selected) {
            item->setRotation(item->rotation() + 90);
            // Updating internal primitive?
            int index = m_drawnItems.indexOf(item);
            if (index != -1) {
                // Ideally update primitive data, but rotation is graphical.
                // For Pad/Text, we might want to store rotation
                FootprintPrimitive& prim = m_footprint.primitives()[index];
                prim.data["rotation"] = item->rotation();
                // Flip handling?
            }
        }
    });

    QAction* flipH = m_toolbar->addAction(QIcon(":/icons/flip_h.svg"), "Flip H");
    connect(flipH, &QAction::triggered, this, &FootprintEditor::onFlipHorizontal);
    
    QAction* flipV = m_toolbar->addAction(QIcon(":/icons/flip_v.svg"), "Flip V");
    connect(flipV, &QAction::triggered, this, &FootprintEditor::onFlipVertical);

    QAction* pairAct = m_toolbar->addAction(QIcon(":/icons/tool_array.svg"), "Mirror Pair");
    pairAct->setToolTip("Create mirrored copies (with optional top/bottom layer swap)");
    connect(pairAct, &QAction::triggered, this, &FootprintEditor::onCreateMirroredPair);

    m_toolbar->addSeparator();
    
    QAction* arrayAct = m_toolbar->addAction(QIcon(":/icons/tool_duplicate.svg"), "Array Tool");
    arrayAct->setToolTip("Create Linear or Circular Array of items");
    connect(arrayAct, &QAction::triggered, this, &FootprintEditor::onArrayTool);

    QAction* polarAct = m_toolbar->addAction(QIcon(":/icons/tool_pad.svg"), "Polar Grid");
    polarAct->setToolTip("Generate pads arranged on a circular/radial grid");
    connect(polarAct, &QAction::triggered, this, &FootprintEditor::onPolarGridTool);

    QAction* drcAct = m_toolbar->addAction(QIcon(":/icons/check.svg"), "Check Footprint");
    drcAct->setToolTip("Run Footprint Rule Check (DRC)");
    connect(drcAct, &QAction::triggered, this, &FootprintEditor::onRunDRC);

    QAction* importKiCadAct = m_toolbar->addAction(QIcon(":/icons/folder_open.svg"), "Import KiCad");
    importKiCadAct->setToolTip("Import KiCad footprint (.kicad_mod)");
    connect(importKiCadAct, &QAction::triggered, this, &FootprintEditor::onImportKicadFootprint);

    QAction* view3DAct = m_toolbar->addAction(QIcon(":/icons/tool_3d.svg"), "3D View");
    view3DAct->setToolTip("Open 3D preview for current footprint");
    connect(view3DAct, &QAction::triggered, this, &FootprintEditor::onOpen3DPreview);

    // Left Toolbar Items (Drawing Tools)
    addTool(m_leftToolbar, "Pad", Pad, "tool_pad.svg", "P");
    m_leftToolbar->addSeparator();
    addTool(m_leftToolbar, "Line", Line, "tool_line.svg", "L");
    addTool(m_leftToolbar, "Rect", Rect, "tool_rect.svg", "R");
    addTool(m_leftToolbar, "Circle", Circle, "tool_circle.svg", "C");
    // Text tool
    addTool(m_leftToolbar, "Text", Text, "tool_text.svg", "T");
    // Measure tool
    addTool(m_leftToolbar, "Measure", Measure, "tool_measure.svg", "M");
    // Anchor tool
    addTool(m_leftToolbar, "Set Anchor", Anchor, "tool_anchor.svg", "H");
    
    QAction* addExactAct = m_leftToolbar->addAction(QIcon(":/icons/tool_line.svg"), "Exact Dimensions...");
    addExactAct->setToolTip("Add Primitive (Exact Dimensions)...");
    connect(addExactAct, &QAction::triggered, this, &FootprintEditor::onAddPrimitiveExact);
    
    // Spacer for left toolbar
    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftToolbar->addWidget(spacer);
}

// ... existing code ...

void FootprintEditor::onAddPrimitiveExact() {
    QDialog dialog(this);
    dialog.setWindowTitle("Add Primitive (Exact Dimensions)");
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QFormLayout* form = new QFormLayout();
    QComboBox* typeCombo = new QComboBox();
    typeCombo->addItems({"Pad", "Line", "Rect", "Circle", "Text"});
    form->addRow("Type:", typeCombo);

    QDoubleSpinBox* x1 = new QDoubleSpinBox(); x1->setRange(-1000, 1000); x1->setSuffix(" mm");
    QDoubleSpinBox* y1 = new QDoubleSpinBox(); y1->setRange(-1000, 1000); y1->setSuffix(" mm");
    QDoubleSpinBox* x2 = new QDoubleSpinBox(); x2->setRange(-1000, 1000); x2->setSuffix(" mm");
    QDoubleSpinBox* y2 = new QDoubleSpinBox(); y2->setRange(-1000, 1000); y2->setSuffix(" mm");
    QLineEdit* textEdit = new QLineEdit("1");
    QComboBox* shapeCombo = new QComboBox(); shapeCombo->addItems({"Rect", "Round", "Oblong", "Trapezoid"});

    form->addRow("X / CX / X1:", x1);
    form->addRow("Y / CY / Y1:", y1);
    form->addRow("W / Radius / X2:", x2);
    form->addRow("H / Y2:", y2);
    form->addRow("Text / Pad Num:", textEdit);
    form->addRow("Pad Shape:", shapeCombo);

    auto updateFields = [&]() {
        QString type = typeCombo->currentText();
        x2->setEnabled(type != "Text");
        y2->setEnabled(type == "Pad" || type == "Line" || type == "Rect");
        textEdit->setEnabled(type == "Text" || type == "Pad");
        shapeCombo->setEnabled(type == "Pad");
        if (type == "Pad") {
            x1->setValue(0); y1->setValue(0); x2->setValue(1.5); y2->setValue(1.5);
        }
    };
    connect(typeCombo, &QComboBox::currentTextChanged, updateFields);
    updateFields();

    layout->addLayout(form);
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        FootprintPrimitive prim;
        QString type = typeCombo->currentText();
        if (type == "Pad") {
            prim = FootprintPrimitive::createPad(QPointF(x1->value(), y1->value()), textEdit->text(), shapeCombo->currentText(), QSizeF(x2->value(), y2->value()));
            prim.layer = FootprintPrimitive::Top_Copper;
        } else if (type == "Line") {
            prim = FootprintPrimitive::createLine(QPointF(x1->value(), y1->value()), QPointF(x2->value(), y2->value()));
            prim.layer = m_activeLayer;
        } else if (type == "Rect") {
            prim = FootprintPrimitive::createRect(QRectF(x1->value(), y1->value(), x2->value(), y2->value()));
            prim.layer = m_activeLayer;
        } else if (type == "Circle") {
            prim = FootprintPrimitive::createCircle(QPointF(x1->value(), y1->value()), x2->value());
            prim.layer = m_activeLayer;
        } else if (type == "Text") {
            prim = FootprintPrimitive::createText(textEdit->text(), QPointF(x1->value(), y1->value()));
            prim.layer = m_activeLayer;
        }
        
        m_undoStack->push(new AddFootprintPrimitiveCommand(this, prim));
    }
}

void FootprintEditor::onFlipHorizontal() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    QRectF bounds = m_scene->selectionArea().boundingRect(); // or manually calculate union
    if (bounds.isNull()) {
        bounds = selected.first()->sceneBoundingRect();
        for(auto item : selected) bounds = bounds.united(item->sceneBoundingRect());
    }
    qreal centerX = bounds.center().x();
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = m_footprint.primitives()[idx];
        
        // Mirror X around centerX: newX = centerX - (oldX - centerX) = 2*centerX - oldX
        // primitives store center or x/y. Pad/Text usage x/y is center. 
        // Rect/Line might differ. 
        // Pad: x,y is center.
        // Text: x,y is anchor (usually bottom-left or center? Standard is usually anchor. Assuming center for now based on draw code).
        // Rect: x,y,w,h. x,y is top-left.
        // Circle: cx, cy.
        // Line: x1,y1, x2,y2.

        if (prim.type == FootprintPrimitive::Pad) {
             prim.data["x"] = 2 * centerX - prim.data["x"].toDouble();
             // Rotation should also flip? Horizontal flip of rotation angle a -> 180 - a?
             // Simple mirroring of position for now.
        } else if (prim.type == FootprintPrimitive::Text) {
             // Text alignment might need adjustment if not centered.
             // Assuming primitive x/y is position.
             // If drawing uses center, then flip calculation is easy.
             // If top-left, need to account for width.
             // Drawing code: t->setPos(x - br.width()/2 ...). So stored x,y is CENTER.
             prim.data["x"] = 2 * centerX - prim.data["x"].toDouble();
        } else if (prim.type == FootprintPrimitive::Circle) {
             prim.data["cx"] = 2 * centerX - prim.data["cx"].toDouble();
        } else if (prim.type == FootprintPrimitive::Rect) {
             // x is top-left.
             // newRight = 2*centerX - oldLeft.
             // newLeft = newRight - width.
             qreal oldX = prim.data["x"].toDouble();
             qreal w = prim.data["width"].toDouble();
             qreal newRight = 2 * centerX - oldX;
             prim.data["x"] = newRight - w;
        } else if (prim.type == FootprintPrimitive::Line) {
             prim.data["x1"] = 2 * centerX - prim.data["x1"].toDouble();
             prim.data["x2"] = 2 * centerX - prim.data["x2"].toDouble();
        } else if (prim.type == FootprintPrimitive::Dimension) {
             prim.data["x1"] = 2 * centerX - prim.data["x1"].toDouble();
             prim.data["x2"] = 2 * centerX - prim.data["x2"].toDouble();
        }
    }
    updateSceneFromDefinition();
}

void FootprintEditor::onFlipVertical() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    QRectF bounds = m_scene->selectionArea().boundingRect();
    if (bounds.isNull()) {
        bounds = selected.first()->sceneBoundingRect();
        for(auto item : selected) bounds = bounds.united(item->sceneBoundingRect());
    }
    qreal centerY = bounds.center().y();
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = m_footprint.primitives()[idx];
        
        if (prim.type == FootprintPrimitive::Pad || prim.type == FootprintPrimitive::Text) {
             prim.data["y"] = 2 * centerY - prim.data["y"].toDouble();
        } else if (prim.type == FootprintPrimitive::Circle) {
             prim.data["cy"] = 2 * centerY - prim.data["cy"].toDouble();
        } else if (prim.type == FootprintPrimitive::Rect) {
             qreal oldY = prim.data["y"].toDouble();
             qreal h = prim.data["height"].toDouble();
             qreal newBottom = 2 * centerY - oldY;
             prim.data["y"] = newBottom - h;
        } else if (prim.type == FootprintPrimitive::Line) {
             prim.data["y1"] = 2 * centerY - prim.data["y1"].toDouble();
             prim.data["y2"] = 2 * centerY - prim.data["y2"].toDouble();
        } else if (prim.type == FootprintPrimitive::Dimension) {
             prim.data["y1"] = 2 * centerY - prim.data["y1"].toDouble();
             prim.data["y2"] = 2 * centerY - prim.data["y2"].toDouble();
        }
    }
    updateSceneFromDefinition();
}

void FootprintEditor::onCreateMirroredPair() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    QDialog dlg(this);
    dlg.setWindowTitle("Create Mirrored Pair");
    QFormLayout form(&dlg);

    QComboBox* axisCombo = new QComboBox(&dlg);
    axisCombo->addItems({"Mirror Left/Right", "Mirror Top/Bottom"});
    axisCombo->setCurrentIndex(0);
    form.addRow("Axis", axisCombo);

    QCheckBox* swapLayersCheck = new QCheckBox("Swap top/bottom layers on copy", &dlg);
    swapLayersCheck->setChecked(true);
    form.addRow("", swapLayersCheck);

    QPushButton* applyBtn = new QPushButton("Create Pair", &dlg);
    form.addRow(applyBtn);
    connect(applyBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() != QDialog::Accepted) return;

    QRectF bounds = selected.first()->sceneBoundingRect();
    for (auto* item : selected) bounds = bounds.united(item->sceneBoundingRect());
    const qreal centerX = bounds.center().x();
    const qreal centerY = bounds.center().y();
    const bool mirrorX = (axisCombo->currentIndex() == 0);
    const bool swapLayers = swapLayersCheck->isChecked();

    auto mirrorPrimitive = [&](const FootprintPrimitive& src) -> FootprintPrimitive {
        FootprintPrimitive dst = src;
        if (swapLayers) dst.layer = mirroredTopBottomLayer(dst.layer);

        if (dst.type == FootprintPrimitive::Line || dst.type == FootprintPrimitive::Dimension) {
            if (mirrorX) {
                dst.data["x1"] = 2 * centerX - dst.data["x1"].toDouble();
                dst.data["x2"] = 2 * centerX - dst.data["x2"].toDouble();
            } else {
                dst.data["y1"] = 2 * centerY - dst.data["y1"].toDouble();
                dst.data["y2"] = 2 * centerY - dst.data["y2"].toDouble();
            }
            return dst;
        }
        if (dst.type == FootprintPrimitive::Rect) {
            const qreal x = dst.data["x"].toDouble();
            const qreal y = dst.data["y"].toDouble();
            const qreal w = dst.data["width"].toDouble();
            const qreal h = dst.data["height"].toDouble();
            if (mirrorX) {
                const qreal newRight = 2 * centerX - x;
                dst.data["x"] = newRight - w;
            } else {
                const qreal newBottom = 2 * centerY - y;
                dst.data["y"] = newBottom - h;
            }
            return dst;
        }
        if (dst.type == FootprintPrimitive::Circle || dst.type == FootprintPrimitive::Arc) {
            if (mirrorX) dst.data["cx"] = 2 * centerX - dst.data["cx"].toDouble();
            else dst.data["cy"] = 2 * centerY - dst.data["cy"].toDouble();
            return dst;
        }

        // Pad/Text/other center-based primitives
        if (mirrorX) dst.data["x"] = 2 * centerX - dst.data["x"].toDouble();
        else dst.data["y"] = 2 * centerY - dst.data["y"].toDouble();
        return dst;
    };

    for (auto* item : selected) {
        const int idx = m_drawnItems.indexOf(item);
        if (idx < 0 || idx >= m_footprint.primitives().size()) continue;
        FootprintPrimitive mirrored = mirrorPrimitive(m_footprint.primitives()[idx]);

        if (mirrored.type == FootprintPrimitive::Pad) {
            // Keep pad numbering unique for automatic pair generation.
            mirrored.data["number"] = getNextPadNumber();
        }
        m_footprint.addPrimitive(mirrored);
    }
    updateSceneFromDefinition();
}

void FootprintEditor::createPropertiesPanel() {
    m_propertyEditor = new PropertyEditor();
    
    connect(m_propertyEditor, &PropertyEditor::propertyChanged, this, [this](const QString& name, const QVariant& value){
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        if (selected.size() != 1) return;
        
        QGraphicsItem* item = selected.first();
        int index = m_drawnItems.indexOf(item);
        if (index == -1 || index >= m_footprint.primitives().size()) return;
        
        FootprintPrimitive& prim = m_footprint.primitives()[index];
        
        bool changed = false;
        
        if (name == "Layer") {
            QString val = value.toString();
            if (val == "Top Silkscreen") prim.layer = FootprintPrimitive::Top_Silkscreen;
            else if (val == "Top Courtyard") prim.layer = FootprintPrimitive::Top_Courtyard;
            else if (val == "Top Fabrication") prim.layer = FootprintPrimitive::Top_Fabrication;
            else if (val == "Top Copper") prim.layer = FootprintPrimitive::Top_Copper;
            else if (val == "Bottom Copper") prim.layer = FootprintPrimitive::Bottom_Copper;
            else if (val == "Bottom Silkscreen") prim.layer = FootprintPrimitive::Bottom_Silkscreen;
            else if (val == "Top Solder Mask") prim.layer = FootprintPrimitive::Top_SolderMask;
            else if (val == "Bottom Solder Mask") prim.layer = FootprintPrimitive::Bottom_SolderMask;
            else if (val == "Top Solder Paste") prim.layer = FootprintPrimitive::Top_SolderPaste;
            else if (val == "Bottom Solder Paste") prim.layer = FootprintPrimitive::Bottom_SolderPaste;
            else if (val == "Top Adhesive") prim.layer = FootprintPrimitive::Top_Adhesive;
            else if (val == "Bottom Adhesive") prim.layer = FootprintPrimitive::Bottom_Adhesive;
            else if (val == "Bottom Courtyard") prim.layer = FootprintPrimitive::Bottom_Courtyard;
            else if (val == "Bottom Fabrication") prim.layer = FootprintPrimitive::Bottom_Fabrication;
            else if (val == "Inner Copper 1") prim.layer = FootprintPrimitive::Inner_Copper_1;
            else if (val == "Inner Copper 2") prim.layer = FootprintPrimitive::Inner_Copper_2;
            else if (val == "Inner Copper 3") prim.layer = FootprintPrimitive::Inner_Copper_3;
            else if (val == "Inner Copper 4") prim.layer = FootprintPrimitive::Inner_Copper_4;
            changed = true;
        }

        if (prim.type == FootprintPrimitive::Pad) {
             if (name == "Number") { prim.data["number"] = value.toString(); changed = true; }
             else if (name == "Pad Type") {
                 const QString padType = value.toString();
                 prim.data["pad_type"] = padType;
                 if (padType == "SMD") {
                     prim.data["drill_size"] = 0.0;
                     prim.data["plated"] = true;
                 } else if (padType == "Through-Hole") {
                     if (prim.data["drill_size"].toDouble() <= 0.0) {
                         prim.data["drill_size"] = 0.8;
                     }
                     prim.data["plated"] = true;
                 } else if (padType == "Connector") {
                     // Edge/connector pads are surface style with no drill.
                     prim.data["drill_size"] = 0.0;
                     prim.data["plated"] = true;
                 }
                 changed = true;
             }
             else if (name == "Shape") { prim.data["shape"] = value.toString(); changed = true; }
             else if (name == "Width") { prim.data["width"] = value.toDouble(); changed = true; }
             else if (name == "Height") { prim.data["height"] = value.toDouble(); changed = true; }
             else if (name == "X") { prim.data["x"] = value.toDouble(); changed = true; }
             else if (name == "Y") { prim.data["y"] = value.toDouble(); changed = true; }
             else if (name == "Rotation") { prim.data["rotation"] = value.toDouble(); changed = true; }
             else if (name == "Corner Radius") { prim.data["corner_radius"] = value.toDouble(); changed = true; }
             else if (name == "Trapezoid Delta X") { prim.data["trapezoid_delta_x"] = value.toDouble(); changed = true; }
             // Enhanced Properties
             else if (name == "Drill Size") { prim.data["drill_size"] = value.toDouble(); changed = true; }
             else if (name == "Clearance Override") { prim.data["net_clearance_override_enabled"] = (value.toString() == "True"); changed = true; }
             else if (name == "Net Clearance") { prim.data["net_clearance"] = value.toDouble(); changed = true; }
             else if (name == "Thermal Relief") { prim.data["thermal_relief_enabled"] = (value.toString() == "True"); changed = true; }
             else if (name == "Thermal Spoke Width") { prim.data["thermal_spoke_width"] = value.toDouble(); changed = true; }
             else if (name == "Thermal Relief Gap") { prim.data["thermal_relief_gap"] = value.toDouble(); changed = true; }
             else if (name == "Thermal Spoke Count") { prim.data["thermal_spoke_count"] = value.toInt(); changed = true; }
             else if (name == "Thermal Spoke Angle") { prim.data["thermal_spoke_angle_deg"] = value.toDouble(); changed = true; }
             else if (name == "Jumper Group") { prim.data["jumper_group"] = qMax(0, value.toInt()); changed = true; }
             else if (name == "Net Tie Group") { prim.data["net_tie_group"] = qMax(0, value.toInt()); changed = true; }
             else if (name == "Solder Mask Exp") { prim.data["solder_mask_expansion"] = value.toDouble(); changed = true; }
             else if (name == "Paste Mask Exp") { prim.data["paste_mask_expansion"] = value.toDouble(); changed = true; }
             else if (name == "Plated") { prim.data["plated"] = (value.toString() == "True"); changed = true; }
        } else if (prim.type == FootprintPrimitive::Text) {
             if (name == "Text") { prim.data["text"] = value.toString(); changed = true; }
             else if (name == "Height") { prim.data["height"] = value.toDouble(); changed = true; }
             else if (name == "X") { prim.data["x"] = value.toDouble(); changed = true; }
             else if (name == "Y") { prim.data["y"] = value.toDouble(); changed = true; }
             else if (name == "Rotation") { prim.data["rotation"] = value.toDouble(); changed = true; }
        } else if (prim.type == FootprintPrimitive::Line || prim.type == FootprintPrimitive::Dimension) {
             if (name == "X1") { prim.data["x1"] = value.toDouble(); changed = true; }
             else if (name == "Y1") { prim.data["y1"] = value.toDouble(); changed = true; }
             else if (name == "X2") { prim.data["x2"] = value.toDouble(); changed = true; }
             else if (name == "Y2") { prim.data["y2"] = value.toDouble(); changed = true; }
        }
        
        if (changed) {
            // Full redraw is simplest way to ensure visualization matches new properties 
            // (e.g. pad shape change, text resize, etc)
            // Save selection to restore it? hard with recreation.
            updateSceneFromDefinition();
        }
    });
}

void FootprintEditor::onToolSelected() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (action) {
        m_currentTool = static_cast<Tool>(action->data().toInt());
        m_view->setCurrentTool(m_currentTool);
    }
}

// Zoom / Align slots
void FootprintEditor::onZoomIn() { m_view->scale(1.2, 1.2); }
void FootprintEditor::onZoomOut() { m_view->scale(1/1.2, 1/1.2); }
void FootprintEditor::onZoomFit() { 
    if (m_scene->items().isEmpty()) m_view->fitInView(QRectF(-5, -5, 10, 10), Qt::KeepAspectRatio);
    else m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio); 
}

void FootprintEditor::onOpenWizard() {
    FootprintWizardDialog* wizard = new FootprintWizardDialog(this, this);
    wizard->exec();
}

void FootprintEditor::onGridSizeChanged(const QString& size) {
    // Extract number from string like "1.27 mm..."
    QString num = size.split(' ').first();
    m_view->setGridSize(num.toDouble());
}

void FootprintEditor::onMeasure(QPointF p1, QPointF p2) {
    if (QLineF(p1, p2).length() < 1e-6) return;

    FootprintPrimitive dim;
    dim.type = FootprintPrimitive::Dimension;
    dim.layer = FootprintPrimitive::Top_Fabrication;
    dim.data["x1"] = p1.x();
    dim.data["y1"] = p1.y();
    dim.data["x2"] = p2.x();
    dim.data["y2"] = p2.y();
    m_footprint.addPrimitive(dim);
    updateSceneFromDefinition();

    if (m_statusLabel) {
        const qreal dx = p2.x() - p1.x();
        const qreal dy = p2.y() - p1.y();
        const qreal d = QLineF(p1, p2).length();
        m_statusLabel->setText(QString("Dimension added | L: %1 mm  ΔX: %2  ΔY: %3")
                               .arg(d, 0, 'f', 2)
                               .arg(dx, 0, 'f', 2)
                               .arg(dy, 0, 'f', 2));
    }
}

void FootprintEditor::onWizardGenerate() {
    m_footprint.clearPrimitives();
    
    QString type = m_wizType->currentText();
    int pins = m_wizPins->value();
    double pitch = m_wizPitch->value();
    double span = m_wizSpan->value();
    QSizeF padSize(m_wizPadW->value(), m_wizPadH->value());

    if (type == "DIP" || type == "SOIC") {
        int half = pins / 2;
        for (int i = 0; i < half; ++i) {
            double y = (i - (half-1)/2.0) * pitch;
            // Pin 1 uses Rect, rests use Round for TH, or Rect for SMD
            QString shape1 = (i == 0) ? "Rect" : (type == "DIP" ? "Round" : "Rect");
            QString shape2 = (type == "DIP") ? "Round" : "Rect";
            
            auto p1 = FootprintPrimitive::createPad(QPointF(-span/2, y), QString::number(i+1), shape1, padSize);
            auto p2 = FootprintPrimitive::createPad(QPointF(span/2, -y), QString::number(half+i+1), shape2, padSize);
            
            p1.layer = FootprintPrimitive::Top_Copper;
            p2.layer = FootprintPrimitive::Top_Copper;

            if (type == "DIP") {
                p1.data["drill_size"] = 0.8;
                p2.data["drill_size"] = 0.8;
            }
            
            // Right row
            m_footprint.addPrimitive(p2);
        }

        // Add Fabrication outline (Gray)
        double yMax = ((pins/2 - 1) / 2.0) * pitch;
        FootprintPrimitive fabRect = FootprintPrimitive::createRect(QRectF(-span/2 + 0.5, -yMax - 0.5, span - 1.0, yMax*2 + 1.0).normalized());
        fabRect.layer = FootprintPrimitive::Top_Fabrication;
        m_footprint.addPrimitive(fabRect);

        // Add Courtyard boundary (Magenta)
        FootprintPrimitive courtRect = FootprintPrimitive::createRect(QRectF(-span/2 - 1.0, -yMax - 1.0, span + 2.0, yMax*2 + 2.0).normalized());
        courtRect.layer = FootprintPrimitive::Top_Courtyard;
        m_footprint.addPrimitive(courtRect);

        if (m_nameEdit->text().isEmpty()) m_nameEdit->setText(QString("%1-%2").arg(type).arg(pins));
    } else if (type == "Passive (TH Axial)") {
        auto p1 = FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Rect", padSize);
        p1.data["drill_size"] = 0.8;
        auto p2 = FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Round", padSize);
        p2.data["drill_size"] = 0.8;
        
        m_footprint.addPrimitive(p1);
        m_footprint.addPrimitive(p2);
        if (m_nameEdit->text().isEmpty()) m_nameEdit->setText("R_Axial_TH");
    } else if (type.startsWith("Passive")) {
        m_footprint.addPrimitive(FootprintPrimitive::createPad(QPointF(-pitch/2, 0), "1", "Rect", padSize));
        m_footprint.addPrimitive(FootprintPrimitive::createPad(QPointF(pitch/2, 0), "2", "Rect", padSize));
        if (m_nameEdit->text().isEmpty()) m_nameEdit->setText("R_0805");
    }


    updateSceneFromDefinition();
    onSave(); // Automatically save and close/refresh
}

void updatePrimitivePos(FootprintPrimitive& prim, qreal dx, qreal dy) {
    prim.move(dx, dy);
}

void FootprintEditor::onAlignLeft() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal minX = std::numeric_limits<qreal>::max();
    for (auto item : selected) minX = qMin(minX, item->sceneBoundingRect().left());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentLeft = item->sceneBoundingRect().left();
        updatePrimitivePos(prim, minX - currentLeft, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Left"));
}

void FootprintEditor::onAlignRight() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal maxX = std::numeric_limits<qreal>::lowest();
    for (auto item : selected) maxX = qMax(maxX, item->sceneBoundingRect().right());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentRight = item->sceneBoundingRect().right();
        updatePrimitivePos(prim, maxX - currentRight, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Right"));
}

void FootprintEditor::onAlignTop() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal minY = std::numeric_limits<qreal>::max();
    for (auto item : selected) minY = qMin(minY, item->sceneBoundingRect().top());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentTop = item->sceneBoundingRect().top();
        updatePrimitivePos(prim, 0, minY - currentTop);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Top"));
}

void FootprintEditor::onAlignBottom() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal maxY = std::numeric_limits<qreal>::lowest();
    for (auto item : selected) maxY = qMax(maxY, item->sceneBoundingRect().bottom());
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentBottom = item->sceneBoundingRect().bottom();
        updatePrimitivePos(prim, 0, maxY - currentBottom);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Align Bottom"));
}

void FootprintEditor::onAlignCenterH() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal sumX = 0;
    for (auto item : selected) sumX += item->sceneBoundingRect().center().x();
    qreal centerX = sumX / selected.size();
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentCenterX = item->sceneBoundingRect().center().x();
        updatePrimitivePos(prim, centerX - currentCenterX, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Center Horizontally"));
}

void FootprintEditor::onAlignCenterV() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;
    
    qreal sumY = 0;
    for (auto item : selected) sumY += item->sceneBoundingRect().center().y();
    qreal centerY = sumY / selected.size();
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (auto item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal currentCenterY = item->sceneBoundingRect().center().y();
        updatePrimitivePos(prim, 0, centerY - currentCenterY);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Center Vertically"));
}

void FootprintEditor::onDistributeH() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 3) return;
    
    std::sort(selected.begin(), selected.end(), [](QGraphicsItem* a, QGraphicsItem* b) {
        return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x();
    });
    
    qreal startX = selected.first()->sceneBoundingRect().center().x();
    qreal endX = selected.last()->sceneBoundingRect().center().x();
    qreal step = (endX - startX) / (selected.size() - 1);
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (int i = 1; i < selected.size() - 1; ++i) {
        int idx = m_drawnItems.indexOf(selected[i]);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal targetX = startX + i * step;
        qreal currentX = selected[i]->sceneBoundingRect().center().x();
        updatePrimitivePos(prim, targetX - currentX, 0);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Distribute Horizontally"));
}

void FootprintEditor::onDistributeV() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 3) return;
    
    std::sort(selected.begin(), selected.end(), [](QGraphicsItem* a, QGraphicsItem* b) {
        return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y();
    });
    
    qreal startY = selected.first()->sceneBoundingRect().center().y();
    qreal endY = selected.last()->sceneBoundingRect().center().y();
    qreal step = (endY - startY) / (selected.size() - 1);
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    for (int i = 1; i < selected.size() - 1; ++i) {
        int idx = m_drawnItems.indexOf(selected[i]);
        if (idx == -1) continue;
        FootprintPrimitive& prim = newDef.primitives()[idx];
        
        qreal targetY = startY + i * step;
        qreal currentY = selected[i]->sceneBoundingRect().center().y();
        updatePrimitivePos(prim, 0, targetY - currentY);
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Distribute Vertically"));
}

void FootprintEditor::onMatchSpacing() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() < 2) return;

    bool ok;
    double pitch = QInputDialog::getDouble(this, "Match Spacing", "Enter spacing (mm):", 
                                          m_view->gridSize(), 0.1, 100.0, 2, &ok);
    if (!ok) return;

    // Determine orientation based on bounding rect
    QRectF totalRect;
    for (auto* item : selected) totalRect = totalRect.united(item->sceneBoundingRect());
    bool horizontal = totalRect.width() > totalRect.height();

    if (horizontal) {
        std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ 
            return a->sceneBoundingRect().center().x() < b->sceneBoundingRect().center().x(); 
        });
    } else {
        std::sort(selected.begin(), selected.end(), [](auto* a, auto* b){ 
            return a->sceneBoundingRect().center().y() < b->sceneBoundingRect().center().y(); 
        });
    }

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    QPointF start = selected.first()->sceneBoundingRect().center();

    for (int i = 1; i < selected.size(); ++i) {
        int idx = m_drawnItems.indexOf(selected[i]);
        if (idx != -1) {
            QPointF target;
            if (horizontal) target = start + QPointF(i * pitch, 0);
            else target = start + QPointF(0, i * pitch);

            QPointF delta = target - selected[i]->sceneBoundingRect().center();
            FootprintPrimitive& prim = newDef.primitives()[idx];
            updatePrimitivePos(prim, delta.x(), delta.y());
        }
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Match Spacing"));
}

void FootprintEditor::onMoveExactly() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    bool okX, okY;
    double dx = QInputDialog::getDouble(this, "Move Exactly", "Delta X (mm):", 0, -100, 100, 2, &okX);
    if (!okX) return;
    double dy = QInputDialog::getDouble(this, "Move Exactly", "Delta Y (mm):", 0, -100, 100, 2, &okY);
    if (!okY) return;

    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;

    for (auto* item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx != -1) {
            FootprintPrimitive& prim = newDef.primitives()[idx];
            updatePrimitivePos(prim, dx, dy);
        }
    }
    m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, "Move Exactly"));
}

QGraphicsItem* FootprintEditor::buildVisual(const FootprintPrimitive& prim, int index) {
    FootprintPrimitiveItem* visual = nullptr;

    switch (prim.type) {
        case FootprintPrimitive::Line:    visual = new FootprintLineItem(prim); break;
        case FootprintPrimitive::Rect:    visual = new FootprintRectItem(prim); break;
        case FootprintPrimitive::Circle:  visual = new FootprintCircleItem(prim); break;
        case FootprintPrimitive::Arc:     visual = new FootprintArcItem(prim); break;
        case FootprintPrimitive::Pad:     visual = new FootprintPadItem(prim); break;
        case FootprintPrimitive::Text:    visual = new FootprintTextItem(prim); break;
        default: return nullptr;
    }

    if (visual) {
        visual->setPrimitiveIndex(index);
    }
    return visual;
}

void FootprintEditor::updateSceneFromDefinition() {
    m_scene->clear();
    m_previewItem = nullptr; 
    m_drawnItems.clear(); 
    
    for (int i = 0; i < m_footprint.primitives().size(); ++i) {
        QGraphicsItem* item = buildVisual(m_footprint.primitives()[i], i);
        if (item) {
            m_scene->addItem(item);
            m_drawnItems.append(item);
        }
    }

    updatePropertiesPanel();
}

void FootprintEditor::onSelectionChanged() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.size() != 1) {
        m_propertyEditor->clear();
        return;
    }
    
    QGraphicsItem* item = selected.first();
    int index = m_drawnItems.indexOf(item);
    if (index == -1 || index >= m_footprint.primitives().size()) {
        m_propertyEditor->clear();
        return;
    }
    
    m_propertyEditor->clear();
    const FootprintPrimitive& prim = m_footprint.primitives()[index];
    
    QString layerEnum = "enum|Top Silkscreen,Top Courtyard,Top Fabrication,Top Copper,Bottom Copper,Bottom Silkscreen,Top Solder Mask,Bottom Solder Mask,Top Solder Paste,Bottom Solder Paste,Top Adhesive,Bottom Adhesive,Bottom Courtyard,Bottom Fabrication,Inner Copper 1,Inner Copper 2,Inner Copper 3,Inner Copper 4";
    QString currentLayerName = "Top Silkscreen";
    switch(prim.layer) {
        case FootprintPrimitive::Top_Silkscreen:    currentLayerName = "Top Silkscreen"; break;
        case FootprintPrimitive::Top_Courtyard:     currentLayerName = "Top Courtyard"; break;
        case FootprintPrimitive::Top_Fabrication:   currentLayerName = "Top Fabrication"; break;
        case FootprintPrimitive::Top_Copper:        currentLayerName = "Top Copper"; break;
        case FootprintPrimitive::Bottom_Copper:     currentLayerName = "Bottom Copper"; break;
        case FootprintPrimitive::Bottom_Silkscreen: currentLayerName = "Bottom Silkscreen"; break;
        case FootprintPrimitive::Top_SolderMask:    currentLayerName = "Top Solder Mask"; break;
        case FootprintPrimitive::Bottom_SolderMask: currentLayerName = "Bottom Solder Mask"; break;
        case FootprintPrimitive::Top_SolderPaste:   currentLayerName = "Top Solder Paste"; break;
        case FootprintPrimitive::Bottom_SolderPaste:currentLayerName = "Bottom Solder Paste"; break;
        case FootprintPrimitive::Top_Adhesive:      currentLayerName = "Top Adhesive"; break;
        case FootprintPrimitive::Bottom_Adhesive:   currentLayerName = "Bottom Adhesive"; break;
        case FootprintPrimitive::Bottom_Courtyard:  currentLayerName = "Bottom Courtyard"; break;
        case FootprintPrimitive::Bottom_Fabrication:currentLayerName = "Bottom Fabrication"; break;
        case FootprintPrimitive::Inner_Copper_1:    currentLayerName = "Inner Copper 1"; break;
        case FootprintPrimitive::Inner_Copper_2:    currentLayerName = "Inner Copper 2"; break;
        case FootprintPrimitive::Inner_Copper_3:    currentLayerName = "Inner Copper 3"; break;
        case FootprintPrimitive::Inner_Copper_4:    currentLayerName = "Inner Copper 4"; break;
    }

    m_propertyEditor->addProperty("Layer", currentLayerName, layerEnum);

    if (prim.type == FootprintPrimitive::Pad) {
        m_propertyEditor->addProperty("Number", prim.data["number"].toString());
        m_propertyEditor->addProperty("Pad Type", prim.data["pad_type"].toString("SMD"), "enum|SMD,Through-Hole,Connector");
        m_propertyEditor->addProperty("Shape", prim.data["shape"].toString(), "enum|Rect,Round,Oblong,Trapezoid,RoundedRect,Custom");
        m_propertyEditor->addProperty("Width", prim.data["width"].toDouble());
        m_propertyEditor->addProperty("Height", prim.data["height"].toDouble());
        m_propertyEditor->addProperty("X", prim.data["x"].toDouble());
        m_propertyEditor->addProperty("Y", prim.data["y"].toDouble());
        m_propertyEditor->addProperty("Rotation", prim.data["rotation"].toDouble());
        m_propertyEditor->addProperty("Corner Radius", prim.data["corner_radius"].toDouble());
        m_propertyEditor->addProperty("Trapezoid Delta X", prim.data["trapezoid_delta_x"].toDouble());
        
        // Advanced Pad Properties
        m_propertyEditor->addProperty("Drill Size", prim.data["drill_size"].toDouble());
        m_propertyEditor->addProperty("Clearance Override", prim.data["net_clearance_override_enabled"].toBool());
        m_propertyEditor->addProperty("Net Clearance", prim.data["net_clearance"].toDouble());
        m_propertyEditor->addProperty("Thermal Relief", prim.data["thermal_relief_enabled"].toBool(true));
        m_propertyEditor->addProperty("Thermal Spoke Width", prim.data["thermal_spoke_width"].toDouble(0.3));
        m_propertyEditor->addProperty("Thermal Relief Gap", prim.data["thermal_relief_gap"].toDouble(0.25));
        m_propertyEditor->addProperty("Thermal Spoke Count", prim.data["thermal_spoke_count"].toInt(4), "enum|1,2,3,4,5,6,7,8");
        m_propertyEditor->addProperty("Thermal Spoke Angle", prim.data["thermal_spoke_angle_deg"].toDouble(0.0));
        m_propertyEditor->addProperty("Jumper Group", prim.data["jumper_group"].toInt(0), "double");
        m_propertyEditor->addProperty("Net Tie Group", prim.data["net_tie_group"].toInt(0), "double");
        m_propertyEditor->addProperty("Solder Mask Exp", prim.data["solder_mask_expansion"].toDouble());
        m_propertyEditor->addProperty("Paste Mask Exp", prim.data["paste_mask_expansion"].toDouble());
        m_propertyEditor->addProperty("Plated", prim.data["plated"].toBool());
    } else if (prim.type == FootprintPrimitive::Text) {
        m_propertyEditor->addProperty("Text", prim.data["text"].toString());
        m_propertyEditor->addProperty("Height", prim.data["height"].toDouble());
        m_propertyEditor->addProperty("X", prim.data["x"].toDouble());
    } else if (prim.type == FootprintPrimitive::Dimension) {
        m_propertyEditor->addProperty("X1", prim.data["x1"].toDouble());
        m_propertyEditor->addProperty("Y1", prim.data["y1"].toDouble());
        m_propertyEditor->addProperty("X2", prim.data["x2"].toDouble());
        m_propertyEditor->addProperty("Y2", prim.data["y2"].toDouble());
    } else if (prim.type == FootprintPrimitive::Line) {
        m_propertyEditor->addProperty("X1", prim.data["x1"].toDouble());
        m_propertyEditor->addProperty("Y1", prim.data["y1"].toDouble());
        m_propertyEditor->addProperty("X2", prim.data["x2"].toDouble());
    }
}

void FootprintEditor::createLibraryBrowser() {
    m_libSearchEdit = new QLineEdit();
    m_libSearchEdit->setPlaceholderText("Search footprints...");
    m_libSearchEdit->setClearButtonEnabled(true);
    connect(m_libSearchEdit, &QLineEdit::textChanged, this, &FootprintEditor::onLibSearchChanged);

    m_libraryTree = new QTreeWidget();
    m_libraryTree->setHeaderHidden(true);
    m_libraryTree->setIndentation(15);
    // Style handled by global setupUI stylesheet
    connect(m_libraryTree, &QTreeWidget::itemDoubleClicked, this, &FootprintEditor::onLoadFootprint);

    populateLibraryTree();
}

void FootprintEditor::populateLibraryTree() {
    m_libraryTree->clear();
    
    QIcon libIcon(":/icons/folder_open.svg");
    QIcon fpIcon(":/icons/component_file.svg");

    for (FootprintLibrary* lib : FootprintLibraryManager::instance().libraries()) {
        QTreeWidgetItem* libItem = new QTreeWidgetItem(m_libraryTree);
        libItem->setText(0, lib->name());
        libItem->setIcon(0, libIcon); 
        libItem->setData(0, Qt::UserRole, "Library");
        
        QStringList footprints = lib->getFootprintNames();
        // Sort for better UX
        std::sort(footprints.begin(), footprints.end());

        for (const QString& fpName : footprints) {
            QTreeWidgetItem* fpItem = new QTreeWidgetItem(libItem);
            fpItem->setText(0, fpName);
            fpItem->setIcon(0, fpIcon);
            fpItem->setData(0, Qt::UserRole, "Footprint");
            fpItem->setData(0, Qt::UserRole + 1, lib->name()); // Store lib name if needed
        }
        
        // Expand if it has few items or is the main one
        if (lib->name() == "User Library" || footprints.size() < 10) {
            libItem->setExpanded(true);
        }
    }
}

void FootprintEditor::onLibSearchChanged(const QString& text) {
    QString query = text.trimmed();
    bool hasQuery = !query.isEmpty();
    
    for (int i = 0; i < m_libraryTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* libItem = m_libraryTree->topLevelItem(i);
        bool libMatches = libItem->text(0).contains(query, Qt::CaseInsensitive);
        bool anyChildMatches = false;
        
        for (int j = 0; j < libItem->childCount(); ++j) {
            QTreeWidgetItem* fpItem = libItem->child(j);
            bool fpMatches = fpItem->text(0).contains(query, Qt::CaseInsensitive);
            
            bool visible = !hasQuery || fpMatches || libMatches;
            fpItem->setHidden(!visible);
            
            if (visible) anyChildMatches = true;
        }
        
        libItem->setHidden(!hasQuery && !libMatches && !anyChildMatches); // Logic might be tricky
        // Better:
        // If query empty: show all
        // If query:
        //   Show lib if lib matches OR any child matches
        //   Show child if child matches OR lib matches property? usually only if child matches
        // Let's stick to standard search: show item if it matches. Expand parent if child matches.
        
        if (hasQuery) {
            libItem->setHidden(!anyChildMatches && !libMatches);
            if (anyChildMatches) libItem->setExpanded(true);
        } else {
            libItem->setHidden(false);
            // Maybe restore expansion state?
        }
    }
}

void FootprintEditor::onLoadFootprint(QTreeWidgetItem* item, int column) {
    if (item->data(0, Qt::UserRole).toString() != "Footprint") return;
    
    QString name = item->text(0);
    FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(name);
    
    if (def.isValid()) {
        if (!m_footprint.primitives().isEmpty()) {
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Load Footprint", 
                                        "Loading a footprint will overwrite current progress.\nAre you sure?",
                                        QMessageBox::Yes|QMessageBox::No);
            if (reply == QMessageBox::No) return;
        }
        
        setFootprintDefinition(def);
        
        // Also update info fields in case they differ from definition
        m_nameEdit->setText(def.name());
        m_descriptionEdit->setText(def.description());
        m_categoryCombo->setCurrentText(def.category());
        m_classificationCombo->setCurrentText(def.classification());
        m_keywordsEdit->setText(def.keywords().join(", "));
        m_excludeBOMCheck->setChecked(def.excludeFromBOM());
        m_excludePosCheck->setChecked(def.excludeFromPosFiles());
        m_dnpCheck->setChecked(def.dnp());
        m_netTieCheck->setChecked(def.isNetTie());
    }
}

void FootprintEditor::onImportKicadFootprint() {
    QString path = QFileDialog::getOpenFileName(
        this,
        "Import KiCad Footprint",
        QString(),
        "KiCad Footprints (*.kicad_mod *.kicad_pcb)");
    if (path.isEmpty()) return;
    importKicadFootprintFromFile(path);
}

bool FootprintEditor::importKicadFootprintFromFile(const QString& path) {
    m_lastImportBaseDir = QFileInfo(path).absolutePath();
    const QStringList names = KicadFootprintImporter::getFootprintNames(path);
    if (names.isEmpty()) {
        QMessageBox::warning(this, "Import KiCad", "No KiCad footprints found in selected file.");
        return false;
    }

    QString chosenName = names.first();
    if (names.size() > 1) {
        bool ok = false;
        chosenName = QInputDialog::getItem(this, "Import KiCad Footprint", "Select footprint:", names, 0, false, &ok);
        if (!ok || chosenName.isEmpty()) return false;
    }

    KicadFootprintImporter::ImportReport report = KicadFootprintImporter::importFootprintDetailed(path, chosenName);
    FootprintDefinition imported = report.footprint;
    if (!imported.isValid()) {
        QMessageBox::critical(this, "Import KiCad", "Failed to parse the selected footprint.");
        return false;
    }

    if (!m_footprint.primitives().isEmpty()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Import KiCad",
            "Importing will replace current footprint contents.\nContinue?",
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return false;
    }

    setFootprintDefinition(imported);
    if (m_statusLabel) {
        m_statusLabel->setText(
            QString("Imported KiCad footprint: %1 (%2 primitives)")
                .arg(imported.name())
                .arg(imported.primitives().size()));
    }

    QString summary = QString(
        "Imported: %1\n\n"
        "Pads: %2\n"
        "Lines: %3\n"
        "Rects: %4\n"
        "Circles: %5\n"
        "Arcs: %6\n"
        "Texts: %7\n"
        "Unsupported primitives: %8")
            .arg(imported.name())
            .arg(report.padCount)
            .arg(report.lineCount)
            .arg(report.rectCount)
            .arg(report.circleCount)
            .arg(report.arcCount)
            .arg(report.textCount)
            .arg(report.unsupportedCount);
    if (!report.unsupportedKinds.isEmpty()) {
        summary += "\n\nUnsupported kinds:\n- " + report.unsupportedKinds.join("\n- ");
    }
    QMessageBox::information(this, "KiCad Import Summary", summary);
    return true;
}

QString FootprintEditor::resolveModelPathForPreview(const QString& rawPath) const {
    QString path = rawPath.trimmed();
    if (path.isEmpty()) return QString();

    auto expandEnv = [](QString in) -> QString {
        QRegularExpression braceVar("\\$\\{([^}]+)\\}");
        QRegularExpressionMatch m = braceVar.match(in);
        while (m.hasMatch()) {
            const QString var = m.captured(1).trimmed();
            const QString val = qEnvironmentVariable(var.toUtf8().constData());
            in.replace(m.capturedStart(0), m.capturedLength(0), val);
            m = braceVar.match(in);
        }
        QRegularExpression plainVar("\\$([A-Za-z_][A-Za-z0-9_]*)");
        m = plainVar.match(in);
        while (m.hasMatch()) {
            const QString var = m.captured(1).trimmed();
            const QString val = qEnvironmentVariable(var.toUtf8().constData());
            in.replace(m.capturedStart(0), m.capturedLength(0), val);
            m = plainVar.match(in);
        }
        return QDir::cleanPath(QDir::fromNativeSeparators(in));
    };

    auto pickExisting = [](const QString& p) -> QString {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(p));
        QFileInfo fi(clean);
        if (fi.exists() && fi.isFile()) return fi.absoluteFilePath();
        const QString suffix = fi.suffix().toLower();
        if (suffix == "wrl" || suffix == "step" || suffix == "stp") {
            const QString objPath = fi.path() + "/" + fi.completeBaseName() + ".obj";
            QFileInfo objFi(objPath);
            if (objFi.exists() && objFi.isFile()) return objFi.absoluteFilePath();
        }
        return QString();
    };

    path = expandEnv(path);

    QFileInfo fi(path);
    if (fi.isAbsolute()) return pickExisting(path);
    {
        const QString local = pickExisting(path);
        if (!local.isEmpty()) return local;
    }

    if (!m_lastImportBaseDir.isEmpty()) {
        const QString fromImportBase = pickExisting(QDir(m_lastImportBaseDir).filePath(path));
        if (!fromImportBase.isEmpty()) return fromImportBase;
    }

    for (const QString& modelRoot : ConfigManager::instance().modelPaths()) {
        const QString fromCfg = pickExisting(QDir(modelRoot).filePath(path));
        if (!fromCfg.isEmpty()) return fromCfg;
    }

    QStringList envRoots;
    envRoots << qEnvironmentVariable("KISYS3DMOD");
    for (int v = 5; v <= 9; ++v) {
        envRoots << qEnvironmentVariable(QString("KICAD%1_3DMODEL_DIR").arg(v).toUtf8().constData());
    }
    for (const QString& envRoot : envRoots) {
        if (envRoot.trimmed().isEmpty()) continue;
        const QString fromEnv = pickExisting(QDir(envRoot).filePath(path));
        if (!fromEnv.isEmpty()) return fromEnv;
    }

    return QString();
}

void FootprintEditor::refreshModelSelector() {
    if (!m_modelSelector) return;
    const int oldIndex = m_modelSelector->currentIndex();
    m_modelSelector->blockSignals(true);
    m_modelSelector->clear();
    for (int i = 0; i < m_models3D.size(); ++i) {
        const QString filename = QFileInfo(m_models3D[i].filename).fileName();
        const QString hiddenSuffix = m_models3D[i].visible ? QString() : QString(" [Hidden]");
        const QString label = filename.isEmpty()
            ? QString("Model %1 (%2%)%3").arg(i + 1).arg(int(std::round(m_models3D[i].opacity * 100.0f))).arg(hiddenSuffix)
            : QString("Model %1: %2 (%3%)%4").arg(i + 1).arg(filename).arg(int(std::round(m_models3D[i].opacity * 100.0f))).arg(hiddenSuffix);
        m_modelSelector->addItem(label);
    }
    m_modelSelector->blockSignals(false);
    if (m_removeModelButton) m_removeModelButton->setEnabled(m_models3D.size() > 1);
    if (!m_models3D.isEmpty()) {
        const int idx = std::clamp(oldIndex, 0, int(m_models3D.size()) - 1);
        m_modelSelector->setCurrentIndex(idx);
    }
}

void FootprintEditor::loadModelToFields(int index) {
    if (index < 0 || index >= m_models3D.size()) return;
    const Footprint3DModel& model = m_models3D[index];
    m_modelFileEdit->setText(model.filename);
    m_modelOffsetX->setText(QString::number(model.offset.x()));
    m_modelOffsetY->setText(QString::number(model.offset.y()));
    m_modelOffsetZ->setText(QString::number(model.offset.z()));
    m_modelRotX->setText(QString::number(model.rotation.x()));
    m_modelRotY->setText(QString::number(model.rotation.y()));
    m_modelRotZ->setText(QString::number(model.rotation.z()));
    m_modelScaleX->setText(QString::number(model.scale.x()));
    m_modelScaleY->setText(QString::number(model.scale.y()));
    m_modelScaleZ->setText(QString::number(model.scale.z()));
    if (m_modelOpacitySpin) {
        m_modelOpacitySpin->blockSignals(true);
        m_modelOpacitySpin->setValue(model.opacity);
        m_modelOpacitySpin->blockSignals(false);
    }
    if (m_modelVisibleCheck) {
        m_modelVisibleCheck->blockSignals(true);
        m_modelVisibleCheck->setChecked(model.visible);
        m_modelVisibleCheck->blockSignals(false);
    }
}

void FootprintEditor::syncCurrentModelFromFields() {
    if (m_models3D.isEmpty()) return;
    const int index = m_modelSelector ? m_modelSelector->currentIndex() : 0;
    if (index < 0 || index >= m_models3D.size()) return;
    Footprint3DModel& model = m_models3D[index];
    model.filename = m_modelFileEdit->text().trimmed();
    model.offset = QVector3D(m_modelOffsetX->text().toDouble(), m_modelOffsetY->text().toDouble(), m_modelOffsetZ->text().toDouble());
    model.rotation = QVector3D(m_modelRotX->text().toDouble(), m_modelRotY->text().toDouble(), m_modelRotZ->text().toDouble());
    model.scale = QVector3D(m_modelScaleX->text().toDouble(), m_modelScaleY->text().toDouble(), m_modelScaleZ->text().toDouble());
    if (m_modelOpacitySpin) model.opacity = float(m_modelOpacitySpin->value());
    if (m_modelVisibleCheck) model.visible = m_modelVisibleCheck->isChecked();
}

void FootprintEditor::onOpen3DPreview() {
    // Snapshot current editor state without requiring a save/name prompt.
    m_footprint.setName(m_nameEdit->text().trimmed());
    if (m_footprint.name().isEmpty()) m_footprint.setName("FootprintPreview");
    m_footprint.setDescription(m_descriptionEdit->text());
    m_footprint.setCategory(m_categoryCombo->currentText());
    m_footprint.setClassification(m_classificationCombo->currentText());
    m_footprint.setExcludeFromBOM(m_excludeBOMCheck->isChecked());
    m_footprint.setExcludeFromPosFiles(m_excludePosCheck->isChecked());
    m_footprint.setDnp(m_dnpCheck->isChecked());
    m_footprint.setIsNetTie(m_netTieCheck->isChecked());
    QStringList keywords;
    for (const QString& token : m_keywordsEdit->text().split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty()) keywords.append(trimmed);
    }
    m_footprint.setKeywords(keywords);
    syncCurrentModelFromFields();
    m_footprint.setModels3D(m_models3D);
    if (!m_models3D.isEmpty()) {
        m_footprint.setModel3D(m_models3D.first());
    }

    // Keep a persistent preview scene/window while dialog is open.
    if (!m_footprint3DScene) {
        m_footprint3DScene = new QGraphicsScene(this);
    } else {
        m_footprint3DScene->clear();
    }

    // Inject current footprint into a private preview library (in-memory), then render one component.
    static const QString kPreviewLibraryName = "__FootprintPreview__";
    FootprintLibraryManager& fpMgr = FootprintLibraryManager::instance();
    FootprintLibrary* previewLib = fpMgr.findLibrary(kPreviewLibraryName);
    if (!previewLib) previewLib = fpMgr.createLibrary(kPreviewLibraryName);
    if (!previewLib) {
        QMessageBox::critical(this, "3D Preview", "Failed to initialize preview library.");
        return;
    }

    const QString previewName = "__preview__" + m_footprint.name();
    FootprintDefinition def = m_footprint.clone();
    def.setName(previewName);
    previewLib->addFootprint(def);

    auto* comp = new ComponentItem(QPointF(0, 0), previewName);
    comp->setName(previewName);
    // Keep component override empty so renderer uses footprint-level multi-model list.
    comp->setModelPath(QString());
    comp->setModelOffset(QVector3D(0.0f, 0.0f, 0.0f));
    comp->setModelRotation(QVector3D(0.0f, 0.0f, 0.0f));
    comp->setModelScale3D(QVector3D(1.0f, 1.0f, 1.0f));
    comp->setModelScale(1.0);

    m_footprint3DScene->addItem(comp);

    if (!m_footprint3DWindow) {
        m_footprint3DWindow = new PCB3DWindow(m_footprint3DScene, this);
        m_footprint3DWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    }

    m_footprint3DWindow->setWindowTitle(QString("Footprint 3D Preview - %1").arg(m_footprint.name()));
    m_footprint3DWindow->setSubstrateAlpha(1.0f);
    m_footprint3DWindow->setComponentAlpha(1.0f);
    m_footprint3DWindow->setShowCopper(true);
    const bool showBottom = m_previewBottomCopperCheck ? m_previewBottomCopperCheck->isChecked() : false;
    m_footprint3DWindow->setShowBottomCopper(showBottom);
    m_footprint3DWindow->updateView();
    m_footprint3DWindow->show();
    m_footprint3DWindow->raise();
    m_footprint3DWindow->activateWindow();
}

void FootprintEditor::dragEnterEvent(QDragEnterEvent* event) {
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) event->ignore();
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        const QString p = url.toLocalFile();
        if (p.endsWith(".kicad_mod", Qt::CaseInsensitive) || p.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void FootprintEditor::dropEvent(QDropEvent* event) {
    if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
        if (event) event->ignore();
        return;
    }
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl& url : urls) {
        const QString p = url.toLocalFile();
        if (p.endsWith(".kicad_mod", Qt::CaseInsensitive) || p.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
            if (importKicadFootprintFromFile(p)) {
                event->acceptProposedAction();
            } else {
                event->ignore();
            }
            return;
        }
    }
    event->ignore();
}

void FootprintEditor::onDelete() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    
    FootprintDefinition oldDef = m_footprint;
    FootprintDefinition newDef = oldDef;
    
    // Sort selected items by index descending to avoid index shift issues if we were removing one by one
    // But since we use UpdateFootprintCommand, we just build the new definition.
    QList<int> indices;
    for (QGraphicsItem* item : selected) {
        int index = m_drawnItems.indexOf(item);
        if (index != -1) indices.append(index);
    }
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    
    for (int index : indices) {
        if (index < newDef.primitives().size()) {
            newDef.primitives().removeAt(index);
        }
    }
    
    if (indices.size() > 0) {
        m_undoStack->push(new UpdateFootprintCommand(this, oldDef, newDef, QString("Delete %1 Items").arg(indices.size())));
    }
}

// Helper to perform save
// Helper to validate and update footprint object
bool FootprintEditor::prepareFootprint() {
    QString name = m_nameEdit->text();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Save Footprint", "Please enter a footprint name.");
        return false;
    }
    
    m_footprint.setName(name);
    m_footprint.setDescription(m_descriptionEdit->text());
    m_footprint.setCategory(m_categoryCombo->currentText());
    m_footprint.setClassification(m_classificationCombo->currentText());
    m_footprint.setExcludeFromBOM(m_excludeBOMCheck->isChecked());
    m_footprint.setExcludeFromPosFiles(m_excludePosCheck->isChecked());
    m_footprint.setDnp(m_dnpCheck->isChecked());
    m_footprint.setIsNetTie(m_netTieCheck->isChecked());
    QStringList keywords;
    for (const QString& token : m_keywordsEdit->text().split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty()) keywords.append(trimmed);
    }
    m_footprint.setKeywords(keywords);
    
    syncCurrentModelFromFields();
    m_footprint.setModels3D(m_models3D);
    if (!m_models3D.isEmpty()) {
        m_footprint.setModel3D(m_models3D.first());
    }
    
    return true;
}

void FootprintEditor::onSave() {
    if (prepareFootprint()) {
        emit footprintSaved(m_footprint);
        accept();
    }
}

void FootprintEditor::onSaveToLibrary() {
    if (!prepareFootprint()) return;

    QStringList libNames;
    for (auto* lib : FootprintLibraryManager::instance().libraries()) {
        libNames << lib->name();
    }
    if (libNames.isEmpty()) libNames << "User Library";

    bool ok;
    QString libName = QInputDialog::getItem(this, "Save to Library", 
                                          "Select or create library:", 
                                          libNames, 0, true, &ok);
    if (ok && !libName.isEmpty()) {
        // Create (or get existing) library
        FootprintLibrary* lib = FootprintLibraryManager::instance().createLibrary(libName);
        if (lib) {
            if (lib->saveFootprint(m_footprint)) {
                QMessageBox::information(this, "Footprint Saved", 
                    QString("Footprint '%1' saved to library '%2'.").arg(m_footprint.name()).arg(lib->name()));
            } else {
                QMessageBox::critical(this, "Save Failed", "Failed to write footprint file.");
            }
        }
    }
}
void FootprintEditor::onClear() {
    m_footprint.clearPrimitives();
    updateSceneFromDefinition();
}

void FootprintEditor::onUndo() {
    if (m_undoStack) m_undoStack->undo();
}

void FootprintEditor::onRedo() {
    if (m_undoStack) m_undoStack->redo();
}
void FootprintEditor::updatePropertiesPanel() {}
void FootprintEditor::updatePreview() {}
void FootprintEditor::drawGrid() {}

FootprintDefinition FootprintEditor::footprintDefinition() const {
    return m_footprint;
}

void FootprintEditor::setFootprintDefinition(const FootprintDefinition& def) {
    m_footprint = def;
    // update fields
    m_nameEdit->setText(def.name());
    m_descriptionEdit->setText(def.description());
    m_categoryCombo->setCurrentText(def.category());
    m_classificationCombo->setCurrentText(def.classification());
    m_keywordsEdit->setText(def.keywords().join(", "));
    m_excludeBOMCheck->setChecked(def.excludeFromBOM());
    m_excludePosCheck->setChecked(def.excludeFromPosFiles());
    m_dnpCheck->setChecked(def.dnp());
    m_netTieCheck->setChecked(def.isNetTie());
    
    // 3D Models
    m_models3D = def.models3D();
    if (m_models3D.isEmpty()) {
        m_models3D.append(def.model3D());
    }
    if (m_models3D.isEmpty()) {
        Footprint3DModel model;
        model.scale = QVector3D(1.0f, 1.0f, 1.0f);
        m_models3D.append(model);
    }
    refreshModelSelector();
    m_modelSelector->setCurrentIndex(0);
    loadModelToFields(0);
    
    updateSceneFromDefinition();
}
    
    void FootprintEditor::onSetAnchor(QPointF pos) {
        // Shift all primitives by -pos
        for (int i = 0; i < m_footprint.primitives().size(); ++i) {
            FootprintPrimitive& prim = m_footprint.primitives()[i];
            if (prim.type == FootprintPrimitive::Line) {
                prim.data["x1"] = prim.data["x1"].toDouble() - pos.x();
                prim.data["y1"] = prim.data["y1"].toDouble() - pos.y();
                prim.data["x2"] = prim.data["x2"].toDouble() - pos.x();
                prim.data["y2"] = prim.data["y2"].toDouble() - pos.y();
            } else {
                prim.data["x"] = prim.data["x"].toDouble() - pos.x();
                prim.data["y"] = prim.data["y"].toDouble() - pos.y();
            }
        }
        updateSceneFromDefinition();
        m_statusLabel->setText(QString("Anchor set at (%1, %2)").arg(pos.x()).arg(pos.y()));
    }
    
    void FootprintEditor::onArrayTool() {
        QList<QGraphicsItem*> selected = m_scene->selectedItems();
        if (selected.isEmpty()) {
            QMessageBox::information(this, "Array Tool", "Select an item (e.g. a Pad) to create an array.");
            return;
        }
    
        QDialog dlg(this);
        dlg.setWindowTitle("Create Array");
        QFormLayout layout(&dlg);
    
        QComboBox* typeCombo = new QComboBox();
        typeCombo->addItems({"Linear", "Circular"});
        layout.addRow("Type:", typeCombo);
    
        QSpinBox* countSpin = new QSpinBox();
        countSpin->setRange(2, 100);
        countSpin->setValue(5);
        layout.addRow("Count:", countSpin);
    
        // Dynamic containers for type-specific options
        QWidget* linearWidget = new QWidget();
        QFormLayout* linearLayout = new QFormLayout(linearWidget);
        QDoubleSpinBox* stepX = new QDoubleSpinBox(); stepX->setRange(-100, 100); stepX->setDecimals(3); stepX->setValue(2.54);
        QDoubleSpinBox* stepY = new QDoubleSpinBox(); stepY->setRange(-100, 100); stepY->setDecimals(3); stepY->setValue(0.0);
        linearLayout->addRow("Step X (mm):", stepX);
        linearLayout->addRow("Step Y (mm):", stepY);
        layout.addRow(linearWidget);
    
            QWidget* circularWidget = new QWidget();
            QFormLayout* circularLayout = new QFormLayout(circularWidget);
            QDoubleSpinBox* centerX = new QDoubleSpinBox(); centerX->setRange(-500, 500); centerX->setValue(0.0);
            QDoubleSpinBox* centerY = new QDoubleSpinBox(); centerY->setRange(-500, 500); centerY->setValue(0.0);
            QDoubleSpinBox* totalAngle = new QDoubleSpinBox(); totalAngle->setRange(-360, 360); totalAngle->setValue(360.0);
            QCheckBox* rotateItems = new QCheckBox("Rotate Items to Center"); rotateItems->setChecked(true);
            circularLayout->addRow("Center X:", centerX);
            circularLayout->addRow("Center Y:", centerY);
            circularLayout->addRow("Total Angle:", totalAngle);
            circularLayout->addRow("", rotateItems);
            layout.addRow(circularWidget);
            circularWidget->hide();
        
            connect(typeCombo, &QComboBox::currentIndexChanged, this, [&dlg, linearWidget, circularWidget](int idx){
                linearWidget->setVisible(idx == 0);
                circularWidget->setVisible(idx == 1);
                dlg.adjustSize();
            });    
        QPushButton* okBtn = new QPushButton("Create");
        connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        layout.addRow(okBtn);
    
        if (dlg.exec() == QDialog::Accepted) {
            int count = countSpin->value();
            bool isLinear = (typeCombo->currentIndex() == 0);
    
            for (auto item : selected) {
                int idx = m_drawnItems.indexOf(item);
                if (idx == -1) continue;
                const FootprintPrimitive& basePrim = m_footprint.primitives()[idx];
                
                for (int i = 1; i < count; ++i) {
                    FootprintPrimitive newPrim = basePrim;
                    
                    if (isLinear) {
                        qreal dx = stepX->value();
                        qreal dy = stepY->value();
                        if (newPrim.type == FootprintPrimitive::Line) {
                            newPrim.data["x1"] = newPrim.data["x1"].toDouble() + dx * i;
                            newPrim.data["y1"] = newPrim.data["y1"].toDouble() + dy * i;
                            newPrim.data["x2"] = newPrim.data["x2"].toDouble() + dx * i;
                            newPrim.data["y2"] = newPrim.data["y2"].toDouble() + dy * i;
                        } else {
                            newPrim.data["x"] = newPrim.data["x"].toDouble() + dx * i;
                            newPrim.data["y"] = newPrim.data["y"].toDouble() + dy * i;
                        }
                    } else {
                        // Circular Math
                        qreal cx = centerX->value();
                        qreal cy = centerY->value();
                        qreal startX = (newPrim.type == FootprintPrimitive::Line) ? newPrim.data["x1"].toDouble() : newPrim.data["x"].toDouble();
                        qreal startY = (newPrim.type == FootprintPrimitive::Line) ? newPrim.data["y1"].toDouble() : newPrim.data["y"].toDouble();
                        
                        qreal relX = startX - cx;
                        qreal relY = startY - cy;
                        qreal radius = std::sqrt(relX*relX + relY*relY);
                        qreal startPhi = std::atan2(relY, relX);
                        qreal angleStep = (totalAngle->value() * M_PI / 180.0) / (totalAngle->value() == 360.0 ? count : count - 1);
                        
                        qreal currentPhi = startPhi + angleStep * i;
                        qreal newX = cx + radius * std::cos(currentPhi);
                        qreal newY = cy + radius * std::sin(currentPhi);
    
                        if (newPrim.type == FootprintPrimitive::Line) {
                            qreal dx = newX - startX;
                            qreal dy = newY - startY;
                            newPrim.data["x1"] = newX;
                            newPrim.data["y1"] = newY;
                            newPrim.data["x2"] = newPrim.data["x2"].toDouble() + dx;
                            newPrim.data["y2"] = newPrim.data["y2"].toDouble() + dy;
                        } else {
                            newPrim.data["x"] = newX;
                            newPrim.data["y"] = newY;
                        }
    
                        if (rotateItems->isChecked()) {
                            qreal rotDeg = (currentPhi - startPhi) * 180.0 / M_PI;
                            newPrim.data["rotation"] = newPrim.data["rotation"].toDouble() + rotDeg;
                        }
                    }
                    
                    // Pad Numbering
                    if (newPrim.type == FootprintPrimitive::Pad) {
                        QString oldNum = newPrim.data["number"].toString();
                        bool ok;
                        int n = oldNum.toInt(&ok);
                        if (ok) newPrim.data["number"] = QString::number(n + i);
                    }
                    
                    m_footprint.addPrimitive(newPrim);
                }
            }
            updateSceneFromDefinition();
        }
    }

void FootprintEditor::onPolarGridTool() {
    QDialog dlg(this);
    dlg.setWindowTitle("Polar Grid Generator");
    QFormLayout layout(&dlg);

    QDoubleSpinBox* centerX = new QDoubleSpinBox(&dlg);
    centerX->setRange(-500, 500);
    centerX->setDecimals(3);
    centerX->setValue(0.0);
    layout.addRow("Center X (mm):", centerX);

    QDoubleSpinBox* centerY = new QDoubleSpinBox(&dlg);
    centerY->setRange(-500, 500);
    centerY->setDecimals(3);
    centerY->setValue(0.0);
    layout.addRow("Center Y (mm):", centerY);

    QSpinBox* countSpin = new QSpinBox(&dlg);
    countSpin->setRange(2, 256);
    countSpin->setValue(8);
    layout.addRow("Count:", countSpin);

    QDoubleSpinBox* radiusSpin = new QDoubleSpinBox(&dlg);
    radiusSpin->setRange(0.1, 500);
    radiusSpin->setDecimals(3);
    radiusSpin->setValue(5.0);
    layout.addRow("Radius (mm):", radiusSpin);

    QDoubleSpinBox* startAngle = new QDoubleSpinBox(&dlg);
    startAngle->setRange(-360, 360);
    startAngle->setDecimals(2);
    startAngle->setValue(0.0);
    layout.addRow("Start Angle (deg):", startAngle);

    QComboBox* shapeCombo = new QComboBox(&dlg);
    shapeCombo->addItems({"Rect", "Round", "Oblong", "Trapezoid"});
    shapeCombo->setCurrentText(m_currentPadShape.isEmpty() ? "Rect" : m_currentPadShape);
    layout.addRow("Pad Shape:", shapeCombo);

    QDoubleSpinBox* padW = new QDoubleSpinBox(&dlg);
    padW->setRange(0.1, 50);
    padW->setDecimals(3);
    padW->setValue(1.5);
    layout.addRow("Pad Width (mm):", padW);

    QDoubleSpinBox* padH = new QDoubleSpinBox(&dlg);
    padH->setRange(0.1, 50);
    padH->setDecimals(3);
    padH->setValue(1.5);
    layout.addRow("Pad Height (mm):", padH);

    QCheckBox* rotatePads = new QCheckBox("Rotate pads radially", &dlg);
    rotatePads->setChecked(true);
    layout.addRow("", rotatePads);

    QPushButton* createBtn = new QPushButton("Create", &dlg);
    connect(createBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout.addRow(createBtn);

    if (dlg.exec() != QDialog::Accepted) return;

    const int count = countSpin->value();
    const qreal cx = centerX->value();
    const qreal cy = centerY->value();
    const qreal radius = radiusSpin->value();
    const qreal start = startAngle->value();
    const QString shape = shapeCombo->currentText();
    const QSizeF size(padW->value(), padH->value());

    for (int i = 0; i < count; ++i) {
        const qreal aDeg = start + (360.0 * i / count);
        const qreal aRad = aDeg * M_PI / 180.0;
        const QPointF pos(cx + radius * std::cos(aRad), cy + radius * std::sin(aRad));

        FootprintPrimitive prim = FootprintPrimitive::createPad(pos, getNextPadNumber(), shape, size);
        prim.layer = FootprintPrimitive::Top_Copper;
        if (shape == "Trapezoid") prim.data["trapezoid_delta_x"] = 0.6;
        if (rotatePads->isChecked()) prim.data["rotation"] = aDeg;
        m_footprint.addPrimitive(prim);
    }

    setPadShape(shape);
    updateSceneFromDefinition();
}

void FootprintEditor::onRunDRC() {
    QList<FootprintViolation> violations = FootprintEngine::checkFootprint(m_footprint);

    if (violations.isEmpty()) {
        QMessageBox::information(this, "Footprint Rule Check", "✅ No issues found. Your footprint follows all design rules.");
        return;
    }

    QString report = "<h3>Footprint Design Rule Check Results</h3><ul>";
    int errorCount = 0;
    int warningCount = 0;

    for (const auto& v : violations) {
        QString icon = "⚠️ ";
        QString color = "#fbbf24"; // Amber
        
        if (v.severity == FootprintViolation::Error || v.severity == FootprintViolation::Critical) {
            icon = "❌ ";
            color = "#f87171"; // Red
            errorCount++;
        } else {
            warningCount++;
        }

        report += QString("<li><span style='color:%1;'>%2 <b>%3:</b> %4</span></li>")
                    .arg(color, icon, v.severityString(), v.message);
    }
    report += "</ul>";
    report += QString("<p><b>Total: %1 Errors, %2 Warnings</b></p>").arg(errorCount).arg(warningCount);

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Footprint Rule Check");
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(report);
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setIcon(errorCount > 0 ? QMessageBox::Warning : QMessageBox::Information);
    msgBox.exec();
}

QString FootprintEditor::getNextPadNumber() const {
    int maxNum = 0;
    for (const auto& prim : m_footprint.primitives()) {
        if (prim.type == FootprintPrimitive::Pad) {
            bool ok;
            int n = prim.data["number"].toString().toInt(&ok);
            if (ok && n > maxNum) maxNum = n;
        }
    }
    return QString::number(maxNum + 1);
}

void FootprintEditor::onContextMenu(QPoint pos) {
    QGraphicsItem* item = m_view->itemAt(pos);
    QMenu menu(this);
    
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    bool hasConvertiblePrimitives = false;
    for (auto* it : selected) {
        const int idx = m_drawnItems.indexOf(it);
        if (idx < 0 || idx >= m_footprint.primitives().size()) continue;
        const FootprintPrimitive& prim = m_footprint.primitives().at(idx);
        if (prim.type == FootprintPrimitive::Polygon || prim.type == FootprintPrimitive::Rect) {
            hasConvertiblePrimitives = true;
            break;
        }
    }

    if (hasConvertiblePrimitives) {
        menu.addAction(QIcon(":/icons/tool_pad.svg"), "Convert to Custom Pad", this, &FootprintEditor::onConvertToPad);
        menu.addSeparator();
    }

    if (item) {
        menu.addAction(QIcon(":/icons/tool_delete.svg"), "Delete", this, &FootprintEditor::onDelete);
    }

    menu.exec(m_view->mapToGlobal(pos));
}

void FootprintEditor::onConvertToPad() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    bool anyConverted = false;
    QList<int> convertibleIndices;
    QList<QPointF> mergedPoints;
    for (auto* item : selected) {
        int idx = m_drawnItems.indexOf(item);
        if (idx == -1) continue;
        if (idx < 0 || idx >= m_footprint.primitives().size()) continue;
        const FootprintPrimitive& prim = m_footprint.primitives().at(idx);
        const QList<QPointF> points = primitiveToPadPolygonPoints(prim);
        if (points.size() >= 3) {
            convertibleIndices.append(idx);
            for (const QPointF& p : points) mergedPoints.append(p);
        }
    }

    if (convertibleIndices.isEmpty()) return;

    std::sort(convertibleIndices.begin(), convertibleIndices.end());
    convertibleIndices.erase(std::unique(convertibleIndices.begin(), convertibleIndices.end()), convertibleIndices.end());

    QList<QPointF> finalPoints;
    if (convertibleIndices.size() == 1) {
        finalPoints = primitiveToPadPolygonPoints(m_footprint.primitives().at(convertibleIndices.first()));
    } else {
        finalPoints = convexHull2D(mergedPoints);
    }
    if (finalPoints.size() < 3) return;

    const QString nextNum = getNextPadNumber();
    FootprintPrimitive pad = FootprintPrimitive::createPolygonPad(finalPoints, nextNum);
    pad.layer = FootprintPrimitive::Top_Copper;

    const int keepIdx = convertibleIndices.first();
    m_footprint.primitives()[keepIdx] = pad;
    for (int i = convertibleIndices.size() - 1; i >= 0; --i) {
        const int idx = convertibleIndices.at(i);
        if (idx != keepIdx) m_footprint.removePrimitive(idx);
    }
    anyConverted = true;

    if (anyConverted) {
        updateSceneFromDefinition();
        if (convertibleIndices.size() > 1) {
            m_statusLabel->setText("Converted selected shapes to one combined custom pad.");
        } else {
            m_statusLabel->setText("Converted selection to custom pad.");
        }
    }
}
