#ifndef PCB_RECT_TOOL_H
#define PCB_RECT_TOOL_H

#include "pcb_tool.h"
#include <QPointF>
#include <QGraphicsRectItem>

/**
 * @brief Tool for drawing rectangular copper pours with two clicks.
 */
class PCBRectTool : public PCBTool {
    Q_OBJECT

public:
    explicit PCBRectTool(QObject* parent = nullptr);
    virtual ~PCBRectTool() = default;

    void activate(PCBView* view) override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    QCursor cursor() const override { return QCursor(Qt::CrossCursor); }

    // Tool properties
    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

    int layer() const { return m_layerId; }
    void setLayer(int layerId) { m_layerId = layerId; }

private:
    void updatePreview(const QPointF& pos);
    void finishRect(const QPointF& pos);

    bool m_isDrawing;
    QPointF m_origin;
    QGraphicsRectItem* m_previewRect;
    int m_layerId;
};

#endif // PCB_RECT_TOOL_H
