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

void testControlledSourcesAC() {
    {
        SimNetlist netlist;
        const int nIn = netlist.addNode("IN");
        const int nOut = netlist.addNode("OUT");

        SimComponentInstance vin;
        vin.name = "VIN";
        vin.type = SimComponentType::VoltageSource;
        vin.nodes = {nIn, 0};
        vin.params["voltage"] = 0.0;
        vin.params["ac_mag"] = 1.0;
        netlist.addComponent(vin);

        SimComponentInstance e1;
        e1.name = "E1";
        e1.type = SimComponentType::VCVS;
        e1.nodes = {nOut, 0, nIn, 0};
        e1.params["gain"] = 2.0;
        netlist.addComponent(e1);

        SimAnalysisConfig cfg;
        cfg.type = SimAnalysisType::AC;
        cfg.fStart = 1e3;
        cfg.fStop = 1e3;
        cfg.fPoints = 1;
        netlist.setAnalysis(cfg);

        SimEngine engine;
        const SimResults results = engine.run(netlist);
        const SimWaveform* w = findWave(results, "V(OUT)");
        assert(w && !w->yData.empty());
        std::cout << "VCVS AC |V(OUT)|=" << w->yData.front() << " (Expected 2)" << std::endl;
        assert(std::abs(w->yData.front() - 2.0) < 0.02);
    }

    {
        SimNetlist netlist;
        const int nIn = netlist.addNode("IN");
        const int nOut = netlist.addNode("OUT");

        SimComponentInstance vin;
        vin.name = "VIN";
        vin.type = SimComponentType::VoltageSource;
        vin.nodes = {nIn, 0};
        vin.params["voltage"] = 0.0;
        vin.params["ac_mag"] = 1.0;
        netlist.addComponent(vin);

        SimComponentInstance g1;
        g1.name = "G1";
        g1.type = SimComponentType::VCCS;
        g1.nodes = {nOut, 0, nIn, 0};
        g1.params["gain"] = 1e-3;
        netlist.addComponent(g1);

        SimComponentInstance rload;
        rload.name = "RLOAD";
        rload.type = SimComponentType::Resistor;
        rload.nodes = {nOut, 0};
        rload.params["resistance"] = 1000.0;
        netlist.addComponent(rload);

        SimAnalysisConfig cfg;
        cfg.type = SimAnalysisType::AC;
        cfg.fStart = 1e3;
        cfg.fStop = 1e3;
        cfg.fPoints = 1;
        netlist.setAnalysis(cfg);

        SimEngine engine;
        const SimResults results = engine.run(netlist);
        const SimWaveform* w = findWave(results, "V(OUT)");
        assert(w && !w->yData.empty());
        std::cout << "VCCS AC |V(OUT)|=" << w->yData.front() << " (Expected 1)" << std::endl;
        assert(std::abs(w->yData.front() - 1.0) < 0.05);
    }
}

void testVoltageControlledSwitch() {
    auto runCase = [](double vControl) -> double {
        SimNetlist netlist;
        const int nVin = netlist.addNode("VIN");
        const int nCtl = netlist.addNode("CTL");
        const int nOut = netlist.addNode("OUT");

        SimComponentInstance vin;
        vin.name = "VIN";
        vin.type = SimComponentType::VoltageSource;
        vin.nodes = {nVin, 0};
        vin.params["voltage"] = 10.0;
        netlist.addComponent(vin);

        SimComponentInstance vctl;
        vctl.name = "VCTL";
        vctl.type = SimComponentType::VoltageSource;
        vctl.nodes = {nCtl, 0};
        vctl.params["voltage"] = vControl;
        netlist.addComponent(vctl);

        SimComponentInstance sw;
        sw.name = "S1";
        sw.type = SimComponentType::Switch;
        sw.nodes = {nVin, nOut, nCtl, 0};
        sw.params["ron"] = 1.0;
        sw.params["roff"] = 1e9;
        sw.params["vt"] = 2.5;
        sw.params["vh"] = 0.1;
        netlist.addComponent(sw);

        SimComponentInstance rload;
        rload.name = "RLOAD";
        rload.type = SimComponentType::Resistor;
        rload.nodes = {nOut, 0};
        rload.params["resistance"] = 1000.0;
        netlist.addComponent(rload);

        SimAnalysisConfig cfg;
        cfg.type = SimAnalysisType::OP;
        netlist.setAnalysis(cfg);

        SimEngine engine;
        const SimResults results = engine.run(netlist);
        return results.nodeVoltages.at("OUT");
    };

    const double vOn = runCase(5.0);
    const double vOff = runCase(0.0);
    std::cout << "Switch ON V(OUT)=" << vOn << " (Expected near 10V)\n";
    std::cout << "Switch OFF V(OUT)=" << vOff << " (Expected near 0V)\n";
    assert(vOn > 9.8);
    assert(vOff < 0.05);
}

void testOpAmpMacroModel() {
    auto runFollower = [](double vIn) -> double {
        SimNetlist netlist;
        const int nIn = netlist.addNode("IN");
        const int nOut = netlist.addNode("OUT");
        const int nVp = netlist.addNode("VP");
        const int nVn = netlist.addNode("VN");

        SimComponentInstance vp;
        vp.name = "VP";
        vp.type = SimComponentType::VoltageSource;
        vp.nodes = {nVp, 0};
        vp.params["voltage"] = 12.0;
        netlist.addComponent(vp);

        SimComponentInstance vn;
        vn.name = "VN";
        vn.type = SimComponentType::VoltageSource;
        vn.nodes = {nVn, 0};
        vn.params["voltage"] = -12.0;
        netlist.addComponent(vn);

        SimComponentInstance vin;
        vin.name = "VIN";
        vin.type = SimComponentType::VoltageSource;
        vin.nodes = {nIn, 0};
        vin.params["voltage"] = vIn;
        netlist.addComponent(vin);

        SimComponentInstance op;
        op.name = "OA1";
        op.type = SimComponentType::OpAmpMacro;
        op.nodes = {nOut, nIn, nOut, nVp, nVn}; // out, in+, in-, v+, v-
        op.params["gain"] = 2e5;
        op.params["headroom"] = 0.2;
        netlist.addComponent(op);

        SimComponentInstance rload;
        rload.name = "RLOAD";
        rload.type = SimComponentType::Resistor;
        rload.nodes = {nOut, 0};
        rload.params["resistance"] = 10000.0;
        netlist.addComponent(rload);

        SimAnalysisConfig cfg;
        cfg.type = SimAnalysisType::OP;
        cfg.maxNRIterations = 300;
        netlist.setAnalysis(cfg);

        SimEngine engine;
        const SimResults results = engine.run(netlist);
        return results.nodeVoltages.at("OUT");
    };

    const double vLinear = runFollower(1.0);
    const double vSat = runFollower(20.0);
    std::cout << "OpAmp follower V(OUT) @1V in: " << vLinear << " (Expected near 1V)\n";
    std::cout << "OpAmp follower V(OUT) @20V in: " << vSat << " (Expected rail-limited near +11.8V)\n";
    assert(std::abs(vLinear - 1.0) < 0.05);
    assert(vSat > 11.0 && vSat < 12.2);
}

void testTransmissionLineQuasiStatic() {
    SimNetlist netlist;
    const int nIn = netlist.addNode("IN");
    const int nOut = netlist.addNode("OUT");

    SimComponentInstance vin;
    vin.name = "VIN";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {nIn, 0};
    vin.params["voltage"] = 5.0;
    netlist.addComponent(vin);

    SimComponentInstance t1;
    t1.name = "T1";
    t1.type = SimComponentType::TransmissionLine;
    t1.nodes = {nIn, 0, nOut, 0};
    t1.params["z0"] = 50.0;
    netlist.addComponent(t1);

    SimComponentInstance rload;
    rload.name = "RLOAD";
    rload.type = SimComponentType::Resistor;
    rload.nodes = {nOut, 0};
    rload.params["resistance"] = 1000.0;
    netlist.addComponent(rload);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const double vOut = results.nodeVoltages.at("OUT");
    std::cout << "Transmission-line quasi-static V(OUT)=" << vOut << " (Expected ~4.76V)\n";
    assert(std::abs(vOut - (5.0 * 1000.0 / 1050.0)) < 0.05);
}

} // namespace

int main() {
    testControlledSourcesAC();
    testVoltageControlledSwitch();
    testOpAmpMacroModel();
    testTransmissionLineQuasiStatic();
    std::cout << "simulator.advanced_device_models: all tests passed\n";
    return 0;
}
