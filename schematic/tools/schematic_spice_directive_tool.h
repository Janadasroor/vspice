#ifndef SCHEMATIC_SPICE_DIRECTIVE_TOOL_H
#define SCHEMATIC_SPICE_DIRECTIVE_TOOL_H

#include "schematic_tool.h"

/**
 * @brief Tool to place SPICE directive text items on the schematic
 */
class SchematicSpiceDirectiveTool : public SchematicTool {
    Q_OBJECT

public:
    explicit SchematicSpiceDirectiveTool(QObject* parent = nullptr);

    QString tooltip() const override { return "SPICE Directive: Add SPICE commands and models (.tran, .model, etc.)"; }
    QString iconName() const override { return "tool_text"; } // Reuse text icon for now, or use a specialized one

    void mouseReleaseEvent(QMouseEvent* event) override;
};

#endif // SCHEMATIC_SPICE_DIRECTIVE_TOOL_H
