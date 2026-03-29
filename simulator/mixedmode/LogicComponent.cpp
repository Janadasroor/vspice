#include "LogicComponent.h"

#include <QRegularExpression>

namespace {
QString sanitizeIdentifier(const QString& text) {
    QString out = text.trimmed();
    out.replace(QRegularExpression("[^A-Za-z0-9_]+"), "_");
    if (out.isEmpty()) {
        out = "logic";
    }
    return out;
}

QString formatReal(double value) {
    return QString::number(value, 'g', 12);
}
}

Component::Component(QString instanceName)
    : m_instanceName(std::move(instanceName)) {
}

QString Component::instanceName() const {
    return m_instanceName;
}

void Component::setInstanceName(const QString& instanceName) {
    m_instanceName = instanceName;
}

LogicComponent::LogicComponent(QString instanceName, QString codeModelName)
    : Component(std::move(instanceName))
    , m_codeModelName(std::move(codeModelName)) {
}

void LogicComponent::addInput(const QString& name, const QString& netName, NodeType type) {
    addPort(Port{name, netName, NetlistManager::PinDirection::INPUT, type, false});
}

void LogicComponent::addOutput(const QString& name, const QString& netName, NodeType type) {
    addPort(Port{name, netName, NetlistManager::PinDirection::OUTPUT, type, false});
}

void LogicComponent::addPort(const Port& port) {
    m_ports.push_back(port);
}

void LogicComponent::setTimingModel(const TimingModel& timing) {
    m_timing = timing;
}

void LogicComponent::setBridgeThresholds(const BridgeThresholds& thresholds) {
    m_thresholds = thresholds;
}

void LogicComponent::setInlineModelName(const QString& modelName) {
    m_inlineModelName = modelName.trimmed();
}

void LogicComponent::setSubcircuitName(const QString& subcircuitName) {
    m_subcircuitName = subcircuitName.trimmed();
}

QString LogicComponent::effectiveSubcircuitName() const {
    if (!m_subcircuitName.isEmpty()) {
        return m_subcircuitName;
    }
    return QString("__logic_%1").arg(sanitizeIdentifier(instanceName()));
}

QString LogicComponent::effectiveInlineModelName() const {
    if (!m_inlineModelName.isEmpty()) {
        return m_inlineModelName;
    }
    return QString("__mdl_%1").arg(sanitizeIdentifier(instanceName()));
}

QString LogicComponent::gateInstanceName() const {
    return QString("A_%1").arg(sanitizeIdentifier(instanceName()));
}

QString LogicComponent::vectorizedInputSyntax() const {
    QStringList inputs;
    for (const Port& port : m_ports) {
        if (port.direction == NetlistManager::PinDirection::INPUT) {
            inputs << port.name;
        }
    }
    return inputs.isEmpty() ? QString() : "[" + inputs.join(' ') + "]";
}

QString LogicComponent::scalarPortsSyntax() const {
    QStringList outputs;
    for (const Port& port : m_ports) {
        if (port.direction != NetlistManager::PinDirection::INPUT) {
            outputs << port.name;
        }
    }
    return outputs.join(' ');
}

QString LogicComponent::modelLine() const {
    QStringList params;
    params << QString("rise_delay=%1").arg(formatReal(m_timing.riseDelay));
    params << QString("fall_delay=%1").arg(formatReal(m_timing.fallDelay));
    params << QString("input_load=%1").arg(formatReal(m_timing.inputLoad));
    params << QString("inertial_delay=%1").arg(m_timing.inertialDelay ? "1" : "0");
    if (!m_timing.family.trimmed().isEmpty()) {
        params << QString("family=\"%1\"").arg(m_timing.family.trimmed());
    }

    return QString(".model %1 %2(%3)")
        .arg(effectiveInlineModelName(), m_codeModelName, params.join(' '));
}

QString LogicComponent::adcBridgeModelLine() const {
    return QString(".model %1_adc adc_bridge(in_low=%2 in_high=%3)")
        .arg(sanitizeIdentifier(instanceName()),
             formatReal(m_thresholds.adcLow),
             formatReal(m_thresholds.adcHigh));
}

QString LogicComponent::dacBridgeModelLine() const {
    return QString(".model %1_dac dac_bridge(out_low=%2 out_high=%3 out_undef=%4)")
        .arg(sanitizeIdentifier(instanceName()),
             formatReal(m_thresholds.dacLow),
             formatReal(m_thresholds.dacHigh),
             formatReal(m_thresholds.dacUndef));
}

QString LogicComponent::generateSubcircuit() const {
    QStringList lines;
    QStringList formalPins;

    for (const Port& port : m_ports) {
        formalPins << port.name;
    }

    lines << QString(".subckt %1 %2").arg(effectiveSubcircuitName(), formalPins.join(' '));
    lines << "* Hidden wrapper around an XSPICE A-device.";
    lines << "* Inputs are grouped with XSPICE vector notation, e.g. [in1 in2 in3].";
    lines << modelLine();
    lines << adcBridgeModelLine();
    lines << dacBridgeModelLine();

    QString gateLine = gateInstanceName();
    const QString inputVector = vectorizedInputSyntax();
    const QString scalarPorts = scalarPortsSyntax();
    if (!inputVector.isEmpty()) {
        gateLine += " " + inputVector;
    }
    if (!scalarPorts.isEmpty()) {
        gateLine += " " + scalarPorts;
    }
    gateLine += " " + effectiveInlineModelName();
    lines << gateLine;
    lines << QString(".ends %1").arg(effectiveSubcircuitName());

    return lines.join('\n');
}

NetlistManager::ComponentEntry LogicComponent::toNetlistEntry() const {
    NetlistManager::ComponentEntry entry;
    entry.componentId = instanceName();
    entry.subcircuitLines << generateSubcircuit();

    QStringList actualNets;
    for (const Port& port : m_ports) {
        actualNets << port.netName.trimmed().replace(' ', '_');

        NetlistManager::PinRef pin;
        pin.componentId = instanceName();
        pin.pinName = port.name;
        pin.netName = port.netName;
        pin.nodeType = port.nodeType;
        pin.direction = port.direction;
        entry.pins << pin;
    }

    entry.instanceLine = QString("X%1 %2 %3")
                             .arg(sanitizeIdentifier(instanceName()),
                                  actualNets.join(' '),
                                  effectiveSubcircuitName());
    return entry;
}
