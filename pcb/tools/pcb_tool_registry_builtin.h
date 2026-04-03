#ifndef PCBTOOLREGISTRYBUILTIN_H
#define PCBTOOLREGISTRYBUILTIN_H

#include "pcb_tool_registry.h"
#include "pcb_select_tool.h"
#include "pcb_pad_tool.h"
#include "pcb_trace_tool.h"
#include "pcb_via_tool.h"

class PCBToolRegistryBuiltIn {
public:
    static void registerBuiltInTools();

private:
    PCBToolRegistryBuiltIn() = default;
};

#endif // PCBTOOLREGISTRYBUILTIN_H
