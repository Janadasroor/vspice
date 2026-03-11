#ifndef SCHEMATIC_ZOOM_AREA_TOOL_H
#define SCHEMATIC_ZOOM_AREA_TOOL_H

#include "schematic_tool.h"
#include <QRubberBand>

class SchematicZoomAreaTool : public SchematicTool {
    Q_OBJECT

public:
    explicit SchematicZoomAreaTool(QObject* parent = nullptr);
    virtual ~SchematicZoomAreaTool();

    virtual void activate(SchematicView* view) override;
    virtual void deactivate() override;

    virtual void mousePressEvent(QMouseEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

    virtual QCursor cursor() const override;
    virtual QString tooltip() const override { return "Zoom Area (Z)"; }

private:
    QRubberBand* m_rubberBand;
    QPoint m_origin;
};

#endif // SCHEMATIC_ZOOM_AREA_TOOL_H
