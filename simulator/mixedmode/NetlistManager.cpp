#include "NetlistManager.h"
#include "LogicComponent.h"

#include <QRegularExpression>

namespace {
QString nodeTypeName(NodeType type) {
    return type == NodeType::DIGITAL_EVENT ? "DIGITAL_EVENT" : "ANALOG";
}
}

NetlistManager::NetlistManager() = default;

void NetlistManager::clear() {
    m_title.clear();
    m_components.clear();
    m_netTypeHints.clear();
}

void NetlistManager::setTitle(const QString& title) {
    m_title = title.trimmed();
}

void NetlistManager::setBridgeModels(const BridgeModels& models) {
    m_bridgeModels = models;
}

void NetlistManager::setNetTypeHint(const QString& netName, NodeType type) {
    const QString canonical = canonicalNetName(netName);
    if (!canonical.isEmpty()) {
        m_netTypeHints.insert(canonical, type);
    }
}

void NetlistManager::addComponent(const ComponentEntry& entry) {
    m_components.push_back(entry);
}

void NetlistManager::addLogicComponent(const LogicComponent& component) {
    addComponent(component.toNetlistEntry());
}

QString NetlistManager::GeneratedNetlist::toString() const {
    QString out;
    out += title.isEmpty() ? "* Mixed-mode netlist" : title;
    out += "\n";

    if (!diagnostics.isEmpty()) {
        out += "* Diagnostics\n";
        for (const QString& diagnostic : diagnostics) {
            out += "* " + diagnostic + "\n";
        }
        out += "\n";
    }

    if (!modelLines.isEmpty()) {
        out += "* Bridge and logic models\n";
        for (const QString& line : modelLines) {
            out += line + "\n";
        }
        out += "\n";
    }

    if (!hiddenSubcircuits.isEmpty()) {
        out += "* Hidden XSPICE wrapper subcircuits\n";
        for (const QString& line : hiddenSubcircuits) {
            out += line + "\n";
        }
        out += "\n";
    }

    out += "* Elements\n";
    for (const QString& line : elementLines) {
        out += line + "\n";
    }
    out += ".end\n";
    return out;
}

QString NetlistManager::canonicalNetName(const QString& netName) {
    return netName.trimmed().replace(' ', '_');
}

QString NetlistManager::sanitizeToken(const QString& text) {
    QString sanitized = text.trimmed();
    sanitized.replace(QRegularExpression("[^A-Za-z0-9_]+"), "_");
    sanitized.remove(QRegularExpression("^_+"));
    sanitized.remove(QRegularExpression("_+$"));
    return sanitized.isEmpty() ? QStringLiteral("N") : sanitized;
}

QString NetlistManager::formatReal(double value) {
    return QString::number(value, 'g', 12);
}

QString NetlistManager::replaceToken(const QString& line, const QString& oldToken, const QString& newToken) {
    if (oldToken == newToken || oldToken.isEmpty()) {
        return line;
    }

    const QString escaped = QRegularExpression::escape(oldToken);
    QRegularExpression re(QStringLiteral("(?<![A-Za-z0-9_\\[\\]])%1(?![A-Za-z0-9_\\[\\]])").arg(escaped));
    QString out = line;
    out.replace(re, newToken);
    return out;
}

QHash<QString, NetlistManager::NetRoleSummary> NetlistManager::summarizeNetRoles() const {
    QHash<QString, NetRoleSummary> summary;

    for (const ComponentEntry& component : m_components) {
        for (const PinRef& pin : component.pins) {
            const QString net = canonicalNetName(pin.netName);
            if (net.isEmpty()) {
                continue;
            }

            NetRoleSummary& role = summary[net];
            const bool isDigital = pin.nodeType == NodeType::DIGITAL_EVENT;
            switch (pin.direction) {
            case PinDirection::INPUT:
                if (isDigital) role.hasDigitalInput = true;
                else role.hasAnalogInput = true;
                break;
            case PinDirection::OUTPUT:
                if (isDigital) role.hasDigitalOutput = true;
                else role.hasAnalogOutput = true;
                break;
            case PinDirection::BIDIRECTIONAL:
                if (isDigital) role.hasDigitalBidirectional = true;
                else role.hasAnalogBidirectional = true;
                break;
            }
        }
    }

    return summary;
}

NodeType NetlistManager::resolveCanonicalNetType(const QString& netName,
                                                 const QHash<QString, NetRoleSummary>& summaries) const {
    const QString canonical = canonicalNetName(netName);
    const auto hintIt = m_netTypeHints.constFind(canonical);
    if (hintIt != m_netTypeHints.constEnd()) {
        return hintIt.value();
    }

    const NetRoleSummary role = summaries.value(canonical);

    if (role.hasDigitalOutput && !role.hasAnalogOutput) {
        return NodeType::DIGITAL_EVENT;
    }
    if (role.hasAnalogOutput && !role.hasDigitalOutput) {
        return NodeType::ANALOG;
    }
    if (role.hasDigitalBidirectional && !role.hasAnalogBidirectional) {
        return NodeType::DIGITAL_EVENT;
    }
    if (role.hasAnalogBidirectional && !role.hasDigitalBidirectional) {
        return NodeType::ANALOG;
    }
    if ((role.hasDigitalOutput || role.hasDigitalBidirectional) &&
        !(role.hasAnalogOutput || role.hasAnalogBidirectional)) {
        return NodeType::DIGITAL_EVENT;
    }

    return NodeType::ANALOG;
}

QStringList NetlistManager::defaultBridgeModelLines() const {
    QStringList lines;
    lines << QString(".model __viospice_adc_bridge adc_bridge(in_low=%1 in_high=%2 rise_delay=%3 fall_delay=%4)")
                 .arg(formatReal(m_bridgeModels.adcLow),
                      formatReal(m_bridgeModels.adcHigh),
                      formatReal(m_bridgeModels.adcRiseDelay),
                      formatReal(m_bridgeModels.adcFallDelay));
    lines << QString(".model __viospice_dac_bridge dac_bridge(out_low=%1 out_high=%2 out_undef=%3 input_load=%4 t_rise=%5 t_fall=%6)")
                 .arg(formatReal(m_bridgeModels.dacLow),
                      formatReal(m_bridgeModels.dacHigh),
                      formatReal(m_bridgeModels.dacUndef),
                      formatReal(m_bridgeModels.dacInputLoad),
                      formatReal(m_bridgeModels.dacRiseTime),
                      formatReal(m_bridgeModels.dacFallTime));
    return lines;
}

QStringList NetlistManager::hiddenBridgeSubcircuits() const {
    QStringList lines;

    lines << ".subckt __viospice_adc_wrap ANA DIG";
    lines << "* XSPICE adc_bridge: analog node -> event-driven digital node.";
    lines << "A_ADC [ANA] [DIG] __viospice_adc_bridge";
    lines << ".ends __viospice_adc_wrap";
    lines << "";
    lines << ".subckt __viospice_dac_wrap DIG ANA";
    lines << "* XSPICE dac_bridge: event-driven digital node -> analog node.";
    lines << "A_DAC [DIG] [ANA] __viospice_dac_bridge";
    lines << ".ends __viospice_dac_wrap";

    return lines;
}

NetlistManager::BridgeDecision NetlistManager::maybeInsertBridge(const PinRef& pin,
                                                                 NodeType canonicalType,
                                                                 const QString& baseNetName,
                                                                 int bridgeIndex) const {
    BridgeDecision decision;
    decision.pinNetName = baseNetName;

    if (pin.nodeType == canonicalType) {
        return decision;
    }

    const QString localNet = QString("__mm_%1_%2_%3")
                                 .arg(sanitizeToken(pin.componentId),
                                      sanitizeToken(pin.pinName),
                                      QString::number(bridgeIndex));

    if (pin.nodeType == NodeType::DIGITAL_EVENT && canonicalType == NodeType::ANALOG) {
        // The user net stays analog. This pin sees a private digital event net generated by adc_bridge.
        decision.pinNetName = localNet;
        decision.elementLines << QString("XBRADC_%1 %2 %3 __viospice_adc_wrap")
                                     .arg(bridgeIndex)
                                     .arg(baseNetName, localNet);
        decision.diagnostics << QString("Inserted adc_bridge for %1.%2 because net '%3' resolved to %4 while the pin requires %5.")
                                        .arg(pin.componentId, pin.pinName, baseNetName,
                                             nodeTypeName(canonicalType), nodeTypeName(pin.nodeType));
        return decision;
    }

    if (pin.nodeType == NodeType::ANALOG && canonicalType == NodeType::DIGITAL_EVENT) {
        // The user net stays digital. This analog pin is isolated behind a dac_bridge-generated analog net.
        decision.pinNetName = localNet;
        decision.elementLines << QString("XBRDAC_%1 %2 %3 __viospice_dac_wrap")
                                     .arg(bridgeIndex)
                                     .arg(baseNetName, localNet);
        decision.diagnostics << QString("Inserted dac_bridge for %1.%2 because net '%3' resolved to %4 while the pin requires %5.")
                                        .arg(pin.componentId, pin.pinName, baseNetName,
                                             nodeTypeName(canonicalType), nodeTypeName(pin.nodeType));
        return decision;
    }

    decision.diagnostics << QString("Unsupported bridge decision on %1.%2 between %3 and %4.")
                                    .arg(pin.componentId, pin.pinName,
                                         nodeTypeName(canonicalType), nodeTypeName(pin.nodeType));
    return decision;
}

NetlistManager::GeneratedNetlist NetlistManager::generateNetlist() const {
    GeneratedNetlist result;
    result.title = m_title.isEmpty() ? "* VioSpice mixed-mode XSPICE netlist" : m_title;

    const QHash<QString, NetRoleSummary> summaries = summarizeNetRoles();
    result.modelLines = defaultBridgeModelLines();
    result.hiddenSubcircuits = hiddenBridgeSubcircuits();

    QSet<QString> seenModelLines;
    for (const QString& line : result.modelLines) {
        seenModelLines.insert(line);
    }

    int bridgeIndex = 0;

    for (const ComponentEntry& component : m_components) {
        QString instanceLine = component.instanceLine;

        for (const QString& modelLine : component.modelLines) {
            if (!seenModelLines.contains(modelLine)) {
                result.modelLines << modelLine;
                seenModelLines.insert(modelLine);
            }
        }

        for (const QString& subcktLine : component.subcircuitLines) {
            result.hiddenSubcircuits << subcktLine;
        }

        for (const PinRef& pin : component.pins) {
            const QString baseNet = canonicalNetName(pin.netName);
            if (baseNet.isEmpty()) {
                continue;
            }

            const NodeType canonicalType = resolveCanonicalNetType(baseNet, summaries);
            const BridgeDecision bridge = maybeInsertBridge(pin, canonicalType, baseNet, ++bridgeIndex);

            instanceLine = replaceToken(instanceLine, baseNet, bridge.pinNetName);
            result.elementLines.append(bridge.elementLines);
            result.diagnostics.append(bridge.diagnostics);
        }

        result.elementLines << instanceLine;
    }

    return result;
}
