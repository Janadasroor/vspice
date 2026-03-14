#include "property_editor.h"
#include "schematic_item.h"
#include "power_item.h"
#include "voltage_source_item.h"
#include "schematic_shape_item.h"
#include "schematic_text_item.h"
#include "net_label_item.h"
#include "net_class.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QColorDialog>
#include <QFileDialog>
#include <QDebug>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFrame>

PropertyEditor::PropertyEditor(QWidget *parent)
    : QWidget(parent)
    , m_blockSignals(false)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Property", "Value"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setStyleSheet(
        "QTableWidget {"
        " background-color: #1e1e1e;"
        " alternate-background-color: #252526;"
        " color: #d4d4d4;"
        " border: none;"
        " gridline-color: #333333;"
        " selection-background-color: #264f78;"
        " selection-color: #ffffff;"
        "}"
        "QTableWidget::item { background: transparent; color: #d4d4d4; padding: 4px; border: none; }"
        "QTableWidget::item:selected { background-color: #264f78; color: #ffffff; }"
        "QHeaderView::section { background-color: #2d2d2d; color: #888888; border: 1px solid #333333; padding: 4px; }"
    );

    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::cellChanged, this, &PropertyEditor::onCellChanged);
}

PropertyEditor::~PropertyEditor() {}

void PropertyEditor::clear() {
    m_blockSignals = true;
    m_table->setRowCount(0);
    m_schematicItems.clear();
    m_blockSignals = false;
}

void PropertyEditor::onBoolToggled(bool checked) {
    if (m_blockSignals) return;
    // Implementation depends on how we want to handle generic bool toggles
    // For now, it's a placeholder for the signal-slot connection
}

void PropertyEditor::setSchematicItems(const QList<SchematicItem*>& items) {
    m_schematicItems = items;
    m_blockSignals = true;
    m_table->blockSignals(true);
    m_table->setRowCount(0);
    m_table->blockSignals(false);

    if (items.isEmpty()) {
        m_blockSignals = false;
        return;
    }
    
    SchematicItem* item = items.first();
    
    auto getCommonValue = [&](auto getter) -> QVariant {
        if (items.size() == 1) return getter(items.first());
        QString val = getter(items.first());
        for (int i = 1; i < items.size(); ++i) {
            if (getter(items[i]) != val) return QVariant("<multiple>");
        }
        return val;
    };

    auto getCommonBool = [&](auto getter) -> QVariant {
        if (items.size() == 1) return getter(items.first());
        bool val = getter(items.first());
        for (int i = 1; i < items.size(); ++i) {
            if (getter(items[i]) != val) return QVariant("<multiple>");
        }
        return val;
    };

    auto getCommonDouble = [&](auto getter) -> QVariant {
        if (items.size() == 1) return getter(items.first());
        double val = getter(items.first());
        for (int i = 1; i < items.size(); ++i) {
            if (qAbs(getter(items[i]) - val) > 0.001) return QVariant("<multiple>");
        }
        return val;
    };

    // Identification
    addSectionHeader("Identification");
    addProperty("Name", getCommonValue([](SchematicItem* it){ return it->name(); }));
    addProperty("Reference", getCommonValue([](SchematicItem* it){ return it->reference(); }));
    addProperty("Value", getCommonValue([](SchematicItem* it){ return it->value(); }));
    addProperty("Exclude from Simulation", getCommonBool([](SchematicItem* it){ return it->excludeFromSimulation(); }));

    // Appearance
    addSectionHeader("Appearance");
    addProperty("Rotation", getCommonDouble([](SchematicItem* it){ return it->rotation(); }));
    
    // Placement
    addSectionHeader("Placement");
    addProperty("Position X", getCommonDouble([](SchematicItem* it){ return it->pos().x(); }));
    addProperty("Position Y", getCommonDouble([](SchematicItem* it){ return it->pos().y(); }));
    addProperty("Locked", getCommonBool([](SchematicItem* it){ return it->isLocked(); }));
    addProperty("Mirrored", getCommonBool([](SchematicItem* it){ return it->isMirroredX(); }));

    if (items.size() == 1) {
        if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(item)) {
            addSectionHeader("Voltage Source");
            addProperty("Source Type", vsrc->sourceType() == VoltageSourceItem::DC ? "DC" : "AC", "enum|DC,AC");
            if (vsrc->sourceType() == VoltageSourceItem::DC) {
                addProperty("DC Voltage", vsrc->dcVoltage());
            } else {
                addProperty("AC Amplitude", vsrc->acAmplitude());
                addProperty("AC Frequency", vsrc->sineFrequency());
                addProperty("AC Phase", vsrc->acPhase());
                addProperty("AC Offset", vsrc->sineOffset());
            }
        } else if (auto* pwr = dynamic_cast<PowerItem*>(item)) {
            addSectionHeader("Power Settings");
            addProperty("Power Name", pwr->netName());
        } else if (auto* shape = dynamic_cast<SchematicShapeItem*>(item)) {
            addSectionHeader("Shape Geometry");
            if (shape->shapeType() == SchematicShapeItem::Bezier && shape->points().size() == 4) {
                QList<QPointF> pts = shape->points();
                addProperty("Start X", pts[0].x()); addProperty("Start Y", pts[0].y());
                addProperty("Ctrl1 X", pts[1].x()); addProperty("Ctrl1 Y", pts[1].y());
                addProperty("Ctrl2 X", pts[2].x()); addProperty("Ctrl2 Y", pts[2].y());
                addProperty("End X",   pts[3].x()); addProperty("End Y",   pts[3].y());
            } else {
                addProperty("Start X", QString::number(shape->startPoint().x(), 'f', 2));
                addProperty("Start Y", QString::number(shape->startPoint().y(), 'f', 2));
                addProperty("End X", QString::number(shape->endPoint().x(), 'f', 2));
                addProperty("End Y", QString::number(shape->endPoint().y(), 'f', 2));
            }
            addSectionHeader("Visual Style");
            addProperty("Width", QString::number(shape->pen().widthF(), 'f', 1));
            addProperty("Color", shape->pen().color().name());
            addProperty("Fill Color", shape->brush().color().name());
            QString currentStyle = "Solid";
            if (shape->pen().style() == Qt::DashLine) currentStyle = "Dash";
            else if (shape->pen().style() == Qt::DotLine) currentStyle = "Dot";
            addProperty("Line Style", currentStyle, "enum|Solid,Dash,Dot");
        } else if (auto* textItem = dynamic_cast<SchematicTextItem*>(item)) {
            addSectionHeader("Text Content");
            addProperty("Text", textItem->text());
            addSectionHeader("Text Style");
            addProperty("Font Size", QString::number(textItem->font().pointSize()));
            addProperty("Color", textItem->color().name());
            addProperty("Bold", textItem->font().bold() ? "True" : "False");
            addProperty("Italic", textItem->font().italic() ? "True" : "False");
            QString alignStr = "Left";
            if (textItem->alignment() == Qt::AlignCenter) alignStr = "Center";
            else if (textItem->alignment() == Qt::AlignRight) alignStr = "Right";
            addProperty("Alignment", alignStr, "enum|Left,Center,Right");
        } else if (auto* netLabel = dynamic_cast<NetLabelItem*>(item)) {
            addSectionHeader("Net Class");
            QStringList classes;
            for (const NetClass& nc : NetClassManager::instance().classes()) classes << nc.name;
            if (classes.isEmpty()) classes << "Default";
            classes.removeDuplicates();
            QString current = netLabel->netClassName().trimmed();
            if (current.isEmpty()) current = "Default";
            if (!classes.contains(current)) classes.prepend(current);
            addProperty("Net Class", current, "enum|" + classes.join(","));
        }
    }
    
    m_blockSignals = false;
}

void PropertyEditor::addSectionHeader(const QString &title) {
    int row = m_table->rowCount();
    m_table->insertRow(row);
    auto *item = new QTableWidgetItem(title);
    item->setBackground(QColor("#2d2d2d"));
    item->setForeground(QColor("#569cd6"));
    item->setFont(QFont("Inter", 10, QFont::Bold));
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    m_table->setItem(row, 0, item);
    m_table->setSpan(row, 0, 1, 2);
}

void PropertyEditor::addProperty(const QString &name, const QVariant &value, const QString &hints) {
    int row = m_table->rowCount();
    m_table->insertRow(row);
    const QColor rowBg = (row % 2 == 0) ? QColor("#1e1e1e") : QColor("#252526");
    
    auto *nameItem = new QTableWidgetItem(name);
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    nameItem->setBackground(rowBg);
    nameItem->setForeground(QColor("#d4d4d4"));
    m_table->setItem(row, 0, nameItem);

    if (hints.startsWith("enum|")) {
        auto *combo = new QComboBox(this);
        QStringList options = hints.mid(5).split(",");
        combo->addItems(options);
        combo->setCurrentText(value.toString());
        combo->setStyleSheet(
            "QComboBox {"
            " background-color: #2d2d2d;"
            " color: #d4d4d4;"
            " border: 1px solid #333;"
            " padding: 2px;"
            "}"
            "QComboBox QAbstractItemView {"
            " background-color: #1e1e1e;"
            " color: #d4d4d4;"
            " selection-background-color: #264f78;"
            "}"
        );
        connect(combo, &QComboBox::currentTextChanged, this, [this, name](const QString &text) {
            if (!m_blockSignals) emit propertyChanged(name, text);
        });
        m_table->setCellWidget(row, 1, combo);
    } else if (value.typeId() == QMetaType::Bool || value.toString() == "True" || value.toString() == "False") {
        auto *check = new QCheckBox(this);
        check->setChecked(value.toBool() || value.toString() == "True");
        check->setStyleSheet(
            "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #8a8a8a; border-radius: 2px; background: #1f1f1f; }"
            "QCheckBox::indicator:hover { border-color: #569cd6; }"
            "QCheckBox::indicator:checked { border-color: #569cd6; background: #569cd6; image: url(:/icons/check.svg); }"
        );
        connect(check, &QCheckBox::stateChanged, this, [this, name](int state) {
            if (!m_blockSignals) emit propertyChanged(name, state == Qt::Checked);
        });
        m_table->setCellWidget(row, 1, check);
    } else {
        auto *valItem = new QTableWidgetItem(value.toString());
        valItem->setBackground(rowBg);
        valItem->setForeground(QColor("#d4d4d4"));
        if (value.toString() == "<multiple>") {
            valItem->setForeground(QColor("#8a8a8a"));
            valItem->setToolTip("Multiple values selected");
        }
        m_table->setItem(row, 1, valItem);
    }
}

void PropertyEditor::onCellChanged(int row, int column) {
    if (m_blockSignals || column != 1) return;
    QString propName = m_table->item(row, 0)->text();
    emit propertyChanged(propName, m_table->item(row, column)->text());
}
