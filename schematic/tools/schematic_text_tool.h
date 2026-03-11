#ifndef SCHEMATIC_TEXT_TOOL_H
#define SCHEMATIC_TEXT_TOOL_H

#include "schematic_tool.h"

class SchematicTextTool : public SchematicTool {
    Q_OBJECT
public:
    SchematicTextTool(QObject* parent = nullptr);
    virtual ~SchematicTextTool() = default;

    void mouseReleaseEvent(QMouseEvent* event) override;
};

#endif // SCHEMATIC_TEXT_TOOL_H
