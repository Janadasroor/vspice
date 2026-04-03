#include "pcb_tool_registry_builtin.h"
#include "pcb_component_tool.h"
#include "pcb_trace_tool.h"
#include "pcb_diff_pair_tool.h"
#include "pcb_via_tool.h"
#include "pcb_pour_tool.h"
#include "pcb_rect_tool.h"
#include "pcb_length_tuning_tool.h"
#include "pcb_zoom_area_tool.h"
#include "pcb_measure_tool.h"
#include "pcb_erase_tool.h"

void PCBToolRegistryBuiltIn::registerBuiltInTools() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    auto& registry = PCBToolRegistry::instance();

    // Selection and basic tools
    registry.registerTool("Select", []() { return new PCBSelectTool(); });
    registry.registerTool("Erase", []() { return new PCBEraseTool(); });
    registry.registerTool("Zoom Area", []() { return new PCBZoomAreaTool(); });
    registry.registerTool("Pad", []() { return new PCBPadTool(); });
    registry.registerTool("Component", []() { return new PCBComponentTool(); });
    
    // Routing tools
    registry.registerTool("Trace", []() { return new PCBTraceTool(); });
    registry.registerTool("Diff Pair", []() { return new PCBDiffPairTool(); });
    registry.registerTool("Via", []() { return new PCBViaTool(); });
    
    // Fill/Zone tools
    registry.registerTool("Polygon Pour", []() { return new PCBPourTool(); });
    registry.registerTool("Rectangle", []() { return new PCBRectTool(); });
    registry.registerTool("Length Tuning", []() { return new PCBLengthTuningTool(); });

    // Measurement
    registry.registerTool("Measure", []() { return new PCBMeasureTool(); });
}
