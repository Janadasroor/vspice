#ifndef SCHEMATICITEMREGISTRY_H
#define SCHEMATICITEMREGISTRY_H

#include "schematic_item_factory.h"
#include "resistor_item.h"

class SchematicItemRegistry {
public:
    static void registerBuiltInItems();

private:
    SchematicItemRegistry() = default;
};

#endif // SCHEMATICITEMREGISTRY_H
