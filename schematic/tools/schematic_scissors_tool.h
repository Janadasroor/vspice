#ifndef SCHEMATICSCISSORSTOOL_H
#define SCHEMATICSCISSORSTOOL_H

#include "schematic_tool.h"

/**
 * @brief Tool for deleting schematic items by clicking on them (Scissors tool)
 */
class SchematicScissorsTool : public SchematicTool {
    Q_OBJECT

public:
    explicit SchematicScissorsTool(QObject* parent = nullptr);

    QString tooltip() const override { return "Scissors: Delete components and wires"; }
    QString iconName() const override { return "tool_scissors"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
};

#endif // SCHEMATICSCISSORSTOOL_H
