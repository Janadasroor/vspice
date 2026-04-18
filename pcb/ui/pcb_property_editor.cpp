#include "pcb_property_editor.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/component_item.h"
#include "../items/pad_item.h"
#include "../items/via_item.h"
#include "../items/copper_pour_item.h"
#include "../items/shape_item.h"
#include "../items/image_item.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGraphicsScene>
#include <QPainter>
#include <QStyleOption>
#include <QSet>

namespace Flux {

// --- PreviewWidget ---

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    setFixedHeight(120);
    setStyleSheet("background: #18181b; border-bottom: 1px solid #27272a;");
}

void PreviewWidget::setItem(PCBItem* item) {
    m_item = item;
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    if (!m_item) {
        painter.setPen(QColor(113, 113, 122));
        painter.drawText(rect(), Qt::AlignCenter, "No selection");
        return;
    }

    painter.translate(width() / 2, height() / 2);
    
    // Scale to fit
    painter.scale(2.0, 2.0);

    QPen p(QColor(59, 130, 246), 2);
    painter.setPen(p);
    painter.setBrush(QColor(59, 130, 246, 40));

    if (auto* trace = dynamic_cast<TraceItem*>(m_item)) {
        painter.drawLine(QPointF(-20, 0), QPointF(20, 0));
    } else if (auto* via = dynamic_cast<ViaItem*>(m_item)) {
        painter.drawEllipse(QPointF(0, 0), 10, 10);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(0, 0), 5, 5);
    } else {
        painter.drawRect(-15, -15, 30, 30);
    }
}

// --- PropertyRow ---

PropertyRow::PropertyRow(const QString& label, QWidget* editor, QWidget* parent)
    : QWidget(parent), m_label(label) {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 4, 16, 4);
    layout->setSpacing(8);

    QLabel* labelWidget = new QLabel(label);
    labelWidget->setStyleSheet("color: #a1a1aa; font-size: 12px;");
    labelWidget->setFixedWidth(120);
    
    layout->addWidget(labelWidget);
    layout->addWidget(editor, 1);
    
    setFixedHeight(36);
}

void PropertyRow::setVisible(bool visible) {
    QWidget::setVisible(visible);
}

// --- PropertySection ---

PropertySection::PropertySection(const QString& title, QWidget* parent)
    : QWidget(parent), m_title(title) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_headerBtn = new QPushButton(title.toUpper());
    m_headerBtn->setCheckable(true);
    m_headerBtn->setChecked(true);
    m_headerBtn->setStyleSheet(
        "QPushButton {"
        "   background: #1a1a1a;"
        "   color: #a1a1aa;"
        "   border: none;"
        "   border-top: 1px solid #333333;"
        "   border-bottom: 1px solid #121212;"
        "   text-align: left;"
        "   padding: 8px 12px;"
        "   font-size: 9px;"
        "   font-weight: 700;"
        "   letter-spacing: 0.8px;"
        "}"
        "QPushButton:hover { background: #27272a; color: #ffffff; }"
        "QPushButton:checked { color: #3b82f6; }"
    );

    m_container = new QWidget();
    m_rowsLayout = new QVBoxLayout(m_container);
    m_rowsLayout->setContentsMargins(0, 4, 0, 8);
    m_rowsLayout->setSpacing(0);

    layout->addWidget(m_headerBtn);
    layout->addWidget(m_container);

    connect(m_headerBtn, &QPushButton::toggled, m_container, &QWidget::setVisible);
}

void PropertySection::addRow(PropertyRow* row) {
    m_rowsLayout->addWidget(row);
}

void PropertySection::clear() {
    QLayoutItem* item;
    while ((item = m_rowsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }
}

void PropertySection::setTitle(const QString& title) {
    m_headerBtn->setText(title.toUpper());
}

void PropertySection::filterRows(const QString& filter) {
    bool anyVisible = false;
    for (int i = 0; i < m_rowsLayout->count(); ++i) {
        QWidget* w = m_rowsLayout->itemAt(i)->widget();
        if (auto* row = qobject_cast<PropertyRow*>(w)) {
            bool match = row->label().contains(filter, Qt::CaseInsensitive);
            row->setVisible(match);
            if (match) anyVisible = true;
        }
    }
    setVisible(anyVisible || filter.isEmpty());
}

// --- PCBPropertyEditor ---

PCBPropertyEditor::PCBPropertyEditor(QWidget* parent)
    : QWidget(parent) {
    setupUi();
}

void PCBPropertyEditor::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setStyleSheet("background: #09090b; border-left: 1px solid #27272a;");

    // Header with Search
    QWidget* header = new QWidget();
    header->setFixedHeight(70);
    header->setStyleSheet("background-color: #1a1a1a; border-bottom: 1px solid #333333;");
    QVBoxLayout* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(12, 10, 12, 10);
    headerLayout->setSpacing(6);
    
    QLabel* title = new QLabel("PROPERTIES");
    title->setStyleSheet("color: #71717a; font-weight: 800; font-size: 10px; letter-spacing: 1.2px;");
    
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Search properties...");
    m_searchEdit->setStyleSheet(
        "QLineEdit {"
        "   background: #121212;"
        "   color: #e4e4e7;"
        "   border: 1px solid #333333;"
        "   border-radius: 4px;"
        "   padding: 5px 10px;"
        "   font-size: 11px;"
        "}"
        "QLineEdit:focus { border-color: #3b82f6; }"
    );
    connect(m_searchEdit, &QLineEdit::textChanged, this, &PCBPropertyEditor::onSearchChanged);

    headerLayout->addWidget(title);
    headerLayout->addWidget(m_searchEdit);
    layout->addWidget(header);

    // Mini Preview Widget
    m_previewWidget = new PreviewWidget();
    layout->addWidget(m_previewWidget);

    // Scroll Area
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet("background: transparent;");
    
    m_contentWidget = new QWidget();
    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    m_contentLayout->addStretch();
    
    m_scrollArea->setWidget(m_contentWidget);
    layout->addWidget(m_scrollArea, 1);
}

void PCBPropertyEditor::setPCBItems(const QList<PCBItem*>& items) {
    if (m_blockSignals) return;
    
    m_items = items;
    clear();
    
    if (items.isEmpty()) return;
    
    populateProperties();
}

void PCBPropertyEditor::clear() {
    m_previewWidget->setItem(nullptr);
    for (auto* section : m_sections) {
        section->deleteLater();
    }
    m_sections.clear();
    
    // Reset layout
    QLayoutItem* item;
    while ((item = m_contentLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->hide();
        delete item;
    }
    m_contentLayout->addStretch();
}

void PCBPropertyEditor::populateProperties() {
    auto addSection = [&](const QString& title) {
        auto* section = new PropertySection(title);
        m_contentLayout->insertWidget(m_contentLayout->count() - 1, section);
        m_sections.append(section);
        return section;
    };

    auto createDoubleSpin = [&](double val, const QString& name) {
        auto* spin = new QDoubleSpinBox();
        spin->setRange(-10000, 10000);
        spin->setDecimals(3);
        spin->setValue(val);
        spin->setStyleSheet(
            "QDoubleSpinBox {"
            "   background: #18181b; color: #fff; border: 1px solid #27272a;"
            "   border-radius: 4px; padding: 2px 8px; font-size: 12px;"
            "}"
        );
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, name](double v) {
            if (!m_blockSignals) emit propertyChanged(name, v);
        });
        return spin;
    };

    auto createEdit = [&](const QString& val, const QString& name) {
        auto* edit = new QLineEdit(val);
        edit->setStyleSheet(
            "QLineEdit {"
            "   background: #18181b; color: #fff; border: 1px solid #27272a;"
            "   border-radius: 4px; padding: 2px 8px; font-size: 12px;"
            "}"
        );
        connect(edit, &QLineEdit::editingFinished, [this, edit, name]() {
            if (!m_blockSignals) emit propertyChanged(name, edit->text());
        });
        return edit;
    };

    auto createBoolCheckBox = [&](bool val, const QString& name) {
        auto* check = new QCheckBox();
        check->setChecked(val);
        check->setStyleSheet(
            "QCheckBox::indicator {"
            "   width: 16px; height: 16px; border: 1px solid #27272a;"
            "   border-radius: 4px; background: #18181b;"
            "}"
            "QCheckBox::indicator:checked {"
            "   background: #3b82f6; border-color: #3b82f6;"
            "   image: url(:/icons/check.svg);"
            "}"
        );
        connect(check, &QCheckBox::toggled, [this, name](bool v) {
            if (!m_blockSignals) emit propertyChanged(name, v);
        });
        return check;
    };

    auto createEnumCombo = [&](const QStringList& options, const QString& current, const QString& name) {
        auto* combo = new QComboBox();
        combo->addItems(options);
        combo->setCurrentText(current);
        combo->setStyleSheet(
            "QComboBox {"
            "   background: #18181b; color: #fff; border: 1px solid #27272a;"
            "   border-radius: 4px; padding: 2px 8px; font-size: 12px;"
            "}"
        );
        connect(combo, &QComboBox::currentTextChanged, [this, name](const QString& v) {
            if (!m_blockSignals) emit propertyChanged(name, v);
        });
        return combo;
    };

    auto createNetCombo = [&](const QString& current, const QString& propertyName = QString("Net")) {
        auto* combo = new QComboBox();
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->setStyleSheet(
            "QComboBox {"
            "   background: #18181b; color: #fff; border: 1px solid #27272a;"
            "   border-radius: 4px; padding: 2px 8px; font-size: 12px;"
            "}"
        );

        QSet<QString> netSet;
        for (PCBItem* pcbItem : m_items) {
            if (!pcbItem || !pcbItem->scene()) {
                continue;
            }
            const QList<QGraphicsItem*> sceneItems = pcbItem->scene()->items();
            for (QGraphicsItem* gItem : sceneItems) {
                if (auto* other = dynamic_cast<PCBItem*>(gItem)) {
                    const QString net = other->netName().trimmed();
                    if (!net.isEmpty()) {
                        netSet.insert(net);
                    }
                }
            }
        }

        QStringList nets = netSet.values();
        nets.sort(Qt::CaseInsensitive);
        if (!nets.contains("No Net")) {
            nets.prepend("No Net");
        }
        combo->addItems(nets);
        combo->setCurrentText(current.isEmpty() ? "No Net" : current);

        connect(combo, &QComboBox::currentTextChanged, [this, propertyName](const QString& v) {
            if (!m_blockSignals) {
                emit propertyChanged(propertyName, v == "No Net" ? QString() : v);
            }
        });
        return combo;
    };

    auto* item = m_items.first();
    m_previewWidget->setItem(item);

    m_blockSignals = true;

    // Identification
    auto* idSection = addSection("Identification");
    idSection->addRow(new PropertyRow("Name", createEdit(item->name(), "Name")));
    idSection->addRow(new PropertyRow("Layer", createDoubleSpin(item->layer(), "Layer"))); // Could be combo later
    idSection->addRow(new PropertyRow("Locked", createBoolCheckBox(item->isLocked(), "Locked")));

    // Geometry
    auto* geoSection = addSection("Geometry");
    geoSection->addRow(new PropertyRow("Pos X (mm)", createDoubleSpin(item->pos().x(), "Position X (mm)")));
    geoSection->addRow(new PropertyRow("Pos Y (mm)", createDoubleSpin(item->pos().y(), "Position Y (mm)")));
    geoSection->addRow(new PropertyRow("Rotation", createDoubleSpin(item->rotation(), "Rotation (deg)")));

    // Type Specific
    if (m_items.size() == 1) {
        if (auto* trace = dynamic_cast<TraceItem*>(item)) {
            auto* traceSection = addSection("Trace Parameters");
            traceSection->addRow(new PropertyRow("Net", createNetCombo(trace->netName())));
            traceSection->addRow(new PropertyRow("Width (mm)", createDoubleSpin(trace->width(), "Width (mm)")));
            traceSection->addRow(new PropertyRow("Start X", createDoubleSpin(trace->startPoint().x(), "Start X (mm)")));
            traceSection->addRow(new PropertyRow("Start Y", createDoubleSpin(trace->startPoint().y(), "Start Y (mm)")));
            traceSection->addRow(new PropertyRow("End X", createDoubleSpin(trace->endPoint().x(), "End X (mm)")));
            traceSection->addRow(new PropertyRow("End Y", createDoubleSpin(trace->endPoint().y(), "End Y (mm)")));
        } else if (auto* via = dynamic_cast<ViaItem*>(item)) {
            auto* viaElectricalSection = addSection("Electrical Properties");
            viaElectricalSection->addRow(new PropertyRow("Net", createNetCombo(via->netName())));

            auto* viaSection = addSection("Via Parameters");
            viaSection->addRow(new PropertyRow("Diameter (mm)", createDoubleSpin(via->diameter(), "Diameter (mm)")));
            viaSection->addRow(new PropertyRow("Drill (mm)", createDoubleSpin(via->drillSize(), "Drill Size (mm)")));
            viaSection->addRow(new PropertyRow("Microvia", createBoolCheckBox(via->isMicrovia(), "Microvia")));
            viaSection->addRow(new PropertyRow("Start Layer", createDoubleSpin(via->startLayer(), "Via Start Layer")));
            viaSection->addRow(new PropertyRow("End Layer", createDoubleSpin(via->endLayer(), "Via End Layer")));
        } else if (auto* pad = dynamic_cast<PadItem*>(item)) {
            auto* padElectricalSection = addSection("Electrical Properties");
            padElectricalSection->addRow(new PropertyRow("Net", createNetCombo(pad->netName())));

            auto* padSection = addSection("Pad Parameters");
            padSection->addRow(new PropertyRow("Shape", createEnumCombo({"Round", "Rect", "Oblong"}, pad->padShape(), "Pad Shape")));
            padSection->addRow(new PropertyRow("Size X (mm)", createDoubleSpin(pad->size().width(), "Size X (mm)")));
            padSection->addRow(new PropertyRow("Size Y (mm)", createDoubleSpin(pad->size().height(), "Size Y (mm)")));
            padSection->addRow(new PropertyRow("Drill (mm)", createDoubleSpin(pad->drillSize(), "Drill Size (mm)")));
        } else if (auto* comp = dynamic_cast<ComponentItem*>(item)) {
            auto* compSection = addSection("Component Info");
            compSection->addRow(new PropertyRow("Height (mm)", createDoubleSpin(comp->height(), "Height (mm)")));
            compSection->addRow(new PropertyRow("3D Model", createEdit(comp->modelPath(), "3D Model Path")));
            compSection->addRow(new PropertyRow("3D Scale", createDoubleSpin(comp->modelScale(), "3D Model Scale")));

            QList<PadItem*> pads;
            for (QGraphicsItem* child : comp->childItems()) {
                if (auto* pad = dynamic_cast<PadItem*>(child)) {
                    pads.append(pad);
                }
            }
            if (pads.size() == 1) {
                auto* compElectricalSection = addSection("Electrical Properties");
                compElectricalSection->addRow(new PropertyRow("Net", createNetCombo(pads.first()->netName(), "Component Net")));
            }
        } else if (auto* image = dynamic_cast<PCBImageItem*>(item)) {
            auto* imageSection = addSection("Image");
            imageSection->addRow(new PropertyRow("Width (mm)", createDoubleSpin(image->sizeMm().width(), "Image Width (mm)")));
            imageSection->addRow(new PropertyRow("Height (mm)", createDoubleSpin(image->sizeMm().height(), "Image Height (mm)")));
        } else if (auto* shape = dynamic_cast<PCBShapeItem*>(item)) {
            auto* shapeSection = addSection("Shape");
            shapeSection->addRow(new PropertyRow("Type", createEdit(shape->shapeKindName(), "Shape Type")));
            shapeSection->addRow(new PropertyRow("Width (mm)", createDoubleSpin(shape->sizeMm().width(), "Shape Width (mm)")));
            shapeSection->addRow(new PropertyRow("Height (mm)", createDoubleSpin(shape->sizeMm().height(), "Shape Height (mm)")));
            shapeSection->addRow(new PropertyRow("Stroke (mm)", createDoubleSpin(shape->strokeWidth(), "Shape Stroke Width (mm)")));
            if (shape->shapeKind() == PCBShapeItem::ShapeKind::Arc) {
                shapeSection->addRow(new PropertyRow("Start Angle", createDoubleSpin(shape->startAngleDeg(), "Arc Start Angle (deg)")));
                shapeSection->addRow(new PropertyRow("Span Angle", createDoubleSpin(shape->spanAngleDeg(), "Arc Span Angle (deg)")));
            }
        } else if (auto* pour = dynamic_cast<CopperPourItem*>(item)) {
            const QRectF bounds = pour->polygon().boundingRect();
            auto* zoneGeoSection = addSection("Zone Geometry");
            zoneGeoSection->addRow(new PropertyRow("Width (mm)", createDoubleSpin(bounds.width(), "Shape Width (mm)")));
            zoneGeoSection->addRow(new PropertyRow("Height (mm)", createDoubleSpin(bounds.height(), "Shape Height (mm)")));

            auto* electricalSection = addSection("Electrical Properties");
            electricalSection->addRow(new PropertyRow("Net", createNetCombo(pour->netName())));
            electricalSection->addRow(new PropertyRow("Clearance (mm)", createDoubleSpin(pour->clearance(), "Clearance (mm)")));
            electricalSection->addRow(new PropertyRow("Min Width (mm)", createDoubleSpin(pour->minWidth(), "Min Width (mm)")));
            electricalSection->addRow(new PropertyRow("Priority", createDoubleSpin(pour->priority(), "Priority")));

            auto* fillSection = addSection("Fill Properties");
            fillSection->addRow(new PropertyRow("Filled", createBoolCheckBox(pour->filled(), "Filled")));
            fillSection->addRow(new PropertyRow(
                "Pour Type",
                createEnumCombo({"Solid", "Hatch"}, pour->pourType() == CopperPourItem::PourType::SolidPour ? "Solid" : "Hatch", "Pour Type")));
            fillSection->addRow(new PropertyRow("Remove Islands", createBoolCheckBox(pour->removeIslands(), "Remove Islands")));

            auto* thermalSection = addSection("Thermal Relief");
            thermalSection->addRow(new PropertyRow("Use Thermals", createBoolCheckBox(pour->useThermalReliefs(), "Use Thermal Reliefs")));
            thermalSection->addRow(new PropertyRow("Spoke Width (mm)", createDoubleSpin(pour->thermalSpokeWidth(), "Thermal Spoke Width (mm)")));
            thermalSection->addRow(new PropertyRow("Spoke Count", createDoubleSpin(pour->thermalSpokeCount(), "Thermal Spoke Count")));
            thermalSection->addRow(new PropertyRow("Spoke Angle (deg)", createDoubleSpin(pour->thermalSpokeAngleDeg(), "Thermal Angle (deg)")));
        }
    }

    m_blockSignals = false;
}

void PCBPropertyEditor::onSearchChanged(const QString& text) {
    for (auto* section : m_sections) {
        section->filterRows(text);
    }
}

} // namespace Flux
