#ifndef PCBTOOLREGISTRY_H
#define PCBTOOLREGISTRY_H

#include "pcb_tool.h"
#include <QMap>
#include <QString>
#include <functional>

class PCBToolRegistry {
public:
    using ToolCreator = std::function<PCBTool*()>;

    static PCBToolRegistry& instance();

    // Register a new tool
    void registerTool(const QString& toolName, ToolCreator creator);

    // Create tool by name
    PCBTool* createTool(const QString& toolName);

    // Get all registered tool names
    QStringList registeredTools() const;

    // Check if tool is registered
    bool isToolRegistered(const QString& toolName) const;

    // Get tool metadata
    PCBTool* getTool(const QString& toolName) const;

private:
    PCBToolRegistry() = default;
    ~PCBToolRegistry() = default;
    PCBToolRegistry(const PCBToolRegistry&) = delete;
    PCBToolRegistry& operator=(const PCBToolRegistry&) = delete;

    QMap<QString, ToolCreator> m_toolCreators;
    QMap<QString, PCBTool*> m_toolInstances;
};

#endif // PCBTOOLREGISTRY_H
