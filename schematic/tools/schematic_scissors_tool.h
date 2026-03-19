#ifndef SCHEMATIC_SCISSORS_TOOL_H
#define SCHEMATIC_SCISSORS_TOOL_H

#include "schematic_tool.h"
#include <QGraphicsRectItem>

class SchematicScissorsTool : public SchematicTool {
    Q_OBJECT
public:
    explicit SchematicScissorsTool(QObject* parent = nullptr);
    ~SchematicScissorsTool();

    QCursor cursor() const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_rubberBandActive;
    QPointF m_rubberBandOrigin;
    QGraphicsRectItem* m_rubberBandItem;
};

#endif // SCHEMATIC_SCISSORS_TOOL_H
