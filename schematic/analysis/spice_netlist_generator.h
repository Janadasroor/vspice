#ifndef SPICE_NETLIST_GENERATOR_H
#define SPICE_NETLIST_GENERATOR_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

namespace Flux {
namespace Model {
class SymbolDefinition;
}
}

class QGraphicsScene;
class NetManager;

class SpiceNetlistGenerator {
public:
    enum SimulationType {
        Transient,
        DC,
        AC,
        OP,
        Noise,
        Fourier,
        TF,
        Disto,
        Meas,
        Step,
        Sens,
        FFT,
        SParameter
    };

    struct SimulationParams {
        SimulationType type;
        QString start;
        QString stop;
        QString step;
        bool transientSteady = false;
        QString steadyStateTol;
        QString steadyStateDelay;
        QString dcSource;
        QString dcStart;
        QString dcStop;
        QString dcStep;

        // Noise analysis: .noise <output> <src> <pts> <fstart> <fstop>
        QString noiseOutput;   // e.g. "V(out)"
        QString noiseSource;   // e.g. "V1"

        // Fourier analysis: .four <freq> <output1> [<output2> ...]
        QString fourFreq;
        QStringList fourOutputs;

        // Transfer function: .tf <output> <src>  or  .tf <i(output)> <src>
        QString tfOutput;
        QString tfSource;

        // Distortion: .disto <pts> <fstart> <fstop> [<f2overf1>]
        QString distoF2OverF1;

        // Measurement: .meas <type> <name> <trig> <val> < targ> <val>
        QString measRaw; // raw text for .meas

        // Parametric step: .step <param> <start> <stop> <incr>  or  .step list <val1> ...
        QString stepRaw; // raw text for .step

        // Sensitivity: .sens <output>
        QString sensOutput;

        // RF / S-Parameter Analysis
        QString rfPort1Source; // e.g. "V1" (Standard Spice source name)
        QString rfPort2Node;   // e.g. "Net1" or "N001"
        QString rfZ0;          // Reference impedance as string
    };

    static QString generate(QGraphicsScene* scene, const QString& projectDir, NetManager* netManager, const SimulationParams& params);
    static QString buildCommand(const SimulationParams& params);
    static QString normalizeXspiceGateModelAlias(const QString& rawToken, const QString& typeName = QString());
static QStringList buildXspiceNodeTokensForPins(const QMap<QString, QString>& pins,
                                                     const Flux::Model::SymbolDefinition* symbol = nullptr,
                                                     bool collapseScalarInputsToVector = false);
    static QString generateCompatibilityLayer(const QString& rawNetlist);

private:
    static QString formatComponent(const class SchematicComponentItem* item, const QMap<QString, QString>& netMap);
    static QString formatValue(double value);
};

#endif // SPICE_NETLIST_GENERATOR_H
