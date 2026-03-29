#ifndef VIOSPICE_MIXEDMODE_NETLISTMANAGER_H
#define VIOSPICE_MIXEDMODE_NETLISTMANAGER_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class LogicComponent;

enum class NodeType {
    ANALOG,
    DIGITAL_EVENT
};

class NetlistManager {
public:
    enum class PinDirection {
        INPUT,
        OUTPUT,
        BIDIRECTIONAL
    };

    struct PinRef {
        QString componentId;
        QString pinName;
        QString netName;
        NodeType nodeType = NodeType::ANALOG;
        PinDirection direction = PinDirection::INPUT;
    };

    struct ComponentEntry {
        QString componentId;
        QString instanceLine;
        QStringList modelLines;
        QStringList subcircuitLines;
        QVector<PinRef> pins;
    };

    struct BridgeModels {
        double adcLow = 0.8;
        double adcHigh = 2.0;
        double adcRiseDelay = 1e-9;
        double adcFallDelay = 1e-9;

        double dacLow = 0.0;
        double dacHigh = 5.0;
        double dacUndef = 2.5;
        double dacRiseTime = 1e-9;
        double dacFallTime = 1e-9;
        double dacInputLoad = 1e-12;
    };

    struct GeneratedNetlist {
        QString title;
        QStringList modelLines;
        QStringList hiddenSubcircuits;
        QStringList elementLines;
        QStringList diagnostics;

        QString toString() const;
    };

    NetlistManager();

    void clear();
    void setTitle(const QString& title);
    void setBridgeModels(const BridgeModels& models);
    void setNetTypeHint(const QString& netName, NodeType type);

    void addComponent(const ComponentEntry& entry);
    void addLogicComponent(const LogicComponent& component);

    GeneratedNetlist generateNetlist() const;

private:
    struct NetRoleSummary {
        bool hasAnalogOutput = false;
        bool hasDigitalOutput = false;
        bool hasAnalogInput = false;
        bool hasDigitalInput = false;
        bool hasAnalogBidirectional = false;
        bool hasDigitalBidirectional = false;
    };

    struct BridgeDecision {
        QString pinNetName;
        QStringList elementLines;
        QStringList diagnostics;
    };

    QString m_title;
    BridgeModels m_bridgeModels;
    QVector<ComponentEntry> m_components;
    QHash<QString, NodeType> m_netTypeHints;

    static QString canonicalNetName(const QString& netName);
    static QString sanitizeToken(const QString& text);
    static QString formatReal(double value);
    static QString replaceToken(const QString& line, const QString& oldToken, const QString& newToken);

    QHash<QString, NetRoleSummary> summarizeNetRoles() const;
    NodeType resolveCanonicalNetType(const QString& netName, const QHash<QString, NetRoleSummary>& summaries) const;
    QStringList defaultBridgeModelLines() const;
    QStringList hiddenBridgeSubcircuits() const;

    BridgeDecision maybeInsertBridge(const PinRef& pin,
                                     NodeType canonicalType,
                                     const QString& baseNetName,
                                     int bridgeIndex) const;
};

#endif
