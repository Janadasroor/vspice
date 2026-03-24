#include "netlist_to_schematic.h"
#include "spice_netlist_parser.h"
#include "schematic_file_io.h"
#include "../items/schematic_item.h"
#include "../items/wire_item.h"
#include "../factories/schematic_item_factory.h"
#include <QGraphicsScene>
#include <QFileInfo>
#include <QJsonObject>
#include <QDebug>

NetlistToSchematic::ConvertResult NetlistToSchematic::convert(const QString& netlistPath, const QString& outputPath) {
    ConvertResult result;

    // Derive output path if not specified
    QString outPath = outputPath;
    if (outPath.isEmpty()) {
        QFileInfo info(netlistPath);
        outPath = info.absolutePath() + "/" + info.completeBaseName() + ".flxsch";
    }

    // Create a temporary scene
    QGraphicsScene scene;
    scene.setSceneRect(-5000, -5000, 10000, 10000);

    result = convertToScene(netlistPath, &scene);
    if (!result.success) return result;

    // Save the scene to .flxsch
    if (SchematicFileIO::saveSchematic(&scene, outPath)) {
        result.outputPath = outPath;
        qDebug() << "NetlistToSchematic: Saved schematic to" << outPath;
    } else {
        result.success = false;
        result.errorMessage = "Failed to save schematic: " + SchematicFileIO::lastError();
    }

    return result;
}

NetlistToSchematic::ConvertResult NetlistToSchematic::convertToScene(const QString& netlistPath, QGraphicsScene* scene) {
    ConvertResult result;

    if (!scene) {
        result.errorMessage = "Invalid scene pointer";
        return result;
    }

    // 1. Parse the netlist
    SpiceNetlistParser::ParsedNetlist netlist = SpiceNetlistParser::parse(netlistPath);

    if (netlist.components.isEmpty()) {
        result.errorMessage = "No components found in netlist file.";
        return result;
    }

    // 2. Place components in a grid layout
    QMap<QString, SchematicItem*> refToItem; // ref -> placed SchematicItem
    int row = 0, col = 0;

    for (const auto& comp : netlist.components) {
        // Calculate grid position
        qreal x = col * GRID_SPACING;
        qreal y = row * GRID_SPACING;

        // Build properties JSON for the factory
        QJsonObject props;
        if (!comp.value.isEmpty()) {
            props["value"] = comp.value;
        }

        // Create the item via factory
        SchematicItem* item = SchematicItemFactory::instance().createItem(
            comp.typeName, QPointF(x, y), props);

        if (!item) {
            qWarning() << "NetlistToSchematic: Failed to create item for type"
                       << comp.typeName << "ref" << comp.reference
                       << "- trying IC fallback";
            // Fallback: create as generic IC with appropriate pin count
            QJsonObject icProps;
            icProps["value"] = comp.value.isEmpty() ? comp.typeName : comp.value;
            icProps["pinCount"] = comp.nodes.size();
            item = SchematicItemFactory::instance().createItem("IC", QPointF(x, y), icProps);
        }

        if (item) {
            item->setReference(comp.reference);
            if (!comp.value.isEmpty()) {
                item->setValue(comp.value);
            }
            scene->addItem(item);
            refToItem[comp.reference] = item;
            result.componentCount++;
        } else {
            qWarning() << "NetlistToSchematic: Skipping unrecognized component"
                       << comp.reference << comp.typeName;
        }

        // Advance grid position
        col++;
        if (col >= COLUMNS) {
            col = 0;
            row++;
        }
    }

    // 3. Add GND power symbols for components connected to ground
    for (const auto& net : netlist.nets) {
        if (net.name == "GND") {
            for (const auto& pin : net.pins) {
                SchematicItem* item = refToItem.value(pin.componentRef);
                if (!item) continue;

                QList<QPointF> connPts = item->connectionPoints();
                if (pin.pinIndex < connPts.size()) {
                    QPointF pinScenePos = item->mapToScene(connPts[pin.pinIndex]);

                    // Place a GND symbol slightly below the pin
                    QJsonObject gndProps;
                    SchematicItem* gnd = SchematicItemFactory::instance().createItem(
                        "GND", pinScenePos + QPointF(0, 30), gndProps);
                    if (gnd) {
                        scene->addItem(gnd);
                        
                        // Create an air wire connecting the pin to the GND symbol's connection point
                        // The GND symbol's local connection point is at (0, -15)
                        QPointF gndPinPos = gnd->mapToScene(QPointF(0, -15));
                        auto* airWire = new WireItem(WireItem::AirWire);
                        airWire->setPoints({pinScenePos, gndPinPos});
                        airWire->setProperty("netName", "GND");
                        airWire->setFlag(QGraphicsItem::ItemIsSelectable, false);
                        airWire->setFlag(QGraphicsItem::ItemIsMovable, false);
                        airWire->setZValue(-5);
                        scene->addItem(airWire);
                        result.airWireCount++;
                    }
                }
            }
        }
    }

    // 4. Create air wires for each net with >= 2 pins
    for (const auto& net : netlist.nets) {
        if (net.pins.size() < 2) continue;
        if (net.name == "GND") continue; // GND is handled with local power symbols and their own air wires

        // Star topology: connect all pins to the first pin
        const auto& firstPin = net.pins[0];
        SchematicItem* firstItem = refToItem.value(firstPin.componentRef);
        if (!firstItem) continue;

        QList<QPointF> firstConnPts = firstItem->connectionPoints();
        if (firstPin.pinIndex >= firstConnPts.size()) continue;
        QPointF firstPinPos = firstItem->mapToScene(firstConnPts[firstPin.pinIndex]);

        for (int i = 1; i < net.pins.size(); ++i) {
            const auto& otherPin = net.pins[i];
            SchematicItem* otherItem = refToItem.value(otherPin.componentRef);
            if (!otherItem) continue;

            QList<QPointF> otherConnPts = otherItem->connectionPoints();
            if (otherPin.pinIndex >= otherConnPts.size()) continue;
            QPointF otherPinPos = otherItem->mapToScene(otherConnPts[otherPin.pinIndex]);

            auto* airWire = new WireItem(WireItem::AirWire);
            airWire->setPoints({firstPinPos, otherPinPos});
            airWire->setProperty("netName", net.name); // Store net name property optionally
            
            // Turn off interactivity for pure visual air wires
            airWire->setFlag(QGraphicsItem::ItemIsSelectable, false);
            airWire->setFlag(QGraphicsItem::ItemIsMovable, false);
            airWire->setZValue(-5);

            scene->addItem(airWire);
            result.airWireCount++;
        }
    }

    result.success = true;
    qDebug() << "NetlistToSchematic: Placed" << result.componentCount
             << "components and" << result.airWireCount << "air wires";

    return result;
}
