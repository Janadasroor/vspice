#ifndef PCBLENGTHTUNINGTOOL_H
#define PCBLENGTHTUNINGTOOL_H

#include "pcb_tool.h"
#include <QPointF>
#include <QGraphicsPathItem>
#include <QList>

class TraceItem;

class PCBLengthTuningTool : public PCBTool {
    Q_OBJECT
public:
    explicit PCBLengthTuningTool(QObject* parent = nullptr);
    ~PCBLengthTuningTool() override;

    QString name() const { return "Length Tuning"; }
    QString iconName() const { return "tool_meander"; }
    QString tooltip() const { return "Interactive Length Tuning: Drag along a trace to add meanders (U/I adjust amplitude)"; }

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

    // Properties
    double targetLength() const { return m_targetLength; }
    void setTargetLength(double length) { m_targetLength = length; }

    double amplitude() const { return m_amplitude; }
    void setAmplitude(double a) { m_amplitude = a; }

    // PCBTool interface
    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

private:
    void updateMeanderPreview(QPointF pos);
    QPainterPath generateMeander(QPointF start, QPointF end, double targetLen);
    QList<QPointF> generateMeanderPoints(QPointF start, QPointF end, double targetLen) const;

    bool m_isActive;
    TraceItem* m_targetTrace;
    double m_targetLength;
    double m_amplitude;
    double m_spacing;
    
    QGraphicsPathItem* m_previewPath;
    class QLabel* m_lengthLabel;
};

#endif // PCBLENGTHTUNINGTOOL_H
