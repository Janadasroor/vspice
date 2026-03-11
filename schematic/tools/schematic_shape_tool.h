#ifndef SCHEMATIC_SHAPE_TOOL_H
#define SCHEMATIC_SHAPE_TOOL_H

#include "schematic_tool.h"
#include "../items/schematic_shape_item.h"

class SchematicShapeTool : public SchematicTool {
    Q_OBJECT
public:
    SchematicShapeTool(SchematicShapeItem::ShapeType type, QObject* parent = nullptr);
    virtual ~SchematicShapeTool() = default;

    QCursor cursor() const override;

    void activate(SchematicView* view) override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    SchematicShapeItem::ShapeType m_type;
    SchematicShapeItem* m_previewItem;
    QPointF m_startPos;
    QList<QPointF> m_points;
    bool m_isDragging;
};

#endif // SCHEMATIC_SHAPE_TOOL_H
