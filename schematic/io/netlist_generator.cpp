#include "netlist_generator.h"
#include "schematic_item.h"
#include "generic_component_item.h"
#include "wire_item.h"
#include "net_manager.h"
#include "schematic_file_io.h"
#include "../items/schematic_sheet_item.h"
#include "../items/net_label_item.h"
#include "../items/hierarchical_port_item.h"
#include "../items/voltage_source_item.h"
#include <QGraphicsScene>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <functional>
#include <algorithm>
#include <QDir>
#include <QSet>

using Flux::Model::SymbolPrimitive;

namespace {
QString exportRefForItem(const SchematicItem* item, const QString& prefix) {
    if (!item) return prefix + "UNKNOWN";
    const QString ref = item->reference().trimmed();
    if (!ref.isEmpty()) return prefix + ref;
    const QString stableId = item->id().toString(QUuid::WithoutBraces);
    return prefix + QString("%1_%2").arg(item->itemTypeName(), stableId);
}
}

struct StringDSU {
    QMap<QString, QString> parent;
    QString find(QString i) {
        if (!parent.contains(i)) return parent[i] = i;
        if (parent[i] == i) return i;
        return parent[i] = find(parent[i]);
    }
    void unite(QString i, QString j) {
        QString root_i = find(i);
        QString root_j = find(j);
        if (root_i != root_j) parent[root_i] = root_j;
    }
};

QString NetlistGenerator::generate(QGraphicsScene* scene, const QString& projectDir, Format format, NetManager* netManager) {
    if (!scene) return QString();
    
    QList<NetlistNet> nets = buildConnectivity(scene, projectDir, netManager);
    
    // Filter components marked for PCB exclusion
    QSet<QString> excludedRefs;
    for (auto* item : scene->items()) {
        if (auto* si = dynamic_cast<SchematicItem*>(item)) {
            if (si->excludeFromPcb()) excludedRefs.insert(si->reference());
        }
    }

    if (!excludedRefs.isEmpty()) {
        for (int i = 0; i < nets.size(); ++i) {
            QList<NetlistPin> filteredPins;
            for (const auto& pin : nets[i].pins) {
                if (!excludedRefs.contains(pin.componentRef)) {
                    filteredPins.append(pin);
                }
            }
            nets[i].pins = filteredPins;
        }
        // Remove empty nets
        nets.erase(std::remove_if(nets.begin(), nets.end(), [](const NetlistNet& n){
            return n.pins.isEmpty();
        }), nets.end());
    }

    QString output;

    if (format == FluxJSON) {
        QJsonObject root;
        root["generator"] = "viospice";
        root["date"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["version"] = "1.0";
        
        QJsonArray netsArray;
        for (const auto& net : nets) {
            QJsonObject netObj;
            netObj["name"] = net.name;
            QJsonArray pinsArray;
            for (const auto& pin : net.pins) {
                QJsonObject pinObj;
                pinObj["ref"] = pin.componentRef;
                pinObj["pin"] = pin.pinName;
                pinsArray.append(pinObj);
            }
            netObj["pins"] = pinsArray;
            netsArray.append(netObj);
        }
        root["nets"] = netsArray;
        
        QJsonDocument doc(root);
        output = doc.toJson(QJsonDocument::Indented);
    } 
    else if (format == IPC356) {
        output += "C   HIERARCHICAL EXPORT\n";
        output += "P  JOB " + QFileInfo(projectDir).baseName().toUpper() + "\n";
        output += "P  UNITS MM\n";
        
        for (const auto& net : nets) {
            for (const auto& pin : net.pins) {
                output += QString("317 %1 %2 %3\n")
                    .arg(net.name.left(14).trimmed().leftJustified(14))
                    .arg(pin.componentRef.left(6).trimmed().leftJustified(6))
                    .arg(pin.pinName.left(4).trimmed().leftJustified(4));
            }
        }
    }
    else if (format == Protel) {
        for (const auto& net : nets) {
             output += "[\n";
             output += QString("  %1\n").arg(net.name);
             for (const auto& pin : net.pins) {
                 output += QString("    %1-%2\n").arg(pin.componentRef, pin.pinName);
             }
             output += "]\n";
        }
    }
    
    return output;
}

ECOPackage NetlistGenerator::generateECOPackage(QGraphicsScene* scene, const QString& projectDir, NetManager* netManager) {
    if (!scene) return ECOPackage();

    ECOPackage pkg;
    
    // 1. Collect Connectivity (Nets)
    QList<NetlistNet> nets = buildConnectivity(scene, projectDir, netManager);
    for (const auto& net : nets) {
        ECONet ecoNet;
        ecoNet.name = net.name;
        for (const auto& pin : net.pins) {
            ecoNet.pins.append({pin.componentRef, pin.pinName});
        }
        pkg.nets.append(ecoNet);
    }

    // 2. Collect Components (Hierarchical)
    std::function<void(QGraphicsScene*, const QString&, const QString&, int)> collectComponents;
    QSet<QString> activeSheetStack;
    constexpr int kMaxHierarchyDepth = 64;
    collectComponents = [&](QGraphicsScene* s, const QString& prefix, const QString& sceneFilePath, int depth) {
        if (!s) return;
        if (depth > kMaxHierarchyDepth) {
            qWarning() << "NetlistGenerator: Hierarchy depth exceeded while collecting components at" << sceneFilePath;
            return;
        }

        const QString canonicalSceneFile = sceneFilePath.isEmpty()
            ? QString()
            : QFileInfo(sceneFilePath).canonicalFilePath();
        const bool trackSceneFile = !canonicalSceneFile.isEmpty();
        if (trackSceneFile && activeSheetStack.contains(canonicalSceneFile)) {
            qWarning() << "NetlistGenerator: Sheet recursion detected, skipping:" << canonicalSceneFile;
            return;
        }
        if (trackSceneFile) activeSheetStack.insert(canonicalSceneFile);

        for (auto* item : s->items()) {
            SchematicItem* sItem = dynamic_cast<SchematicItem*>(item);
            if (!sItem) continue;

            if (sItem->itemType() == SchematicItem::SheetType) {
                SchematicSheetItem* sheet = static_cast<SchematicSheetItem*>(sItem);
                QString childFile = sheet->fileName();
                if (QFileInfo(childFile).isRelative() && !projectDir.isEmpty()) {
                    childFile = projectDir + "/" + childFile;
                }
                
                if (QFile::exists(childFile)) {
                    QGraphicsScene* childScene = new QGraphicsScene();
                    QString dummySize;
                    TitleBlockData dummyTB;
                    QString dummyScript;
                    QMap<QString, QStringList> dummyBusAliases;
                    if (SchematicFileIO::loadSchematic(childScene, childFile, dummySize, dummyTB, &dummyScript, &dummyBusAliases)) {
                        collectComponents(childScene, prefix + sheet->sheetName() + "/", childFile, depth + 1);
                    }
                    delete childScene;
                }
            } else if (sItem->itemType() != SchematicItem::WireType && 
                       sItem->itemType() != SchematicItem::LabelType && 
                       sItem->itemType() != SchematicItem::NetLabelType && 
                       sItem->itemType() != SchematicItem::JunctionType &&
                       sItem->itemType() != SchematicItem::NoConnectType &&
                       sItem->itemType() != SchematicItem::BusType &&
                       sItem->itemType() != SchematicItem::SpiceDirectiveType &&
                       sItem->itemTypeName() != "BusEntry") {
                
                // Note: HierarchicalPortType and PowerType are now INCLUDED so they appear in netlist/simulator
                ECOComponent comp;
                comp.reference = exportRefForItem(sItem, prefix);
                comp.footprint = sItem->footprint().trimmed();
                comp.symbolPinCount = sItem->connectionPoints().size();
                comp.manufacturer = sItem->manufacturer();
                comp.mpn = sItem->mpn();
                comp.spiceModel = sItem->spiceModel();
                
                comp.value = sItem->value();

                comp.type = sItem->itemType();
                comp.typeName = sItem->itemTypeName();
                comp.excludeFromSim = sItem->excludeFromSimulation();
                comp.excludeFromPcb = sItem->excludeFromPcb();
                comp.paramExpressions = sItem->paramExpressions();
                comp.tolerances = sItem->tolerances();
                comp.pinPadMapping = sItem->pinPadMapping();
                
                // Heuristic: If value starts with {, treat it as an expression for the primary param
                if (comp.value.startsWith("{") && comp.value.endsWith("}")) {
                    QString primary = "resistance";
                    if (comp.type == SchematicItem::CapacitorType) primary = "capacitance";
                    else if (comp.type == SchematicItem::InductorType) primary = "inductance";
                    else if (comp.type == SchematicItem::VoltageSourceType) primary = "voltage";
                    
                    if (!comp.paramExpressions.contains(primary)) {
                        comp.paramExpressions[primary] = comp.value;
                    }
                }

                pkg.components.append(comp);
            }
        }

        if (trackSceneFile) activeSheetStack.remove(canonicalSceneFile);
    };

    collectComponents(scene, "", QString(), 0);

    return pkg;
}

QList<NetlistNet> NetlistGenerator::buildConnectivity(QGraphicsScene* scene, const QString& projectDir, NetManager* /*netManager*/) {
    if (!scene) return {};

    StringDSU dsu;
    QMap<QString, QList<NetlistPin>> netToPins;
    
    std::function<void(QGraphicsScene*, const QString&, const QString&, int)> traverse;
    QSet<QString> activeSheetStack;
    constexpr int kMaxHierarchyDepth = 64;
    traverse = [&](QGraphicsScene* s, const QString& prefix, const QString& sceneFilePath, int depth) {
        qDebug() << "NetlistGenerator: Traversing scene with prefix:" << prefix;
        if (!s) { qDebug() << "NetlistGenerator: Scene is null!"; return; }
        if (depth > kMaxHierarchyDepth) {
            qWarning() << "NetlistGenerator: Hierarchy depth exceeded while traversing at" << sceneFilePath;
            return;
        }

        const QString canonicalSceneFile = sceneFilePath.isEmpty()
            ? QString()
            : QFileInfo(sceneFilePath).canonicalFilePath();
        const bool trackSceneFile = !canonicalSceneFile.isEmpty();
        if (trackSceneFile && activeSheetStack.contains(canonicalSceneFile)) {
            qWarning() << "NetlistGenerator: Sheet recursion detected, skipping:" << canonicalSceneFile;
            return;
        }
        if (trackSceneFile) activeSheetStack.insert(canonicalSceneFile);
        
        NetManager localManager;
        localManager.updateNets(s);
        qDebug() << "NetlistGenerator: Local nets rebuilt.";
        
        QStringList netNames = localManager.netNames();
        for (const QString& ln : netNames) {
            QString gNetId = "NET:" + prefix + ln;
            dsu.unite(gNetId, gNetId);
            
            QList<NetConnection> conns = localManager.getConnections(ln);
            for (const auto& conn : conns) {
                if (!conn.item) continue;
                
                auto type = conn.item->itemType();
                if (type == SchematicItem::NetLabelType) {
                    auto* lbl = static_cast<NetLabelItem*>(conn.item);
                    if (lbl->labelScope() == NetLabelItem::Global) {
                        dsu.unite(gNetId, "GLOBAL:" + lbl->label());
                    }
                } else if (type == SchematicItem::PowerType) {
                    // Power symbols are global by name
                    QString globalName = conn.item->value();
                    if (globalName.isEmpty()) globalName = conn.item->name(); // Fallback to default name
                    dsu.unite(gNetId, "GLOBAL:" + globalName);
                    
                    // Register as a pin so bridge finds it
                    NetlistPin pin;
                    pin.componentRef = exportRefForItem(conn.item, prefix);
                    pin.pinName = conn.pinName.isEmpty() ? "1" : conn.pinName;
                    netToPins[gNetId].append(pin);
                } else if (type == SchematicItem::HierarchicalPortType) {
                    auto* port = static_cast<HierarchicalPortItem*>(conn.item);
                    dsu.unite(gNetId, "PORT:" + prefix + port->label());
                } else if (type == SchematicItem::SheetType) {
                    auto* sheet = static_cast<SchematicSheetItem*>(conn.item);
                    for (auto* pin : sheet->getPins()) {
                        // Check if connection point is at this pin
                        if (conn.item->mapToScene(pin->pos()) == conn.connectionPoint) {
                            dsu.unite(gNetId, "PORT:" + prefix + sheet->sheetName() + "/" + pin->name());
                        }
                    }
                } else if (type != SchematicItem::WireType && 
                           type != SchematicItem::JunctionType && 
                           type != SchematicItem::BusType &&
                           type != SchematicItem::NoConnectType &&
                           conn.item->itemTypeName() != "BusEntry") {
                    
                    if (conn.item->excludeFromSimulation()) continue;

                    NetlistPin pin;
                    pin.componentRef = exportRefForItem(conn.item, prefix);
                    pin.pinName = conn.pinName.isEmpty() ? "1" : conn.pinName;
                    netToPins[gNetId].append(pin);

                    // --- PIN STACKING LOGIC ---
                    if (auto* generic = dynamic_cast<GenericComponentItem*>(conn.item)) {
                        for (const auto& prim : generic->symbol().primitives()) {
                            if (prim.type == SymbolPrimitive::Pin) {
                                QString numStr = QString::number(prim.data.value("number").toInt());
                                if (numStr == conn.pinName) {
                                    QString stacked = prim.data.value("stackedNumbers").toString();
                                    if (!stacked.isEmpty()) {
                                        for (const QString& en : stacked.split(",", Qt::SkipEmptyParts)) {
                                            NetlistPin ep;
                                            ep.componentRef = pin.componentRef;
                                            ep.pinName = en.trimmed();
                                            if (!netToPins[gNetId].contains(ep)) netToPins[gNetId].append(ep);
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        qDebug() << "NetlistGenerator: Connectivity processed.";

        // --- HIDDEN PIN LOGIC ---
        // Scan all components for pins marked as hidden (visible: false)
        // and connect them to global nets matching their name.
        for (auto* item : s->items()) {
            if (auto* generic = dynamic_cast<GenericComponentItem*>(item)) {
                if (generic->excludeFromSimulation()) continue;

                // Filter by unit: only process pins for this instance's unit (or shared)
                int instanceUnit = generic->unit();
                
                for (const auto& prim : generic->symbol().primitives()) {
                    if (prim.type == SymbolPrimitive::Pin) {
                        // Skip if not for this unit
                        if (prim.unit() != 0 && prim.unit() != instanceUnit) continue;

                        if (prim.data.value("visible").toBool(true) == false) {
                            QString pinName = prim.data.value("name").toString();
                            if (pinName.isEmpty()) continue;

                            // Connect this hidden pin to a global net of the same name
                            QString globalNetId = "GLOBAL:" + pinName;
                            dsu.unite(globalNetId, globalNetId); // Ensure net exists

                            NetlistPin p;
                            p.componentRef = exportRefForItem(generic, prefix);
                            p.pinName = QString::number(prim.data.value("number").toInt());
                            
                            // Add pin to the global net
                            netToPins[globalNetId].append(p);

                            // Also handle stacking for hidden pins
                            QString stacked = prim.data.value("stackedNumbers").toString();
                            if (!stacked.isEmpty()) {
                                for (const QString& en : stacked.split(",", Qt::SkipEmptyParts)) {
                                    NetlistPin ep;
                                    ep.componentRef = p.componentRef;
                                    ep.pinName = en.trimmed();
                                    if (!netToPins[globalNetId].contains(ep)) netToPins[globalNetId].append(ep);
                                }
                            }
                        }
                    }
                }
            }
        }
        qDebug() << "NetlistGenerator: Hidden pins processed.";

        // --- JUMPER GROUP LOGIC ---
        // Second pass over the local items to find pins in the same jumper group
        // and unite their nets in the DSU.
        
        // Find nets connected to each pin (only need to do this ONCE for the scene!)
        localManager.updateNets(s);
        QStringList localNets = localManager.netNames();

        for (auto* item : s->items()) {
            if (auto* generic = dynamic_cast<GenericComponentItem*>(item)) {
                if (generic->excludeFromSimulation()) continue;

                QMap<int, QString> groupToNet; // jumperGroupId -> globalNetId

                for (const auto& prim : generic->symbol().primitives()) {
                    if (prim.type == SymbolPrimitive::Pin) {
                        int jg = prim.data.value("jumperGroup").toInt(0);
                        if (jg <= 0) continue;
                        
                        int pinNum = prim.data["number"].toInt();
                        QString pinName = QString::number(pinNum);
                        
                        // Find which local net this pin belongs to
                        QString foundNet;
                        for (const QString& ln : localNets) {
                            for (const auto& conn : localManager.getConnections(ln)) {
                                if (conn.item == generic && conn.pinName == pinName) {
                                    foundNet = ln;
                                    break;
                                }
                            }
                            if (!foundNet.isEmpty()) break;
                        }
                        
                        if (!foundNet.isEmpty()) {
                            QString gNetId = "NET:" + prefix + foundNet;
                            if (groupToNet.contains(jg)) {
                                dsu.unite(gNetId, groupToNet[jg]);
                            } else {
                                groupToNet[jg] = gNetId;
                            }
                        }
                    }
                }
            }
        }
        qDebug() << "NetlistGenerator: Jumper groups processed.";
        
        // Recurse
        for (auto* item : s->items()) {
            if (auto* sheet = dynamic_cast<SchematicSheetItem*>(item)) {
                QString childFile = sheet->fileName();
                qDebug() << "NetlistGenerator: Recursing into sheet:" << childFile;
                if (QFileInfo(childFile).isRelative() && !projectDir.isEmpty()) {
                    childFile = projectDir + "/" + childFile;
                }
                
                if (QFile::exists(childFile)) {
                    QGraphicsScene* childScene = new QGraphicsScene();
                    QString dummySize;
                    TitleBlockData dummyTB;
                    QString dummyScript;
                    QMap<QString, QStringList> dummyBusAliases;
                    if (SchematicFileIO::loadSchematic(childScene, childFile, dummySize, dummyTB, &dummyScript, &dummyBusAliases)) {
                        traverse(childScene, prefix + sheet->sheetName() + "/", childFile, depth + 1);
                    }
                    delete childScene;
                }
            }
        }

        if (trackSceneFile) activeSheetStack.remove(canonicalSceneFile);
    };

    traverse(scene, "", QString(), 0);

    // Final merge
    QMap<QString, NetlistNet> finalNets;
    for (auto it = netToPins.begin(); it != netToPins.end(); ++it) {
        QString root = dsu.find(it.key());
        if (!finalNets.contains(root)) {
            QString displayName = root;
            if (root.startsWith("GLOBAL:")) displayName = root.mid(7);
            else if (root.startsWith("PORT:")) displayName = root.mid(5).replace("/", "_");
            else if (root.startsWith("NET:")) displayName = root.mid(4).replace("/", "_");
            
            finalNets[root].name = displayName;
        }
        
        for (const auto& pin : it.value()) {
            if (!finalNets[root].pins.contains(pin))
                finalNets[root].pins.append(pin);
        }
    }

    QList<NetlistNet> result;
    for (auto it = finalNets.begin(); it != finalNets.end(); ++it) {
        NetlistNet& net = it.value();
        const bool isLogicalGlobal = net.name.toUpper() == "GND" || it.key().startsWith("GLOBAL:");
        if (net.pins.size() >= 1) {
            std::sort(net.pins.begin(), net.pins.end(), [](const NetlistPin& a, const NetlistPin& b) {
                if (a.componentRef != b.componentRef) return a.componentRef < b.componentRef;
                return a.pinName < b.pinName;
            });
            result.append(net);
        }
    }

    return result;
}
