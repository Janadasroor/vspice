#include "pcb_tool_registry.h"
#include <QDebug>

PCBToolRegistry& PCBToolRegistry::instance() {
    static PCBToolRegistry instance;
    return instance;
}

void PCBToolRegistry::registerTool(const QString& toolName, ToolCreator creator) {
    if (m_toolCreators.contains(toolName)) {
        qWarning() << "Tool" << toolName << "is already registered. Overwriting.";
    }
    m_toolCreators[toolName] = creator;
    qDebug() << "Registered PCB tool:" << toolName;
}

PCBTool* PCBToolRegistry::createTool(const QString& toolName) {
    if (m_toolInstances.contains(toolName)) {
        return m_toolInstances[toolName];
    }

    auto it = m_toolCreators.find(toolName);
    if (it == m_toolCreators.end()) {
        qWarning() << "Unknown PCB tool:" << toolName;
        return nullptr;
    }

    PCBTool* tool = it.value()();
    if (tool) {
        m_toolInstances[toolName] = tool;
    }
    return tool;
}

QStringList PCBToolRegistry::registeredTools() const {
    return m_toolCreators.keys();
}

bool PCBToolRegistry::isToolRegistered(const QString& toolName) const {
    return m_toolCreators.contains(toolName);
}

PCBTool* PCBToolRegistry::getTool(const QString& toolName) const {
    return m_toolInstances.value(toolName, nullptr);
}
