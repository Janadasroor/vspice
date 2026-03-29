#ifndef VIOSPICE_MIXEDMODE_LOGICCOMPONENT_H
#define VIOSPICE_MIXEDMODE_LOGICCOMPONENT_H

#include "NetlistManager.h"

#include <QString>
#include <QStringList>
#include <QVector>

class Component {
public:
    explicit Component(QString instanceName = {});
    virtual ~Component() = default;

    QString instanceName() const;
    void setInstanceName(const QString& instanceName);

private:
    QString m_instanceName;
};

class LogicComponent : public Component {
public:
    struct TimingModel {
        double riseDelay = 1e-9;
        double fallDelay = 1e-9;
        double inputLoad = 1e-12;
        bool inertialDelay = true;
        QString family;
    };

    struct BridgeThresholds {
        double adcLow = 0.8;
        double adcHigh = 2.0;
        double dacLow = 0.0;
        double dacHigh = 5.0;
        double dacUndef = 2.5;
    };

    struct Port {
        QString name;
        QString netName;
        NetlistManager::PinDirection direction = NetlistManager::PinDirection::INPUT;
        NodeType nodeType = NodeType::DIGITAL_EVENT;
        bool optional = false;
    };

    LogicComponent(QString instanceName, QString codeModelName);

    void addInput(const QString& name, const QString& netName, NodeType type = NodeType::DIGITAL_EVENT);
    void addOutput(const QString& name, const QString& netName, NodeType type = NodeType::DIGITAL_EVENT);
    void addPort(const Port& port);

    void setTimingModel(const TimingModel& timing);
    void setBridgeThresholds(const BridgeThresholds& thresholds);
    void setInlineModelName(const QString& modelName);
    void setSubcircuitName(const QString& subcircuitName);

    QString generateSubcircuit() const;
    NetlistManager::ComponentEntry toNetlistEntry() const;

private:
    QString m_codeModelName;
    QString m_inlineModelName;
    QString m_subcircuitName;
    TimingModel m_timing;
    BridgeThresholds m_thresholds;
    QVector<Port> m_ports;

    QString effectiveSubcircuitName() const;
    QString effectiveInlineModelName() const;
    QString gateInstanceName() const;
    QString vectorizedInputSyntax() const;
    QString scalarPortsSyntax() const;
    QString modelLine() const;
    QString adcBridgeModelLine() const;
    QString dacBridgeModelLine() const;
};

#endif
