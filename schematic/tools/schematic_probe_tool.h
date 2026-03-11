#ifndef SCHEMATICPROBETOOL_H
#define SCHEMATICPROBETOOL_H

#include "schematic_tool.h"

class SchematicProbeTool : public SchematicTool {
    Q_OBJECT

public:
    enum class ProbeKind {
        Voltage,
        Current,
        Power
    };

    explicit SchematicProbeTool(const QString& toolName = "Probe", ProbeKind kind = ProbeKind::Voltage);
    ~SchematicProbeTool() = default;

    static QCursor createProbeCursor(ProbeKind kind);

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
    QCursor cursor() const override;
    QString tooltip() const override;
    QString iconName() const override {
        if (m_kind == ProbeKind::Current) return "ammeter.svg";
        if (m_kind == ProbeKind::Power) return "power_meter.svg";
        return "probe.svg";
    }

signals:
    void signalProbed(const QString& signalName);
    void signalUnprobed(const QString& signalName);
    void signalDifferentialProbed(const QString& pNet, const QString& nNet);

private:
    ProbeKind m_kind = ProbeKind::Voltage;
    bool m_isDragging = false;
    QString m_startNetName;
};

#endif // SCHEMATICPROBETOOL_H
