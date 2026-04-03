#ifndef PCBITEMREGISTRY_H
#define PCBITEMREGISTRY_H

#include "pcb_item_factory.h"
#include "pad_item.h"
#include "via_item.h"
#include "trace_item.h"
#include "component_item.h"
#include "copper_pour_item.h"

class PCBItemRegistry {
public:
    static void registerBuiltInItems();

private:
    PCBItemRegistry() = default;
};

#endif // PCBITEMREGISTRY_H
