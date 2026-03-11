#ifndef SCHEMATIC_MENU_REGISTRY_H
#define SCHEMATIC_MENU_REGISTRY_H

#include <QString>
#include <QIcon>
#include <QKeySequence>
#include <functional>
#include <vector>
#include <map>
#include "../items/schematic_item.h"

class SchematicView;

struct ContextAction {
    QString label;
    QIcon icon;
    QKeySequence shortcut;
    int priority = 0;
    
    // Predicate to determine if action is visible/enabled for specific items
    std::function<bool(const QList<SchematicItem*>&)> isVisible = [](auto) { return true; };
    std::function<bool(const QList<SchematicItem*>&)> isEnabled = [](auto) { return true; };
    
    // The action itself
    std::function<void(SchematicView*, const QList<SchematicItem*>&)> handler;
    
    bool isSeparator = false;

    static ContextAction separator(int priority = 0) {
        ContextAction a;
        a.isSeparator = true;
        a.priority = priority;
        return a;
    }
};

class SchematicMenuRegistry {
public:
    static SchematicMenuRegistry& instance();

    // Register an action for a specific item type
    void registerAction(SchematicItem::ItemType type, const ContextAction& action);
    
    // Register a global action (always available if applicable)
    void registerGlobalAction(const ContextAction& action);

    // Get all applicable actions for a set of items
    std::vector<ContextAction> getActions(const QList<SchematicItem*>& items) const;

    // Initialize with standard EDA actions
    void initializeDefaultActions();

private:
    SchematicMenuRegistry() = default;
    
    std::multimap<SchematicItem::ItemType, ContextAction> m_typeActions;
    std::vector<ContextAction> m_globalActions;
};

#endif // SCHEMATIC_MENU_REGISTRY_H
