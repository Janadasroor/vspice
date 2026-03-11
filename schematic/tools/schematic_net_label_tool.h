#ifndef SCHEMATIC_NET_LABEL_TOOL_H
#define SCHEMATIC_NET_LABEL_TOOL_H

#include "schematic_tool.h"
#include "../items/net_label_item.h"

class SchematicNetLabelTool : public SchematicTool {
    Q_OBJECT
public:
    SchematicNetLabelTool(NetLabelItem::LabelScope scope = NetLabelItem::Local, QObject* parent = nullptr);
    virtual ~SchematicNetLabelTool() = default;

    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    NetLabelItem::LabelScope m_scope;
};

#endif // SCHEMATIC_NET_LABEL_TOOL_H
