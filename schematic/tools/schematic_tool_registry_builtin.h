#ifndef SCHEMATICTOOLREGISTRYBUILTIN_H
#define SCHEMATICTOOLREGISTRYBUILTIN_H

#include "schematic_tool_registry.h"
#include "schematic_select_tool.h"
#include "schematic_component_tool.h"

class SchematicToolRegistryBuiltIn {
public:
    static void registerBuiltInTools();

private:
    SchematicToolRegistryBuiltIn() = default;
};

#endif // SCHEMATICTOOLREGISTRYBUILTIN_H
