#ifndef PCBCOURTOOL_H
#define PCBCOURTOOL_H

#include "pcb_tool.h"
#include <QPolygonF>

class CopperPourItem;
class QGraphicsPathItem;

class PCBPourTool : public PCBTool {
    Q_OBJECT

public:
    explicit PCBPourTool(QObject* parent = nullptr);
    ~PCBPourTool() override;

    QCursor cursor() const override;
    QString tooltip() const override { return "Click to add polygon points. Double-click to close and fill."; }

    void activate(PCBView* view) override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // Tool properties
    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

    // Properties
    QString netName() const { return m_netName; }
    void setNetName(const QString& net) { m_netName = net; }

    double clearance() const { return m_clearance; }
    void setClearance(double clearance) { m_clearance = clearance; }

    int layer() const { return m_layerId; }
    void setLayer(int layerId) { m_layerId = layerId; }

private:
    void startPour(QPointF pos);
    void addPoint(QPointF pos);
    void updatePreview(QPointF pos);
    void finishPour();
    void cancelPour();

    bool m_isDrawing;
    QPolygonF m_currentPolygon;
    QGraphicsPathItem* m_previewPath;
    QString m_netName;
    double m_clearance;
    int m_layerId;
};

#endif // PCBCOURTOOL_H
