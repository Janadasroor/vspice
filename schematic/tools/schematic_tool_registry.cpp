#include "schematic_tool_registry.h"
#include <QDebug>
#include "symbol_library.h"
#include "schematic_component_tool.h"

SchematicToolRegistry& SchematicToolRegistry::instance() {
    static SchematicToolRegistry instance;
    return instance;
}

void SchematicToolRegistry::registerTool(const QString& toolName, ToolCreator creator) {
    if (m_toolCreators.contains(toolName)) {
        qWarning() << "Schematic tool" << toolName << "is already registered. Overwriting.";
    }
    m_toolCreators[toolName] = creator;
    qDebug() << "Registered schematic tool:" << toolName;
}

SchematicTool* SchematicToolRegistry::createTool(const QString& toolName) {
    auto it = m_toolCreators.find(toolName);
    if (it == m_toolCreators.end()) {
        // Fallback: Check library for dynamic symbol
        if (SymbolLibraryManager::instance().findSymbol(toolName)) {
             if (m_toolInstances.contains(toolName)) return m_toolInstances[toolName];
             SchematicTool* tool = new SchematicComponentTool(toolName);
             m_toolInstances[toolName] = tool;
             return tool;
        }

        qWarning() << "Unknown schematic tool:" << toolName;
        return nullptr;
    }

    // Reuse existing instance if available
    if (m_toolInstances.contains(toolName)) {
        return m_toolInstances[toolName];
    }

    SchematicTool* tool = it.value()();
    if (tool) {
        m_toolInstances[toolName] = tool;
    }
    return tool;
}

QStringList SchematicToolRegistry::registeredTools() const {
    return m_toolCreators.keys();
}

bool SchematicToolRegistry::isToolRegistered(const QString& toolName) const {
    return m_toolCreators.contains(toolName);
}

SchematicTool* SchematicToolRegistry::getTool(const QString& toolName) const {
    return m_toolInstances.value(toolName, nullptr);
}
