#ifndef SCHEMATICTOOLREGISTRY_H
#define SCHEMATICTOOLREGISTRY_H

#include "schematic_tool.h"
#include <QMap>
#include <QString>
#include <functional>

class SchematicToolRegistry {
public:
    using ToolCreator = std::function<SchematicTool*()>;

    static SchematicToolRegistry& instance();

    // Register a new tool
    void registerTool(const QString& toolName, ToolCreator creator);

    // Create tool by name
    SchematicTool* createTool(const QString& toolName);

    // Get all registered tool names
    QStringList registeredTools() const;

    // Check if tool is registered
    bool isToolRegistered(const QString& toolName) const;

    // Get tool metadata
    SchematicTool* getTool(const QString& toolName) const;

private:
    SchematicToolRegistry() = default;
    ~SchematicToolRegistry() = default;
    SchematicToolRegistry(const SchematicToolRegistry&) = delete;
    SchematicToolRegistry& operator=(const SchematicToolRegistry&) = delete;

    QMap<QString, ToolCreator> m_toolCreators;
    QMap<QString, SchematicTool*> m_toolInstances;
};

#endif // SCHEMATICTOOLREGISTRY_H
