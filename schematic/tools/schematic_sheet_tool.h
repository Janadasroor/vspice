#ifndef SCHEMATIC_SHEET_TOOL_H
#define SCHEMATIC_SHEET_TOOL_H

#include "schematic_tool.h"
#include <QPointF>

class SchematicSheetItem;

class SchematicSheetTool : public SchematicTool {
    Q_OBJECT
public:
    explicit SchematicSheetTool(QObject* parent = nullptr);
    
    // name() is handled by base constructor
    QString iconName() const override { return "tool_sheet"; }
    QString tooltip() const override { return "Place Hierarchical Sheet (S)"; }

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void activate(SchematicView* view) override;
    void deactivate() override;

private:
    SchematicSheetItem* m_previewItem;
};

#endif // SCHEMATIC_SHEET_TOOL_H