#include "schematic_menu_registry.h"
#include "schematic_view.h"
#include "schematic_commands.h"
#include "schematic_editor.h"
#include "../items/wire_item.h"
#include "../items/resistor_item.h"
#include "../items/capacitor_item.h"
#include "../items/inductor_item.h"
#include "../items/oscilloscope_item.h"
#include "../items/voltage_source_item.h"
#include "../items/generic_component_item.h"
#include "../items/schematic_sheet_item.h"
#include "../items/hierarchical_port_item.h"
#include "../items/led_item.h"
#include "../items/blinking_led_item.h"
#include "../items/tuning_slider_item.h"
#include "../ui/simulation_panel.h"
#include "../analysis/net_manager.h"
#include "../tools/schematic_net_label_tool.h"
#include "../../core/theme_manager.h"
#include "../dialogs/led_properties_dialog.h"
#include <algorithm>
#include <set>
#include <QApplication>
#include <QDesktopServices>
#include <QInputDialog>
#include <QLineF>
#include <QMessageBox>
#include <QCursor>
#include <QMenu>
#include <QUrl>

SchematicMenuRegistry& SchematicMenuRegistry::instance() {
    static SchematicMenuRegistry reg;
    return reg;
}

void SchematicMenuRegistry::registerAction(SchematicItem::ItemType type, const ContextAction& action) {
    m_typeActions.insert({type, action});
}

void SchematicMenuRegistry::registerGlobalAction(const ContextAction& action) {
    m_globalActions.push_back(action);
}

std::vector<ContextAction> SchematicMenuRegistry::getActions(const QList<SchematicItem*>& items) const {
    std::vector<ContextAction> result;
    
    // 1. Collect global actions applicable to selection
    for (const auto& action : m_globalActions) {
        if (action.isVisible(items)) {
            result.push_back(action);
        }
    }
    
    // 2. Identify unique types in selection
    std::set<SchematicItem::ItemType> types;
    for (auto* item : items) {
        types.insert(item->itemType());
    }
    
    // 3. Collect type-specific actions
    for (auto type : types) {
        auto range = m_typeActions.equal_range(type);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second.isVisible(items)) {
                result.push_back(it->second);
            }
        }
    }
    
    // 4. Sort by priority
    std::sort(result.begin(), result.end(), [](const ContextAction& a, const ContextAction& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority; 
        return a.label < b.label;
    });
    
    return result;
}

void SchematicMenuRegistry::initializeDefaultActions() {
    m_globalActions.clear();
    m_typeActions.clear();

    // --- Standard Edit Actions ---
    
    ContextAction cut;
    cut.label = "Cut";
    cut.shortcut = QKeySequence::Cut;
    cut.priority = 50;
    cut.isVisible = [](const auto& items) { return !items.isEmpty(); };
    cut.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onCut();
    };
    registerGlobalAction(cut);

    ContextAction copy;
    copy.label = "Copy";
    copy.shortcut = QKeySequence::Copy;
    copy.priority = 49;
    copy.isVisible = [](const auto& items) { return !items.isEmpty(); };
    copy.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onCopy();
    };
    registerGlobalAction(copy);

    ContextAction paste;
    paste.label = "Paste";
    paste.shortcut = QKeySequence::Paste;
    paste.priority = 48;
    paste.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onPaste();
    };
    registerGlobalAction(paste);

    ContextAction del;
    del.label = "Delete";
    del.shortcut = QKeySequence();
    del.priority = 47;
    del.isVisible = [](const auto& items) { return !items.isEmpty(); };
    del.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onDelete();
    };
    registerGlobalAction(del);

    registerGlobalAction(ContextAction::separator(40));

    // --- View Actions ---

    ContextAction zoomFit;
    zoomFit.label = "Zoom Fit";
    zoomFit.shortcut = QKeySequence("F");
    zoomFit.priority = 30;
    zoomFit.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onZoomFit();
    };
    registerGlobalAction(zoomFit);

    ContextAction zoomSel;
    zoomSel.label = "Zoom to Selection";
    zoomSel.shortcut = QKeySequence("Ctrl+0");
    zoomSel.priority = 29;
    zoomSel.isVisible = [](const auto& items) { return !items.isEmpty(); };
    zoomSel.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onZoomSelection();
    };
    registerGlobalAction(zoomSel);

    registerGlobalAction(ContextAction::separator(20));

    // --- Tuning Actions ---
    ContextAction addSlider;
    addSlider.label = "Add Tuning Slider";
    addSlider.priority = 15;
    addSlider.isVisible = [](const auto& items) {
        if (items.size() != 1) return false;
        auto type = items.first()->itemType();
        return (type == SchematicItem::ResistorType || 
                type == SchematicItem::CapacitorType || 
                type == SchematicItem::InductorType || 
                type == SchematicItem::VoltageSourceType);
    };
    addSlider.isEnabled = [](const auto&) {
        auto* editor = qobject_cast<SchematicEditor*>(QApplication::activeWindow());
        if (editor && editor->getSimulationPanel()) {
            return editor->getSimulationPanel()->isRealTimeMode();
        }
        return false;
    };
    addSlider.handler = [](SchematicView* view, const auto& items) {
        if (items.isEmpty()) return;
        auto* slider = new TuningSliderItem(items.first());
        view->scene()->addItem(slider);
    };
    registerGlobalAction(addSlider);

    ContextAction selectAll;
    selectAll.label = "Select All";
    selectAll.shortcut = QKeySequence::SelectAll;
    selectAll.priority = 10;
    selectAll.handler = [](SchematicView* view, const auto&) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) editor->onSelectAll();
    };
    registerGlobalAction(selectAll);

    ContextAction runERC;
    runERC.label = "Run Live ERC";
    runERC.priority = 9;
    runERC.handler = [](SchematicView* view, const auto& items) {
        if (auto* editor = qobject_cast<SchematicEditor*>(view->parent())) {
            editor->runLiveERC(items);
        } else if (auto* editor = qobject_cast<SchematicEditor*>(view->window())) {
            editor->runLiveERC(items);
        }
    };
    registerGlobalAction(runERC);

    // --- Wire Actions ---
    
    ContextAction editNetLabel;
    editNetLabel.label = "Edit Net Label...";
    editNetLabel.priority = 100;
    editNetLabel.isVisible = [](const QList<SchematicItem*>& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::WireType;
    };
    editNetLabel.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
        auto* wire = dynamic_cast<WireItem*>(items.first());
        if (!wire || !view) return;

        QPointF pos;
        const QList<QPointF> pts = wire->points();
        if (!pts.isEmpty()) {
            const QPointF a = wire->mapToScene(pts.first());
            const QPointF b = wire->mapToScene(pts.last());
            pos = (a + b) * 0.5;
        } else {
            pos = wire->scenePos();
        }
        pos = view->snapToGridOrPin(pos).point;

        QString netName = "NET";
        if (auto* nm = view->netManager()) {
            nm->updateNets(view->scene());
            netName = nm->findNetAtPoint(pos);
            if (netName.isEmpty()) netName = "NET";
        }

        NetLabelDialogResult res = promptNetLabelDialog(view, "Net Label", netName, HierarchicalPortItem::Passive, true);
        if (!res.accepted) return;

        auto* item = new HierarchicalPortItem(pos, res.label, res.portType);
        view->undoStack()->push(new AddItemCommand(view->scene(), item));
    };
    registerAction(SchematicItem::WireType, editNetLabel);

    ContextAction wireInfo;
    wireInfo.label = "Info...";
    wireInfo.priority = 95;
    wireInfo.isVisible = [](const QList<SchematicItem*>& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::WireType;
    };
    wireInfo.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
        auto* wire = dynamic_cast<WireItem*>(items.first());
        if (!wire || !view) return;

        const QList<QPointF> pts = wire->points();
        if (pts.isEmpty()) return;

        NetManager* netManager = view->netManager();
        QString netName = "N/A";
        QString netEndName;
        QPointF startScene = wire->mapToScene(pts.first());
        QPointF endScene = wire->mapToScene(pts.last());
        if (netManager && view->scene()) {
            netManager->updateNets(view->scene());
            netName = netManager->findNetAtPoint(startScene);
            netEndName = netManager->findNetAtPoint(endScene);
            if (netName.isEmpty()) netName = "N/A";
            if (!netEndName.isEmpty() && netEndName != netName) {
                netName = QString("%1 -> %2").arg(netName, netEndName);
            }
        }

        double totalLength = 0.0;
        for (int i = 0; i < pts.size() - 1; ++i) {
            const QPointF a = wire->mapToScene(pts[i]);
            const QPointF b = wire->mapToScene(pts[i + 1]);
            totalLength += QLineF(a, b).length();
        }

        QString wireType = (wire->wireType() == WireItem::PowerWire) ? "Power" : "Signal";
        QString styleStr = "Solid";
        if (wire->pen().style() == Qt::DashLine) styleStr = "Dash";
        else if (wire->pen().style() == Qt::DotLine) styleStr = "Dot";

        QString info = QString(
            "Net: %1\n"
            "Type: %2\n"
            "Points: %3\n"
            "Segments: %4\n"
            "Length (scene units): %5\n"
            "Start: (%6, %7)\n"
            "End: (%8, %9)\n"
            "Junctions: %10\n"
            "Jump-overs: %11\n"
            "Line width: %12\n"
            "Line style: %13")
            .arg(netName)
            .arg(wireType)
            .arg(pts.size())
            .arg(qMax(0, pts.size() - 1))
            .arg(QString::number(totalLength, 'f', 2))
            .arg(QString::number(startScene.x(), 'f', 2))
            .arg(QString::number(startScene.y(), 'f', 2))
            .arg(QString::number(endScene.x(), 'f', 2))
            .arg(QString::number(endScene.y(), 'f', 2))
            .arg(wire->junctions().size())
            .arg(wire->jumpOvers().size())
            .arg(QString::number(wire->pen().widthF(), 'f', 2))
            .arg(styleStr);

        QMessageBox::information(view, "Wire Info", info);
    };
    registerAction(SchematicItem::WireType, wireInfo);

    // --- LED Actions ---
    ContextAction ledOptions;
    ledOptions.label = "LED Properties...";
    ledOptions.priority = 90;
    ledOptions.isVisible = [](const QList<SchematicItem*>& items) {
        if (items.size() != 1) return false;
        const QString t = items.first()->itemTypeName();
        return t == "LED" || t == "Blinking LED";
    };
    ledOptions.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
        if (!view || items.size() != 1) return;
        SchematicItem* base = items.first();
        LedPropertiesDialog dlg(base, view->scene(), view);
        dlg.exec();
    };
    registerGlobalAction(ledOptions);

    // --- Bus Actions ---
    
    ContextAction editBusLabel;
    editBusLabel.label = "Edit Bus Label...";
    editBusLabel.priority = 100;
    editBusLabel.isVisible = [](const QList<SchematicItem*>& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::BusType;
    };
    editBusLabel.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
        emit view->itemDoubleClicked(items.first());
    };
    registerAction(SchematicItem::BusType, editBusLabel);

    // --- Net Label Actions ---
    
    ContextAction editNetLabelItem;
    editNetLabelItem.label = "Edit Label...";
    editNetLabelItem.priority = 100;
    editNetLabelItem.isVisible = [](const QList<SchematicItem*>& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::NetLabelType;
    };
    editNetLabelItem.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
        emit view->itemDoubleClicked(items.first());
    };
    registerAction(SchematicItem::NetLabelType, editNetLabelItem);

    ContextAction rotateNetLabel;
    rotateNetLabel.label = "Rotate (90° CCW)";
    rotateNetLabel.shortcut = QKeySequence("Space");
    rotateNetLabel.priority = 90;
    rotateNetLabel.isVisible = [](const QList<SchematicItem*>& items) {
        return !items.isEmpty() && items.first()->itemType() == SchematicItem::NetLabelType;
    };
    rotateNetLabel.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
        if (view->undoStack()) {
            view->undoStack()->push(new RotateItemCommand(view->scene(), items, 90));
        }
    };
    registerAction(SchematicItem::NetLabelType, rotateNetLabel);

    // --- Symbol Actions ---

    auto registerSymbolActions = [&](SchematicItem::ItemType type) {
        ContextAction rotate;
        rotate.label = "Rotate (90° CCW)";
        rotate.shortcut = QKeySequence("Space");
        rotate.priority = 90;
        rotate.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
            if (view->undoStack()) {
                view->undoStack()->push(new RotateItemCommand(view->scene(), items, 90));
            }
        };
        registerAction(type, rotate);

        ContextAction rotateCW;
        rotateCW.label = "Rotate (90° CW)";
        rotateCW.shortcut = QKeySequence("Shift+Space");
        rotateCW.priority = 89;
        rotateCW.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
            if (view->undoStack()) {
                view->undoStack()->push(new RotateItemCommand(view->scene(), items, -90));
            }
        };
        registerAction(type, rotateCW);

        ContextAction flip;
        flip.label = "Flip Horizontal";
        flip.shortcut = QKeySequence("F");
        flip.priority = 80;
        flip.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
            if (view->undoStack()) {
                view->undoStack()->push(new FlipItemCommand(view->scene(), items));
            }
        };
        registerAction(type, flip);

        ContextAction flipV;
        flipV.label = "Flip Vertical";
        flipV.shortcut = QKeySequence("V");
        flipV.priority = 79;
        flipV.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
            if (view->undoStack()) {
                // Assuming FlipItemCommand handles vertical if we add a param, or we add a new command
                // For now, let's assume we need to implement Vertical Flip
                view->undoStack()->push(new FlipItemCommand(view->scene(), items, true /* vertical */));
            }
        };
        registerAction(type, flipV);

        ContextAction datasheet;
        datasheet.label = "View Datasheet";
        datasheet.priority = 70;
        datasheet.isVisible = [](const QList<SchematicItem*>& items) {
            if (items.size() != 1) return false;
            auto* generic = dynamic_cast<GenericComponentItem*>(items.first());
            return generic && !generic->symbol().datasheet().isEmpty();
        };
        datasheet.handler = [](SchematicView*, const QList<SchematicItem*>& items) {
            auto* generic = dynamic_cast<GenericComponentItem*>(items.first());
            QDesktopServices::openUrl(QUrl::fromLocalFile(generic->symbol().datasheet()));
        };
        registerAction(type, datasheet);

        ContextAction toBehavioral;
        toBehavioral.label = "Convert to Behavioral";
        toBehavioral.priority = 60;
        toBehavioral.isVisible = [type](const QList<SchematicItem*>& items) {
            if (items.size() != 1) return false;
            // Only for sources, resistors, capacitors for now
            return (type == SchematicItem::VoltageSourceType || 
                    type == SchematicItem::ResistorType || 
                    type == SchematicItem::CapacitorType);
        };
        toBehavioral.handler = [](SchematicView* view, const QList<SchematicItem*>& items) {
            if (items.size() != 1) return;
            // This would involve creating a new BV/BI item and replacing the current one
            QMessageBox::information(view, "Premium Feature", "Converting to Behavioral Source (BV/BI) is coming in the next minor update!");
        };
        registerAction(type, toBehavioral);
    };

    registerSymbolActions(SchematicItem::ResistorType);
    registerSymbolActions(SchematicItem::CapacitorType);
    registerSymbolActions(SchematicItem::InductorType);
    registerSymbolActions(SchematicItem::ComponentType);
    registerSymbolActions(SchematicItem::DiodeType);
    registerSymbolActions(SchematicItem::TransistorType);
    registerSymbolActions(SchematicItem::ICType);
    registerSymbolActions(SchematicItem::VoltageSourceType);

    // --- Voltage Source Specialized Actions ---
    
    ContextAction editWaveform;
    editWaveform.label = "Configure Waveform...";
    editWaveform.priority = 110;
    editWaveform.isVisible = [](const auto& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::VoltageSourceType;
    };
    editWaveform.handler = [](SchematicView* view, const auto& items) {
        emit view->itemDoubleClicked(items.first());
    };
    registerAction(SchematicItem::VoltageSourceType, editWaveform);

    ContextAction setDC;
    setDC.label = "Quick Set DC Value...";
    setDC.priority = 109;
    setDC.isVisible = [](const auto& items) {
        if (items.size() != 1) return false;
        auto* vsrc = dynamic_cast<VoltageSourceItem*>(items.first());
        return vsrc && vsrc->sourceType() == VoltageSourceItem::DC;
    };
    setDC.handler = [](SchematicView* view, const auto& items) {
        auto* vsrc = dynamic_cast<VoltageSourceItem*>(items.first());
        bool ok;
        QString val = QInputDialog::getText(view, "Quick Edit", "DC Voltage (V):", QLineEdit::Normal, vsrc->dcVoltage(), &ok);
        if (ok && view->undoStack()) {
            view->undoStack()->push(new ChangePropertyCommand(view->scene(), vsrc, "DC Voltage", vsrc->dcVoltage(), val));
        }
    };
    registerAction(SchematicItem::VoltageSourceType, setDC);

    ContextAction setSine;
    setSine.label = "Quick Set Sine Parameters...";
    setSine.priority = 108;
    setSine.isVisible = [](const auto& items) {
        if (items.size() != 1) return false;
        auto* vsrc = dynamic_cast<VoltageSourceItem*>(items.first());
        return vsrc && vsrc->sourceType() == VoltageSourceItem::Sine;
    };
    setSine.handler = [](SchematicView* view, const auto& items) {
        emit view->itemDoubleClicked(items.first()); // Since Sine needs multiple fields, just open properties
    };
    registerAction(SchematicItem::VoltageSourceType, setSine);

    ContextAction setPulse;
    setPulse.label = "Quick Set Pulse Parameters...";
    setPulse.priority = 107;
    setPulse.isVisible = [](const auto& items) {
        if (items.size() != 1) return false;
        auto* vsrc = dynamic_cast<VoltageSourceItem*>(items.first());
        return vsrc && vsrc->sourceType() == VoltageSourceItem::Pulse;
    };
    setPulse.handler = [](SchematicView* view, const auto& items) {
        emit view->itemDoubleClicked(items.first()); // Same for pulse
    };
    registerAction(SchematicItem::VoltageSourceType, setPulse);

    // --- Label / Text Actions ---
    
    ContextAction editText;
    editText.label = "Edit Text...";
    editText.priority = 100;
    editText.isVisible = [](const auto& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::LabelType;
    };
    editText.handler = [](SchematicView* view, const auto& items) {
        emit view->itemDoubleClicked(items.first());
    };
    registerAction(SchematicItem::LabelType, editText);

    ContextAction rotateLabel;
    rotateLabel.label = "Rotate (90° CCW)";
    rotateLabel.shortcut = QKeySequence("Space");
    rotateLabel.priority = 90;
    rotateLabel.handler = [](SchematicView* view, const auto& items) {
        if (view->undoStack()) {
            view->undoStack()->push(new RotateItemCommand(view->scene(), items, 90));
        }
    };
    registerAction(SchematicItem::LabelType, rotateLabel);

    ContextAction resetPos;
    resetPos.label = "Reset to Default Position";
    resetPos.priority = 85;
    resetPos.isVisible = [](const auto& items) {
        // Only show if it's a sub-item (part of a component)
        return !items.isEmpty() && items.first()->isSubItem();
    };
    resetPos.handler = [](SchematicView*, const auto& items) {
        for (auto* it : items) {
            if (auto* parent = dynamic_cast<SchematicItem*>(it->parentItem())) {
                parent->resetLabels();
            }
        }
    };
    registerAction(SchematicItem::LabelType, resetPos);

    ContextAction toggleVis;
    toggleVis.label = "Hide Label";
    toggleVis.priority = 80;
    toggleVis.isVisible = [](const auto& items) {
        return !items.isEmpty() && items.first()->isVisible();
    };
    toggleVis.handler = [](SchematicView*, const auto& items) {
        for (auto* it : items) it->setVisible(false);
    };
    registerAction(SchematicItem::LabelType, toggleVis);

    // --- Sheet Actions ---
    
    ContextAction enterSheet;
    enterSheet.label = "Enter Sheet";
    enterSheet.priority = 100;
    enterSheet.isVisible = [](const auto& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::SheetType;
    };
    enterSheet.handler = [](SchematicView* view, const auto& items) {
        emit view->itemDoubleClicked(items.first());
    };
    registerAction(SchematicItem::SheetType, enterSheet);

    ContextAction syncPins;
    syncPins.label = "Synchronize Pins";
    syncPins.priority = 99;
    syncPins.isVisible = [](const auto& items) {
        return items.size() == 1 && items.first()->itemType() == SchematicItem::SheetType;
    };
    syncPins.handler = [](SchematicView* view, const auto& items) {
        emit view->syncSheetRequested(static_cast<SchematicSheetItem*>(items.first()));
    };
    registerAction(SchematicItem::SheetType, syncPins);
}
