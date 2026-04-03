#include "pcb_item_registry.h"

void PCBItemRegistry::registerBuiltInItems() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    auto& factory = PCBItemFactory::instance();

    // Register Pad item
    factory.registerItemType("Pad", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> PCBItem* {
        double diameter = properties.value("diameter").toDouble(1.0);
        return new PadItem(pos, diameter, parent);
    });

    // Register Via item
    factory.registerItemType("Via", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> PCBItem* {
        double diameter = properties.value("diameter").toDouble(0.8);
        return new ViaItem(pos, diameter, parent);
    });

    // Register Trace item
    factory.registerItemType("Trace", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> PCBItem* {
        QPointF endPos = pos;
        if (properties.contains("endX") && properties.contains("endY")) {
            endPos = QPointF(properties["endX"].toDouble(), properties["endY"].toDouble());
        }
        double width = properties.value("width").toDouble(0.2);
        return new TraceItem(pos, endPos, width, parent);
    });

    // Register Component item
    factory.registerItemType("Component", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> PCBItem* {
        QString type = properties.value("componentType").toString("IC");
        return new ComponentItem(pos, type, parent);
    });

    // Register Copper Pour item
    factory.registerItemType("CopperPour", [](QPointF, const QJsonObject&, QGraphicsItem* parent) -> PCBItem* {
        return new CopperPourItem(parent);
    });
}
