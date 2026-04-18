#include "generic_symbol_properties_dialog.h"
#include "../editor/schematic_commands.h"
#include "../items/generic_component_item.h"
#include "subcircuit_picker_dialog.h"
#include "assignment_validator.h"
#include "../../symbols/symbol_library.h"
#include "../../simulator/bridge/model_library_manager.h"
#include <QComboBox>
#include <QCompleter>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

Flux::Model::SymbolDefinition symbolForValidation(const SchematicItem* item) {
    const QString symbolName = item->name().trimmed();
    if (!symbolName.isEmpty()) {
        if (auto* found = SymbolLibraryManager::instance().findSymbol(symbolName)) {
            return found->clone();
        }
    }

    Flux::Model::SymbolDefinition symbol(symbolName.isEmpty() ? item->itemTypeName() : symbolName);
    symbol.setReferencePrefix(item->referencePrefix());
    symbol.setIsPowerSymbol(item->itemType() == SchematicItem::PowerType);
    if (item->itemType() == SchematicItem::ICType) {
        symbol.setCategory("Integrated Circuits");
    }

    const QList<QPointF> pins = item->connectionPoints();
    for (int i = 0; i < pins.size(); ++i) {
        symbol.addPrimitive(Flux::Model::SymbolPrimitive::createPin(pins[i], i + 1, QString::number(i + 1)));
    }

    return symbol;
}

}

GenericSymbolPropertiesDialog::GenericSymbolPropertiesDialog(SchematicItem* item, QUndoStack* undoStack, QGraphicsScene* scene, QWidget* parent)
    : SmartPropertiesDialog({item}, undoStack, scene, parent), m_item(item), m_genericItem(dynamic_cast<GenericComponentItem*>(item)), m_subcktPicker(nullptr), m_browseSubcktButton(nullptr), m_spiceModelEdit(nullptr), m_pinMappingTable(nullptr) {
    setWindowTitle(item->itemTypeName() + " Properties");
    
    PropertyTab identityTab;
    identityTab.title = "Identity";
    
    identityTab.fields.append({"reference", "Reference", PropertyField::Text});
    identityTab.fields.append({"value", "Value", PropertyField::Text});
    identityTab.fields.append({"name", "Component Name", PropertyField::Text});
    
    addTab(identityTab);
    
    PropertyTab mfrTab;
    mfrTab.title = "Manufacturer";
    mfrTab.fields.append({"manufacturer", "Manufacturer", PropertyField::Text});
    mfrTab.fields.append({"mpn", "MPN", PropertyField::Text});
    mfrTab.fields.append({"description", "Description", PropertyField::Text});
    
    addTab(mfrTab);
    
    PropertyTab visualTab;
    visualTab.title = "Appearance";
    visualTab.fields.append({"rotation", "Rotation", PropertyField::Double, 0.0, {}, "°"});
    
    addTab(visualTab);

    addSimulationTab();
    
    // Initialize values
    setPropertyValue("reference", item->reference());
    setPropertyValue("value", item->value());
    setPropertyValue("name", item->name());
    setPropertyValue("manufacturer", item->manufacturer());
    setPropertyValue("mpn", item->mpn());
    setPropertyValue("description", item->description());
    setPropertyValue("rotation", item->rotation());
}

void GenericSymbolPropertiesDialog::addSimulationTab() {
    if (!m_genericItem) return;

    const SymbolDefinition symbol = m_genericItem->symbol();
    const bool hasSubcktMetadata = !symbol.spiceNodeMapping().isEmpty() || !symbol.modelName().trimmed().isEmpty() ||
                                   !symbol.spiceModelName().trimmed().isEmpty() || !symbol.modelPath().trimmed().isEmpty();
    if (!hasSubcktMetadata) return;

    QWidget* page = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(page);

    QFormLayout* form = new QFormLayout();
    m_spiceModelEdit = new QLineEdit(page);
    m_spiceModelEdit->setPlaceholderText(symbol.modelName().isEmpty() ? symbol.spiceModelName() : symbol.modelName());
    m_spiceModelEdit->setText(m_item->spiceModel());
    form->addRow("Instance Model", m_spiceModelEdit);

    m_subcktPicker = new QComboBox(page);
    m_subcktPicker->setEditable(true);
    m_subcktPicker->setInsertPolicy(QComboBox::NoInsert);
    m_subcktPicker->addItem("Custom...", QString());
    const QVector<SpiceModelInfo> allModels = ModelLibraryManager::instance().allModels();
    QVector<SpiceModelInfo> subcktInfos;
    for (const SpiceModelInfo& info : allModels) {
        if (info.type.compare("Subcircuit", Qt::CaseInsensitive) == 0 && !info.name.trimmed().isEmpty()) {
            subcktInfos.append(info);
        }
    }
    std::sort(subcktInfos.begin(), subcktInfos.end(), [](const SpiceModelInfo& a, const SpiceModelInfo& b) {
        return a.name.toLower() < b.name.toLower();
    });
    QSet<QString> seenNames;
    for (const SpiceModelInfo& info : subcktInfos) {
        const QString modelName = info.name.trimmed();
        if (seenNames.contains(modelName.toLower())) continue;
        seenNames.insert(modelName.toLower());

        QString displayText = modelName;
        const QString libPath = info.libraryPath.trimmed();
        const QString description = info.description.trimmed();
        if (!description.isEmpty() || !libPath.isEmpty()) {
            QStringList parts;
            if (!description.isEmpty()) parts << description;
            if (!libPath.isEmpty()) parts << QFileInfo(libPath).fileName();
            displayText += QString("  (%1)").arg(parts.join(", "));
        }

        m_subcktPicker->addItem(displayText, modelName);
        const int idx = m_subcktPicker->count() - 1;
        QStringList details;
        details << modelName;
        if (!description.isEmpty()) details << description;
        if (!libPath.isEmpty()) details << QString("Library: %1").arg(libPath);
        m_subcktPicker->setItemData(idx, details.join('\n'), Qt::ToolTipRole);
    }

    if (QCompleter* completer = m_subcktPicker->completer()) {
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
    }

    const QString currentModel = m_item->spiceModel().trimmed().isEmpty()
        ? (symbol.modelName().trimmed().isEmpty() ? symbol.spiceModelName().trimmed() : symbol.modelName().trimmed())
        : m_item->spiceModel().trimmed();
    int comboIndex = -1;
    for (int i = 1; i < m_subcktPicker->count(); ++i) {
        if (m_subcktPicker->itemData(i).toString().compare(currentModel, Qt::CaseInsensitive) == 0) {
            comboIndex = i;
            break;
        }
    }
    if (comboIndex >= 0) {
        m_subcktPicker->setCurrentIndex(comboIndex);
        m_subcktPicker->setEditText(m_subcktPicker->itemText(comboIndex));
    } else {
        m_subcktPicker->setCurrentIndex(0);
        m_subcktPicker->setEditText(currentModel);
    }
    QWidget* subcktPickerRow = new QWidget(page);
    QHBoxLayout* subcktPickerLayout = new QHBoxLayout(subcktPickerRow);
    subcktPickerLayout->setContentsMargins(0, 0, 0, 0);
    subcktPickerLayout->addWidget(m_subcktPicker, 1);
    m_browseSubcktButton = new QPushButton("Browse...", subcktPickerRow);
    subcktPickerLayout->addWidget(m_browseSubcktButton);
    form->addRow("Project Subckt", subcktPickerRow);

    QLineEdit* sourcePath = new QLineEdit(page);
    sourcePath->setReadOnly(true);
    sourcePath->setText(symbol.modelPath());
    form->addRow("Library Path", sourcePath);
    layout->addLayout(form);

    m_pinMappingTable = new QTableWidget(page);
    m_pinMappingTable->setColumnCount(3);
    m_pinMappingTable->setHorizontalHeaderLabels({"Symbol Pin", "Symbol Label", "Subckt Pin"});
    m_pinMappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_pinMappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_pinMappingTable->horizontalHeader()->setStretchLastSection(true);
    m_pinMappingTable->verticalHeader()->setVisible(false);
    layout->addWidget(m_pinMappingTable);

    connect(m_subcktPicker, &QComboBox::editTextChanged, this, [this](const QString& text) {
        if (!m_spiceModelEdit || !m_subcktPicker) return;
        int matchedIndex = -1;
        for (int i = 1; i < m_subcktPicker->count(); ++i) {
            if (m_subcktPicker->itemText(i).compare(text.trimmed(), Qt::CaseInsensitive) == 0) {
                matchedIndex = i;
                break;
            }
        }
        if (matchedIndex >= 0) {
            m_spiceModelEdit->setText(m_subcktPicker->itemData(matchedIndex).toString().trimmed());
        } else {
            m_spiceModelEdit->setText(text.trimmed());
        }
    });

    connect(m_subcktPicker, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_spiceModelEdit || !m_subcktPicker) return;
        if (index == 0 && m_subcktPicker->currentText() == "Custom...") return;
        const QString modelName = m_subcktPicker->itemData(index).toString().trimmed();
        m_spiceModelEdit->setText(modelName.isEmpty() ? m_subcktPicker->currentText().trimmed() : modelName);
    });

    connect(m_browseSubcktButton, &QPushButton::clicked, this, [this]() {
        const QString currentModel = m_spiceModelEdit ? m_spiceModelEdit->text().trimmed() : QString();
        QStringList symbolPins;
        if (m_pinMappingTable) {
            for (int row = 0; row < m_pinMappingTable->rowCount(); ++row) {
                QTableWidgetItem* nameItem = m_pinMappingTable->item(row, 1);
                symbolPins << (nameItem ? nameItem->text().trimmed() : QString());
            }
        }

        SubcircuitPickerDialog dlg(currentModel, symbolPins, this);
        if (dlg.exec() != QDialog::Accepted) return;

        const QString selected = dlg.selectedModel().trimmed();
        if (selected.isEmpty()) return;
        if (m_spiceModelEdit) m_spiceModelEdit->setText(selected);

        if ((dlg.applySmartMapRequested() || dlg.applyByOrderRequested()) && m_pinMappingTable) {
            const QStringList subcktPins = dlg.applySmartMapRequested()
                ? dlg.selectedSuggestedMapping()
                : dlg.selectedSubcktPins();
            for (int row = 0; row < m_pinMappingTable->rowCount(); ++row) {
                QTableWidgetItem* mapItem = m_pinMappingTable->item(row, 2);
                if (!mapItem) continue;
                if (row < subcktPins.size()) {
                    mapItem->setText(subcktPins.at(row));
                } else {
                    mapItem->setText(QString());
                }
            }
        }

        for (int i = 1; i < m_subcktPicker->count(); ++i) {
            if (m_subcktPicker->itemData(i).toString().compare(selected, Qt::CaseInsensitive) == 0) {
                m_subcktPicker->setCurrentIndex(i);
                return;
            }
        }
        m_subcktPicker->setCurrentIndex(0);
        m_subcktPicker->setEditText(selected);
    });

    QList<SymbolPrimitive> pins;
    for (const SymbolPrimitive& prim : symbol.effectivePrimitives()) {
        if (prim.type == SymbolPrimitive::Pin) pins.append(prim);
    }
    std::sort(pins.begin(), pins.end(), [](const SymbolPrimitive& a, const SymbolPrimitive& b) {
        return a.data.value("number").toInt() < b.data.value("number").toInt();
    });

    const QMap<QString, QString> instanceMap = m_item->pinPadMapping();
    const QMap<int, QString> symbolMap = symbol.spiceNodeMapping();
    m_pinMappingTable->setRowCount(pins.size());
    for (int row = 0; row < pins.size(); ++row) {
        const SymbolPrimitive& pin = pins.at(row);
        const QString pinNumber = QString::number(pin.data.value("number").toInt());
        const QString pinName = pin.data.value("name").toString();
        const QString mappedSubcktPin = instanceMap.value(pinNumber, symbolMap.value(pinNumber.toInt(), pinName));

        QTableWidgetItem* numItem = new QTableWidgetItem(pinNumber);
        numItem->setFlags(numItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem* nameItem = new QTableWidgetItem(pinName);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem* mapItem = new QTableWidgetItem(mappedSubcktPin);

        m_pinMappingTable->setItem(row, 0, numItem);
        m_pinMappingTable->setItem(row, 1, nameItem);
        m_pinMappingTable->setItem(row, 2, mapItem);
    }

    m_tabWidget->addTab(page, "Simulation");
}

QMap<QString, QString> GenericSymbolPropertiesDialog::pinMappingFromTable() const {
    QMap<QString, QString> mapping;
    if (!m_pinMappingTable) return mapping;

    for (int row = 0; row < m_pinMappingTable->rowCount(); ++row) {
        QTableWidgetItem* pinNumberItem = m_pinMappingTable->item(row, 0);
        QTableWidgetItem* subcktPinItem = m_pinMappingTable->item(row, 2);
        if (!pinNumberItem || !subcktPinItem) continue;
        const QString pinNumber = pinNumberItem->text().trimmed();
        const QString subcktPin = subcktPinItem->text().trimmed();
        if (!pinNumber.isEmpty() && !subcktPin.isEmpty()) {
            mapping.insert(pinNumber, subcktPin);
        }
    }
    return mapping;
}

void GenericSymbolPropertiesDialog::onApply() {
    if (!validateAll()) return;
    if (!m_undoStack || !m_scene || m_items.isEmpty()) return;

    const QString newRef = getPropertyValue("reference").toString();
    const QString newVal = getPropertyValue("value").toString();
    const double newRot = getPropertyValue("rotation").toDouble();
    const QString newSpiceModel = m_spiceModelEdit ? m_spiceModelEdit->text().trimmed() : QString();
    const QMap<QString, QString> newPinMapping = pinMappingFromTable();

    m_undoStack->beginMacro("Update Component Properties");
    for (auto* item : m_items) {
        QJsonObject newState = item->toJson();
        if (m_items.size() == 1) newState["reference"] = newRef;
        newState["value"] = newVal;
        newState["rotation"] = newRot;
        if (m_spiceModelEdit) newState["spiceModel"] = newSpiceModel;

        QJsonObject pinMapObj;
        for (auto it = newPinMapping.constBegin(); it != newPinMapping.constEnd(); ++it) {
            pinMapObj[it.key()] = it.value();
        }
        if (m_pinMappingTable) newState["pinPadMapping"] = pinMapObj;

        m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
    }
    m_undoStack->endMacro();
}
