#ifndef PCBVIATOOL_H
#define PCBVIATOOL_H

#include "pcb_tool.h"

/**
 * PCB Via Tool - Places vias on the PCB
 * 
 * Vias connect traces between layers.
 */
class PCBViaTool : public PCBTool {
    Q_OBJECT
public:
    explicit PCBViaTool(QObject* parent = nullptr);

    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

    // Via properties
    double viaDiameter() const { return m_viaDiameter; }
    void setViaDiameter(double diameter) { m_viaDiameter = diameter; }
    
    double holeDiameter() const { return m_holeDiameter; }
    void setHoleDiameter(double diameter) { m_holeDiameter = diameter; }

    int startLayer() const { return m_startLayer; }
    void setStartLayer(int layer) { m_startLayer = layer; }

    int endLayer() const { return m_endLayer; }
    void setEndLayer(int layer) { m_endLayer = layer; }

    bool microviaMode() const { return m_microviaMode; }
    void setMicroviaMode(bool enable) { m_microviaMode = enable; }

private:
    double m_viaDiameter;  // Outer copper diameter
    double m_holeDiameter; // Drill hole diameter
    int m_startLayer;
    int m_endLayer;
    bool m_microviaMode;
};

#endif // PCBVIATOOL_H
