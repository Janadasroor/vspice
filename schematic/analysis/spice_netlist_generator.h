#ifndef SPICE_NETLIST_GENERATOR_H
#define SPICE_NETLIST_GENERATOR_H

#include <QString>
#include <QList>

class QGraphicsScene;
class NetManager;

class SpiceNetlistGenerator {
public:
    enum SimulationType {
        Transient,
        DC,
        AC,
        OP
    };

    struct SimulationParams {
        SimulationType type;
        QString start;
        QString stop;
        QString step;
        QString dcSource;
        QString dcStart;
        QString dcStop;
        QString dcStep;
    };

    static QString generate(QGraphicsScene* scene, const QString& projectDir, NetManager* netManager, const SimulationParams& params);
    static QString buildCommand(const SimulationParams& params);

private:
    static QString formatComponent(const class SchematicComponentItem* item, const QMap<QString, QString>& netMap);
    static QString formatValue(double value);
};

#endif // SPICE_NETLIST_GENERATOR_H
