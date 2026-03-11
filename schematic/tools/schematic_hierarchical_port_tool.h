#ifndef SCHEMATIC_HIERARCHICAL_PORT_TOOL_H
#define SCHEMATIC_HIERARCHICAL_PORT_TOOL_H

#include "schematic_tool.h"

class SchematicHierarchicalPortTool : public SchematicTool {
    Q_OBJECT
public:
    SchematicHierarchicalPortTool(QObject* parent = nullptr);
    virtual ~SchematicHierarchicalPortTool() = default;

    void mouseReleaseEvent(QMouseEvent* event) override;
};

#endif // SCHEMATIC_HIERARCHICAL_PORT_TOOL_H
