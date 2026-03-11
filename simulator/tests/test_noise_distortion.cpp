#include "../core/sim_engine.h"

#include <cassert>
#include <cmath>
#include <iostream>

void testNoiseAnalysis() {
    SimNetlist netlist;
    const int nOut = netlist.addNode("OUT");

    SimComponentInstance v0;
    v0.name = "VIN";
    v0.type = SimComponentType::VoltageSource;
    v0.nodes = {nOut, 0};
    v0.params["voltage"] = 0.0;
    v0.params["ac_mag"] = 1.0;
    netlist.addComponent(v0);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {nOut, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Noise;
    cfg.noiseOutputSignal = "V(OUT)";
    cfg.noiseInputSource = "VIN";
    cfg.noiseTemperatureK = 300.0;
    cfg.fStart = 10.0;
    cfg.fStop = 1e6;
    cfg.fPoints = 16;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    assert(!results.waveforms.empty());
    const auto it = results.measurements.find("onoise_rms_v");
    assert(it != results.measurements.end());
    std::cout << "Noise onoise_rms_v=" << it->second << std::endl;
    assert(it->second > 0.0);
}

void testDistortionAnalysis() {
    SimNetlist netlist;
    const int nOut = netlist.addNode("OUT");

    SimComponentInstance vin;
    vin.name = "VIN";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {nOut, 0};
    vin.params["wave_type"] = 1.0; // SIN
    vin.params["v_offset"] = 0.0;
    vin.params["v_ampl"] = 1.0;
    vin.params["v_freq"] = 1000.0;
    vin.params["v_delay"] = 0.0;
    vin.params["v_phase"] = 0.0;
    netlist.addComponent(vin);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {nOut, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Distortion;
    cfg.thdTargetSignal = "V(OUT)";
    cfg.thdFundamentalHz = 1000.0;
    cfg.thdHarmonics = 5;
    cfg.thdSkipCycles = 1;
    cfg.thdCycles = 5;
    cfg.tStep = 1e-5;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    const auto it = results.measurements.find("thd_percent");
    assert(it != results.measurements.end());
    std::cout << "Distortion thd_percent=" << it->second << std::endl;
    assert(it->second >= 0.0);
    assert(it->second < 1.0);
}

int main() {
    testNoiseAnalysis();
    testDistortionAnalysis();
    std::cout << "simulator.noise_distortion: all tests passed\n";
    return 0;
}
