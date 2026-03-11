// schematic_editor_file.cpp
// File operations (new, open, save, export) for SchematicEditor

#include "schematic_editor.h"
#include "schematic_api.h"
#include "schematic_file_io.h"
#include "schematic_connectivity.h"
#include "schematic_page_item.h"
#include "../items/schematic_sheet_item.h"
#include "schematic_erc.h"
#include "netlist_generator.h"
#include "../ui/netlist_editor.h"
#include "../ui/bom_dialog.h"
#include "sync_manager.h"
#include "gemini_dialog.h"
#include "../../python/gemini_panel.h"
#include "../dialogs/bus_aliases_dialog.h"
#include "../../symbols/symbol_editor.h"
#include "../../core/project.h"
#include "../../core/recent_projects.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QFileInfo>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QStatusBar>
#include <QApplication>
#include <QTimer>
#include <QJsonObject>
#include <QSet>
#include <cmath>
#include <algorithm>

namespace {
QString ercViolationKey(const ERCViolation& v) {
    const int px = int(std::round(v.position.x()));
    const int py = int(std::round(v.position.y()));
    return QString("%1|%2|%3|%4|%5")
        .arg(int(v.severity))
        .arg(int(v.category))
        .arg(v.netName.trimmed())
        .arg(px)
        .arg(py) + "|" + v.message.trimmed();
}

QStringList itemPinNames(const SchematicItem* item) {
    QStringList pins;
    if (!item) return pins;
    const QList<QPointF> points = item->connectionPoints();
    for (int i = 0; i < points.size(); ++i) {
        const QString pin = item->pinName(i).trimmed();
        pins.append(pin.isEmpty() ? QString::number(i + 1) : pin);
    }
    return pins;
}
}

void SchematicEditor::setProjectContext(const QString& projectName, const QString& projectDir) {
    m_projectName = projectName;
    m_projectDir = projectDir;
    
    // Auto-derive file path from project
    if (!projectName.isEmpty() && !projectDir.isEmpty()) {
        QString derivedPath = projectDir + "/" + projectName + ".flxsch";
        if (m_currentFilePath.isEmpty()) {
            m_currentFilePath = derivedPath;
            updateGeminiProjectEffect();
            setWindowTitle(QString("Viora EDA - Schematic Editor [%1.flxsch]").arg(projectName));
        }
    }
}

void SchematicEditor::onNewSchematic() {
    addSchematicTab("New Schematic");
}

void SchematicEditor::onOpenSchematic() {
    if (m_isModified) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Unsaved Changes",
            "Do you want to save changes before opening another schematic?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
        );
        if (reply == QMessageBox::Save) {
            onSaveSchematic();
            if (m_isModified) return;
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }
    
    QString filePath = QFileDialog::getOpenFileName(
        this, "Open Schematic", QString(),
        "Viora EDA Schematic (*.flxsch *.flux *.sch);;FluxScript (*.flux);;KiCad Schematic (*.kicad_sch);;Altium Schematic (*.SchDoc);;All Files (*)"
    );
    if (filePath.isEmpty()) return;
    m_navigationStack.clear();
    openFile(filePath);
}

bool SchematicEditor::openFile(const QString& filePath) {
    if (filePath.isEmpty()) return false;

    // Check if already open
    for (int i = 0; i < m_workspaceTabs->count(); ++i) {
        if (m_workspaceTabs->widget(i)->property("filePath").toString() == filePath) {
            m_workspaceTabs->setCurrentIndex(i);
            return true;
        }
    }

    if (filePath.endsWith(".sym", Qt::CaseInsensitive)) {
        openSymbolEditorWindow(QFileInfo(filePath).baseName());
        // For now, assume it's a new symbol or handle loading logic in SymbolEditor
        return true;
    }

    // Create a new schematic tab for this file
    addSchematicTab(QFileInfo(filePath).fileName());
    m_view->setProperty("filePath", filePath);

    if (filePath.endsWith(".flux", Qt::CaseInsensitive)) {
        /* Legacy FluxScript file loading disabled
        QFile file(filePath);
        ...
        */
        QMessageBox::information(this, "Legacy Format", "FluxScript files (.flux) are currently disabled during the migration to Python.");
        return false;
    }

    QString loadedPageSize;
    QString embeddedScript;
    QMap<QString, QList<QString>> loadedBusAliases;
    QSet<QString> loadedErcExclusions;
    QJsonObject loadedSimulationSetup;
    if (SchematicFileIO::loadSchematic(m_scene, filePath, loadedPageSize, m_titleBlock, &embeddedScript, &loadedBusAliases, &loadedErcExclusions, &loadedSimulationSetup)) {
        m_currentFilePath = filePath;
        updateGeminiProjectEffect();
        m_currentPageSize = loadedPageSize;
        m_isModified = false;
        m_busAliases = loadedBusAliases;
        m_ercExclusions = loadedErcExclusions;
        if (m_netManager) m_netManager->setBusAliases(m_busAliases);

        if (!loadedSimulationSetup.isEmpty()) {
            m_simConfig = SimulationSetupDialog::Config::fromJson(loadedSimulationSetup);
        }

        if (!embeddedScript.isEmpty() && m_scriptPanel) {
            m_scriptPanel->setScript(embeddedScript);
        }
        
        updateGrid();
        updatePageFrame();
        
        // Sync hierarchical ports for any sheets in this file
        for (auto* item : m_scene->items()) {
            if (auto* sheet = dynamic_cast<SchematicSheetItem*>(item)) {
                sheet->updatePorts(QFileInfo(filePath).absolutePath());
            }
        }

        SchematicConnectivity::updateVisualConnections(m_scene);
        
        m_ercList->clear();
        m_ercDock->hide();

        QFileInfo fileInfo(filePath);
        setWindowTitle(QString("Viora EDA - Schematic Editor [%1]").arg(fileInfo.fileName()));
        updateBreadcrumbs();
        statusBar()->showMessage(QString("Loaded: %1").arg(filePath), 5000);
        return true;
    } else {
        QMessageBox::critical(this, "Load Error",
            QString("Failed to load schematic:\n%1").arg(SchematicFileIO::lastError()));
        return false;
    }
}

void SchematicEditor::onSaveSchematic() {
    // If we have a project context but no file path yet, derive it
    if (m_currentFilePath.isEmpty() && !m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        m_currentFilePath = m_projectDir + "/" + m_projectName + ".flxsch";
        updateGeminiProjectEffect();
    }
    
    if (m_currentFilePath.isEmpty()) {
        onSaveSchematicAs();
        return;
    }
    
    // Ensure directory exists
    QFileInfo fi(m_currentFilePath);
    QDir().mkpath(fi.absolutePath());
    
    bool success = false;
    if (m_currentFilePath.endsWith(".flux", Qt::CaseInsensitive)) {
        QString code = SchematicFileIO::convertToFluxScript(m_scene, m_netManager);
        QFile file(m_currentFilePath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(code.toUtf8());
            file.close();
            success = true;
        }
    } else {
        QString script = SchematicFileIO::convertToFluxScript(m_scene, m_netManager);
        const QJsonObject simSetup = m_simConfig.toJson();
        success = SchematicFileIO::saveSchematic(m_scene, m_currentFilePath, m_currentPageSize, script, m_titleBlock, m_busAliases, m_ercExclusions, &simSetup);
    }

    if (success) {
        m_isModified = false;
        QFileInfo fileInfo(m_currentFilePath);
        setWindowTitle(QString("Viora EDA - Schematic Editor [%1]").arg(fileInfo.fileName()));
        statusBar()->showMessage(QString("Saved: %1").arg(m_currentFilePath), 3000);
    } else {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save schematic:\n%1").arg(SchematicFileIO::lastError()));
    }
}

void SchematicEditor::onSaveSchematicAs() {
    if (m_projectName.isEmpty()) {
        // First time saving, prompt to create a project
        QString projectName = QInputDialog::getText(this, "Save New Project", "Project Name:");
        if (projectName.isEmpty()) return;

        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        QDir().mkpath(defaultPath);
        QString projectPath = QFileDialog::getExistingDirectory(this, "Choose Project Location", defaultPath);
        if (projectPath.isEmpty()) return;

        QString fullProjectPath = projectPath + "/" + projectName;
        
        Project* project = new Project(projectName, fullProjectPath);
        if (project->createNew()) {
            RecentProjects::instance().addProject(project->projectFilePath());
            setProjectContext(projectName, fullProjectPath);
            m_currentFilePath = fullProjectPath + "/" + projectName + ".flxsch";
            delete project;
            
            // Re-route to onSaveSchematic now that we have a context and path
            onSaveSchematic();
            return;
        } else {
            QMessageBox::critical(this, "Error", "Failed to create project");
            delete project;
            return;
        }
    }

    // Default to project-derived name if available
    QString defaultPath;
    if (!m_projectName.isEmpty() && !m_projectDir.isEmpty()) {
        defaultPath = m_projectDir + "/" + m_projectName + ".flxsch";
    } else if (!m_currentFilePath.isEmpty()) {
        defaultPath = m_currentFilePath;
    } else {
        defaultPath = "untitled.flxsch";
    }
    
    QString filePath = QFileDialog::getSaveFileName(
        this, "Save Schematic As",
        defaultPath,
        "Viora EDA Schematic (*.flxsch);;FluxScript (*.flux);;KiCad Schematic (*.kicad_sch);;Altium Schematic (*.SchDoc);;All Files (*)"
    );
    if (filePath.isEmpty()) return;
    
    QFileInfo fi(filePath);
    if (fi.suffix().isEmpty())
        filePath += ".flxsch";
    
    const QJsonObject simSetup = m_simConfig.toJson();
    if (SchematicFileIO::saveSchematic(m_scene, filePath, m_currentPageSize, QString(), m_titleBlock, m_busAliases, m_ercExclusions, &simSetup)) {
        m_currentFilePath = filePath;
        updateGeminiProjectEffect();
        m_isModified = false;
        QFileInfo fileInfo(filePath);
        setWindowTitle(QString("Viora EDA - Schematic Editor [%1]").arg(fileInfo.fileName()));
        statusBar()->showMessage(QString("Saved: %1").arg(filePath), 3000);
    } else {
        QMessageBox::critical(this, "Save Error",
            QString("Failed to save schematic:\n%1").arg(SchematicFileIO::lastError()));
    }
}

#include "schematic_annotator.h"
#include "schematic_commands.h"

void SchematicEditor::onAnnotate() {
    if (!m_scene) return;
    
    // Check if we have any hierarchical sheets
    bool isHierarchical = false;
    for (auto* item : m_scene->items()) {
        if (dynamic_cast<class SchematicSheetItem*>(item)) {
            isHierarchical = true;
            break;
        }
    }

    if (isHierarchical && !m_currentFilePath.isEmpty()) {
        // Project-wide annotation (saves changes to files on disk)
        SchematicAnnotator::annotateProject(m_currentFilePath, m_projectDir, true);
        // Reload current sheet to show new references
        openFile(m_currentFilePath);
        statusBar()->showMessage("Project-wide annotation complete", 5000);
        return;
    }

    // 1. Capture old state (for single sheet undo support)
    QMap<SchematicItem*, QString> oldRefs;
    for (QGraphicsItem* gi : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
            oldRefs[si] = si->reference();
        }
    }

    // 2. Perform annotation
    QMap<SchematicItem*, QString> newRefs = SchematicAnnotator::annotate(m_scene, true, SchematicAnnotator::TopToBottom);
    
    if (!newRefs.isEmpty()) {
        // Restore old values temporarily to let command handle redo properly
        for (auto it = newRefs.begin(); it != newRefs.end(); ++it) {
            it.key()->setReference(oldRefs[it.key()]);
        }
        
        m_undoStack->push(new SchematicAnnotateCommand(m_scene, oldRefs, newRefs));
        m_view->update();
        statusBar()->showMessage("Sheet annotated successfully", 5000);
    }
}

void SchematicEditor::onResetAnnotations() {
    if (!m_scene) return;

    QMap<SchematicItem*, QString> oldRefs;
    for (QGraphicsItem* gi : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
            oldRefs[si] = si->reference();
        }
    }

    QMap<SchematicItem*, QString> newRefs = SchematicAnnotator::resetAnnotations(m_scene);
    
    if (!newRefs.isEmpty()) {
        for (auto it = newRefs.begin(); it != newRefs.end(); ++it) {
            it.key()->setReference(oldRefs[it.key()]);
        }
        
        m_undoStack->push(new SchematicAnnotateCommand(m_scene, oldRefs, newRefs));
        m_view->update();
        statusBar()->showMessage("All component annotations reset", 5000);
    }
}

#include "../items/erc_marker_item.h"
#include "../dialogs/bus_aliases_dialog.h"


void SchematicEditor::onRunERC() {
    // 1. Clear previous markers
    for (QGraphicsItem* item : m_scene->items()) {
        if (dynamic_cast<ERCMarkerItem*>(item)) {
            m_scene->removeItem(item);
            delete item;
        }
    }

    m_ercList->clear();
    QList<ERCViolation> violations = SchematicERC::run(m_scene, m_projectDir, m_netManager, m_ercRules);
    
    if (violations.isEmpty()) {
        statusBar()->showMessage("ERC Check Passed: No violations found. ✨", 5000);
        QMessageBox::information(this, "ERC Results", "No electrical rules violations found. Your schematic looks clean!");
        m_ercDock->hide();
    } else {
        m_ercDock->show();
        m_ercDock->raise();
        
        int errors = 0;
        int warnings = 0;
        int criticals = 0;

        int excludedCount = 0;
        for (const ERCViolation& v : violations) {
            const QString key = ercViolationKey(v);
            if (m_ercExclusions.contains(key)) {
                excludedCount++;
                continue;
            }
            // Add visual marker to scene
            m_scene->addItem(new ERCMarkerItem(v));

            QString typeStr;
            QColor color;
            if (v.severity == ERCViolation::Critical) {
                typeStr = "CRITICAL";
                color = Qt::magenta;
                criticals++;
            } else if (v.severity == ERCViolation::Error) {
                typeStr = "ERROR";
                color = Qt::red;
                errors++;
            } else {
                typeStr = "Warning";
                color = QColor("#d97706"); // Amber
                warnings++;
            }

            QString categoryPrefix;
            if (v.category == ERCViolation::Conflict) categoryPrefix = "[Conflict] ";
            else if (v.category == ERCViolation::Connectivity) categoryPrefix = "[Conn] ";
            else if (v.category == ERCViolation::Annotation) categoryPrefix = "[Ref] ";

            QString text = QString("[%1] %2%3").arg(typeStr, categoryPrefix, v.message);
            if (!v.netName.isEmpty()) text += QString(" (Net: %1)").arg(v.netName);

            QListWidgetItem* item = new QListWidgetItem(text);
            item->setData(Qt::UserRole, v.position);
            item->setData(Qt::UserRole + 1, key);
            item->setForeground(color);
            item->setFont(QFont("Inter", 9, v.severity == ERCViolation::Critical ? QFont::Bold : QFont::Normal));
            
            m_ercList->addItem(item);
        }

        if (m_ercList->count() == 0) {
            statusBar()->showMessage(QString("All %1 ERC violations are excluded.").arg(excludedCount), 8000);
        } else {
            statusBar()->showMessage(QString("ERC Results: %1 Critical, %2 Errors, %3 Warnings (%4 excluded)")
                                     .arg(criticals).arg(errors).arg(warnings).arg(excludedCount), 10000);
        }
    }
}

void SchematicEditor::onIgnoreSelectedErc() {
    if (!m_ercList) return;
    const QList<QListWidgetItem*> selected = m_ercList->selectedItems();
    if (selected.isEmpty()) return;
    for (QListWidgetItem* item : selected) {
        const QString key = item->data(Qt::UserRole + 1).toString();
        if (!key.isEmpty()) m_ercExclusions.insert(key);
    }
    m_isModified = true;
    onRunERC();
}

void SchematicEditor::onClearErcExclusions() {
    if (m_ercExclusions.isEmpty()) return;
    m_ercExclusions.clear();
    m_isModified = true;
    onRunERC();
}

void SchematicEditor::onGenerateNetlist() {
    QString initialPath = m_currentFilePath.isEmpty() ? "netlist.json" : QFileInfo(m_currentFilePath).absolutePath() + "/netlist.json";
    QString file = QFileDialog::getSaveFileName(this, "Save Netlist", initialPath, "Flux Netlist (*.json);;IPC-D-356 (*.ipc);;Protel (*.net)");
    if (file.isEmpty()) return;

    NetlistGenerator::Format format = NetlistGenerator::FluxJSON;
    if (file.endsWith(".ipc")) format = NetlistGenerator::IPC356;
    else if (file.endsWith(".net")) format = NetlistGenerator::Protel;

    QString content = NetlistGenerator::generate(m_scene, m_projectDir, format, m_netManager);
    
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(content.toUtf8());
        f.close();
        statusBar()->showMessage("Netlist saved to " + file, 5000);
    } else {
        QMessageBox::warning(this, "Error", "Could not save netlist file.");
    }
}

#include "../dialogs/symbol_field_editor_dialog.h"

void SchematicEditor::onOpenSymbolFieldEditor() {
    if (m_currentFilePath.isEmpty()) {
        QMessageBox::information(this, "Symbol Field Editor",
                                 "Please save the schematic once so the field editor can scan project sheets.");
        onSaveSchematicAs();
        if (m_currentFilePath.isEmpty()) return;
    }

    SymbolFieldEditorDialog dlg(m_currentFilePath, m_projectDir, this);
    if (dlg.exec() == QDialog::Accepted) {
        // Reload current file to see changes
        openFile(m_currentFilePath);
    }
}

void SchematicEditor::onOpenBusAliasesManager() {
    BusAliasesDialog dlg(m_busAliases, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_busAliases = dlg.aliases();
    if (m_netManager) {
        m_netManager->setBusAliases(m_busAliases);
        m_netManager->updateNets(m_scene);
    }
    m_isModified = true;
    statusBar()->showMessage(QString("Updated %1 bus alias(es)").arg(m_busAliases.size()), 3000);
}

void SchematicEditor::onOpenBOM() {
    ECOPackage pkg = NetlistGenerator::generateECOPackage(m_scene, m_projectDir, m_netManager);
    BOMDialog dlg(pkg, this);
    dlg.exec();
}

#include "../analysis/spice_netlist_generator.h"

void SchematicEditor::onOpenNetlistEditor() {
    NetlistEditor* editor = new NetlistEditor(); // Top-level
    editor->setAttribute(Qt::WA_DeleteOnClose);
    
    // Pre-populate with current schematic netlist
    if (m_scene && m_netManager) {
        m_netManager->updateNets(m_scene);
        
        SpiceNetlistGenerator::SimulationParams params;
        switch (m_simConfig.type) {
            case SimAnalysisType::Transient: params.type = SpiceNetlistGenerator::Transient; break;
            case SimAnalysisType::AC:        params.type = SpiceNetlistGenerator::AC; break;
            case SimAnalysisType::OP:        params.type = SpiceNetlistGenerator::OP; break;
            default:                         params.type = SpiceNetlistGenerator::Transient; break;
        }
        
        params.stop = QString::number(m_simConfig.stop);
        params.step = QString::number(m_simConfig.step);
        params.start = QString::number(m_simConfig.start);
        
        QString netlist = SpiceNetlistGenerator::generate(m_scene, m_projectDir, m_netManager, params);
        editor->setNetlist(netlist);
    }
    
    editor->show();
}

#include "../items/net_label_item.h"
#include "../items/generic_component_item.h"
#include "schematic_item.h"

void SchematicEditor::handleIncomingECO() {
    if (!SyncManager::instance().hasPendingECO()) return;
    const SyncManager::ECOTarget target = SyncManager::instance().pendingECOTarget();
    if (target == SyncManager::ECOTarget::PCB) return;

    ECOPackage pkg = SyncManager::instance().pendingECO();
    statusBar()->showMessage("🔄 Applying Netlist Sync...", 3000);

    // 1. Map existing components by reference
    QMap<QString, SchematicItem*> compMap;
    for (auto* item : m_scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            QString ref = si->reference();
            if (!ref.isEmpty()) {
                compMap[ref] = si;
            }
        }
    }

    // 2. Update components
    bool modified = false;
    for (const auto& ecoComp : pkg.components) {
        if (compMap.contains(ecoComp.reference)) {
            SchematicItem* item = compMap[ecoComp.reference];
            
            if (!ecoComp.value.isEmpty() && item->value() != ecoComp.value) {
                item->setValue(ecoComp.value);
                modified = true;
            }
        }
    }

    if (modified) {
        m_isModified = true;
        m_scene->update();
    }

    SyncManager::instance().clearPendingECO();
    statusBar()->showMessage("Synchronization complete", 3000);
}

void SchematicEditor::onOpenGeminiAI() {
    GeminiDialog* dialog = new GeminiDialog(m_scene, this);
    
    // Connect dialog signals too, using QueuedConnection for GUI safety
    connect(dialog->panel(), &GeminiPanel::fluxScriptGenerated, this, [this](const QString& code) {
        if (m_scriptPanel) {
            m_scriptPanel->setScript(code);
            onOpenFluxScript(); 
            statusBar()->showMessage("AI generated FluxScript is ready in the editor!", 5000);
        }
    }, Qt::QueuedConnection);

    dialog->show();
}

void SchematicEditor::updateGeminiProjectEffect() {
    if (m_geminiPanel) {
        m_geminiPanel->setProjectFilePath(m_currentFilePath);
    }
}

void SchematicEditor::onItemsHighlighted(const QStringList& references) {
    if (!m_scene) return;
    
    m_scene->clearSelection();
    QRectF totalRect;
    bool found = false;
    
    for (auto* item : m_scene->items()) {
        auto* sItem = dynamic_cast<SchematicItem*>(item);
        if (sItem && references.contains(sItem->reference(), Qt::CaseInsensitive)) {
            sItem->setSelected(true);
            if (!found) totalRect = sItem->sceneBoundingRect();
            else totalRect = totalRect.united(sItem->sceneBoundingRect());
            found = true;
        }
    }
    
    if (found && m_view) {
        m_view->fitInView(totalRect.adjusted(-100, -100, 100, 100), Qt::KeepAspectRatio);
    }
}

void SchematicEditor::onSnippetGenerated(const QString& jsonSnippet) {
    if (!m_scene || !m_api) return;

    QJsonDocument doc = QJsonDocument::fromJson(jsonSnippet.toUtf8());
    if (!doc.isArray() && !doc.isObject()) return;

    // Handle both direct arrays of items and command-style snippets
    if (doc.isObject() && doc.object().contains("commands")) {
        m_undoStack->beginMacro("AI Generated Commands");
        m_api->executeBatch(doc.object()["commands"].toArray());
        m_undoStack->endMacro();
        statusBar()->showMessage("Executed AI generated commands", 3000);
        return;
    }

    QJsonArray items = doc.isArray() ? doc.array() : doc.object()["items"].toArray();
    if (items.isEmpty()) return;

    m_undoStack->beginMacro("Place AI Snippet");

    // Calculate offset to place at center of current view
    QPointF center = m_view ? m_view->mapToScene(m_view->viewport()->rect().center()) : QPointF(0,0);

    for (const auto& val : items) {
        QJsonObject obj = val.toObject();
        QString type = obj["type"].toString();
        QPointF pos(obj["x"].toDouble() + center.x(), obj["y"].toDouble() + center.y());
        QString ref = obj["reference"].toString();

        m_api->addComponent(type, pos, ref, obj);
    }
    m_undoStack->endMacro();

    statusBar()->showMessage(QString("Placed AI Snippet with %1 items").arg(items.size()), 3000);
}
QList<ERCViolation> SchematicEditor::getErcViolations() const {
    if (!m_scene) return {};
    return SchematicERC::run(m_scene, m_projectDir, m_netManager, m_ercRules);
}

void SchematicEditor::onOpenSymbolEditor() {
    openSymbolEditorWindow("New Symbol");
}

void SchematicEditor::onPlaceSymbolInSchematic(const SymbolDefinition& symbol) {
    if (!m_scene || !m_view) return;

    // Calculate center of current view
    QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    
    // Create the item
    auto* item = new GenericComponentItem(symbol);
    item->setPos(center);
    
    // Use undo stack for the addition
    m_undoStack->push(new AddItemCommand(m_scene, item));
    
    // Select the new item and focus back to schematic
    m_scene->clearSelection();
    item->setSelected(true);
    
    this->activateWindow();
    this->raise();
    m_view->setFocus();
    
    statusBar()->showMessage(QString("Placed symbol: %1").arg(symbol.name()), 3000);
}
