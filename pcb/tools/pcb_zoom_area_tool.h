#ifndef PCB_ZOOM_AREA_TOOL_H
#define PCB_ZOOM_AREA_TOOL_H

#include "pcb_tool.h"
#include <QRubberBand>

class PCBZoomAreaTool : public PCBTool {
    Q_OBJECT

public:
    explicit PCBZoomAreaTool(QObject* parent = nullptr);
    virtual ~PCBZoomAreaTool();

    virtual void activate(PCBView* view) override;
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

#endif // PCB_ZOOM_AREA_TOOL_H
