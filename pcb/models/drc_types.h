#ifndef DRC_TYPES_H
#define DRC_TYPES_H

#include <QString>
#include <QPointF>
#include <QColor>

// Represents a single DRC violation
class DRCViolation {
public:
    enum Severity {
        Error,
        Warning,
        Info
    };

    enum ViolationType {
        ClearanceViolation,      // Items too close
        ShortCircuit,            // Different nets touching  
        UnconnectedNet,          // Net not fully connected
        MinTraceWidth,           // Trace too narrow
        MinDrillSize,            // Drill too small
        SilkTextOnPad,           // Silkscreen over exposed copper
        MissingFootprint,        // Component without footprint
        OverlappingItems,        // Items overlap incorrectly
        AcuteAngle,              // Trace angles too sharp
        StubTrace,               // Trace ending without connection
        ThermalRelief,           // Missing thermal relief
        BoardEdgeClearance,      // Items too close to board edge
        ViaInPad,                 // Via placed directly on a pad
        FloatingCopper           // Copper without a net
    };

    DRCViolation() = default;
    DRCViolation(ViolationType type, Severity severity, const QString& message,
                 QPointF location = QPointF(), const QString& item1 = "", const QString& item2 = "");

    ViolationType type() const { return m_type; }
    Severity severity() const { return m_severity; }
    QString message() const { return m_message; }
    QPointF location() const { return m_location; }
    QString item1() const { return m_item1; }
    QString item2() const { return m_item2; }

    QString typeString() const;
    QString severityString() const;
    QColor severityColor() const;

private:
    ViolationType m_type = ClearanceViolation;
    Severity m_severity = Error;
    QString m_message;
    QPointF m_location;
    QString m_item1;  // First involved item ID
    QString m_item2;  // Second involved item ID (if applicable)
};

// Design rule settings
class DRCRules {
public:
    DRCRules();

    // Clearance rules (in mm)
    double minClearance() const { return m_minClearance; }
    void setMinClearance(double mm) { m_minClearance = mm; }

    double copperToEdge() const { return m_copperToEdge; }
    void setCopperToEdge(double mm) { m_copperToEdge = mm; }

    // Trace rules
    double minTraceWidth() const { return m_minTraceWidth; }
    void setMinTraceWidth(double mm) { m_minTraceWidth = mm; }

    double maxTraceWidth() const { return m_maxTraceWidth; }
    void setMaxTraceWidth(double mm) { m_maxTraceWidth = mm; }

    // Via rules
    double minViaDiameter() const { return m_minViaDiameter; }
    void setMinViaDiameter(double mm) { m_minViaDiameter = mm; }

    double minViaDrill() const { return m_minViaDrill; }
    void setMinViaDrill(double mm) { m_minViaDrill = mm; }

    // Drill rules
    double minDrillSize() const { return m_minDrillSize; }
    void setMinDrillSize(double mm) { m_minDrillSize = mm; }

    double minAnnularRing() const { return m_minAnnularRing; }
    void setMinAnnularRing(double mm) { m_minAnnularRing = mm; }

    double minSolderMaskBridge() const { return m_minSolderMaskBridge; }
    void setMinSolderMaskBridge(double mm) { m_minSolderMaskBridge = mm; }

    // Silkscreen rules
    double silkToPad() const { return m_silkToPad; }
    void setSilkToPad(double mm) { m_silkToPad = mm; }

    // Load/save
    void loadDefaults();

private:
    double m_minClearance;
    double m_copperToEdge;
    double m_minTraceWidth;
    double m_maxTraceWidth;
    double m_minViaDiameter;
    double m_minViaDrill;
    double m_minDrillSize;
    double m_minAnnularRing;
    double m_minSolderMaskBridge;
    double m_silkToPad;
};

#endif // DRC_TYPES_H
