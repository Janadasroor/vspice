#include "schematic_erc.h"
#include "schematic_item.h"
#include "net_manager.h"
#include "power_item.h"
#include "wire_item.h"
#include "../items/schematic_sheet_item.h"
#include "../items/hierarchical_port_item.h"
#include "../io/schematic_file_io.h"
#include <QSet>
#include <QMap>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

QList<ERCViolation> SchematicERC::run(QGraphicsScene* scene, const QString& projectDir, NetManager* netManager, const SchematicERCRules& rules) {
    QList<ERCViolation> violations;
    if (!scene) return violations;

    // 1. Local Analysis
    NetManager* localNM = netManager;
    bool ownNM = false;
    if (!localNM) {
        localNM = new NetManager();
        ownNM = true;
    }
    localNM->updateNets(scene);

    QList<SchematicItem*> schItems;
    for (QGraphicsItem* item : scene->items()) {
        if (SchematicItem* si = dynamic_cast<SchematicItem*>(item)) {
            schItems.append(si);
        }
    }

    // --- Duplicate Designators (Recursive Hierarchy) ---
    QMap<QString, QString> designatorToSheet;
    QSet<QString> visitedFiles;
    
    std::function<void(QGraphicsScene*, const QString&)> checkDuplicates;
    checkDuplicates = [&](QGraphicsScene* activeScene, const QString& sheetPath) {
        if (!activeScene) return;
        
        QString displayName = sheetPath.isEmpty() ? "Root" : sheetPath;
        if (displayName.endsWith("/")) displayName.chop(1);

        for (QGraphicsItem* item : activeScene->items()) {
            if (SchematicItem* si = dynamic_cast<SchematicItem*>(item)) {
                if (si->itemType() == SchematicItem::WireType || si->itemType() == SchematicItem::BusType || 
                    si->itemType() == SchematicItem::JunctionType || si->itemType() == SchematicItem::LabelType ||
                    si->itemType() == SchematicItem::HierarchicalPortType || si->itemType() == SchematicItem::NoConnectType) continue;

                if (auto* sheet = dynamic_cast<SchematicSheetItem*>(si)) {
                    QString childFile = sheet->fileName();
                    if (QFileInfo(childFile).isRelative() && !projectDir.isEmpty())
                        childFile = projectDir + "/" + childFile;
                    
                    QString absPath = QFileInfo(childFile).absoluteFilePath();
                    if (visitedFiles.contains(absPath)) {
                        ERCViolation v;
                        v.severity = ERCViolation::Warning;
                        v.category = ERCViolation::Conflict;
                        v.message = QString("Circular sheet reference detected: %1").arg(sheet->fileName());
                        v.position = sheet->scenePos();
                        v.item = sheet;
                        violations.append(v);
                        continue;
                    }

                    if (QFileInfo::exists(childFile)) {
                        visitedFiles.insert(absPath);
                        QGraphicsScene* childScene = new QGraphicsScene();
                        QString dummySize;
                        TitleBlockData dummyTB;
                        if (SchematicFileIO::loadSchematic(childScene, childFile, dummySize, dummyTB)) {
                            checkDuplicates(childScene, sheetPath + sheet->sheetName() + "/");
                        }
                        delete childScene;
                        visitedFiles.remove(absPath);
                    } else if (!childFile.isEmpty()) {
                        ERCViolation v;
                        v.severity = ERCViolation::Error;
                        v.category = ERCViolation::Connectivity;
                        v.message = QString("Sub-sheet file not found: %1").arg(sheet->fileName());
                        v.position = sheet->scenePos();
                        v.item = sheet;
                        violations.append(v);
                    }
                    continue;
                }

                QString ref = si->reference();
                if (ref.isEmpty() || ref.contains("?")) continue;

                if (designatorToSheet.contains(ref)) {
                    ERCViolation v;
                    v.severity = ERCViolation::Error;
                    v.category = ERCViolation::Annotation;
                    v.message = QString("Duplicate reference designator: '%1' found in '%2' and '%3'").arg(ref).arg(designatorToSheet[ref]).arg(displayName);
                    v.position = si->scenePos(); 
                    v.item = (sheetPath.isEmpty()) ? si : nullptr;
                    violations.append(v);
                } else {
                    designatorToSheet[ref] = displayName;
                }
            }
        }
    };
    
    checkDuplicates(scene, "");

    // --- Hierarchical Port/Pin Consistency ---
    for (SchematicItem* item : schItems) {
        if (auto* sheet = dynamic_cast<SchematicSheetItem*>(item)) {
            QString subFilePath = sheet->fileName();
            if (QFileInfo(subFilePath).isRelative() && !projectDir.isEmpty())
                subFilePath = projectDir + "/" + subFilePath;

            if (!QFileInfo::exists(subFilePath)) continue;

            QFile file(subFilePath);
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                QJsonArray items = doc.object()["items"].toArray();
                QSet<QString> subSheetPorts;
                for (int i = 0; i < items.size(); ++i) {
                    QJsonObject obj = items[i].toObject();
                    QString typeName = obj["type"].toString();
                    if (typeName == "HierarchicalPort" || typeName == "Hierarchical Port") {
                        subSheetPorts.insert(obj["label"].toString());
                    }
                }

                QList<SheetPinItem*> sheetPins = sheet->getPins();
                QSet<QString> existingPinNames;
                for (SheetPinItem* pin : sheetPins) {
                    existingPinNames.insert(pin->name());
                    if (!subSheetPorts.contains(pin->name())) {
                        ERCViolation v;
                        v.severity = ERCViolation::Warning;
                        v.category = ERCViolation::Connectivity;
                        v.message = QString("Sheet pin '%1' has no matching port in sub-sheet").arg(pin->name());
                        v.position = pin->scenePos();
                        v.item = sheet;
                        violations.append(v);
                    }
                }

                for (const QString& portName : subSheetPorts) {
                    if (!existingPinNames.contains(portName)) {
                        ERCViolation v;
                        v.severity = ERCViolation::Error;
                        v.category = ERCViolation::Connectivity;
                        v.message = QString("Missing pin for hierarchical port '%1'").arg(portName);
                        v.position = sheet->pos();
                        v.item = sheet;
                        violations.append(v);
                    }
                }
            }
        }
    }

    // --- Net-Based Analysis (Pin Type Conflicts & Floating Pins) ---
    QStringList nets = localNM->netNames();
    for (const QString& netName : nets) {
        QList<NetConnection> connections = localNM->getConnections(netName);

        struct ConnectedPin {
            SchematicItem* item = nullptr;
            QPointF point;
            SchematicItem::PinElectricalType type = SchematicItem::PassivePin;
            QString pinId;
        };

        auto pinTypeName = [](SchematicItem::PinElectricalType type) -> QString {
            switch (type) {
            case SchematicItem::PassivePin: return "Passive";
            case SchematicItem::InputPin: return "Input";
            case SchematicItem::OutputPin: return "Output";
            case SchematicItem::BidirectionalPin: return "Bidirectional";
            case SchematicItem::TriStatePin: return "Tri-state";
            case SchematicItem::FreePin: return "Free";
            case SchematicItem::UnspecifiedPin: return "Unspecified";
            case SchematicItem::PowerInputPin: return "Power Input";
            case SchematicItem::PowerOutputPin: return "Power Output";
            case SchematicItem::OpenCollectorPin: return "Open Collector";
            case SchematicItem::OpenEmitterPin: return "Open Emitter";
            case SchematicItem::NotConnectedPin: return "Not Connected";
            default: return "Unknown";
            }
        };

        auto pinDisplay = [](const ConnectedPin& pin) -> QString {
            const QString id = !pin.pinId.isEmpty() ? pin.pinId : "?";
            QString owner = pin.item ? pin.item->reference() : QString();
            if (owner.trimmed().isEmpty() && pin.item) owner = pin.item->name();
            if (owner.trimmed().isEmpty() && pin.item) owner = pin.item->itemTypeName();
            if (owner.trimmed().isEmpty()) owner = "Item";
            return QString("%1 pin %2").arg(owner, id);
        };

        QMap<SchematicItem::PinElectricalType, QList<ConnectedPin>> typedPins;
        int outputCount = 0;
        int inputCount = 0;
        int powerOutCount = 0;
        int powerInCount = 0;
        int passiveCount = 0;
        int openCount = 0;

        QList<ConnectedPin> loadPins;
        QList<ConnectedPin> allConnectedPins;

        for (const auto& conn : connections) {
            if (!conn.item) continue;

            QList<QPointF> pinPoints = conn.item->connectionPoints();
            QList<SchematicItem::PinElectricalType> pinTypes = conn.item->pinElectricalTypes();

            int pinIdx = -1;
            for (int i = 0; i < pinPoints.size(); ++i) {
                if (QLineF(conn.item->mapToScene(pinPoints[i]), conn.connectionPoint).length() < 5.0) {
                    pinIdx = i;
                    break;
                }
            }

            if (pinIdx != -1 && pinIdx < pinTypes.size()) {
                SchematicItem::PinElectricalType type = pinTypes[pinIdx];
                ConnectedPin pinConn{conn.item, conn.connectionPoint, type, conn.pinName};
                typedPins[type].append(pinConn);
                allConnectedPins.append(pinConn);

                switch (type) {
                    case SchematicItem::OutputPin: outputCount++; break;
                    case SchematicItem::InputPin: inputCount++; loadPins << pinConn; break;
                    case SchematicItem::PowerOutputPin: powerOutCount++; break;
                    case SchematicItem::PowerInputPin: powerInCount++; loadPins << pinConn; break;
                    case SchematicItem::PassivePin: passiveCount++; break;
                    case SchematicItem::OpenCollectorPin:
                    case SchematicItem::OpenEmitterPin:
                        openCount++;
                        break;
                    default: break;
                }
            }
        }

        // --- New Matrix-Based Conflict Detection ---
        for (int i = 0; i < allConnectedPins.size(); ++i) {
            for (int j = i + 1; j < allConnectedPins.size(); ++j) {
                const auto& p1 = allConnectedPins[i];
                const auto& p2 = allConnectedPins[j];
                auto result = rules.getRule(p1.type, p2.type);
                
                if (result != SchematicERCRules::OK) {
                    ERCViolation v;
                    v.category = ERCViolation::Conflict;
                    v.netName = netName;
                    v.position = p1.point;
                    v.item = p1.item;
                    
                    switch (result) {
                        case SchematicERCRules::Warning: v.severity = ERCViolation::Warning; break;
                        case SchematicERCRules::Error: v.severity = ERCViolation::Error; break;
                        case SchematicERCRules::Critical: v.severity = ERCViolation::Critical; break;
                        default: break;
                    }

                    v.message = QString("Pin Conflict on net %1: %2 (%3) connected to %4 (%5)")
                                    .arg(netName, 
                                         pinDisplay(p1), pinTypeName(p1.type),
                                         pinDisplay(p2), pinTypeName(p2.type));
                    
                    bool exists = false;
                    for (const auto& existing : violations) {
                        if (existing.netName == netName && existing.message == v.message) {
                            exists = true; break;
                        }
                    }
                    if (!exists) violations.append(v);
                }
            }
        }

        // --- Net-wide Heuristic Checks ---
        if (inputCount > 0 && outputCount == 0 && powerOutCount == 0 && passiveCount == 0) {
            ERCViolation v;
            v.severity = ERCViolation::Warning;
            v.category = ERCViolation::Connectivity;
            v.message = QString("Floating Input on net %1: No driver (Output/Power) found for input pins.").arg(netName);
            v.netName = netName;
            v.position = loadPins.isEmpty() ? connections.first().connectionPoint : loadPins.first().point;
            v.item = loadPins.isEmpty() ? nullptr : loadPins.first().item;
            violations.append(v);
        }

        if (powerInCount > 0 && outputCount == 0 && powerOutCount == 0 && passiveCount == 0 && openCount == 0) {
            ERCViolation v;
            v.severity = ERCViolation::Warning;
            v.category = ERCViolation::Connectivity;
            v.message = QString("Power net %1 has only power-input pins and no source.").arg(netName);
            v.netName = netName;
            v.position = loadPins.isEmpty() ? connections.first().connectionPoint : loadPins.first().point;
            v.item = loadPins.isEmpty() ? nullptr : loadPins.first().item;
            violations.append(v);
        }
    }

    // 3. Unconnected Pin Check
    for (SchematicItem* item : schItems) {
        if (item->itemType() == SchematicItem::WireType || item->itemType() == SchematicItem::BusType || 
            item->itemType() == SchematicItem::JunctionType || item->itemType() == (QGraphicsItem::UserType + 101)) continue;

        QList<QPointF> pins = item->connectionPoints();
        const QList<SchematicItem::PinElectricalType> pinTypes = item->pinElectricalTypes();
        for (int i = 0; i < pins.size(); ++i) {
            if (i < pinTypes.size() && pinTypes[i] == SchematicItem::NotConnectedPin) continue;
            
            QPointF scenePt = item->mapToScene(pins[i]);
            if (localNM->findNetAtPoint(scenePt).isEmpty()) {
                bool hasNC = false;
                constexpr qreal kNcTol = 5.0;
                constexpr qreal kNcTolSq = kNcTol * kNcTol;
                for (QGraphicsItem* gi : scene->items(QRectF(scenePt.x() - 10, scenePt.y() - 10, 20, 20))) {
                    SchematicItem* si = dynamic_cast<SchematicItem*>(gi);
                    if (!si || si->itemType() != SchematicItem::NoConnectType) continue;
                    const QList<QPointF> ncPoints = si->connectionPoints();
                    for (const QPointF& ncLocal : ncPoints) {
                        QPointF p1 = si->mapToScene(ncLocal);
                        QPointF delta = p1 - scenePt;
                        if ((delta.x() * delta.x() + delta.y() * delta.y()) <= kNcTolSq) {
                            hasNC = true; break;
                        }
                    }
                    if (hasNC) break;
                }
                if (!hasNC) {
                    ERCViolation v;
                    v.severity = ERCViolation::Warning;
                    v.category = ERCViolation::Connectivity;
                    v.message = QString("Unconnected pin %1 on %2").arg(i + 1).arg(item->reference());
                    v.position = scenePt;
                    v.item = item;
                    violations.append(v);
                }
            }
        }
    }

    if (ownNM) delete localNM;
    return violations;
}

QList<ERCViolation> SchematicERC::runLive(QGraphicsScene* scene, const QList<SchematicItem*>& items, NetManager* netManager, const SchematicERCRules& rules) {
    QList<ERCViolation> violations;
    if (!scene || !netManager || items.isEmpty()) return violations;

    QSet<QString> targetNets;
    for (auto* item : items) {
        for (const QPointF& p : item->connectionPoints()) {
            QString net = netManager->findNetAtPoint(item->mapToScene(p));
            if (!net.isEmpty()) targetNets.insert(net);
        }
    }

    if (targetNets.isEmpty()) return violations;

    // Run localized net analysis
    auto pinTypeName = [](SchematicItem::PinElectricalType type) -> QString {
        switch (type) {
        case SchematicItem::PassivePin: return "Passive";
        case SchematicItem::InputPin: return "Input";
        case SchematicItem::OutputPin: return "Output";
        case SchematicItem::PowerInputPin: return "Power Input";
        case SchematicItem::PowerOutputPin: return "Power Output";
        default: return "Generic";
        }
    };

    for (const QString& netName : targetNets) {
        QList<NetConnection> connections = netManager->getConnections(netName);
        
        struct ConnectedPin {
            SchematicItem* item = nullptr;
            QPointF point;
            SchematicItem::PinElectricalType type;
            QString pinId;
        };

        QList<ConnectedPin> allPins;
        int drivers = 0;

        for (const auto& conn : connections) {
            if (!conn.item) continue;
            QList<QPointF> pts = conn.item->connectionPoints();
            QList<SchematicItem::PinElectricalType> types = conn.item->pinElectricalTypes();
            
            for (int i = 0; i < pts.size(); ++i) {
                if (QLineF(conn.item->mapToScene(pts[i]), conn.connectionPoint).length() < 5.0) {
                    SchematicItem::PinElectricalType t = (i < types.size()) ? types[i] : SchematicItem::PassivePin;
                    allPins.append({conn.item, conn.connectionPoint, t, conn.pinName});
                    if (t == SchematicItem::OutputPin || t == SchematicItem::PowerOutputPin) drivers++;
                    break;
                }
            }
        }

        // 1. Conflict Check (Multiple drivers or forbidden pairings)
        for (int i = 0; i < allPins.size(); ++i) {
            for (int j = i + 1; j < allPins.size(); ++j) {
                auto result = rules.getRule(allPins[i].type, allPins[j].type);
                if (result != SchematicERCRules::OK) {
                    ERCViolation v;
                    v.severity = (result == SchematicERCRules::Error) ? ERCViolation::Error : ERCViolation::Warning;
                    v.category = ERCViolation::Conflict;
                    v.netName = netName;
                    v.position = allPins[i].point;
                    v.item = allPins[i].item;
                    v.message = QString("Conflict on %1: %2 paired with %3")
                                    .arg(netName, pinTypeName(allPins[i].type), pinTypeName(allPins[j].type));
                    violations.append(v);
                }
            }
        }

        // 2. Floating Net Check (Drivers required if inputs exist)
        bool hasInputs = false;
        for (const auto& p : allPins) {
            if (p.type == SchematicItem::InputPin || p.type == SchematicItem::PowerInputPin) {
                hasInputs = true; break;
            }
        }
        if (hasInputs && drivers == 0) {
            ERCViolation v;
            v.severity = ERCViolation::Warning;
            v.category = ERCViolation::Connectivity;
            v.netName = netName;
            v.message = "Floating net: " + netName + " (Input has no driver)";
            v.position = allPins.isEmpty() ? QPointF(0,0) : allPins.first().point;
            violations.append(v);
        }
    }

    return violations;
}
