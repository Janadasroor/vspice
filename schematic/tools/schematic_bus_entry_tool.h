#ifndef SCHEMATICBUSENTRYTOOL_H
#define SCHEMATICBUSENTRYTOOL_H

#include "schematic_tool.h"

class SchematicBusEntryTool : public SchematicTool {
    Q_OBJECT
public:
    SchematicBusEntryTool(QObject* parent = nullptr) : SchematicTool("Bus Entry", parent) {}
    
    QString tooltip() const override { return "Connect wire to bus via 45-degree entry"; }
    QString iconName() const override { return "bus_entry"; }
    QCursor cursor() const override { return Qt::CrossCursor; }

    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool m_flipped = false;
};

#endif // SCHEMATICBUSENTRYTOOL_H
