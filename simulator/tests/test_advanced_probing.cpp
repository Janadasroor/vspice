#include "../core/sim_engine.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {
const SimWaveform* findWave(const SimResults& results, const std::string& name) {
    for (const auto& wave : results.waveforms) {
        if (wave.name == name) return &wave;
    }
    return nullptr;
}
}

void testEquationChannelsOnOp() {
    SimNetlist netlist;
    const int nIn = netlist.addNode("IN");
    const int nOut = netlist.addNode("OUT");

    SimComponentInstance vin;
    vin.name = "VIN";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {nIn, 0};
    vin.params["voltage"] = 10.0;
    netlist.addComponent(vin);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {nIn, nOut};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {nOut, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    netlist.addAutoProbe("V(OUT)-V(IN)");
    netlist.addAutoProbe("V(OUT)/V(IN)");

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    const SimWaveform* diff = findWave(results, "V(OUT)-V(IN)");
    assert(diff && !diff->yData.empty());
    assert(std::abs(diff->yData.front() + 5.0) < 1e-6);

    const SimWaveform* ratio = findWave(results, "V(OUT)/V(IN)");
    assert(ratio && !ratio->yData.empty());
    assert(std::abs(ratio->yData.front() - 0.5) < 1e-6);
}

void testDerivativeChannelOnTransient() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");

    SimComponentInstance i1;
    i1.name = "I1";
    i1.type = SimComponentType::CurrentSource;
    i1.nodes = {n1, 0};
    i1.params["current"] = 1.0;
    netlist.addComponent(i1);

    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n1, 0};
    c1.params["capacitance"] = 1.0;
    c1.params["ic"] = 0.0;
    netlist.addComponent(c1);

    netlist.addAutoProbe("D(V(N1))");

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Transient;
    cfg.tStart = 0.0;
    cfg.tStop = 0.5;
    cfg.tStep = 0.01;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    const SimWaveform* deriv = findWave(results, "D(V(N1))");
    assert(deriv && deriv->yData.size() >= 8);

    double meanAbs = 0.0;
    int count = 0;
    for (size_t i = 1; i + 1 < deriv->yData.size(); ++i) {
        meanAbs += std::abs(deriv->yData[i]);
        ++count;
    }
    meanAbs = (count > 0) ? (meanAbs / static_cast<double>(count)) : 0.0;
    std::cout << "Derivative mean |dV/dt| = " << meanAbs << std::endl;
    assert(meanAbs > 0.7 && meanAbs < 1.3);
}

int main() {
    testEquationChannelsOnOp();
    testDerivativeChannelOnTransient();
    std::cout << "simulator.advanced_probing: all tests passed\n";
    return 0;
}
