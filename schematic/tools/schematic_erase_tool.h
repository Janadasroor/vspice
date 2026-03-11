#ifndef SCHEMATICERASETOOL_H
#define SCHEMATICERASETOOL_H

#include "schematic_tool.h"

/**
 * @brief Tool for deleting schematic items by clicking on them
 */
class SchematicEraseTool : public SchematicTool {
    Q_OBJECT

public:
    explicit SchematicEraseTool(QObject* parent = nullptr);

    QString tooltip() const override { return "Erase components and wires"; }
    QString iconName() const override { return "erase"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
};

#endif // SCHEMATICERASETOOL_H
