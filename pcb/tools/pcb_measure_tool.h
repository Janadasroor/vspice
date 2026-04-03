#ifndef PCB_MEASURE_TOOL_H
#define PCB_MEASURE_TOOL_H

#include "pcb_tool.h"
#include <QPointF>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>

class PCBMeasureTool : public PCBTool {
    Q_OBJECT

public:
    explicit PCBMeasureTool(QObject* parent = nullptr);
    virtual ~PCBMeasureTool();

    void activate(PCBView* view) override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    QCursor cursor() const override;
    QString tooltip() const override { return "Measure Distance (M)"; }

private:
    void updateMeasureGraphics(QPointF endPos);
    void cleanup();

    bool m_isMeasuring;
    QPointF m_startPos;
    
    QGraphicsLineItem* m_line;
    QGraphicsSimpleTextItem* m_label;
    QGraphicsRectItem* m_labelBg;
};

#endif // PCB_MEASURE_TOOL_H
