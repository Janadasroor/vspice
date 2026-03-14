#include "schematic_item_registry.h"
#include "wire_item.h"
#include "resistor_item.h"
#include "capacitor_item.h"
#include "inductor_item.h"
#include "diode_item.h"
#include "transistor_item.h"
#include "transformer_item.h"
#include "ic_item.h"
#include "power_item.h"
#include "no_connect_item.h"
#include "bus_item.h"
#include "net_label_item.h"
#include "hierarchical_port_item.h"
#include "schematic_sheet_item.h"
#include "voltage_source_item.h"
#include "schematic_shape_item.h"
#include "symbol_library.h"
#include "generic_component_item.h"
#include "schematic_text_item.h"
#include "switch_item.h"
#include "push_button_item.h"
#include "led_item.h"
#include "signal_generator_item.h"
#include "logic_analyzer_item.h"
#include "oscilloscope_item.h"
#include "smart_signal_item.h"
#include "instrument_probe_item.h"
#include "schematic_spice_directive_item.h"

using Flux::Model::SymbolDefinition;

void SchematicItemRegistry::registerBuiltInItems() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    auto& factory = SchematicItemFactory::instance();
    auto makeGenericFromLibrary = [](const QString& symbolName, QPointF pos, const QString& value,
                                     QGraphicsItem* parent) -> GenericComponentItem* {
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol(symbolName)) {
            auto* item = new GenericComponentItem(*def, parent);
            item->setPos(pos);
            if (!value.isEmpty()) item->setValue(value);
            return item;
        }
        return nullptr;
    };

    // Register Wire item
    factory.registerItemType("Wire", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        QPointF endPos = pos;
        if (properties.contains("endX") && properties.contains("endY")) {
            endPos = QPointF(properties["endX"].toDouble(), properties["endY"].toDouble());
        }
        return new WireItem(pos, endPos, parent);
    });

    factory.registerItemType("NoConnect", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return new NoConnectItem(parent);
    });

    factory.registerItemType("Bus", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return new BusItem(pos, pos, parent);
    });

    factory.registerItemType("Polygon", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new SchematicShapeItem(SchematicShapeItem::Polygon, pos, pos, parent);
        if (properties.contains("points")) {
            QList<QPointF> points;
            QJsonArray pts = properties["points"].toArray();
            for (const auto& v : pts) {
                QJsonObject pt = v.toObject();
                points << QPointF(pt["x"].toDouble(), pt["y"].toDouble());
            }
            item->setPoints(points);
        }
        return item;
    });

    // Register Resistor item
    factory.registerItemType("Resistor", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol("Resistor")) {
            auto* item = new GenericComponentItem(*def, parent);
            item->setPos(pos);
            item->setValue(properties.value("value").toString("10k"));
            return item;
        }
        return new ResistorItem(pos, properties.value("value").toString("10k"), ResistorItem::US, parent);
    });

    // Register Capacitor item
    factory.registerItemType("Capacitor", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol("Capacitor")) {
            auto* item = new GenericComponentItem(*def, parent);
            item->setPos(pos);
            item->setValue(properties.value("value").toString("10uF"));
            return item;
        }
        return new CapacitorItem(pos, properties.value("value").toString("10uF"), CapacitorItem::NonPolarized, parent);
    });

    // Register Inductor item
    factory.registerItemType("Inductor", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol("Inductor")) {
            auto* item = new GenericComponentItem(*def, parent);
            item->setPos(pos);
            item->setValue(properties.value("value").toString("10uH"));
            return item;
        }
        return new InductorItem(pos, properties.value("value").toString("10uH"), parent);
    });

    // Register Diode item
    factory.registerItemType("Diode", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol("Diode")) {
            auto* item = new GenericComponentItem(*def, parent);
            item->setPos(pos);
            item->setValue(properties.value("value").toString("1N4148"));
            return item;
        }
        return new DiodeItem(pos, properties.value("value").toString("1N4148"), parent);
    });

    // Register Transistor item (Generic NPN Default)
    factory.registerItemType("Transistor", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("2N2222");
        if (auto* item = makeGenericFromLibrary("NPN Transistor", pos, value, parent)) return item;
        if (auto* item = makeGenericFromLibrary("Transistor", pos, value, parent)) return item;
        return new TransistorItem(pos, value, TransistorItem::NPN, parent);
    });

    // Register Transistor PNP
    factory.registerItemType("Transistor_PNP", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("2N3906");
        if (auto* item = makeGenericFromLibrary("Transistor_PNP", pos, value, parent)) return item;
        return new TransistorItem(pos, value, TransistorItem::PNP, parent);
    });

    // Register Transistor NMOS
    factory.registerItemType("Transistor_NMOS", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("2N7000");
        if (auto* item = makeGenericFromLibrary("Transistor_NMOS", pos, value, parent)) return item;
        return new TransistorItem(pos, value, TransistorItem::NMOS, parent);
    });

    // Register Transistor PMOS
    factory.registerItemType("Transistor_PMOS", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("BS250");
        if (auto* item = makeGenericFromLibrary("Transistor_PMOS", pos, value, parent)) return item;
        return new TransistorItem(pos, value, TransistorItem::PMOS, parent);
    });

    // Register Resistor US
    factory.registerItemType("Resistor_US", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("10k");
        if (auto* item = makeGenericFromLibrary("Resistor_US", pos, value, parent)) return item;
        return new ResistorItem(pos, value, ResistorItem::US, parent);
    });

    // Register Resistor IEC
    factory.registerItemType("Resistor_IEC", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("10k");
        if (auto* item = makeGenericFromLibrary("Resistor_IEC", pos, value, parent)) return item;
        return new ResistorItem(pos, value, ResistorItem::IEC, parent);
    });
    
    // Register Capacitor Non-Polarized
    factory.registerItemType("Capacitor_NonPolar", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        QString value = properties.value("value").toString("100nF");
        return new CapacitorItem(pos, value, CapacitorItem::NonPolarized, parent);
    });
    
    // Register Capacitor Polarized
    factory.registerItemType("Capacitor_Polarized", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("10uF");
        if (auto* item = makeGenericFromLibrary("Capacitor_Polarized", pos, value, parent)) return item;
        return new CapacitorItem(pos, value, CapacitorItem::Polarized, parent);
    });

    // Register Transformer
    factory.registerItemType("Transformer", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        if (SymbolDefinition* def = SymbolLibraryManager::instance().findSymbol("Transformer")) {
            auto* item = new GenericComponentItem(*def, parent);
            item->setPos(pos);
            item->setValue(properties.value("value").toString("1:1"));
            return item;
        }
        return new TransformerItem(pos, properties.value("value").toString("1:1"), parent);
    });

    // Register IC item
    factory.registerItemType("IC", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("74HC00");
        if (auto* item = makeGenericFromLibrary("IC", pos, value, parent)) return item;
        const int pinCount = properties.value("pinCount").toInt(8);
        return new ICItem(pos, value, pinCount, parent);
    });

    factory.registerItemType("RAM", [makeGenericFromLibrary](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        const QString value = properties.value("value").toString("AS6C62256");
        if (auto* item = makeGenericFromLibrary("RAM", pos, value, parent)) return item;
        const int pinCount = properties.value("pinCount").toInt(16);
        return new ICItem(pos, value, pinCount, parent);
    });

    auto addLogicGate = [&](const QString& gateName, const QString& defaultValue) {
        factory.registerItemType(gateName, [makeGenericFromLibrary, gateName, defaultValue](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
            const QString value = properties.value("value").toString(defaultValue);
            return makeGenericFromLibrary(gateName, pos, value, parent);
        });
    };
    addLogicGate("Gate_AND", "74HC08");
    addLogicGate("Gate_OR", "74HC32");
    addLogicGate("Gate_XOR", "74HC86");
    addLogicGate("Gate_NAND", "74HC00");
    addLogicGate("Gate_NOR", "74HC02");
    addLogicGate("Gate_NOT", "74HC04");

    // Register Power items
    // Each lambda restores from properties (custom net names, position, etc.) via fromJson
    auto makePowerItem = [](PowerItem::PowerType type, QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new PowerItem(pos, type, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    };

    factory.registerItemType("Power", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        // Legacy fallback: read power_type int from old files saved with type="Power"
        PowerItem::PowerType t = (PowerItem::PowerType)properties.value("power_type").toInt(PowerItem::GND);
        return makePowerItem(t, pos, properties, parent);
    });

    factory.registerItemType("GND", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::GND, pos, properties, parent);
    });

    factory.registerItemType("VCC", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::VCC, pos, properties, parent);
    });

    factory.registerItemType("VDD", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::VDD, pos, properties, parent);
    });

    factory.registerItemType("VSS", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::VSS, pos, properties, parent);
    });

    factory.registerItemType("VBAT", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::VBAT, pos, properties, parent);
    });

    factory.registerItemType("3.3V", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::THREE_V_THREE, pos, properties, parent);
    });

    factory.registerItemType("5V", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::FIVE_V, pos, properties, parent);
    });

    factory.registerItemType("12V", [makePowerItem](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        return makePowerItem(PowerItem::TWELVE_V, pos, properties, parent);
    });

    factory.registerItemType("Net Label", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        QString label = properties.value("label").toString("NET");
        return new NetLabelItem(pos, label, parent);
    });

    factory.registerItemType("HierarchicalPort", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        QString label = properties.value("label").toString("PORT");
        return new HierarchicalPortItem(pos, label, HierarchicalPortItem::Input, parent);
    });

    factory.registerItemType("Voltage_Source_DC", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        QString value = properties.value("value").toString("5V");
        auto* item = new VoltageSourceItem(pos, value, VoltageSourceItem::DC, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });

    factory.registerItemType("Voltage_Source_Sine", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new VoltageSourceItem(pos, "SINE(0 1 1k)", VoltageSourceItem::Sine, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });

    factory.registerItemType("Voltage_Source_Pulse", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new VoltageSourceItem(pos, "PULSE(0 5 0 1u 1u 0.5m 1m)", VoltageSourceItem::Pulse, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });

    factory.registerItemType("Voltage_Source_Behavioral", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new VoltageSourceItem(pos, "V=V(1)*2", VoltageSourceItem::Behavioral, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });

    factory.registerItemType("BV", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new VoltageSourceItem(pos, "V=V(1)*2", VoltageSourceItem::Behavioral, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });

    factory.registerItemType("Voltage_Source_AC", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        QString value = properties.value("value").toString("1V");
        auto* item = new VoltageSourceItem(pos, value, VoltageSourceItem::Sine, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });

    factory.registerItemType("Sheet", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new SchematicSheetItem(pos, parent);
    });

    factory.registerItemType("Text", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new SchematicTextItem("", pos, parent);
    });

    factory.registerItemType("Spice Directive", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new SchematicSpiceDirectiveItem("", pos, parent);
    });

    factory.registerItemType("Switch", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new SwitchItem(pos, parent);
    });

    factory.registerItemType("PushButton", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new PushButtonItem(pos, parent);
    });

    factory.registerItemType("LED", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new LEDItem(pos, parent);
    });

    factory.registerItemType("Signal Generator", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new SignalGeneratorItem(pos, parent);
    });

    factory.registerItemType("Logic Analyzer", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new LogicAnalyzerItem(pos, parent);
    });

    factory.registerItemType("Oscilloscope Instrument", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::Oscilloscope, pos, parent);
    });

    factory.registerItemType("Voltmeter (DC)", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::Voltmeter, pos, parent);
    });

    factory.registerItemType("Voltmeter (AC)", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::Voltmeter, pos, parent);
    });

    factory.registerItemType("Ammeter (DC)", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::Ammeter, pos, parent);
    });

    factory.registerItemType("Ammeter (AC)", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::Ammeter, pos, parent);
    });

    factory.registerItemType("Wattmeter", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::Wattmeter, pos, parent);
    });

    factory.registerItemType("Frequency Counter", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::FrequencyCounter, pos, parent);
    });

    factory.registerItemType("Logic Probe", [](QPointF pos, const QJsonObject&, QGraphicsItem* parent) -> SchematicItem* {
        return new InstrumentProbeItem(InstrumentProbeItem::Kind::LogicProbe, pos, parent);
    });

    factory.registerItemType("SmartSignalBlock", [](QPointF pos, const QJsonObject& properties, QGraphicsItem* parent) -> SchematicItem* {
        auto* item = new SmartSignalItem(pos, parent);
        if (!properties.isEmpty()) item->fromJson(properties);
        return item;
    });
}
