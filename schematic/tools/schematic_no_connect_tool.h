#ifndef SCHEMATIC_NO_CONNECT_TOOL_H
#define SCHEMATIC_NO_CONNECT_TOOL_H

#include "schematic_tool.h"

class SchematicNoConnectTool : public SchematicTool {
    Q_OBJECT
public:
    explicit SchematicNoConnectTool(QObject* parent = nullptr);

    void mousePressEvent(QMouseEvent* event) override;
    
    QString iconName() const override { return "tool_no_connect"; }
};

#endif // SCHEMATIC_NO_CONNECT_TOOL_H
