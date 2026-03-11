#include "../core/sim_engine.h"

#include <iostream>
#include <stdexcept>

namespace {

double runNotGateOp(double vinValue, double vthLow, double vthHigh) {
    SimNetlist netlist;
    const int nIn = netlist.addNode("IN");
    const int nOut = netlist.addNode("OUT");

    SimComponentInstance vin;
    vin.name = "VIN";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {nIn, 0};
    vin.params["voltage"] = vinValue;
    netlist.addComponent(vin);

    SimComponentInstance not1;
    not1.name = "NOT1";
    not1.type = SimComponentType::LOGIC_NOT;
    not1.nodes = {nIn, nOut};
    netlist.addComponent(not1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    cfg.digitalThresholdLow = vthLow;
    cfg.digitalThresholdHigh = vthHigh;
    cfg.digitalOutputLow = 0.0;
    cfg.digitalOutputHigh = 5.0;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    SimResults results = engine.run(netlist);
    return results.nodeVoltages["OUT"];
}

size_t runNotGateTransientPointCount(bool enableEventRefinement) {
    SimNetlist netlist;
    const int nIn = netlist.addNode("IN");
    const int nOut = netlist.addNode("OUT");

    SimComponentInstance vin;
    vin.name = "VIN";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {nIn, 0};
    vin.params["wave_type"] = 1.0; // SIN
    vin.params["v_offset"] = 2.5;
    vin.params["v_ampl"] = 2.5;
    vin.params["v_freq"] = 1000.0;
    vin.params["v_delay"] = 0.0;
    vin.params["v_phase"] = 0.0;
    netlist.addComponent(vin);

    SimComponentInstance not1;
    not1.name = "NOT1";
    not1.type = SimComponentType::LOGIC_NOT;
    not1.nodes = {nIn, nOut};
    netlist.addComponent(not1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Transient;
    cfg.tStart = 0.0;
    cfg.tStop = 3e-3;
    cfg.tStep = 5e-4;
    cfg.useAdaptiveStep = false;
    cfg.digitalThresholdLow = 2.0;
    cfg.digitalThresholdHigh = 3.0;
    cfg.mixedSignalEnableEventRefinement = enableEventRefinement;
    cfg.mixedSignalEventStep = 1e-5;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    SimResults results = engine.run(netlist);
    for (const auto& w : results.waveforms) {
        if (w.name == "V(OUT)") return w.yData.size();
    }
    return 0;
}

} // namespace

int main() {
    auto require = [](bool cond, const char* msg) {
        if (!cond) throw std::runtime_error(msg);
    };

    const double vLowIn = runNotGateOp(0.5, 1.0, 2.0);
    const double vHighIn = runNotGateOp(3.0, 1.0, 2.0);
    std::cout << "Mixed-signal thresholds NOT output @0.5V in: " << vLowIn << "V (Expected high)\n";
    std::cout << "Mixed-signal thresholds NOT output @3.0V in: " << vHighIn << "V (Expected low)\n";
    require(vLowIn > 4.5, "logic threshold low-input case failed");
    require(vHighIn < 0.5, "logic threshold high-input case failed");

    const size_t pointsNoRefine = runNotGateTransientPointCount(false);
    const size_t pointsRefine = runNotGateTransientPointCount(true);
    std::cout << "Mixed-signal transient points no-refine/refine: "
              << pointsNoRefine << "/" << pointsRefine << std::endl;
    require(pointsRefine > pointsNoRefine, "event refinement did not increase transient resolution");

    std::cout << "simulator.mixed_signal: all tests passed\n";
    return 0;
}
