#include "schematic_file_io.h"
#include "flux/core/net_manager.h"
#include "core/diagnostics/runtime_diagnostics.h"
#include "schematic_item.h"
#include "schematic_item_factory.h"
#include "wire_item.h"
#include "resistor_item.h"
#include "capacitor_item.h"
#include "inductor_item.h"
#include "diode_item.h"
#include "transistor_item.h"
#include "transformer_item.h"
#include "ic_item.h"
#include "power_item.h"
#include "net_label_item.h"
#include "no_connect_item.h"
#include "bus_item.h"
#include "schematic_sheet_item.h"
#include "hierarchical_port_item.h"
#include "../items/voltage_source_item.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>

QString SchematicFileIO::s_lastError;

bool SchematicFileIO::saveSchematic(QGraphicsScene* scene, const QString& filePath,
                                    const QString& pageSize, const QString& script,
                                    const TitleBlockData& titleBlock,
                                    const QMap<QString, QList<QString>>& busAliases,
                                    const QSet<QString>& ercExclusions,
                                    const QJsonObject* simulationSetup) {
    if (!scene) {
        s_lastError = "Invalid scene pointer";
        return false;
    }
    
    QJsonObject root;
    
    // File metadata
    QJsonObject metadata;
    metadata["version"] = FILE_FORMAT_VERSION;
    metadata["application"] = "Viora EDA";
    metadata["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    metadata["modifiedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["metadata"] = metadata;
    
    // Page settings
    QJsonObject pageSettings;
    pageSettings["size"] = pageSize;
    pageSettings["orientation"] = "landscape";
    root["pageSettings"] = pageSettings;

    // Title Block metadata
    root["titleBlock"] = titleBlock.toJson();

    // Embed FluxScript (Hybrid Format)
    if (!script.isEmpty()) {
        root["fluxScript"] = script;
    }

    if (!busAliases.isEmpty()) {
        QJsonObject aliasesObj;
        for (auto it = busAliases.begin(); it != busAliases.end(); ++it) {
            aliasesObj[it.key()] = QJsonArray::fromStringList(it.value());
        }
        root["busAliases"] = aliasesObj;
    }
    if (!ercExclusions.isEmpty()) {
        QJsonArray arr;
        for (const QString& v : ercExclusions) arr.append(v);
        root["ercExclusions"] = arr;
    }

    if (simulationSetup && !simulationSetup->isEmpty()) {
        root["simulationSetup"] = *simulationSetup;
    }
    
    // Serialize all items
    root["items"] = serializeItems(scene);
    
    // Write to file
    QJsonDocument doc(root);
    QFile file(filePath);
    
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        s_lastError = QString("Failed to open file for writing: %1").arg(file.errorString());
        return false;
    }
    
    qint64 bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    if (bytesWritten < 0) {
        s_lastError = QString("Failed to write file: %1").arg(file.errorString());
        return false;
    }
    
    qDebug() << "Schematic saved successfully to" << filePath 
             << "(" << bytesWritten << "bytes)";
    return true;
}

bool SchematicFileIO::loadSchematic(QGraphicsScene* scene, const QString& filePath,
                                    QString& pageSize, TitleBlockData& titleBlock, QString* script,
                                    QMap<QString, QList<QString>>* busAliases,
                                    QSet<QString>* ercExclusions,
                                    QJsonObject* simulationSetup) {
    FLUX_DIAG_SCOPE("SchematicFileIO::loadSchematic");
    if (!scene) {
        s_lastError = "Invalid scene pointer";
        return false;
    }

    if (filePath.endsWith(".kicad_sch", Qt::CaseInsensitive) || 
        filePath.endsWith(".SchDoc", Qt::CaseInsensitive)) {
        s_lastError = "Importing this file format is not yet supported.";
        return false;
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        s_lastError = QString("Failed to open file: %1").arg(file.errorString());
        return false;
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        s_lastError = QString("JSON parse error at offset %1: %2")
                        .arg(parseError.offset)
                        .arg(parseError.errorString());
        return false;
    }
    
    if (!doc.isObject()) {
        s_lastError = "Invalid file format: root is not an object";
        return false;
    }
    
    QJsonObject root = doc.object();
    
    // Check file version
    QJsonObject metadata = root["metadata"].toObject();
    int version = metadata["version"].toInt(0);
    if (version > FILE_FORMAT_VERSION) {
        s_lastError = QString("File version %1 is newer than supported version %2")
                        .arg(version)
                        .arg(FILE_FORMAT_VERSION);
        return false;
    }
    
    // Load page settings
    QJsonObject pageSettings = root["pageSettings"].toObject();
    pageSize = pageSettings["size"].toString("A4");

    // Load Title Block
    if (root.contains("titleBlock")) {
        titleBlock = TitleBlockData::fromJson(root["titleBlock"].toObject());
    }

    // Extract FluxScript if present
    if (script && root.contains("fluxScript")) {
        *script = root["fluxScript"].toString();
    }

    if (busAliases) {
        busAliases->clear();
        const QJsonObject aliasesObj = root.value("busAliases").toObject();
        for (auto it = aliasesObj.begin(); it != aliasesObj.end(); ++it) {
            QStringList members;
            const QJsonArray arr = it.value().toArray();
            for (const QJsonValue& v : arr) {
                const QString member = v.toString().trimmed();
                if (!member.isEmpty()) members.append(member);
            }
            if (!members.isEmpty()) (*busAliases)[it.key()] = members;
        }
    }
    if (ercExclusions) {
        ercExclusions->clear();
        const QJsonArray arr = root.value("ercExclusions").toArray();
        for (const QJsonValue& v : arr) {
            const QString key = v.toString().trimmed();
            if (!key.isEmpty()) ercExclusions->insert(key);
        }
    }

    if (simulationSetup) {
        *simulationSetup = QJsonObject();
        const QJsonValue simVal = root.value("simulationSetup");
        if (simVal.isObject()) {
            *simulationSetup = simVal.toObject();
        }
    }
    
    // Clear scene and load items
    scene->clear();
    
    if (!deserializeItems(scene, root["items"].toArray())) {
        return false;
    }
    
    qDebug() << "Schematic loaded successfully from" << filePath;
    return true;
}

QString SchematicFileIO::lastError() {
    return s_lastError;
}

QJsonArray SchematicFileIO::serializeItems(QGraphicsScene* scene) {
    QJsonArray itemsArray;
    
    QList<QGraphicsItem*> items = scene->items();
    for (QGraphicsItem* item : items) {
        if (!item) continue;

        // Skip grid lines and other non-schematic items
        if (item->data(0).toString() == "grid") {
            continue;
        }
        
        // Use qgraphicsitem_cast if possible, or dynamic_cast for our custom interface
        SchematicItem* schematicItem = dynamic_cast<SchematicItem*>(item);
        if (schematicItem && !schematicItem->isSubItem()) {
            QJsonObject itemJson = schematicItem->toJson();
            // Also save rotation
            itemJson["rotation"] = item->rotation();
            itemsArray.append(itemJson);
        }
    }
    
    qDebug() << "Serialized" << itemsArray.size() << "schematic items";
    return itemsArray;
}

bool SchematicFileIO::deserializeItems(QGraphicsScene* scene, const QJsonArray& itemsArray) {
    int loadedCount = 0;
    int errorCount = 0;
    
    for (const QJsonValue& itemValue : itemsArray) {
        if (!itemValue.isObject()) {
            errorCount++;
            continue;
        }
        
        QJsonObject itemJson = itemValue.toObject();
        SchematicItem* item = createItemFromJson(itemJson);
        
        if (item) {
            // Apply rotation if saved
            if (itemJson.contains("rotation")) {
                item->setRotation(itemJson["rotation"].toDouble());
            }
            scene->addItem(item);
            loadedCount++;
        } else {
            qWarning() << "Failed to create item of type:" << itemJson["type"].toString();
            errorCount++;
        }
    }
    
    qDebug() << "Loaded" << loadedCount << "items," << errorCount << "errors";
    
    if (errorCount > 0 && loadedCount == 0) {
        s_lastError = "Failed to load any items from file";
        return false;
    }
    
    return true;
}

SchematicItem* SchematicFileIO::createItemFromJson(const QJsonObject& json) {
    QString type = json["type"].toString();
    
    if (type.isEmpty()) {
        return nullptr;
    }
    
    // Use factory to create the item
    QPointF pos(json["x"].toDouble(), json["y"].toDouble());
    SchematicItem* item = SchematicItemFactory::instance().createItem(type, pos, json);
    
    if (item) {
        // Let the item restore its specific properties from JSON
        if (!item->fromJson(json)) {
            qWarning() << "Item of type" << type << "failed to load its properties from JSON";
            // We keep it anyway as factory created it, but fromJson should be robust
        }
    } else {
        qWarning() << "Unknown item type:" << type;
    }
    
    return item;
}

QJsonObject SchematicFileIO::serializeSceneToJson(QGraphicsScene* scene, const QString& pageSize) {
    QJsonObject root;
    if (!scene) return root;

    QJsonObject metadata;
    metadata["version"] = FILE_FORMAT_VERSION;
    metadata["application"] = "Viora EDA";
    metadata["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["metadata"] = metadata;

    QJsonObject pageSettings;
    pageSettings["size"] = pageSize;
    root["pageSettings"] = pageSettings;

    root["items"] = serializeItems(scene);
    return root;
}

QString SchematicFileIO::convertToFluxScript(QGraphicsScene* scene, NetManager* netManager) {
    if (!scene) return "";

    QString code = "# FluxScript generated by Viora EDA\n";
    code += QString("# Created: %1\n\n").arg(QDateTime::currentDateTime().toString());

    QMap<SchematicItem*, QString> itemVars;
    int compCount = 0;

    // 1. Export Components
    code += "# Components\n";
    for (QGraphicsItem* item : scene->items()) {
        if (auto* schItem = dynamic_cast<SchematicItem*>(item)) {
            // Skip wires and other non-symbolic items
            if (schItem->itemType() == SchematicItem::WireType || 
                schItem->itemType() == SchematicItem::JunctionType) continue;

            QString varName = schItem->reference();
            if (varName.isEmpty()) varName = QString("U%1").arg(++compCount);
            
            // Clean up variable name
            varName.replace("-", "_").replace(" ", "_");
            itemVars[schItem] = varName;

            code += QString("%1 = component(\"%2\", \"%3\")\n")
                    .arg(varName, schItem->itemTypeName(), schItem->value());
        }
    }
    code += "\n";

    // 2. Export Nets
    if (netManager) {
        code += "# Connectivity\n";
        for (const QString& netName : netManager->netNames()) {
            if (netName.startsWith("AutoNet")) continue; // Skip internal nets if simple

            QList<NetConnection> connections = netManager->getConnections(netName);
            if (connections.isEmpty()) continue;

            QStringList pinRefs;
            for (const auto& conn : connections) {
                if (itemVars.contains(conn.item)) {
                    QString varName = itemVars[conn.item];
                    
                    // Determine pin index (p1, p2, ...)
                    int pinIndex = -1;
                    QList<QPointF> points = conn.item->connectionPoints();
                    for (int i = 0; i < points.size(); ++i) {
                        // Compare local points (connectionPoint is usually local to item in newer impl, 
                        // but let's check NetManager's storage)
                        // Note: NetManager store's point is usually the scenePos.
                        if (conn.item->mapToScene(points[i]) == conn.connectionPoint) {
                            pinIndex = i + 1;
                            break;
                        }
                    }

                    if (pinIndex != -1) {
                        pinRefs.append(QString("%1.p%2").arg(varName).arg(pinIndex));
                    }
                }
            }

            if (!pinRefs.isEmpty()) {
                code += QString("net %1 { %2 }\n").arg(netName, pinRefs.join(", "));
            }
        }
    }

    return code;
}
