#include "schematic_tool_registry_builtin.h"
#include "schematic_wire_tool.h"
#include "schematic_select_tool.h"
#include "schematic_component_tool.h"
#include "schematic_zoom_area_tool.h"
#include "schematic_erase_tool.h"
#include "schematic_probe_tool.h"

#include "schematic_shape_tool.h"
#include "schematic_text_tool.h"
#include "schematic_no_connect_tool.h"
#include "schematic_bus_tool.h"
#include "schematic_bus_entry_tool.h"
#include "schematic_net_label_tool.h"
#include "schematic_hierarchical_port_tool.h"
#include "schematic_sheet_tool.h"

void SchematicToolRegistryBuiltIn::registerBuiltInTools() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    auto& registry = SchematicToolRegistry::instance();

    registry.registerTool("Select", []() { return new SchematicSelectTool(); });
    registry.registerTool("Probe", []() { return new SchematicProbeTool("Probe", SchematicProbeTool::ProbeKind::Voltage); });
    registry.registerTool("Voltage Probe", []() { return new SchematicProbeTool("Voltage Probe", SchematicProbeTool::ProbeKind::Voltage); });
    registry.registerTool("Current Probe", []() { return new SchematicProbeTool("Current Probe", SchematicProbeTool::ProbeKind::Current); });
    registry.registerTool("Power Probe", []() { return new SchematicProbeTool("Power Probe", SchematicProbeTool::ProbeKind::Power); });
    registry.registerTool("Erase", []() { return new SchematicEraseTool(); });
    registry.registerTool("Wire", []() { return new SchematicWireTool(); });
    registry.registerTool("Zoom Area", []() { return new SchematicZoomAreaTool(); });
    registry.registerTool("No-Connect", []() { return new SchematicNoConnectTool(); });
    registry.registerTool("Bus", []() { return new SchematicBusTool(); });
    registry.registerTool("Bus Entry", []() { return new SchematicBusEntryTool(); });
    registry.registerTool("Net Label", []() { return new SchematicNetLabelTool(NetLabelItem::Local); });
    registry.registerTool("Global Label", []() { return new SchematicNetLabelTool(NetLabelItem::Global); });
    registry.registerTool("Hierarchical Port", []() { return new SchematicHierarchicalPortTool(); });
    registry.registerTool("Sheet", []() { return new SchematicSheetTool(); });
    
    // Drawings
    registry.registerTool("Rectangle", []() { return new SchematicShapeTool(SchematicShapeItem::Rectangle); });
    registry.registerTool("Circle", []() { return new SchematicShapeTool(SchematicShapeItem::Circle); });
    registry.registerTool("Line", []() { return new SchematicShapeTool(SchematicShapeItem::Line); });
    registry.registerTool("Polygon", []() { return new SchematicShapeTool(SchematicShapeItem::Polygon); });
    registry.registerTool("Bezier", []() { return new SchematicShapeTool(SchematicShapeItem::Bezier); });
    registry.registerTool("Text", []() { return new SchematicTextTool(); });

    registry.registerTool("Resistor", []() { return new SchematicComponentTool("Resistor"); });
    registry.registerTool("Resistor (US)", []() { return new SchematicComponentTool("Resistor_US"); });
    registry.registerTool("Resistor (IEC)", []() { return new SchematicComponentTool("Resistor_IEC"); });
    
    registry.registerTool("Capacitor", []() { return new SchematicComponentTool("Capacitor"); });
    registry.registerTool("Capacitor (Non-Polar)", []() { return new SchematicComponentTool("Capacitor_NonPolar"); });
    registry.registerTool("Capacitor (Polarized)", []() { return new SchematicComponentTool("Capacitor_Polarized"); });
    
    registry.registerTool("Inductor", []() { return new SchematicComponentTool("Inductor"); });
    registry.registerTool("Transformer", []() { return new SchematicComponentTool("Transformer"); });
    
    registry.registerTool("Diode", []() { return new SchematicComponentTool("Diode"); });
    
    registry.registerTool("Transistor", []() { return new SchematicComponentTool("Transistor"); });
    registry.registerTool("NPN Transistor", []() { return new SchematicComponentTool("Transistor"); }); // Alias
    registry.registerTool("PNP Transistor", []() { return new SchematicComponentTool("Transistor_PNP"); });
    registry.registerTool("NMOS Transistor", []() { return new SchematicComponentTool("Transistor_NMOS"); });
    registry.registerTool("PMOS Transistor", []() { return new SchematicComponentTool("Transistor_PMOS"); });
    registry.registerTool("Gate_AND", []() { return new SchematicComponentTool("Gate_AND"); });
    registry.registerTool("Gate_OR", []() { return new SchematicComponentTool("Gate_OR"); });
    registry.registerTool("Gate_XOR", []() { return new SchematicComponentTool("Gate_XOR"); });
    registry.registerTool("Gate_NAND", []() { return new SchematicComponentTool("Gate_NAND"); });
    registry.registerTool("Gate_NOR", []() { return new SchematicComponentTool("Gate_NOR"); });
    registry.registerTool("Gate_NOT", []() { return new SchematicComponentTool("Gate_NOT"); });
    
    registry.registerTool("IC", []() { return new SchematicComponentTool("IC"); });
    registry.registerTool("RAM", []() { return new SchematicComponentTool("RAM"); });
    
    // Interactive Components
    registry.registerTool("Switch", []() { return new SchematicComponentTool("Switch"); });
    registry.registerTool("Push Button", []() { return new SchematicComponentTool("PushButton"); });
    registry.registerTool("LED", []() { return new SchematicComponentTool("LED"); });
    registry.registerTool("Signal Generator", []() { return new SchematicComponentTool("Signal Generator"); });
    registry.registerTool("Logic Analyzer", []() { return new SchematicComponentTool("Logic Analyzer"); });
    registry.registerTool("Oscilloscope Instrument", []() { return new SchematicComponentTool("Oscilloscope Instrument"); });
    registry.registerTool("Smart Signal Block", []() { return new SchematicComponentTool("SmartSignalBlock"); });
    
    registry.registerTool("Voltage Source (DC)", []() { return new SchematicComponentTool("Voltage_Source_DC"); });
    registry.registerTool("Voltage Source (Sine)", []() { return new SchematicComponentTool("Voltage_Source_Sine"); });
    registry.registerTool("Voltage Source (Pulse)", []() { return new SchematicComponentTool("Voltage_Source_Pulse"); });
    registry.registerTool("Voltage Source (AC)", []() { return new SchematicComponentTool("Voltage_Source_Sine"); }); // Alias
    registry.registerTool("Voltmeter (DC)", []() { return new SchematicComponentTool("Voltmeter (DC)"); });
    registry.registerTool("Voltmeter (AC)", []() { return new SchematicComponentTool("Voltmeter (AC)"); });
    registry.registerTool("Ammeter (DC)", []() { return new SchematicComponentTool("Ammeter (DC)"); });
    registry.registerTool("Ammeter (AC)", []() { return new SchematicComponentTool("Ammeter (AC)"); });
    registry.registerTool("Wattmeter", []() { return new SchematicComponentTool("Wattmeter"); });
    registry.registerTool("Power Meter", []() { return new SchematicComponentTool("Wattmeter"); }); // alias
    registry.registerTool("Frequency Counter", []() { return new SchematicComponentTool("Frequency Counter"); });
    registry.registerTool("Logic Probe", []() { return new SchematicComponentTool("Logic Probe"); });
    
    // Power components
    registry.registerTool("GND", []() { return new SchematicComponentTool("GND"); });
    registry.registerTool("VCC", []() { return new SchematicComponentTool("VCC"); });
    registry.registerTool("VDD", []() { return new SchematicComponentTool("VDD"); });
    registry.registerTool("VSS", []() { return new SchematicComponentTool("VSS"); });
    registry.registerTool("VBAT", []() { return new SchematicComponentTool("VBAT"); });
    registry.registerTool("3.3V", []() { return new SchematicComponentTool("3.3V"); });
    registry.registerTool("5V", []() { return new SchematicComponentTool("5V"); });
    registry.registerTool("12V", []() { return new SchematicComponentTool("12V"); });
}
