#include "drc_types.h"

DRCViolation::DRCViolation(ViolationType type, Severity severity, const QString& message,
                            QPointF location, const QString& item1, const QString& item2)
    : m_type(type)
    , m_severity(severity)
    , m_message(message)
    , m_location(location)
    , m_item1(item1)
    , m_item2(item2)
{
}

QString DRCViolation::typeString() const {
    switch (m_type) {
        case ClearanceViolation: return "Clearance";
        case ShortCircuit: return "Short Circuit";
        case UnconnectedNet: return "Unconnected";
        case MinTraceWidth: return "Trace Width";
        case MinDrillSize: return "Drill Size";
        case SilkTextOnPad: return "Silkscreen Over Pad";
        case MissingFootprint: return "Missing Footprint";
        case OverlappingItems: return "Overlap";
        case AcuteAngle: return "Acute Angle";
        case StubTrace: return "Stub Trace";
        case ThermalRelief: return "Thermal Relief";
        case BoardEdgeClearance: return "Edge Clearance";
        case ViaInPad: return "Via In Pad";
        case FloatingCopper: return "Floating Copper";
        case LengthToleranceViolation: return "Length Tolerance";
        default: return "Unknown";
    }
}

QString DRCViolation::severityString() const {
    switch (m_severity) {
        case Error: return "Error";
        case Warning: return "Warning";
        case Info: return "Info";
        default: return "Unknown";
    }
}

QColor DRCViolation::severityColor() const {
    switch (m_severity) {
        case Error: return QColor(239, 68, 68);    // Red
        case Warning: return QColor(245, 158, 11); // Orange
        case Info: return QColor(59, 130, 246);    // Blue
        default: return QColor(156, 163, 175);     // Gray
    }
}

DRCRules::DRCRules() {
    loadDefaults();
}

void DRCRules::loadDefaults() {
    m_minClearance = 0.15;
    m_copperToEdge = 0.25;
    m_minTraceWidth = 0.15;
    m_maxTraceWidth = 10.0;
    m_minViaDiameter = 0.45;
    m_minViaDrill = 0.2;
    m_minDrillSize = 0.2;
    m_minAnnularRing = 0.1;
    m_minSolderMaskBridge = 0.1;
    m_silkToPad = 0.15;
}
