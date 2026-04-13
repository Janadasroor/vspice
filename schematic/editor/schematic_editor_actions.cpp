// schematic_editor_actions.cpp
// Clipboard, selection, undo/redo, and property editing actions for SchematicEditor

#include "schematic_editor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "../../core/config_manager.h"
#include "schematic_item.h"
#include "schematic_commands.h"
#include "schematic_file_io.h"
#include "../items/schematic_text_item.h"
#include "../items/schematic_shape_item.h"
#include "../items/schematic_spice_directive_item.h"
#include "../items/schematic_sheet_item.h"
#include "../items/power_item.h"
#include "../items/voltage_source_item.h"
#include "../items/current_source_item.h"
#include "../items/net_label_item.h"
#include "../items/oscilloscope_item.h"
#include "../items/signal_generator_item.h"
#include "../items/switch_item.h"
#include "../items/led_item.h"
#include "../items/smart_signal_item.h"
#include "../ui/logic_editor_panel.h"
#include "../ui/logic_analyzer_window.h"
#include "../ui/schematic_components_widget.h"
#include "../ui/library_browser_dialog.h"
#include "../../ui/help_window.h"
#include "../../ui/developer_help_window.h"
#include "../../core/ui/project_audit_dialog.h"
#include "../dialogs/power_nets_manager_dialog.h"
#include "../dialogs/component_properties_dialog.h"
#include "../dialogs/title_block_dialog.h"
#include "../dialogs/wire_properties_dialog.h"
#include "../dialogs/sheet_properties_dialog.h"
#include "../dialogs/bus_properties_dialog.h"
#include "../dialogs/net_label_properties_dialog.h"
#include "../dialogs/passive_properties_dialog.h"
#include "../dialogs/passive_model_properties_dialog.h"
#include "../dialogs/generic_symbol_properties_dialog.h"
#include "../dialogs/schematic_text_properties_dialog.h"
#include "../dialogs/voltage_source_properties_dialog.h"
#include "../dialogs/voltage_source_ltspice_dialog.h"
#include "../dialogs/current_source_properties_dialog.h"
#include "../dialogs/behavioral_current_source_dialog.h"
#include "../items/behavioral_current_source_item.h"
#include "../dialogs/current_source_ltspice_dialog.h"
#include "../dialogs/spice_directive_dialog.h"
#include "../dialogs/spice_subcircuit_import_dialog.h"
#include "../dialogs/signal_generator_properties_dialog.h"
#include "../dialogs/batch_edit_dialog.h"
#include "../dialogs/led_properties_dialog.h"
#include "../dialogs/switch_properties_dialog.h"
#include "../items/tuning_slider_symbol_item.h"
#include "../dialogs/tuning_slider_properties_dialog.h"
#include "../dialogs/design_rule_editor.h"
#include "../dialogs/voltage_controlled_switch_dialog.h"
#include "../dialogs/csw_properties_dialog.h"
#include "../dialogs/vcvs_properties_dialog.h"
#include "../dialogs/cccs_properties_dialog.h"
#include "../dialogs/ccvs_properties_dialog.h"
#include "../dialogs/transmission_line_properties_dialog.h"
#include "../dialogs/diode_model_picker_dialog.h"
#include "../dialogs/diode_properties_dialog.h"
#include "../dialogs/bjt_properties_dialog.h"
#include "../dialogs/jfet_properties_dialog.h"
#include "../dialogs/mos_properties_dialog.h"
#include "../dialogs/mesfet_properties_dialog.h"
#include "../items/generic_component_item.h"
#include "../dialogs/component_label_properties_dialog.h"
#include "../items/voltage_controlled_switch_item.h"
#include "../dialogs/oscilloscope_properties_dialog.h"
#include "../dialogs/erc_rules_dialog.h"
#include "../ui/simulation_panel.h"
#include "../dialogs/find_replace_dialog.h"
#include "../../core/assignment_validator.h"
#include "../../core/ui/command_palette.h"
#include "../../core/sync_manager.h"
#include "../../core/config_manager.h"
#include "../../symbols/symbol_library.h"
#include "../../symbols/symbol_editor.h"
#include "../../ui/spice_model_architect.h"
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QClipboard>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>
#include <QStatusBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QQueue>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <QTreeWidget>
#include <QJsonDocument>
#include <QVector>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

namespace {
QList<SchematicItem*> uniqueTopLevelSchematicItems(const QList<QGraphicsItem*>& graphicsItems) {
    QList<SchematicItem*> result;
    QSet<SchematicItem*> seen;

    for (QGraphicsItem* graphicsItem : graphicsItems) {
        SchematicItem* schematicItem = nullptr;
        QGraphicsItem* current = graphicsItem;
        while (current) {
            schematicItem = dynamic_cast<SchematicItem*>(current);
            if (schematicItem && !schematicItem->isSubItem()) break;
            current = current->parentItem();
        }
        if (!schematicItem || seen.contains(schematicItem)) continue;
        seen.insert(schematicItem);
        result.append(schematicItem);
    }

    return result;
}

void offsetPointArray(QJsonArray& points, const QPointF& delta) {
    for (int i = 0; i < points.size(); ++i) {
        QJsonObject point = points[i].toObject();
        point["x"] = point["x"].toDouble() + delta.x();
        point["y"] = point["y"].toDouble() + delta.y();
        points[i] = point;
    }
}

void offsetItemJson(QJsonObject& itemJson, const QPointF& delta) {
    if (itemJson.contains("x")) itemJson["x"] = itemJson["x"].toDouble() + delta.x();
    if (itemJson.contains("y")) itemJson["y"] = itemJson["y"].toDouble() + delta.y();

    if (itemJson.contains("points")) {
        QJsonArray points = itemJson["points"].toArray();
        offsetPointArray(points, delta);
        itemJson["points"] = points;
    }

    if (itemJson.contains("junctions")) {
        QJsonArray junctions = itemJson["junctions"].toArray();
        offsetPointArray(junctions, delta);
        itemJson["junctions"] = junctions;
    }

    if (itemJson.contains("start_x")) itemJson["start_x"] = itemJson["start_x"].toDouble() + delta.x();
    if (itemJson.contains("start_y")) itemJson["start_y"] = itemJson["start_y"].toDouble() + delta.y();
    if (itemJson.contains("end_x")) itemJson["end_x"] = itemJson["end_x"].toDouble() + delta.x();
    if (itemJson.contains("end_y")) itemJson["end_y"] = itemJson["end_y"].toDouble() + delta.y();
}
}

static QString defaultPowerNetFromType(int powerType) {
    switch (powerType) {
        case 0: return "GND";
        case 1: return "VCC";
        case 2: return "VDD";
        case 3: return "VSS";
        case 4: return "VBAT";
        case 5: return "+3.3V";
        case 6: return "+5V";
        case 7: return "+12V";
        default: return "PWR";
    }
}

static QString powerNetNameFromJson(const QJsonObject& obj) {
    const QString value = obj.value("value").toString().trimmed();
    if (!value.isEmpty()) return value;
    const QString name = obj.value("name").toString().trimmed();
    if (!name.isEmpty()) return name;
    return defaultPowerNetFromType(obj.value("power_type").toInt(-1));
}

static QString displayPath(const QString& absolutePath, const QString& projectDir) {
    if (projectDir.isEmpty()) return QFileInfo(absolutePath).fileName();
    QDir project(projectDir);
    const QString rel = project.relativeFilePath(absolutePath);
    return rel.startsWith("..") ? QFileInfo(absolutePath).fileName() : rel;
}

static QString decodeSpiceImportText(const QByteArray& raw) {
    if (raw.isEmpty()) return QString();

    auto decodeUtf16Le = [](const QByteArray& bytes, int start) {
        QVector<char16_t> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const char16_t ch = static_cast<char16_t>(static_cast<unsigned char>(bytes[i])) |
                                (static_cast<char16_t>(static_cast<unsigned char>(bytes[i + 1])) << 8);
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    auto decodeUtf16Be = [](const QByteArray& bytes, int start) {
        QVector<char16_t> u16;
        u16.reserve((bytes.size() - start) / 2);
        for (int i = start; i + 1 < bytes.size(); i += 2) {
            const char16_t ch = (static_cast<char16_t>(static_cast<unsigned char>(bytes[i])) << 8) |
                                static_cast<char16_t>(static_cast<unsigned char>(bytes[i + 1]));
            u16.push_back(ch);
        }
        return QString::fromUtf16(u16.constData(), u16.size());
    };

    if (raw.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(raw[0]);
        const unsigned char b1 = static_cast<unsigned char>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) return decodeUtf16Le(raw, 2);
        if (b0 == 0xFE && b1 == 0xFF) return decodeUtf16Be(raw, 2);
    }

    QString decoded = QString::fromUtf8(raw);
    if (decoded.contains(QChar::ReplacementCharacter)) {
        decoded = QString::fromLatin1(raw);
    }
    return decoded;
}

static QSet<QString> collectHierarchicalSheets(const QString& rootFilePath) {
    QSet<QString> result;
    if (rootFilePath.isEmpty()) return result;

    QQueue<QString> queue;
    const QString rootAbs = QFileInfo(rootFilePath).absoluteFilePath();
    queue.enqueue(rootAbs);
    result.insert(rootAbs);

    while (!queue.isEmpty()) {
        const QString filePath = queue.dequeue();
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) continue;

        const QJsonArray items = doc.object().value("items").toArray();
        const QString baseDir = QFileInfo(filePath).absolutePath();
        for (const QJsonValue& val : items) {
            const QJsonObject obj = val.toObject();
            if (obj.value("type").toString() != "Sheet") continue;
            const QString linked = obj.value("fileName").toString().trimmed();
            if (linked.isEmpty()) continue;
            const QString abs = QFileInfo(QDir(baseDir).filePath(linked)).absoluteFilePath();
            if (!QFile::exists(abs) || result.contains(abs)) continue;
            result.insert(abs);
            queue.enqueue(abs);
        }
    }

    return result;
}

static int renamePowerNetInSchematicFile(const QString& filePath, const QString& oldName, const QString& newName) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return 0;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return 0;

    QJsonObject root = doc.object();
    QJsonArray items = root.value("items").toArray();
    int changedCount = 0;

    for (int i = 0; i < items.size(); ++i) {
        QJsonObject obj = items[i].toObject();
        if (obj.value("type").toString() != "Power") continue;
        if (powerNetNameFromJson(obj) != oldName) continue;
        obj["name"] = newName;
        obj["value"] = newName;
        items[i] = obj;
        ++changedCount;
    }

    if (changedCount <= 0) return 0;

    root["items"] = items;
    doc.setObject(root);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 0;
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return changedCount;
}

static SymbolDefinition buildImportedSubcktSymbol(const SpiceSubcircuitImportDialog::Result& res) {
    SymbolDefinition def(res.subcktName);
    def.setDescription(QString("Auto-generated symbol for .subckt %1").arg(res.subcktName));
    def.setCategory("Integrated Circuits");
    def.setReferencePrefix("U");
    def.setDefaultValue(res.subcktName);
    def.setModelSource("project");
    def.setModelPath(res.relativeIncludePath);
    def.setModelName(res.subcktName);
    def.setSpiceModelName(res.subcktName);

    QMap<int, QString> mapping;
    QList<SpiceSubcircuitImportDialog::Result::PinMapping> pinMappings = res.pinMappings;
    if (pinMappings.isEmpty()) {
        for (int i = 0; i < res.pins.size(); ++i) {
            SpiceSubcircuitImportDialog::Result::PinMapping mappingEntry;
            mappingEntry.subcktPin = res.pins.at(i);
            mappingEntry.symbolPinName = res.pins.at(i);
            mappingEntry.symbolPinNumber = i + 1;
            pinMappings.append(mappingEntry);
        }
    }

    std::sort(pinMappings.begin(), pinMappings.end(), [](const auto& a, const auto& b) {
        return a.symbolPinNumber < b.symbolPinNumber;
    });

    const int pinCount = pinMappings.size();
    const int leftCount = (pinCount + 1) / 2;
    const int rightCount = pinCount - leftCount;
    const int pinsPerSide = qMax(leftCount, rightCount);
    const qreal bodyWidth = 120.0;
    const qreal pinSpacing = 25.0;
    const qreal pinLength = 20.0;
    const qreal bodyHalfHeight = qMax<qreal>(40.0, pinsPerSide * pinSpacing * 0.5);

    def.addPrimitive(SymbolPrimitive::createRect(QRectF(-bodyWidth / 2.0, -bodyHalfHeight, bodyWidth, bodyHalfHeight * 2.0), false));
    def.addPrimitive(SymbolPrimitive::createText(res.subcktName, QPointF(-30.0, -bodyHalfHeight - 18.0), 10));

    for (int i = 0; i < pinCount; ++i) {
        const auto& pinMapping = pinMappings.at(i);
        const bool leftSide = (i < leftCount);
        const int sideIndex = leftSide ? i : (i - leftCount);
        const int countOnSide = leftSide ? leftCount : (pinCount - leftCount);
        const int displayIndex = leftSide ? sideIndex : (countOnSide - 1 - sideIndex);
        const qreal y = ((countOnSide - 1) * pinSpacing * -0.5) + (displayIndex * pinSpacing);
        const QPointF pos(leftSide ? (-bodyWidth / 2.0 - pinLength) : (bodyWidth / 2.0 + pinLength), y);
        const QString orientation = leftSide ? "Right" : "Left";
        const int symbolPinNumber = pinMapping.symbolPinNumber;
        const QString subcktPinName = pinMapping.subcktPin;
        const QString symbolPinName = pinMapping.symbolPinName.isEmpty() ? subcktPinName : pinMapping.symbolPinName;

        SymbolPrimitive pin = SymbolPrimitive::createPin(pos, symbolPinNumber, symbolPinName, orientation, pinLength);
        def.addPrimitive(pin);
        mapping.insert(symbolPinNumber, subcktPinName);
    }

    def.setSpiceNodeMapping(mapping);
    return def;
}

// ─── Sheet Hierarchy Panel ───────────────────────────────────────────────────

void SchematicEditor::buildHierarchyTree(QTreeWidgetItem* parentItem,
                                          const QString& filePath,
                                          int depth)
{
    // Guard against runaway recursion and cycles
    if (depth > 20 || filePath.isEmpty()) return;

    // Track visited paths to prevent infinite loops (cycles in hierarchy)
    static thread_local QSet<QString> visitedPaths;
    if (visitedPaths.contains(filePath)) return;
    visitedPaths.insert(filePath);

    // Read the schematic file to find child Sheet items
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    QJsonArray items = doc.object().value("items").toArray();
    QString baseDir = QFileInfo(filePath).absolutePath();

    for (const QJsonValue& val : items) {
        QJsonObject obj = val.toObject();
        if (obj.value("type").toString() != "Sheet") continue;

        QString sheetName = obj.value("sheetName").toString();
        if (sheetName.isEmpty()) sheetName = "Unnamed Sheet";
        QString sheetFile = obj.value("fileName").toString();

        // Resolve relative path
        QString absPath = sheetFile;
        if (QFileInfo(sheetFile).isRelative() && !baseDir.isEmpty()) {
            absPath = baseDir + "/" + sheetFile;
        }
        absPath = QFileInfo(absPath).absoluteFilePath();

        // Build item
        QTreeWidgetItem* sheetItem = new QTreeWidgetItem(parentItem);
        sheetItem->setData(0, Qt::UserRole, absPath);

        bool isActive = (absPath == m_currentFilePath);
        sheetItem->setText(0, sheetName);
        sheetItem->setToolTip(0, absPath);

        // Icon: active sheet gets a highlighted icon, others use sheet icon
        if (isActive) {
            sheetItem->setIcon(0, QIcon(":/icons/tool_sheet.svg"));
            QFont f = sheetItem->font(0);
            f.setBold(true);
            sheetItem->setFont(0, f);
            sheetItem->setForeground(0, QColor("#4f9ef8")); // Blue highlight
        } else if (!QFile::exists(absPath)) {
            sheetItem->setIcon(0, QIcon(":/icons/erc_error.svg"));
            sheetItem->setForeground(0, QColor("#f87171")); // Red for missing
            sheetItem->setToolTip(0, "File not found: " + absPath);
        } else {
            sheetItem->setIcon(0, QIcon(":/icons/tool_sheet.svg"));
            sheetItem->setForeground(0, QColor("#d0d0e0"));
        }

        // Add file name as secondary info (smaller, dimmer)
        sheetItem->setText(0, sheetName);
        if (!sheetFile.isEmpty()) {
            sheetItem->setToolTip(0, QString("%1\n%2").arg(sheetName, absPath));
        }

        // Recurse into child
        if (QFile::exists(absPath)) {
            buildHierarchyTree(sheetItem, absPath, depth + 1);
        }

        sheetItem->setExpanded(true);
    }

    visitedPaths.remove(filePath);
}

void SchematicEditor::refreshHierarchyPanel() {
    if (!m_hierarchyTree) return;
    m_hierarchyTree->clear();

    // Determine root file: either the top of navigation stack or current file
    QString rootFile = m_navigationStack.isEmpty() ? m_currentFilePath : m_navigationStack.first();
    if (rootFile.isEmpty()) {
        QTreeWidgetItem* placeholder = new QTreeWidgetItem(m_hierarchyTree);
        placeholder->setText(0, "No schematic loaded");
        placeholder->setForeground(0, QColor("#666688"));
        placeholder->setFlags(Qt::NoItemFlags);
        return;
    }

    // Root item
    QFileInfo fi(rootFile);
    QTreeWidgetItem* rootItem = new QTreeWidgetItem(m_hierarchyTree);
    rootItem->setData(0, Qt::UserRole, rootFile);
    rootItem->setText(0, fi.baseName().isEmpty() ? "Root Sheet" : fi.baseName());
    rootItem->setToolTip(0, rootFile);
    rootItem->setIcon(0, QIcon(":/icons/tool_sheet.svg"));

    bool rootIsActive = (rootFile == m_currentFilePath);
    if (rootIsActive) {
        QFont f = rootItem->font(0);
        f.setBold(true);
        rootItem->setFont(0, f);
        rootItem->setForeground(0, QColor("#4f9ef8"));
    } else {
        rootItem->setForeground(0, QColor("#d0d0e0"));
    }
    rootItem->setExpanded(true);

    // Recursively build tree
    buildHierarchyTree(rootItem, rootFile, 0);

    // Expand all and scroll to active item
    m_hierarchyTree->expandAll();

    // Scroll to the active (current) sheet item
    QTreeWidgetItemIterator it(m_hierarchyTree);
    while (*it) {
        QString path = (*it)->data(0, Qt::UserRole).toString();
        if (path == m_currentFilePath) {
            m_hierarchyTree->setCurrentItem(*it);
            m_hierarchyTree->scrollToItem(*it);
            break;
        }
        ++it;
    }
}

void SchematicEditor::onEditTitleBlock() {
    // Count total sheets (simple: 1 unless navigation stack is larger)
    int totalSheets = qMax(1, m_navigationStack.size() + 1);

    TitleBlockDialog dlg(m_titleBlock, totalSheets, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_titleBlock = dlg.result();
    m_isModified = true;

    // Update the page frame immediately
    if (m_pageFrame) {
        m_pageFrame->setTitleBlock(m_titleBlock);
    }

    statusBar()->showMessage("Title block updated.", 3000);
    onSaveSchematic();
}

void SchematicEditor::onUndo() {
    if (m_undoStack->canUndo()) {
        m_undoStack->undo();
        statusBar()->showMessage("Undo: " + m_undoStack->undoText(), 2000);
    }
}

void SchematicEditor::onRedo() {
    if (m_undoStack->canRedo()) {
        m_undoStack->redo();
        statusBar()->showMessage("Redo: " + m_undoStack->redoText(), 2000);
    }
}

void SchematicEditor::onUndoStackIndexChanged() {
    if (!m_scene) return;
    
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    QList<QGraphicsItem*> validSelected;
    
    for (QGraphicsItem* item : selected) {
        if (item->scene() == m_scene) {
            validSelected.append(item);
        }
    }
    
    if (validSelected.size() != selected.size()) {
        for (QGraphicsItem* item : selected) {
            item->setSelected(item->scene() == m_scene);
        }
        updatePropertyBar();
    }
}

void SchematicEditor::onDelete() {
    QList<QGraphicsItem*> selectedItems = m_scene->selectedItems();
    if (selectedItems.isEmpty()) {
        // Toggle Scissors Tool if nothing is selected
        if (m_view->currentTool() && m_view->currentTool()->name() == "Scissors") {
            m_view->setCurrentTool("Select");
            statusBar()->showMessage("Select tool active", 2000);
        } else {
            m_view->setCurrentTool("Scissors");
            statusBar()->showMessage("Scissors tool active", 2000);
        }
        return;
    }

    QList<SchematicItem*> schematicItems;
    for (QGraphicsItem* item : selectedItems) {
        SchematicItem* sItem = nullptr;
        QGraphicsItem* current = item;
        while (current) {
            sItem = dynamic_cast<SchematicItem*>(current);
            if (sItem && !sItem->isSubItem()) break;
            current = current->parentItem();
        }
        if (sItem) schematicItems.append(sItem);
    }

    if (!schematicItems.isEmpty()) {
        m_scene->clearSelection();
        RemoveItemCommand* cmd = new RemoveItemCommand(m_scene, schematicItems);
        m_undoStack->push(cmd);
        statusBar()->showMessage(QString("Deleted %1 item(s)").arg(schematicItems.size()), 2000);
    }
}
void SchematicEditor::onCopy() {
    const QList<SchematicItem*> selectedItems = uniqueTopLevelSchematicItems(m_scene->selectedItems());
    if (selectedItems.isEmpty()) return;

    QJsonArray itemsArray;
    int count = 0;
    for (SchematicItem* sItem : selectedItems) {
        QJsonObject itemJson = sItem->toJson();
        itemJson["x"] = sItem->pos().x();
        itemJson["y"] = sItem->pos().y();
        itemJson["rotation"] = sItem->rotation();
        itemsArray.append(itemJson);
        count++;
    }
    if (itemsArray.isEmpty()) return;

    QJsonObject clipboardData;
    clipboardData["application"] = "viospice";
    clipboardData["type"] = "schematic-items";
    clipboardData["items"] = itemsArray;

    QMimeData* mimeData = new QMimeData;
    mimeData->setData("application/x-viora_eda-schematic-items", QJsonDocument(clipboardData).toJson());
    QApplication::clipboard()->setMimeData(mimeData);
    statusBar()->showMessage(QString("Copied %1 item(s)").arg(count), 2000);
}

void SchematicEditor::onCut() {
    onCopy();
    onDelete();
}

void SchematicEditor::onPaste() {
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData->hasFormat("application/x-viora_eda-schematic-items")) return;

    QByteArray data = mimeData->data("application/x-viora_eda-schematic-items");
    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject clipboardData = doc.object();

    if (clipboardData["application"].toString() != "viospice" ||
        clipboardData["type"].toString() != "schematic-items") return;

    QJsonArray itemsArray = clipboardData["items"].toArray();
    if (itemsArray.isEmpty()) return;

    QPointF pasteOffset(20, 20);

    m_undoStack->beginMacro("Paste Items");
    QList<SchematicItem*> pastedItems;
    int count = 0;

    for (const QJsonValue& val : itemsArray) {
        QJsonObject itemJson = val.toObject();
        offsetItemJson(itemJson, pasteOffset);
        itemJson["id"] = QUuid::createUuid().toString();

        SchematicItem* item = SchematicFileIO::createItemFromJson(itemJson);
        if (item) {
            item->setRotation(itemJson["rotation"].toDouble());
            // Avoid duplicate references when pasting (e.g., V1/V1/V1).
            const int t = item->itemType();
            if (t != SchematicItem::WireType &&
                t != SchematicItem::LabelType &&
                t != SchematicItem::NetLabelType &&
                t != SchematicItem::JunctionType &&
                t != SchematicItem::NoConnectType &&
                t != SchematicItem::BusType &&
                t != SchematicItem::SheetType &&
                t != SchematicItem::HierarchicalPortType) {
                const QString prefix = item->referencePrefix();
                if (!prefix.isEmpty() && m_view) {
                    item->setReference(m_view->getNextReference(prefix));
                }
            }
            m_undoStack->push(new AddItemCommand(m_scene, item));
            pastedItems.append(item);
            count++;
        }
    }
    m_undoStack->endMacro();

    m_scene->clearSelection();
    for (auto item : pastedItems) item->setSelected(true);
    statusBar()->showMessage(QString("Pasted %1 item(s)").arg(count), 2000);
    beginMouseFollowPlacement(pastedItems, "Paste");
}

void SchematicEditor::onDuplicate() {
    const QList<SchematicItem*> selectedItems = uniqueTopLevelSchematicItems(m_scene->selectedItems());
    if (selectedItems.isEmpty()) {
        statusBar()->showMessage("No items selected to duplicate", 2000);
        return;
    }

    QJsonArray itemsArray;
    int count = 0;
    for (SchematicItem* sItem : selectedItems) {
        QJsonObject itemJson = sItem->toJson();
        itemJson["x"] = sItem->pos().x();
        itemJson["y"] = sItem->pos().y();
        itemJson["rotation"] = sItem->rotation();
        itemsArray.append(itemJson);
        count++;
    }
    if (itemsArray.isEmpty()) return;

    QPointF duplicateOffset(20, 20);
    m_undoStack->beginMacro("Duplicate Items");
    QList<SchematicItem*> duplicatedItems;

    for (const QJsonValue& val : itemsArray) {
        QJsonObject itemJson = val.toObject();
        offsetItemJson(itemJson, duplicateOffset);
        itemJson["id"] = QUuid::createUuid().toString();

        SchematicItem* item = SchematicFileIO::createItemFromJson(itemJson);
        if (item) {
            item->setRotation(itemJson["rotation"].toDouble());
            // Avoid duplicate references when duplicating, just like paste.
            const int t = item->itemType();
            if (t != SchematicItem::WireType &&
                t != SchematicItem::LabelType &&
                t != SchematicItem::NetLabelType &&
                t != SchematicItem::JunctionType &&
                t != SchematicItem::NoConnectType &&
                t != SchematicItem::BusType &&
                t != SchematicItem::SheetType &&
                t != SchematicItem::HierarchicalPortType) {
                const QString prefix = item->referencePrefix();
                if (!prefix.isEmpty() && m_view) {
                    item->setReference(m_view->getNextReference(prefix));
                }
            }
            m_undoStack->push(new AddItemCommand(m_scene, item));
            duplicatedItems.append(item);
        }
    }
    m_undoStack->endMacro();

    m_scene->clearSelection();
    for (auto item : duplicatedItems) item->setSelected(true);
    statusBar()->showMessage(QString("Duplicated %1 item(s)").arg(count), 2000);
    beginMouseFollowPlacement(duplicatedItems, "Duplicate");
}

void SchematicEditor::onSelectAll() {
    int count = 0;
    for (QGraphicsItem* item : m_scene->items()) {
        SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
        if (sItem) { sItem->setSelected(true); count++; }
    }
    statusBar()->showMessage(QString("Selected %1 item(s)").arg(count), 2000);
}

void SchematicEditor::onSelectionChanged() {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    QList<SchematicItem*> sItems;
    for (QGraphicsItem* item : selected) {
        if (auto* si = dynamic_cast<SchematicItem*>(item))
            sItems.append(si);
    }
    
    // Connect simulation directive edit signal
    for (QGraphicsItem* item : selected) {
        if (auto* directive = dynamic_cast<SchematicSpiceDirectiveItem*>(item)) {
            connect(directive, &SchematicSpiceDirectiveItem::editSimulationRequested,
                    this, &SchematicEditor::onEditSimulationFromDirective,
                    Qt::UniqueConnection);
            break;
        }
    }
    
    // Clear all previous highlights
    if (m_netManager) {
        m_netManager->clearAllHighlights(m_scene);
    }

    if (!sItems.isEmpty()) {
        if (sItems.size() == 1) {
            SchematicItem* item = sItems.first();
            // 0. Smart Signal Logic Editor (Standalone IDE)
            if (auto* smartBlock = dynamic_cast<SmartSignalItem*>(item)) {
                if (m_logicEditorPanel) {
                    m_logicEditorPanel->setTargetBlock(smartBlock);
                }
            }

            // 1. Component Cross-Probe
            if (ConfigManager::instance().autoFocusOnCrossProbe() && !item->reference().isEmpty()) {
                SyncManager::instance().pushCrossProbe(item->reference());
            }

            // 2. Net Cross-Probe
            if (m_netManager) {
                QString netName = m_netManager->findNetAtPoint(item->pos());
                if (netName.isEmpty() && item->itemType() == SchematicItem::LabelType) {
                    netName = item->value();
                }
                if (!netName.isEmpty()) {
                    SyncManager::instance().pushCrossProbe("", netName);
                }
            }

            // Perform real-time net highlighting with component-centric expansion
            if (m_netManager) {
                // 1. Trace the primary net (the one the user clicked)
                QMap<SchematicItem*, QSet<int>> primaryNet = m_netManager->traceNetWithPins(item);
                
                // 2. Identify all components on this primary net
                QList<SchematicItem*> componentsOnNet;
                for (SchematicItem* ni : primaryNet.keys()) {
                    if (ni->itemType() != SchematicItem::WireType && 
                        ni->itemType() != SchematicItem::BusType &&
                        ni->itemType() != SchematicItem::JunctionType) {
                        componentsOnNet.append(ni);
                    }
                }
                
                // 3. For each component, trace ALL its nets (fulfill "all the nets of this component")
                QMap<SchematicItem*, QSet<int>> finalHighlightMap = primaryNet;
                for (SchematicItem* comp : componentsOnNet) {
                    // Trace every pin of this component
                    QList<QPointF> pins = comp->connectionPoints();
                    for (int i = 0; i < pins.size(); ++i) {
                        QMap<SchematicItem*, QSet<int>> secondaryNet = m_netManager->traceNetWithPins(comp);
                        for (auto it = secondaryNet.begin(); it != secondaryNet.end(); ++it) {
                            finalHighlightMap[it.key()].unite(it.value());
                        }
                    }
                }

                // 4. Apply the expanded highlights
                for (auto it = finalHighlightMap.begin(); it != finalHighlightMap.end(); ++it) {
                    SchematicItem* ni = it.key();
                    const QSet<int>& pins = it.value();
                    
                    if (ni->itemType() == SchematicItem::WireType || 
                        ni->itemType() == SchematicItem::BusType ||
                        ni->itemType() == SchematicItem::JunctionType ||
                        ni->itemTypeName() == "BusEntry") {
                        ni->setHighlighted(true);
                    } else {
                        for (int pinIdx : pins) {
                            ni->setHighlightedPin(pinIdx, true);
                        }
                    }
                }
            }
        }
    }

    // Visual selection highlighting (Glow effect)
    for (QGraphicsItem* item : m_scene->items()) {
        if (dynamic_cast<SchematicItem*>(item)) {
            if (item->isSelected()) {
                if (!item->graphicsEffect()) {
                    auto* glow = new QGraphicsDropShadowEffect();
                    glow->setBlurRadius(15);
                    glow->setOffset(0);
                    glow->setColor(QColor("#3b82f6")); // Professional Tech Blue
                    item->setGraphicsEffect(glow);
                }
            } else {
                if (item->graphicsEffect()) {
                    item->setGraphicsEffect(nullptr);
                }
            }
        }
    }
    syncWsState();
}

void SchematicEditor::onItemDoubleClicked(SchematicItem* item) {
    if (!item) return;
    
    // 1. Specialized Smart Dialogs
    bool smartEnabled = ConfigManager::instance().isFeatureEnabled("ux.smart_properties_v2", true);

    if (smartEnabled && item->itemType() == SchematicItem::SheetType) {
        if (auto* sheet = dynamic_cast<SchematicSheetItem*>(item)) {
            SheetPropertiesDialog dlg(sheet, m_undoStack, m_scene, this);
            if (dlg.exec() == QDialog::Accepted) {
                // After editing properties, offer to enter the sheet
                QMessageBox::StandardButton reply = QMessageBox::question(this, "Enter Sheet",
                    "Do you want to enter this sheet now?", QMessageBox::Yes | QMessageBox::No);
                
                if (reply == QMessageBox::Yes) {
                    QString filePath = sheet->fileName();
                    if (QFileInfo(filePath).isRelative() && !m_projectDir.isEmpty()) {
                        filePath = m_projectDir + "/" + filePath;
                    }
                    
                    if (!QFile::exists(filePath)) {
                        // Create if missing
                        QFile f(filePath);
                        if (f.open(QIODevice::WriteOnly)) { f.write("{}"); f.close(); }
                    }

                    onSaveSchematic();
                    QString oldFile = m_currentFilePath;
                    if (openFile(filePath)) {
                        if (!oldFile.isEmpty()) m_navigationStack.append(oldFile);
                        updateBreadcrumbs();
                    }
                }
            }
            return;
        }
    }

    if (smartEnabled && item->itemType() == SchematicItem::WireType) {
        if (auto* wire = dynamic_cast<WireItem*>(item)) {
            WirePropertiesDialog dlg(wire, m_netManager, m_undoStack, m_scene, this);
            if (dlg.exec() == QDialog::Accepted) {
                // Now handled by onApply in dlg
                if (m_netManager) m_netManager->updateNets(m_scene);
            }
            return;
        }
    } else if (smartEnabled && item->itemType() == SchematicItem::BusType) {
        if (auto* bus = dynamic_cast<BusItem*>(item)) {
            BusPropertiesDialog dlg(bus, m_undoStack, m_scene, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (m_netManager) m_netManager->updateNets(m_scene);
            }
            return;
        }
    } else if (smartEnabled && item->itemType() == SchematicItem::NetLabelType) {
        if (auto* nl = dynamic_cast<NetLabelItem*>(item)) {
            NetLabelPropertiesDialog dlg(nl, m_undoStack, m_scene, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (m_netManager) m_netManager->updateNets(m_scene);
            }
            return;
        }
    } else if (item->itemType() == SchematicItem::ResistorType ||
               item->itemType() == SchematicItem::CapacitorType ||
               item->itemType() == SchematicItem::InductorType) {
        const PassiveModelPropertiesDialog::Kind kind =
            (item->itemType() == SchematicItem::ResistorType)
                ? PassiveModelPropertiesDialog::Kind::Resistor
                : (item->itemType() == SchematicItem::CapacitorType
                    ? PassiveModelPropertiesDialog::Kind::Capacitor
                    : PassiveModelPropertiesDialog::Kind::Inductor);
        PassiveModelPropertiesDialog dlg(item, kind, this);
        if (dlg.exec() == QDialog::Accepted) {
            QJsonObject newState = item->toJson();
            newState["reference"] = dlg.reference();
            newState["value"] = dlg.valueText();
            newState["spiceModel"] = dlg.spiceModel();
            newState["manufacturer"] = dlg.manufacturer();
            newState["mpn"] = dlg.mpn();
            newState["footprint"] = dlg.footprint();
            newState["excludeFromSim"] = dlg.excludeFromSimulation();
            newState["excludeFromPcb"] = dlg.excludeFromPcb();
            m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
        }
        return;
    } else if (item->itemType() == SchematicItem::LabelType) {
        if (auto* textItem = dynamic_cast<SchematicTextItem*>(item)) {
            SchematicTextPropertiesDialog dlg(textItem, m_undoStack, m_scene, this);
            dlg.exec();
            return;
        }
    } else if (item->itemType() == SchematicItem::ComponentType) {
        if (auto* comp = dynamic_cast<GenericComponentItem*>(item)) {
            ComponentLabelPropertiesDialog dlg(comp, ComponentLabelPropertiesDialog::Reference, this);
            dlg.exec();
            return;
        }
    } else if (item->itemTypeName() == "OscilloscopeInstrument") {
        // Support both specialized OscilloscopeItem and InstrumentProbeItem(Oscilloscope)
        openOscilloscopeWindow(item);
        return;
    } else if (item->itemTypeName() == "TuningSlider") {
        if (auto* slider = dynamic_cast<TuningSliderSymbolItem*>(item)) {
            TuningSliderPropertiesDialog dlg(slider, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = slider->toJson();
                newState["reference"] = dlg.reference();
                newState["min"] = dlg.minValue();
                newState["max"] = dlg.maxValue();
                newState["current"] = dlg.currentValue();
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, slider, newState));
            }
            return;
        }
    } else if (item->itemTypeName().contains("LogicAnalyzer", Qt::CaseInsensitive) ||
 
               item->itemTypeName().contains("Logic Analyzer", Qt::CaseInsensitive)) {
        
        QString id = item->id().toString();
        if (id.isEmpty()) id = QString("LA_%1").arg(reinterpret_cast<quintptr>(item), 0, 16);

        if (!m_laWindows.contains(id)) {
            auto* win = new LogicAnalyzerWindow("Logic Analyzer - " + item->reference(), this);
            win->setInstrumentId(id);
            connect(win, &LogicAnalyzerWindow::windowClosing, this, [this](const QString& windowId) {
                m_laWindows.remove(windowId);
            });
            m_laWindows[id] = win;
        }

        const QStringList nets = resolveConnectedInstrumentNets(item);
        m_laWindows[id]->setChannels(nets);
        m_laWindows[id]->show();
        m_laWindows[id]->raise();
        return;
    } else if (item->itemTypeName() == "SignalGenerator") {
        if (auto* gen = dynamic_cast<SignalGeneratorItem*>(item)) {
            SignalGeneratorPropertiesDialog dlg(gen, m_undoStack, m_scene, this);
            dlg.exec();
            return;
        }
    } else if (item->itemTypeName() == "LED" || item->itemTypeName() == "Blinking LED") {
        LedPropertiesDialog dlg(item, m_scene, this);
        dlg.exec();
        return;
    } else if (item->itemTypeName() == "Switch") {
        if (auto* sw = dynamic_cast<SwitchItem*>(item)) {
            SwitchPropertiesDialog dlg(sw, this);
            dlg.exec();
            return;
        }
        if (auto* gen = dynamic_cast<GenericComponentItem*>(item)) {
            const QString name = gen->symbol().name().trimmed().toLower();
            const QString prefix = gen->symbol().referencePrefix().trimmed().toLower();
            if (name == "sw" || name == "switch" || prefix == "s") {
                SwitchPropertiesDialog dlg(gen, this);
                dlg.exec();
                return;
            }
        }
    } else if (item->itemTypeName() == "Voltage Controlled Switch") {
        if (auto* vcsw = dynamic_cast<VoltageControlledSwitchItem*>(item)) {
            VoltageControlledSwitchDialog dlg(vcsw, this);
            dlg.exec();
            return;
        }
    } else if (item->itemType() == SchematicItem::VoltageSourceType) {
        if (auto* vSrc = dynamic_cast<VoltageSourceItem*>(item)) {
            if (vSrc->sourceType() == VoltageSourceItem::Behavioral) {
                bool ok;
                QString val = QInputDialog::getText(this, "Behavioral Voltage Source", 
                                                   "Expression (e.g. V=V(in)*2):", QLineEdit::Normal, 
                                                   vSrc->value(), &ok);
                if (ok) {
                    vSrc->setValue(val);
                }
            } else {
                VoltageSourceLTSpiceDialog dlg(vSrc, m_undoStack, m_scene, m_projectDir, this);
                dlg.exec();
            }
            return;
        }
    } else if (item->itemType() == SchematicItem::CurrentSourceType) {
        if (auto* cSrc = dynamic_cast<CurrentSourceItem*>(item)) {
            CurrentSourceLTSpiceDialog dlg(cSrc, m_undoStack, m_scene, m_projectDir, this);
            dlg.exec();
            return;
        }
    } else if (item->itemTypeName().compare("Current_Source_Behavioral", Qt::CaseInsensitive) == 0 ||
               item->itemTypeName().compare("bi", Qt::CaseInsensitive) == 0 ||
               item->itemTypeName().compare("bi2", Qt::CaseInsensitive) == 0) {
        if (auto* bi = dynamic_cast<BehavioralCurrentSourceItem*>(item)) {
            BehavioralCurrentSourceDialog dlg(bi, m_scene, this);
            dlg.exec();
            return;
        }
    } else if (item->itemType() == SchematicItem::SpiceDirectiveType) {
        if (auto* spice = dynamic_cast<SchematicSpiceDirectiveItem*>(item)) {
            onEditSimulationFromDirective(spice->text());
            return;
        }
    } else if (item->itemType() == SchematicItem::ComponentType ||
               item->itemType() == SchematicItem::ICType ||
               item->itemType() == SchematicItem::TransistorType ||
               item->itemType() == SchematicItem::DiodeType ||
               item->itemType() == SchematicItem::CustomType ||
               item->itemType() == SchematicItem::PowerType) {

        const QString prefix = item->referencePrefix().trimmed().toUpper();
        const QString typeNameLower = item->itemTypeName().trimmed().toLower();
        if (prefix == "R" || typeNameLower == "resistor") {
            PassiveModelPropertiesDialog dlg(item, PassiveModelPropertiesDialog::Kind::Resistor, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = item->toJson();
                newState["reference"] = dlg.reference();
                newState["value"] = dlg.valueText();
                newState["spiceModel"] = dlg.spiceModel();
                newState["manufacturer"] = dlg.manufacturer();
                newState["mpn"] = dlg.mpn();
                newState["footprint"] = dlg.footprint();
                newState["excludeFromSim"] = dlg.excludeFromSimulation();
                newState["excludeFromPcb"] = dlg.excludeFromPcb();
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
            }
            return;
        }
        if (prefix == "C" || typeNameLower == "capacitor") {
            PassiveModelPropertiesDialog dlg(item, PassiveModelPropertiesDialog::Kind::Capacitor, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = item->toJson();
                newState["reference"] = dlg.reference();
                newState["value"] = dlg.valueText();
                newState["spiceModel"] = dlg.spiceModel();
                newState["manufacturer"] = dlg.manufacturer();
                newState["mpn"] = dlg.mpn();
                newState["footprint"] = dlg.footprint();
                newState["excludeFromSim"] = dlg.excludeFromSimulation();
                newState["excludeFromPcb"] = dlg.excludeFromPcb();
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
            }
            return;
        }
        if (prefix == "L" || typeNameLower == "inductor") {
            PassiveModelPropertiesDialog dlg(item, PassiveModelPropertiesDialog::Kind::Inductor, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = item->toJson();
                newState["reference"] = dlg.reference();
                newState["value"] = dlg.valueText();
                newState["spiceModel"] = dlg.spiceModel();
                newState["manufacturer"] = dlg.manufacturer();
                newState["mpn"] = dlg.mpn();
                newState["footprint"] = dlg.footprint();
                newState["excludeFromSim"] = dlg.excludeFromSimulation();
                newState["excludeFromPcb"] = dlg.excludeFromPcb();
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
            }
            return;
        }
        
        if (item->itemTypeName().compare("csw", Qt::CaseInsensitive) == 0) {
            CSWPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_undoStack->beginMacro("Update CSW Properties");
                if (item->value() != dlg.modelName()) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, item, "Value", item->value(), dlg.modelName(), m_projectDir));
                }
                m_undoStack->endMacro();

                if (m_simulationPanel) {
                    QString prefix = ".model " + dlg.modelName() + " ";
                    SchematicSpiceDirectiveItem* targetDirective = nullptr;
                    for (auto* gi : m_scene->items()) {
                        if (auto* existing = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
                            // Support ".model MySwitch" or ".model MySwitch CSW..."
                            if (existing->text().startsWith(prefix, Qt::CaseInsensitive)) {
                                targetDirective = existing;
                                break;
                            }
                        }
                    }
                    if (targetDirective) {
                        m_undoStack->push(new ChangePropertyCommand(m_scene, targetDirective, "Text", targetDirective->text(), dlg.commandText(), m_projectDir));
                    } else {
                        m_simulationPanel->updateSchematicDirectiveFromCommand(dlg.commandText());
                    }
                }
            }
            return;
        }

        if (item->itemTypeName().compare("e", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("e2", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("vcvs", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("g", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("g2", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("vccs", Qt::CaseInsensitive) == 0) {
            VCVSPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_undoStack->beginMacro("Update Controlled Source Properties");
                if (item->value() != dlg.gainValue()) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, item, "value", item->value(), dlg.gainValue(), m_projectDir));
                }
                m_undoStack->endMacro();
            }
            return;
        }

        if (item->itemTypeName().compare("f", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("cccs", Qt::CaseInsensitive) == 0) {
            CCCSPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                const QString newValue = dlg.controlSource() + " " + dlg.gainValue();
                m_undoStack->beginMacro("Update CCCS Properties");
                if (item->value() != newValue) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, item, "value", item->value(), newValue, m_projectDir));
                }
                m_undoStack->endMacro();
            }
            return;
        }

        if (item->itemTypeName().compare("h", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("ccvs", Qt::CaseInsensitive) == 0) {
            CCVSPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                const QString newValue = dlg.controlSource() + " " + dlg.transresistance();
                m_undoStack->beginMacro("Update CCVS Properties");
                if (item->value() != newValue) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, item, "value", item->value(), newValue, m_projectDir));
                }
                m_undoStack->endMacro();
            }
            return;
        }

        if (item->itemTypeName().compare("tline", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("ltline", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("T", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("O", Qt::CaseInsensitive) == 0) {
            TransmissionLinePropertiesDialog dlg(item, m_scene, m_undoStack, this);
            if (dlg.exec() == QDialog::Accepted) {
                const QString newValue = dlg.valueString();
                m_undoStack->beginMacro("Update Transmission Line Properties");
                QJsonObject newState = item->toJson();
                newState["value"] = newValue;
                if (item->itemTypeName().compare("ltline", Qt::CaseInsensitive) == 0 ||
                    item->referencePrefix().compare("O", Qt::CaseInsensitive) == 0) {
                    QJsonObject peObj = newState["paramExpressions"].toObject();
                    const auto newPE = dlg.ltraParams();
                    for (auto it = newPE.constBegin(); it != newPE.constEnd(); ++it) {
                        peObj[it.key()] = it.value();
                    }
                    newState["paramExpressions"] = peObj;
                }
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
                
                if (dlg.wantsDirectiveUpdate()) {
                    const QString directiveText = dlg.directiveText();
                    const QString origModel = dlg.originalModelName();
                    const QRegularExpression exactRe(
                        QString("^\\s*\\.model\\s+%1\\s+LTRA\\(.*\\)\\s*$").arg(QRegularExpression::escape(origModel)),
                        QRegularExpression::CaseInsensitiveOption);
                        
                    bool updated = false;
                    for (QGraphicsItem* gi : m_scene->items()) {
                        if (auto* dir = dynamic_cast<SchematicSpiceDirectiveItem*>(gi)) {
                            if (exactRe.match(dir->text()).hasMatch()) {
                                m_undoStack->push(new ChangePropertyCommand(m_scene, dir, "Text", dir->text(), directiveText, m_projectDir));
                                updated = true;
                                break;
                            }
                        }
                    }
                    if (!updated) {
                        QPointF at = item->scenePos() + QPointF(120, -40);
                        auto* dirItem = new SchematicSpiceDirectiveItem(directiveText, at, nullptr);
                        m_undoStack->push(new AddItemCommand(m_scene, dirItem));
                    }
                }
                m_undoStack->endMacro();
            }
            return;
        }

        // Diode properties dialog (auto-detect type from symbol name)
        if (item->referencePrefix() == "D") {
            DiodePropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_undoStack->beginMacro("Update Diode Properties");
                const QString newName = dlg.modelName();
                if (item->value() != newName) {
                    m_undoStack->push(new ChangePropertyCommand(
                        m_scene, item, "value",
                        item->value(), newName, m_projectDir));
                }
                const auto newPE = dlg.paramExpressions();
                const auto oldPE = item->paramExpressions();
                for (auto it = newPE.constBegin(); it != newPE.constEnd(); ++it) {
                    if (oldPE.value(it.key()) != it.value()) {
                        item->setParamExpression(it.key(), it.value());
                    }
                }
                // Auto-switch symbol if type changed
                const QString newSym = dlg.newSymbolName();
                if (!newSym.isEmpty()) {
                    if (auto* gen = dynamic_cast<GenericComponentItem*>(item)) {
                        if (SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(newSym)) {
                            gen->setSymbol(*sym);
                        }
                    }
                }
                m_undoStack->endMacro();
            }
            return;
        }

        // JFET properties dialog
        if (item->itemTypeName().compare("njf", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("pjf", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("JN", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("JP", Qt::CaseInsensitive) == 0) {
            JfetPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = item->toJson();
                newState["value"] = dlg.modelName();
                QJsonObject peObj;
                const auto newPE = dlg.paramExpressions();
                for (auto it = newPE.constBegin(); it != newPE.constEnd(); ++it) {
                    peObj[it.key()] = it.value();
                }
                newState["paramExpressions"] = peObj;
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));
            }
            return;
        }

        // BJT properties dialog
        if (item->itemTypeName().compare("Transistor", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("Transistor_PNP", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("npn", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("npn2", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("npn3", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("npn4", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("pnp", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("pnp2", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("pnp4", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("lpnp", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("QN", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("QP", Qt::CaseInsensitive) == 0) {
            BjtPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = item->toJson();
                newState["value"] = dlg.modelName();
                newState["footprint"] = dlg.footprint();
                QJsonObject peObj;
                const auto newPE = dlg.paramExpressions();
                for (auto it = newPE.constBegin(); it != newPE.constEnd(); ++it) {
                    peObj[it.key()] = it.value();
                }
                newState["paramExpressions"] = peObj;
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));

                const QString newSym = dlg.newSymbolName();
                if (!newSym.isEmpty()) {
                    if (auto* gen = dynamic_cast<GenericComponentItem*>(item)) {
                        if (SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(newSym)) {
                            gen->setSymbol(*sym);
                        }
                    }
                }
            }
            return;
        }

        // MOSFET properties dialog
        if (item->itemTypeName().compare("Transistor_NMOS", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("Transistor_PMOS", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("nmos", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("nmos4", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("pmos", Qt::CaseInsensitive) == 0 ||
            item->itemTypeName().compare("pmos4", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("MN", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("MP", Qt::CaseInsensitive) == 0) {
            MosPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                QJsonObject newState = item->toJson();
                newState["value"] = dlg.modelName();
                newState["footprint"] = dlg.footprint();
                QJsonObject peObj;
                const auto newPE = dlg.paramExpressions();
                for (auto it = newPE.constBegin(); it != newPE.constEnd(); ++it) {
                    peObj[it.key()] = it.value();
                }
                newState["paramExpressions"] = peObj;
                m_undoStack->push(new BulkChangePropertyCommand(m_scene, item, newState));

                const QString newSym = dlg.newSymbolName();
                if (!newSym.isEmpty()) {
                    if (auto* gen = dynamic_cast<GenericComponentItem*>(item)) {
                        if (SymbolDefinition* sym = SymbolLibraryManager::instance().findSymbol(newSym)) {
                            gen->setSymbol(*sym);
                        }
                    }
                }
            }
            return;
        }

        // MESFET properties dialog
        if (item->itemTypeName().compare("mesfet", Qt::CaseInsensitive) == 0 ||
            item->referencePrefix().compare("Z", Qt::CaseInsensitive) == 0) {
            MesfetPropertiesDialog dlg(item, this);
            if (dlg.exec() == QDialog::Accepted) {
                const QString newModel = dlg.modelName();
                if (newModel != item->value()) {
                    m_undoStack->push(new ChangePropertyCommand(m_scene, item, "value", item->value(), newModel, m_projectDir));
                }
            }
            return;
        }

        GenericSymbolPropertiesDialog dlg(item, m_undoStack, m_scene, this);
        dlg.exec();
        return;
    }

    // 3. Fallback to Generic Properties Dialog
    if (item->itemType() != SchematicItem::BusType &&
        item->itemType() != SchematicItem::JunctionType) {
        openItemProperties(item);
    }
}

void SchematicEditor::onComponentLabelDoubleClicked(GenericComponentItem* component, bool isReferenceLabel) {
    if (!component) return;
    ComponentLabelPropertiesDialog::LabelType type = isReferenceLabel
        ? ComponentLabelPropertiesDialog::Reference
        : ComponentLabelPropertiesDialog::Value;
    ComponentLabelPropertiesDialog dlg(component, type, this);
    dlg.exec();
}

void SchematicEditor::onItemPlaced(SchematicItem* item) {
    if (!item) return;

    // Professional UX: Auto-open the instrument window when placed
    if (item->itemTypeName() == "OscilloscopeInstrument" || item->itemTypeName() == "Oscilloscope") {
        openOscilloscopeWindow(item);
    }
}

void SchematicEditor::onSelectionDoubleClicked(const QList<SchematicItem*>& items) {
    if (items.isEmpty()) return;

    // Check if all items are of the same type for specialized bulk edit
    SchematicItem::ItemType commonType = items.first()->itemType();
    bool allSameType = true;
    for (auto* it : items) {
        if (it->itemType() != commonType) {
            allSameType = false;
            break;
        }
    }

    if (allSameType) {
        if (commonType == SchematicItem::WireType) {
            // Bulk Edit Wires
            WirePropertiesDialog dlg(dynamic_cast<WireItem*>(items.first()), m_netManager, m_undoStack, m_scene, this);
            // We need to tell the dialog it's in bulk mode or pass all items
            // For now, SmartPropertiesDialog already has m_items, let's make sure it uses them.
            if (dlg.exec() == QDialog::Accepted) {
                if (m_netManager) m_netManager->updateNets(m_scene);
            }
            return;
        } else if (commonType == SchematicItem::BusType) {
            BusPropertiesDialog dlg(dynamic_cast<BusItem*>(items.first()), m_undoStack, m_scene, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (m_netManager) m_netManager->updateNets(m_scene);
            }
            return;
        } else if (commonType == SchematicItem::NetLabelType) {
            NetLabelPropertiesDialog dlg(dynamic_cast<NetLabelItem*>(items.first()), m_undoStack, m_scene, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (m_netManager) m_netManager->updateNets(m_scene);
            }
            return;
        } else if (commonType == SchematicItem::ResistorType ||
                   commonType == SchematicItem::CapacitorType ||
                   commonType == SchematicItem::InductorType) {
            const PassiveModelPropertiesDialog::Kind kind =
                (commonType == SchematicItem::ResistorType)
                    ? PassiveModelPropertiesDialog::Kind::Resistor
                    : (commonType == SchematicItem::CapacitorType
                        ? PassiveModelPropertiesDialog::Kind::Capacitor
                        : PassiveModelPropertiesDialog::Kind::Inductor);
            PassiveModelPropertiesDialog dlg(items.first(), kind, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_undoStack->beginMacro("Update Passive Component Properties");
                for (SchematicItem* it : items) {
                    QJsonObject newState = it->toJson();
                    newState["value"] = dlg.valueText();
                    newState["spiceModel"] = dlg.spiceModel();
                    newState["manufacturer"] = dlg.manufacturer();
                    newState["mpn"] = dlg.mpn();
                    newState["footprint"] = dlg.footprint();
                    newState["excludeFromSim"] = dlg.excludeFromSimulation();
                    newState["excludeFromPcb"] = dlg.excludeFromPcb();
                    m_undoStack->push(new BulkChangePropertyCommand(m_scene, it, newState));
                }
                m_undoStack->endMacro();
            }
            return;
        }
    }

    // Fallback: If not all same type or no specialized bulk dialog, 
    // open the generic multi-item property editor.
    openItemProperties(items.first());
}

void SchematicEditor::onLeaveSheet() {
    if (m_navigationStack.isEmpty()) {
        statusBar()->showMessage("Already at top level", 2000);
        return;
    }
    
    onSaveSchematic(); // Save current before going up
    
    QString parentPath = m_navigationStack.takeLast();
    if (openFile(parentPath)) {
        statusBar()->showMessage("Returned to parent sheet", 3000);
        updateBreadcrumbs();
    }
}

void SchematicEditor::openItemProperties(SchematicItem* item) {
    if (!item) return;

    QList<SchematicItem*> targetItems;
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    
    // If the clicked item is part of the selection, we edit the whole selection
    bool itemInSelection = false;
    for (auto* it : selected) {
        if (it == item) {
            itemInSelection = true;
            break;
        }
    }

    if (itemInSelection) {
        for (auto* it : selected) {
            if (auto* si = dynamic_cast<SchematicItem*>(it))
                targetItems.append(si);
        }
    } else {
        targetItems.append(item);
    }

    ComponentPropertiesDialog dialog(targetItems, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_undoStack->beginMacro("Update Component Properties");
        
        auto validateAndApply = [&](const QString& propName, const QString& newVal) {
            if (newVal == "<multiple>") return; // Don't apply if it's the multiple placeholder
            
            // Check if ANY item in targetItems actually differs from this value
            bool anyChanged = false;
            for (auto* it : targetItems) {
                QString currentVal;
                if (propName == "Reference") currentVal = it->reference();
                else if (propName == "Value") currentVal = it->value();
                else if (propName == "Manufacturer") currentVal = it->manufacturer();
                else if (propName == "MPN") currentVal = it->mpn();
                else if (propName == "Description") currentVal = it->description();
                
                if (currentVal != newVal) {
                    anyChanged = true;
                    break;
                }
            }

            if (anyChanged) {
                // We use onPropertyChanged but we only want it to apply to our targetItems
                // However, onPropertyChanged uses the GLOBAL selection.
                // Since targetItems IS the selection (if itemInSelection is true), it works.
                // If targetItems is just a single item not in selection, it might be weird.
                // Let's ensure the items are selected if they aren't.
                if (!itemInSelection) {
                    m_scene->clearSelection();
                    item->setSelected(true);
                }
                onPropertyChanged(propName, newVal);
            }
        };

        validateAndApply("Reference", dialog.reference());
        validateAndApply("Value", dialog.value());
        
        // Handle Sheet properties
        if (dialog.fileName() != "<multiple>") {
            for (auto* it : targetItems) {
                if (auto* sheet = dynamic_cast<SchematicSheetItem*>(it)) {
                    if (sheet->fileName() != dialog.fileName()) {
                        m_undoStack->push(new ChangePropertyCommand(m_scene, sheet, "fileName", sheet->fileName(), dialog.fileName(), m_projectDir));
                    }
                }
            }
        }
            
        m_undoStack->endMacro();
    }
}

void SchematicEditor::onPropertyChanged(const QString& name, const QVariant& value) {
    QList<QGraphicsItem*> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;

    m_undoStack->beginMacro(QString("Change Property: %1").arg(name));

    for (QGraphicsItem* item : selected) {
        SchematicItem* si = dynamic_cast<SchematicItem*>(item);
        if (!si) continue;

        QVariant oldValue;
        bool changed = false;

        if (name == "Reference") {
            oldValue = si->reference();
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "reference", oldValue, value, m_projectDir));
        } else if (name == "Value") {
            oldValue = si->value();
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "value", oldValue, value, m_projectDir));
        } else if (name == "Manufacturer") {
            oldValue = si->manufacturer();
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "manufacturer", oldValue, value, m_projectDir));
        } else if (name == "MPN") {
            oldValue = si->mpn();
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "mpn", oldValue, value, m_projectDir));
        } else if (name == "Description") {
            oldValue = si->description();
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "description", oldValue, value, m_projectDir));
        } else if (name == "Net Class") {
            if (auto* netLabel = dynamic_cast<NetLabelItem*>(si)) {
                oldValue = netLabel->netClassName().isEmpty() ? QString("Default") : netLabel->netClassName();
                QString newClass = value.toString().trimmed();
                if (newClass.isEmpty()) newClass = "Default";
                if (oldValue != newClass) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "netClass", oldValue, newClass, m_projectDir));
            }
        } else if (name == "Text") {
            if (auto* textItem = dynamic_cast<SchematicTextItem*>(si)) {
                oldValue = textItem->text();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Text", oldValue, value));
            }
        } else if (name == "Font Size") {
            if (auto* textItem = dynamic_cast<SchematicTextItem*>(si)) {
                oldValue = QString::number(textItem->font().pointSize());
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Font Size", oldValue, value));
            }
        } else if (name == "Color") {
            if (auto* textItem = dynamic_cast<SchematicTextItem*>(si)) {
                oldValue = textItem->color().name();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Color", oldValue, value));
            } else if (auto* shape = dynamic_cast<SchematicShapeItem*>(si)) {
                oldValue = shape->pen().color().name();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Color", oldValue, value));
            }
        } else if (name == "Bold") {
            if (auto* textItem = dynamic_cast<SchematicTextItem*>(si)) {
                oldValue = textItem->font().bold() ? "True" : "False";
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Bold", oldValue, value));
            }
        } else if (name == "Italic") {
            if (auto* textItem = dynamic_cast<SchematicTextItem*>(si)) {
                oldValue = textItem->font().italic() ? "True" : "False";
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Italic", oldValue, value));
            }
        } else if (name == "Alignment") {
            if (auto* textItem = dynamic_cast<SchematicTextItem*>(si)) {
                QString currentStr = "Left";
                if (textItem->alignment() == Qt::AlignCenter) currentStr = "Center";
                else if (textItem->alignment() == Qt::AlignRight) currentStr = "Right";
                oldValue = currentStr;
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Alignment", oldValue, value));
            }
        } else if (name == "Fill Color") {
            if (auto* shape = dynamic_cast<SchematicShapeItem*>(si)) {
                oldValue = shape->brush().color().name();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Fill Color", oldValue, value));
            }
        } else if (name == "Source Type") {
            if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(si)) {
                VoltageSourceItem::SourceType val = value.toString() == "DC" ? VoltageSourceItem::DC : VoltageSourceItem::Sine;
                oldValue = vsrc->sourceType() == VoltageSourceItem::DC ? "DC" : "Sine";
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Source Type", oldValue, value));
            }
        } else if (name == "DC Voltage") {
            if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(si)) {
                oldValue = vsrc->dcVoltage();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "DC Voltage", oldValue, value));
            }
        } else if (name == "AC Amplitude") {
            if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(si)) {
                oldValue = vsrc->sineAmplitude();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "AC Amplitude", oldValue, value));
            }
        } else if (name == "AC Frequency") {
            if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(si)) {
                oldValue = vsrc->sineFrequency();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "AC Frequency", oldValue, value));
            }
        } else if (name == "AC Phase") {
            if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(si)) {
                oldValue = "0.00";
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "AC Phase", oldValue, value));
            }
        } else if (name == "AC Offset") {
            if (auto* vsrc = dynamic_cast<VoltageSourceItem*>(si)) {
                oldValue = vsrc->sineOffset();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "AC Offset", oldValue, value));
            }
        } else if (name == "Line Style") {
            if (auto* shape = dynamic_cast<SchematicShapeItem*>(si)) {
                QString currentStr = "Solid";
                if (shape->pen().style() == Qt::DashLine) currentStr = "Dash";
                else if (shape->pen().style() == Qt::DotLine) currentStr = "Dot";
                oldValue = currentStr;
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Line Style", oldValue, value));
            }
        } else if (name == "Position X") {
            oldValue = si->pos().x();
            if (qAbs(oldValue.toDouble() - value.toDouble()) > 0.001) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Position X", oldValue, value));
        } else if (name == "Position Y") {
            oldValue = si->pos().y();
            if (qAbs(oldValue.toDouble() - value.toDouble()) > 0.001) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Position Y", oldValue, value));
        } else if (name == "Locked") {
            oldValue = si->isLocked() ? "True" : "False";
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Locked", oldValue, value));
        } else if (name == "Mirrored") {
            oldValue = si->isMirroredX() ? "True" : "False";
            if (oldValue != value) changed = true;
            if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Mirrored", oldValue, value));
        } else if (name == "Power Name") {
            if (auto* pwr = dynamic_cast<PowerItem*>(si)) {
                oldValue = pwr->value();
                if (oldValue != value) changed = true;
                if (changed) m_undoStack->push(new ChangePropertyCommand(m_scene, si, "value", oldValue, value));
            }
        } else if (selected.size() == 1) {
            // Position items only for single selection to avoid confusion (or handle relative move?)
            if (auto* shape = dynamic_cast<SchematicShapeItem*>(si)) {
                if (name == "Start X") {
                    oldValue = shape->startPoint().x();
                    m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Start X", oldValue, value));
                } else if (name == "Start Y") {
                    oldValue = shape->startPoint().y();
                    m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Start Y", oldValue, value));
                } else if (name == "End X") {
                    oldValue = shape->endPoint().x();
                    m_undoStack->push(new ChangePropertyCommand(m_scene, si, "End X", oldValue, value));
                } else if (name == "End Y") {
                    oldValue = shape->endPoint().y();
                    m_undoStack->push(new ChangePropertyCommand(m_scene, si, "End Y", oldValue, value));
                } else if (name == "Ctrl1 X" || name == "Ctrl1 Y" || name == "Ctrl2 X" || name == "Ctrl2 Y") {
                    if (shape->shapeType() == SchematicShapeItem::Bezier && shape->points().size() == 4) {
                        oldValue = name.contains("X") ? shape->points()[name.contains("1") ? 1 : 2].x() : shape->points()[name.contains("1") ? 1 : 2].y();
                        m_undoStack->push(new ChangePropertyCommand(m_scene, si, name, oldValue, value));
                    }
                } else if (name == "Width") {
                    oldValue = shape->pen().widthF();
                    m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Width", oldValue, value));
                }
            }
        }
    }

    m_undoStack->endMacro();
}
// ─── Manipulation Handlers ──────────────────────────────────────────────────

void SchematicEditor::onRotateCW() {
    handleTransformAction(SchematicTool::TransformAction::RotateCW);
}

void SchematicEditor::onRotateCCW() {
    handleTransformAction(SchematicTool::TransformAction::RotateCCW);
}

void SchematicEditor::onFlipHorizontal() {
    handleTransformAction(SchematicTool::TransformAction::FlipHorizontal);
}

void SchematicEditor::onFlipVertical() {
    handleTransformAction(SchematicTool::TransformAction::FlipVertical);
}

void SchematicEditor::onBringToFront() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (!items.isEmpty()) {
        m_undoStack->push(new ZValueItemCommand(m_scene, items, true));
        statusBar()->showMessage("Brought to Front", 2000);
    }
}

void SchematicEditor::onSendToBack() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (!items.isEmpty()) {
        m_undoStack->push(new ZValueItemCommand(m_scene, items, false));
        statusBar()->showMessage("Sent to Back", 2000);
    }
}

void SchematicEditor::onAlignLeft() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 1) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::AlignLeft));
        statusBar()->showMessage("Aligned Left", 2000);
    }
}

void SchematicEditor::onAlignRight() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 1) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::AlignRight));
        statusBar()->showMessage("Aligned Right", 2000);
    }
}

void SchematicEditor::onAlignTop() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 1) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::AlignTop));
        statusBar()->showMessage("Aligned Top", 2000);
    }
}

void SchematicEditor::onAlignBottom() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 1) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::AlignBottom));
        statusBar()->showMessage("Aligned Bottom", 2000);
    }
}

void SchematicEditor::onAlignCenterX() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 1) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::AlignCenterX));
        statusBar()->showMessage("Aligned Center X", 2000);
    }
}

void SchematicEditor::onAlignCenterY() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 1) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::AlignCenterY));
        statusBar()->showMessage("Aligned Center Y", 2000);
    }
}

void SchematicEditor::onDistributeH() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 2) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::DistributeH));
        statusBar()->showMessage("Distributed Horizontally", 2000);
    }
}

void SchematicEditor::onDistributeV() {
    QList<SchematicItem*> items = selectedSchematicItems();
    if (items.size() > 2) {
        m_undoStack->push(new AlignItemCommand(m_scene, items, AlignItemCommand::DistributeV));
        statusBar()->showMessage("Distributed Vertically", 2000);
    }
}

void SchematicEditor::onOpenPowerNetsManager() {
    struct Usage {
        int count = 0;
        QSet<QString> files;
    };

    auto buildRows = [&]() -> QVector<PowerNetUsageRow> {
        QMap<QString, Usage> usage;

        const QString currentAbsPath = m_currentFilePath.isEmpty()
                                           ? QString()
                                           : QFileInfo(m_currentFilePath).absoluteFilePath();
        const QString currentLabel = currentAbsPath.isEmpty()
                                         ? "Current Sheet (unsaved)"
                                         : displayPath(currentAbsPath, m_projectDir);

        // Use live scene for current sheet (captures unsaved edits).
        for (QGraphicsItem* g : m_scene->items()) {
            auto* pwr = dynamic_cast<PowerItem*>(g);
            if (!pwr) continue;
            const QString net = pwr->value().trimmed().isEmpty() ? pwr->netName() : pwr->value().trimmed();
            usage[net].count++;
            usage[net].files.insert(currentLabel);
        }

        // Use disk files for hierarchical child sheets.
        QSet<QString> files = collectHierarchicalSheets(currentAbsPath);
        files.remove(currentAbsPath);
        for (const QString& filePath : files) {
            QFile file(filePath);
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (!doc.isObject()) continue;

            const QString label = displayPath(filePath, m_projectDir);
            const QJsonArray items = doc.object().value("items").toArray();
            for (const QJsonValue& val : items) {
                const QJsonObject obj = val.toObject();
                if (obj.value("type").toString() != "Power") continue;
                const QString net = powerNetNameFromJson(obj);
                usage[net].count++;
                usage[net].files.insert(label);
            }
        }

        QVector<PowerNetUsageRow> rows;
        rows.reserve(usage.size());
        for (auto it = usage.begin(); it != usage.end(); ++it) {
            PowerNetUsageRow row;
            row.netName = it.key();
            row.symbolCount = it.value().count;
            row.files = it.value().files.values();
            std::sort(row.files.begin(), row.files.end());
            rows.push_back(row);
        }

        std::sort(rows.begin(), rows.end(), [](const PowerNetUsageRow& a, const PowerNetUsageRow& b) {
            return a.netName.toLower() < b.netName.toLower();
        });
        return rows;
    };

    PowerNetsManagerDialog dialog(this);
    auto refreshDialog = [&]() { dialog.setRows(buildRows()); };

    connect(&dialog, &PowerNetsManagerDialog::refreshRequested, &dialog, refreshDialog);
    connect(&dialog, &PowerNetsManagerDialog::renameRequested, this,
            [&](const QString& oldName, const QString& newName) {
        int changedCurrent = 0;
        QList<PowerItem*> matched;
        for (QGraphicsItem* g : m_scene->items()) {
            auto* pwr = dynamic_cast<PowerItem*>(g);
            if (!pwr) continue;
            const QString net = pwr->value().trimmed().isEmpty() ? pwr->netName() : pwr->value().trimmed();
            if (net != oldName) continue;
            matched.append(pwr);
        }
        if (!matched.isEmpty()) {
            m_undoStack->beginMacro(QString("Rename Power Net %1 to %2").arg(oldName, newName));
            for (PowerItem* pwr : matched) {
                const QString net = pwr->value().trimmed().isEmpty() ? pwr->netName() : pwr->value().trimmed();
                m_undoStack->push(new ChangePropertyCommand(m_scene, pwr, "value", net, newName, m_projectDir));
                ++changedCurrent;
            }
            m_undoStack->endMacro();
        }

        int changedFiles = 0;
        if (!m_currentFilePath.isEmpty()) {
            const QString currentAbsPath = QFileInfo(m_currentFilePath).absoluteFilePath();
            QSet<QString> files = collectHierarchicalSheets(currentAbsPath);
            files.remove(currentAbsPath);
            for (const QString& filePath : files) {
                changedFiles += renamePowerNetInSchematicFile(filePath, oldName, newName);
            }
        }

        if (changedCurrent > 0) {
            m_isModified = true;
            m_netManager->updateNets(m_scene);
        }

        statusBar()->showMessage(
            QString("Renamed %1 -> %2 (%3 in current sheet, %4 in child sheets)")
                .arg(oldName, newName)
                .arg(changedCurrent)
                .arg(changedFiles),
            4000);
        refreshDialog();
    });

    refreshDialog();
    dialog.exec();
}

void SchematicEditor::onOpenComponentBrowser() {
    LibraryBrowserDialog dialog(this);
    
    // Connect the placement signal from the dialog to our placement slot
    connect(&dialog, &LibraryBrowserDialog::symbolPlaced, this, &SchematicEditor::onPlaceSymbolInSchematic);
    
    dialog.exec();
}

void SchematicEditor::onShowHelp() {
    HelpWindow* help = new HelpWindow();
    help->setAttribute(Qt::WA_DeleteOnClose);
    help->show();
}

void SchematicEditor::onShowDeveloperHelp() {
    DeveloperHelpWindow* devHelp = new DeveloperHelpWindow();
    devHelp->setAttribute(Qt::WA_DeleteOnClose);
    devHelp->show();
}

void SchematicEditor::onProjectAudit() {
    ProjectAuditDialog dlg(this);
    dlg.exec();
}

void SchematicEditor::onOpenModelArchitect() {
    addModelArchitectTab();
}

void SchematicEditor::onImportSpiceSubcircuit() {
    SpiceSubcircuitImportDialog dlg(m_projectDir, m_currentFilePath, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const auto res = dlg.result();
    statusBar()->showMessage(QString("Saved subcircuit %1 to %2").arg(res.subcktName, res.relativeIncludePath), 5000);

    if (!res.insertIncludeDirective) return;

    auto* dirItem = new SchematicSpiceDirectiveItem(QString(".include \"%1\"").arg(res.relativeIncludePath));
    if (m_view) {
        dirItem->setPos(m_view->mapToScene(m_view->viewport()->rect().center()));
    }
    if (res.insertIncludeDirective) {
        m_undoStack->push(new AddItemCommand(m_scene, dirItem));
    } else {
        delete dirItem;
    }

    if (res.openSymbolEditor) {
        const SymbolDefinition def = buildImportedSubcktSymbol(res);
        SymbolEditor* editor = new SymbolEditor(def, nullptr);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->setWindowTitle("Symbol Editor - " + def.name());

        QString projectKey = m_projectDir;
        if (projectKey.isEmpty() && !m_currentFilePath.isEmpty()) {
            projectKey = QFileInfo(m_currentFilePath).absolutePath();
        }
        editor->setProjectKey(projectKey);
        connect(editor, &SymbolEditor::symbolSaved, this, [this](const SymbolDefinition&) {
            if (m_componentsPanel) m_componentsPanel->populate();
        });

        connect(editor, &SymbolEditor::placeInSchematicRequested, this, &SchematicEditor::onPlaceSymbolInSchematic);

        if (res.autoPlaceAfterSave) {
            auto placed = std::make_shared<bool>(false);
            connect(editor, &SymbolEditor::symbolSaved, this, [this, placed](const SymbolDefinition& sym) {
                if (*placed) return;
                *placed = true;
                onPlaceSymbolInSchematic(sym);
                statusBar()->showMessage(QString("Placed generated symbol %1 in schematic").arg(sym.name()), 5000);
            });
        }

        editor->show();
    }

    m_isModified = true;
}

void SchematicEditor::onImportSpiceSubcircuitFile(const QString& filePath) {
    SpiceSubcircuitImportDialog dlg(m_projectDir, m_currentFilePath, this);
    dlg.setSuggestedLibraryFileName(QFileInfo(filePath).fileName());

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        statusBar()->showMessage(QString("Failed to open subcircuit file: %1").arg(filePath), 5000);
        // Fall back to empty dialog
    } else {
        const QString netlist = decodeSpiceImportText(file.readAll());
        file.close();
        dlg.setPreloadedNetlist(netlist);
    }

    if (dlg.exec() != QDialog::Accepted) return;

    const auto res = dlg.result();
    statusBar()->showMessage(QString("Saved subcircuit %1 to %2").arg(res.subcktName, res.relativeIncludePath), 5000);

    if (!res.insertIncludeDirective) return;

    auto* dirItem = new SchematicSpiceDirectiveItem(QString(".include \"%1\"").arg(res.relativeIncludePath));
    if (m_view) {
        dirItem->setPos(m_view->mapToScene(m_view->viewport()->rect().center()));
    }
    if (res.insertIncludeDirective) {
        m_undoStack->push(new AddItemCommand(m_scene, dirItem));
    } else {
        delete dirItem;
    }

    if (res.openSymbolEditor) {
        const SymbolDefinition def = buildImportedSubcktSymbol(res);
        SymbolEditor* editor = new SymbolEditor(def, nullptr);
        editor->setAttribute(Qt::WA_DeleteOnClose);
        editor->setWindowTitle("Symbol Editor - " + def.name());

        QString projectKey = m_projectDir;
        if (projectKey.isEmpty() && !m_currentFilePath.isEmpty()) {
            projectKey = QFileInfo(m_currentFilePath).absolutePath();
        }
        editor->setProjectKey(projectKey);
        connect(editor, &SymbolEditor::symbolSaved, this, [this](const SymbolDefinition&) {
            if (m_componentsPanel) m_componentsPanel->populate();
        });

        connect(editor, &SymbolEditor::placeInSchematicRequested, this, &SchematicEditor::onPlaceSymbolInSchematic);

        if (res.autoPlaceAfterSave) {
            auto placed = std::make_shared<bool>(false);
            connect(editor, &SymbolEditor::symbolSaved, this, [this, placed](const SymbolDefinition& sym) {
                if (*placed) return;
                *placed = true;
                onPlaceSymbolInSchematic(sym);
                statusBar()->showMessage(QString("Placed generated symbol %1 in schematic").arg(sym.name()), 5000);
            });
        }

        editor->show();
    }

    m_isModified = true;
}

void SchematicEditor::onOpenCommandPalette() {
    CommandPalette* palette = new CommandPalette(this);
    palette->setPlaceholderText("Search symbols, pins, or run commands...");

    // 1. Add all menu actions
    for (auto menu : menuBar()->findChildren<QMenu*>()) {
        for (auto action : menu->actions()) {
            if (!action->text().isEmpty() && !action->isSeparator()) {
                palette->addAction(action);
            }
        }
    }

    // 2. Add all items in the current schematic
    for (auto item : m_scene->items()) {
        if (auto si = dynamic_cast<SchematicItem*>(item)) {
            QString ref = si->reference();
            QString val = si->value();
            if (ref.isEmpty()) continue;

            PaletteResult res;
            res.title = QString("%1: %2").arg(ref, val);
            res.description = QString("Component in Schematic at (%1, %2)").arg(si->pos().x()).arg(si->pos().y());
            res.icon = createItemPreviewIcon(si);
            res.action = [this, si]() {
                m_view->centerOn(si);
                m_scene->clearSelection();
                si->setSelected(true);
            };
            palette->addResult(res);
        }
    }

    // 3. Add simulation commands if available
    {
        PaletteResult res;
        res.title = "Run Simulation";
        res.description = "Execute SPICE simulation on the current circuit";
        res.icon = QIcon(":/icons/tool_sync.svg");
        res.action = [this]() { onRunSimulation(); };
        palette->addResult(res);
    }

    palette->show();
}

void SchematicEditor::onOpenERCRulesConfig() {
    // Show the existing ERC Matrix Dialog
    ERCRulesDialog dlg(m_ercRules, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_ercRules = dlg.getRules();
        m_isModified = true;
        statusBar()->showMessage("ERC Rules updated.", 3000);
    }
}

void SchematicEditor::onOpenDesignRuleEditor() {
    // Open the new DesignRuleEditor for creating/editing custom rules
    DesignRule* rule = DesignRuleEditor::createRule(RuleCategory::Custom, this);
    if (rule) {
        statusBar()->showMessage(QString("Created rule: %1").arg(rule->name()), 3000);
        // TODO: Add rule to project rule set
    }
}

void SchematicEditor::onOpenFindReplace() {
    FindReplaceDialog* dlg = new FindReplaceDialog(this);
    dlg->setProjectContext(m_currentFilePath, m_projectDir);
    
    connect(dlg, &FindReplaceDialog::navigateToResult, this, [this](const EditorSearchResult& res) {
        if (res.fileName != m_currentFilePath) {
            onSaveSchematic();
            m_navigationStack.clear(); 
            openFile(res.fileName);
        }
        
        for (auto* item : m_scene->items()) {
            if (auto* si = dynamic_cast<SchematicItem*>(item)) {
                if (si->id() == res.itemId) {
                    m_scene->clearSelection();
                    si->setSelected(true);
                    m_view->centerOn(si);
                    return;
                }
            }
        }
    });

    connect(dlg, &FindReplaceDialog::replaceRequested, this, [this](const EditorSearchResult& res, const QString& newValue) {
        if (res.fileName == m_currentFilePath) {
            for (auto* item : m_scene->items()) {
                if (auto* si = dynamic_cast<SchematicItem*>(item)) {
                    if (si->id() == res.itemId) {
                        m_undoStack->beginMacro("Find and Replace");
                        if (res.context == "Reference") {
                            m_undoStack->push(new ChangePropertyCommand(m_scene, si, "reference", si->reference(), newValue));
                        } else if (res.context == "Value" || res.context == "Net") {
                            m_undoStack->push(new ChangePropertyCommand(m_scene, si, "value", si->value(), newValue));
                        } else if (res.context == "Text") {
                            m_undoStack->push(new ChangePropertyCommand(m_scene, si, "Text", si->value(), newValue));
                        }
                        m_undoStack->endMacro();
                        break;
                    }
                }
            }
        }
    });

    dlg->show();
}

void SchematicEditor::onCrossProbeReceived(const QString& refDes, const QString& netName) {
    if (refDes.isEmpty() && netName.isEmpty()) return;
    if (!m_scene) return;
    const bool autoFocus = ConfigManager::instance().autoFocusOnCrossProbe();

    // Fix infinite loop: If the item is already selected, don't re-select it.
    // This prevents triggering selectionChanged -> pushCrossProbe -> loop.
    if (!refDes.isEmpty() && m_scene->selectedItems().size() == 1) {
        if (auto* si = dynamic_cast<SchematicItem*>(m_scene->selectedItems().first())) {
            if (si->reference() == refDes) {
                if (autoFocus) {
                    m_view->centerOn(si);
                }

                return;
            }
        }
    }

    m_scene->clearSelection();

    if (!refDes.isEmpty()) {
        // 1. Try local match first (in case it's in the current sheet)
        if (findAndSelectInScene(m_scene, refDes)) {
            statusBar()->showMessage("Cross-probed: " + refDes, 3000);
        } else {
            // 2. Hierarchical navigation
            // If not found locally, go to root and search from there
            if (!m_navigationStack.isEmpty()) {
                onSaveSchematic();
                QString rootPath = m_navigationStack.first();
                m_navigationStack.clear();
                if (openFile(rootPath)) {
                    updateBreadcrumbs();
                }
            }
            navigateAndSelectHierarchical(refDes);
            statusBar()->showMessage("Cross-probed (Hierarchical): " + refDes, 3000);
        }
    }
}

bool SchematicEditor::findAndSelectInScene(QGraphicsScene* scene, const QString& refDes) {
    if (!scene) return false;
    const bool autoFocus = ConfigManager::instance().autoFocusOnCrossProbe();
    for (QGraphicsItem* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->reference() == refDes) {
                si->setSelected(true);
                if (autoFocus) {
                    m_view->centerOn(si);
                }
                return true;
            }
        }
    }
    return false;
}

void SchematicEditor::navigateAndSelectHierarchical(const QString& refDes) {
    QStringList parts = refDes.split('/');
    if (parts.size() < 2) {
        // This is the leaf reference (e.g. "R1")
        findAndSelectInScene(m_scene, refDes);
        return;
    }

    QString sheetName = parts.takeFirst();
    QString remaining = parts.join('/');

    for (QGraphicsItem* item : m_scene->items()) {
        if (auto* sheet = dynamic_cast<SchematicSheetItem*>(item)) {
            if (sheet->sheetName() == sheetName) {
                QString filePath = sheet->fileName();
                if (QFileInfo(filePath).isRelative() && !m_projectDir.isEmpty()) {
                    filePath = m_projectDir + "/" + filePath;
                }
                
                QString oldFile = m_currentFilePath;
                if (openFile(filePath)) {
                    if (!oldFile.isEmpty()) m_navigationStack.append(oldFile);
                    updateBreadcrumbs();
                    // Recurse to find the next part of the path
                    navigateAndSelectHierarchical(remaining);
                    return;
                }
            }
        }
    }
    
    // Fallback search in current scene if hierarchy traversal fails
    findAndSelectInScene(m_scene, refDes);
}

void SchematicEditor::onAssignModel(const QString& modelName) {
    const auto selected = m_scene->selectedItems();
    QList<SchematicItem*> targets;
    for (auto* item : selected) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) targets.append(si);
    }

    if (targets.isEmpty()) {
        statusBar()->showMessage("No component selected to apply model.", 3000);
        return;
    }

    m_undoStack->beginMacro(QString("Assign SPICE Model: %1").arg(modelName));
    for (auto* si : targets) {
        m_undoStack->push(new ChangePropertyCommand(m_scene, si, "spiceModel", si->spiceModel(), modelName, m_projectDir));
    }
    m_undoStack->endMacro();

    statusBar()->showMessage(QString("Assigned model '%1' to %2 component(s)").arg(modelName).arg(targets.size()), 4000);
}

void SchematicEditor::onBatchEdit() {
    const auto selected = m_scene->selectedItems();
    QList<SchematicItem*> targetItems;

    for (auto* item : selected) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            // Only include components that have editable values
            if (!si->value().isEmpty()) {
                targetItems.append(si);
            }
        }
    }

    if (targetItems.isEmpty()) {
        statusBar()->showMessage("No components with editable values selected", 3000);
        return;
    }

    BatchEditDialog dialog(targetItems, this);
    if (dialog.exec() == QDialog::Accepted) {
        statusBar()->showMessage(QString("Batch edited %1 component(s)").arg(targetItems.size()), 3000);
    }
}
void SchematicEditor::onCheckpointRequested() {
    if (!m_scene) return;
    qDebug() << "[SchematicEditor] Creating AI checkpoint...";
    m_lastCheckpoint = SchematicFileIO::serializeSceneToJson(m_scene, m_currentPageSize);
}

void SchematicEditor::onRewindRequested() {
    if (m_lastCheckpoint.isEmpty()) {
        qDebug() << "[SchematicEditor] Rewind requested but no checkpoint exists.";
        return;
    }
    if (!m_scene) return;

    qDebug() << "[SchematicEditor] Executing AI Rewind...";
    QString error;
    if (SchematicFileIO::loadSchematicFromJson(m_scene, m_lastCheckpoint, &error)) {
        m_isModified = true;
        update();
    } else {
        qWarning() << "[SchematicEditor] Rewind failed:" << error;
    }
}
