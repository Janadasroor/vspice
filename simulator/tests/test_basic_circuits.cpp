#include "../core/sim_engine.h"
#include <iostream>
#include <cassert>
#include <limits>

void testACFilter();
void testMonteCarlo();
void testSensitivity();
void testAdaptiveTransient();
void testBJT();
void testPNP();
void testMOSFET();
void testPMOSFET();
void testControlledSources();
void testGearTransient();
void testParametricSweep();
void testParametricSweepParallelDeterministic();
void testHierarchicalFlatteningEquivalence();
void testDeterministicAssemblyOrdering();
void testSingularMatrixGracefulHandling();
void testRLTransient();
void testRLCTransient();
void testACSourceMagnitude();
void testCurrentSourceDC();
void testCurrentSourceSineTransient();
void testVoltageSourcePulseTransient();
void testDiodeHalfWaveRectifierTransient();
void testTransientOpHandoff();
void testInstrumentCalibrationDC();
void testInstrumentCalibrationTransient();
void testInstrumentCalibrationAC();
void testVoltageSourcePwlTransient();
void testVoltageSourceSffmTransient();
void testVoltageSourceAMTransient();
void testVoltageSourceFMTransient();
void testTransientStorageStrided();
void testTransientStorageAutoDecimateCap();

namespace {
const SimWaveform* findWave(const SimResults& results, const std::string& name) {
    for (const auto& wave : results.waveforms) {
        if (wave.name == name) return &wave;
    }
    return nullptr;
}

double rmsFromWave(const SimWaveform& wave) {
    if (wave.yData.empty()) return 0.0;
    double sumSq = 0.0;
    for (const double y : wave.yData) sumSq += y * y;
    return std::sqrt(sumSq / static_cast<double>(wave.yData.size()));
}

double estimateZeroCrossFrequencyHz(const SimWaveform& wave) {
    if (wave.xData.size() < 4 || wave.yData.size() < 4) return 0.0;
    int crossings = 0;
    double tFirst = std::numeric_limits<double>::quiet_NaN();
    double tLast = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 1; i < wave.yData.size(); ++i) {
        const double y0 = wave.yData[i - 1];
        const double y1 = wave.yData[i];
        if ((y0 <= 0.0 && y1 > 0.0) || (y0 >= 0.0 && y1 < 0.0)) {
            const double tc = wave.xData[i];
            if (crossings == 0) tFirst = tc;
            tLast = tc;
            ++crossings;
        }
    }
    if (crossings < 3 || !(tLast > tFirst)) return 0.0;
    return (static_cast<double>(crossings - 1) / 2.0) / (tLast - tFirst);
}
}

void testVoltageDivider() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // R2 N2 0 1k
    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double v2 = results.nodeVoltages["N2"];
    std::cout << "Voltage Divider N2: " << v2 << "V (Expected 5V)" << std::endl;
    assert(std::abs(v2 - 5.0) < 1e-6);
}

void testDiodeCircuit() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 5V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 5.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // D1 N2 0
    SimComponentInstance d1;
    d1.name = "D1";
    d1.type = SimComponentType::Diode;
    d1.nodes = {n2, 0};
    netlist.addComponent(d1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double v2 = results.nodeVoltages["N2"];
    std::cout << "Diode Circuit N2: " << v2 << "V (Expected ~0.6-0.7V)" << std::endl;
    assert(v2 > 0.5 && v2 < 0.8);
}

void testRCTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // C1 N2 0 100uF
    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 100e-6; // 100uF
    netlist.addComponent(c1);

    // RC = 1k * 100u = 0.1s
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0;
    config.tStop = 0.5; // 5 time constants
    config.tStep = 0.01;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    assert(!results.waveforms.empty());
    // Find V(N2) waveform
    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            double finalV = wave.yData.back();
            std::cout << "RC Transient V(N2) at 0.5s: " << finalV << "V (Expected ~9.9V)" << std::endl;
            assert(finalV > 9.0); // Should be mostly charged
            return;
        }
    }
    assert(false && "Waveform V(N2) not found");
}

void testRLTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["v_init"] = 0.0;
    v1.params["v_step"] = 10.0;
    netlist.addComponent(v1);

    // R=10 Ohm, L=0.1 H => tau = 0.01 s
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 10.0;
    netlist.addComponent(r1);

    SimComponentInstance l1;
    l1.name = "L1";
    l1.type = SimComponentType::Inductor;
    l1.nodes = {n2, 0};
    l1.params["inductance"] = 0.1;
    netlist.addComponent(l1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 0.05;
    config.tStep = 1e-4;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            double finalV = wave.yData.back();
            std::cout << "RL Transient V(N2) at 5tau: " << finalV << "V (Expected near 0V)" << std::endl;
            assert(finalV >= -0.2 && finalV <= 0.2);
            return;
        }
    }
    assert(false && "Waveform V(N2) not found");
}

void testRLCTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    int n3 = netlist.addNode("N3");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["v_init"] = 0.0;
    v1.params["v_step"] = 10.0;
    netlist.addComponent(v1);

    // Overdamped RLC: source -> R -> L -> C -> GND
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 100.0;
    netlist.addComponent(r1);

    SimComponentInstance l1;
    l1.name = "L1";
    l1.type = SimComponentType::Inductor;
    l1.nodes = {n2, n3};
    l1.params["inductance"] = 10e-3;
    netlist.addComponent(l1);

    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n3, 0};
    c1.params["capacitance"] = 100e-6;
    netlist.addComponent(c1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 0.1;
    config.tStep = 1e-4;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N3)") {
            double finalV = wave.yData.back();
            std::cout << "RLC Transient V(N3) final: " << finalV << "V (Expected close to 10V)" << std::endl;
            assert(finalV > 9.0 && finalV < 10.5);
            return;
        }
    }
    assert(false && "Waveform V(N3) not found");
}

void testACSourceMagnitude() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["ac_mag"] = 2.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = 100.0;
    config.fStop = 100.0;
    config.fPoints = 1;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            double mag = wave.yData.front();
            std::cout << "AC source magnitude V(N2): " << mag << "V (Expected 1.0V)" << std::endl;
            assert(std::abs(mag - 1.0) < 1e-3);
            return;
        }
    }
    assert(false && "Waveform V(N2) not found");
}

void testInstrumentCalibrationDC() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 2000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);

    const double v = results.nodeVoltages.at("N1");
    const double i = std::abs(results.branchCurrents.at("V1"));
    const double p = v * i;

    std::cout << "Instrument DC calibration V/I/P: "
              << v << " V, " << i << " A, " << p << " W (Expected 10V, 5mA, 50mW)" << std::endl;
    assert(std::abs(v - 10.0) < 1e-6);
    assert(std::abs(i - 5e-3) < 1e-6);
    assert(std::abs(p - 50e-3) < 1e-5);
}

void testInstrumentCalibrationTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 1; // SIN
    v1.params["v_offset"] = 0.0;
    v1.params["v_ampl"] = 5.0;
    v1.params["v_freq"] = 1000.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 5e-3;
    config.tStep = 2e-5;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N1)");
    assert(wave && "Waveform V(N1) not found");

    const double vrms = rmsFromWave(*wave);
    const double freqHz = estimateZeroCrossFrequencyHz(*wave);
    std::cout << "Instrument TRAN calibration RMS/Freq: "
              << vrms << " Vrms, " << freqHz << " Hz (Expected ~3.535Vrms, 1kHz)" << std::endl;
    assert(std::abs(vrms - (5.0 / std::sqrt(2.0))) < 0.08);
    assert(std::abs(freqHz - 1000.0) < 80.0);
}

void testInstrumentCalibrationAC() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["ac_mag"] = 2.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = 10.0;
    config.fStop = 100000.0;
    config.fPoints = 7;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N2)");
    assert(wave && "Waveform V(N2) not found");
    assert(wave->yData.size() == 7);

    double worstErr = 0.0;
    for (double mag : wave->yData) {
        worstErr = std::max(worstErr, std::abs(mag - 1.0));
    }
    std::cout << "Instrument AC calibration magnitude worst error: " << worstErr << " V" << std::endl;
    assert(worstErr < 2e-2);
}

void testVoltageSourcePwlTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 5.0;
    v1.params["pwl_n"] = 3.0;
    v1.params["pwl_t0"] = 0.0;
    v1.params["pwl_v0"] = 0.0;
    v1.params["pwl_t1"] = 1e-3;
    v1.params["pwl_v1"] = 5.0;
    v1.params["pwl_t2"] = 2e-3;
    v1.params["pwl_v2"] = 0.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 2e-3;
    config.tStep = 1e-4;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N1)");
    assert(wave && "Waveform V(N1) not found");

    double vMid = 0.0;
    double bestErr = 1e9;
    for (size_t i = 0; i < wave->xData.size(); ++i) {
        const double e = std::abs(wave->xData[i] - 5e-4);
        if (e < bestErr) {
            bestErr = e;
            vMid = wave->yData[i];
        }
    }

    std::cout << "PWL source V(N1) @0.5ms: " << vMid << "V (Expected ~2.5V)" << std::endl;
    assert(std::abs(vMid - 2.5) < 0.4);
}

void testVoltageSourceSffmTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 4.0;
    v1.params["sffm_offset"] = 0.0;
    v1.params["sffm_ampl"] = 5.0;
    v1.params["sffm_carrier_freq"] = 10000.0;
    v1.params["sffm_mod_index"] = 2.0;
    v1.params["sffm_signal_freq"] = 500.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 1e-3;
    config.tStep = 2e-6;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N1)");
    assert(wave && "Waveform V(N1) not found");

    double vMin = 1e9;
    double vMax = -1e9;
    for (double v : wave->yData) {
        vMin = std::min(vMin, v);
        vMax = std::max(vMax, v);
    }
    std::cout << "SFFM source V(N1) min/max: " << vMin << "V / " << vMax << "V" << std::endl;
    assert(vMax > 4.0 && vMin < -4.0);
}

void testVoltageSourceAMTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 6.0;
    v1.params["am_scale"] = 2.0;
    v1.params["am_offset_coeff"] = 1.0;
    v1.params["am_mod_freq"] = 500.0;
    v1.params["am_carrier_freq"] = 5000.0;
    v1.params["am_delay"] = 0.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 2e-3;
    config.tStep = 2e-6;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N1)");
    assert(wave && "Waveform V(N1) not found");

    double vMin = 1e9;
    double vMax = -1e9;
    for (double v : wave->yData) {
        vMin = std::min(vMin, v);
        vMax = std::max(vMax, v);
    }
    std::cout << "AM source V(N1) min/max: " << vMin << "V / " << vMax << "V" << std::endl;
    assert(vMax > 1.5 && vMin < -1.5);
}

void testVoltageSourceFMTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 7.0;
    v1.params["fm_offset"] = 1.0;
    v1.params["fm_ampl"] = 3.0;
    v1.params["fm_carrier_freq"] = 4000.0;
    v1.params["fm_freq_dev"] = 1000.0;
    v1.params["fm_mod_freq"] = 500.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 2e-3;
    config.tStep = 2e-6;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N1)");
    assert(wave && "Waveform V(N1) not found");

    double avg = 0.0;
    double vMin = 1e9;
    double vMax = -1e9;
    for (double v : wave->yData) {
        avg += v;
        vMin = std::min(vMin, v);
        vMax = std::max(vMax, v);
    }
    avg /= static_cast<double>(wave->yData.size());

    std::cout << "FM source V(N1) avg/min/max: " << avg << "V / "
              << vMin << "V / " << vMax << "V" << std::endl;
    assert(std::abs(avg - 1.0) < 0.3);
    assert(vMax > 3.5 && vMin < -1.5);
}

void testTransientStorageStrided() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 5.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 100e-9;
    netlist.addComponent(c1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Transient;
    cfg.tStart = 0.0;
    cfg.tStop = 1e-3;
    cfg.tStep = 1e-6;
    cfg.useAdaptiveStep = false;
    cfg.transientStorageMode = SimTransientStorageMode::Strided;
    cfg.transientStoreStride = 10;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N2)");
    assert(wave && "Waveform V(N2) not found");

    std::cout << "Transient storage (strided) point count: " << wave->xData.size() << std::endl;
    assert(wave->xData.size() >= 90 && wave->xData.size() <= 120);
    assert(std::abs(wave->xData.front() - 0.0) < 1e-15);
    assert(std::abs(wave->xData.back() - 1e-3) < 1e-12);
}

void testTransientStorageAutoDecimateCap() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 5.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 100e-9;
    netlist.addComponent(c1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Transient;
    cfg.tStart = 0.0;
    cfg.tStop = 5e-3;
    cfg.tStep = 1e-6;
    cfg.useAdaptiveStep = false;
    cfg.transientStorageMode = SimTransientStorageMode::AutoDecimate;
    cfg.transientMaxStoredPoints = 128;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const SimWaveform* wave = findWave(results, "V(N2)");
    assert(wave && "Waveform V(N2) not found");

    std::cout << "Transient storage (auto-decimate) point count: " << wave->xData.size() << std::endl;
    assert(!wave->xData.empty());
    assert(wave->xData.size() <= 128);
    assert(std::abs(wave->xData.front() - 0.0) < 1e-15);
    assert(std::abs(wave->xData.back() - 5e-3) < 1e-12);
}

void testCurrentSourceDC() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance i1;
    i1.name = "I1";
    i1.type = SimComponentType::CurrentSource;
    i1.nodes = {n1, 0};
    i1.params["current"] = 2e-3;
    netlist.addComponent(i1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);
    double v1 = results.nodeVoltages.at("N1");
    std::cout << "Current source DC V(N1): " << v1 << "V (Expected -2V)" << std::endl;
    assert(std::abs(v1 + 2.0) < 1e-6);
}

void testCurrentSourceSineTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance i1;
    i1.name = "I1";
    i1.type = SimComponentType::CurrentSource;
    i1.nodes = {n1, 0};
    i1.params["wave_type"] = 1; // SIN
    i1.params["i_offset"] = 0.0;
    i1.params["i_ampl"] = 1e-3;
    i1.params["i_freq"] = 1000.0;
    netlist.addComponent(i1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 1e-3;
    config.tStep = 1e-5;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N1)") {
            size_t idxQuarter = 0;
            double bestErr = 1e9;
            for (size_t i = 0; i < wave.xData.size(); ++i) {
                double e = std::abs(wave.xData[i] - 0.00025);
                if (e < bestErr) {
                    bestErr = e;
                    idxQuarter = i;
                }
            }
            double vQuarter = wave.yData[idxQuarter];
            std::cout << "Current source SINE V(N1) @T/4: " << vQuarter << "V (Expected about -1V)" << std::endl;
            assert(vQuarter < -0.8 && vQuarter > -1.2);
            return;
        }
    }
    assert(false && "Waveform V(N1) not found");
}

void testVoltageSourcePulseTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 2; // PULSE
    v1.params["pulse_v1"] = 0.0;
    v1.params["pulse_v2"] = 5.0;
    v1.params["pulse_td"] = 2e-4;
    v1.params["pulse_tr"] = 1e-6;
    v1.params["pulse_tf"] = 1e-6;
    v1.params["pulse_pw"] = 2e-4;
    v1.params["pulse_per"] = 1e-3;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 6e-4;
    config.tStep = 1e-5;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N1)") {
            double pre = 0.0;
            double high = 0.0;
            double preErr = 1e9;
            double highErr = 1e9;
            for (size_t i = 0; i < wave.xData.size(); ++i) {
                double ePre = std::abs(wave.xData[i] - 1e-4);
                if (ePre < preErr) {
                    preErr = ePre;
                    pre = wave.yData[i];
                }
                double eHigh = std::abs(wave.xData[i] - 3e-4);
                if (eHigh < highErr) {
                    highErr = eHigh;
                    high = wave.yData[i];
                }
            }
            std::cout << "Voltage PULSE pre/high: " << pre << "V / " << high << "V" << std::endl;
            assert(pre > -0.2 && pre < 0.2);
            assert(high > 4.5 && high < 5.5);
            return;
        }
    }
    assert(false && "Waveform V(N1) not found");
}

void testDiodeHalfWaveRectifierTransient() {
    SimNetlist netlist;
    int nIn = netlist.addNode("NIN");
    int nOut = netlist.addNode("NOUT");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {nIn, 0};
    v1.params["wave_type"] = 1; // SIN
    v1.params["v_offset"] = 0.0;
    v1.params["v_ampl"] = 5.0;
    v1.params["v_freq"] = 1000.0;
    netlist.addComponent(v1);

    SimComponentInstance d1;
    d1.name = "D1";
    d1.type = SimComponentType::Diode;
    d1.nodes = {nIn, nOut};
    d1.params["Is"] = 1e-14;
    d1.params["N"] = 1.0;
    netlist.addComponent(d1);

    SimComponentInstance rload;
    rload.name = "RLOAD";
    rload.type = SimComponentType::Resistor;
    rload.nodes = {nOut, 0};
    rload.params["resistance"] = 1000.0;
    netlist.addComponent(rload);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 2e-3;
    config.tStep = 2e-5;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(NOUT)") {
            double vMin = 1e9;
            double vMax = -1e9;
            for (double v : wave.yData) {
                vMin = std::min(vMin, v);
                vMax = std::max(vMax, v);
            }

            std::cout << "Half-wave rectifier V(NOUT) min/max: " << vMin << "V / " << vMax << "V" << std::endl;
            assert(vMin > -0.3);
            assert(vMax > 4.0 && vMax < 5.2);
            return;
        }
    }
    assert(false && "Waveform V(NOUT) not found");
}

void testTransientOpHandoff() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 10.0;
    netlist.addComponent(r1);

    SimComponentInstance l1;
    l1.name = "L1";
    l1.type = SimComponentType::Inductor;
    l1.nodes = {n2, 0};
    l1.params["inductance"] = 0.1;
    netlist.addComponent(l1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0.0;
    config.tStop = 2e-4;
    config.tStep = 1e-4;
    config.useAdaptiveStep = false;
    config.transientUseOperatingPointInit = true;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            assert(wave.yData.size() >= 2);
            double v0 = wave.yData[0];
            double v1s = wave.yData[1];
            std::cout << "TRAN OP handoff V(N2) t0/t1: " << v0 << "V / " << v1s << "V" << std::endl;
            assert(std::abs(v0) < 0.2);
            assert(std::abs(v1s) < 0.5);
            return;
        }
    }
    assert(false && "Waveform V(N2) not found");
}

void testBehavioralSources() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 2V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 2.0;
    netlist.addComponent(v1);

    // B1 N2 0 V=V(N1)*2.5
    SimComponentInstance b1;
    b1.name = "B1";
    b1.type = SimComponentType::B_VoltageSource;
    b1.nodes = {n2, 0};
    b1.modelName = "V(N1)*2.5";
    netlist.addComponent(b1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double v2 = results.nodeVoltages["N2"];
    std::cout << "Behavioral Source N2: " << v2 << "V (Expected 5V)" << std::endl;
    assert(std::abs(v2 - 5.0) < 1e-6);
}

void testOpAmpSubcircuit();
void testComplexConvergence();
void testParameterExpressions();
void testFFT();

int main() {
    try {
        testVoltageDivider();
        testDiodeCircuit();
        testRCTransient();
        testRLTransient();
        testRLCTransient();
        testACSourceMagnitude();
        testCurrentSourceDC();
        testCurrentSourceSineTransient();
        testVoltageSourcePulseTransient();
        testDiodeHalfWaveRectifierTransient();
        testTransientOpHandoff();
        testInstrumentCalibrationDC();
        testInstrumentCalibrationTransient();
        testInstrumentCalibrationAC();
        testVoltageSourcePwlTransient();
        testVoltageSourceSffmTransient();
        testVoltageSourceAMTransient();
        testVoltageSourceFMTransient();
        testTransientStorageStrided();
        testTransientStorageAutoDecimateCap();
        testACFilter();
        testMonteCarlo();
        testSensitivity();
        testAdaptiveTransient();
        testBJT();
        testPNP();
        testMOSFET();
        testPMOSFET();
        testControlledSources();
        testGearTransient();
        testBehavioralSources();
        testParameterExpressions();
        testFFT();
        testOpAmpSubcircuit();
        testHierarchicalFlatteningEquivalence();
        testDeterministicAssemblyOrdering();
        testSingularMatrixGracefulHandling();
        testComplexConvergence();
        testParametricSweep();
        testParametricSweepParallelDeterministic();
        std::cout << "All basic and complex simulator tests PASSED!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

void testAdaptiveTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // C1 N2 0 100uF
    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 100e-6; 
    netlist.addComponent(c1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0;
    config.tStop = 0.5;
    config.tStep = 0.01;
    config.useAdaptiveStep = true;
    config.relTol = 1e-3;
    config.absTol = 1e-8;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    bool found = false;
    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            std::cout << "Adaptive Transient V(N2) points: " << wave.xData.size() << std::endl;
            assert(wave.xData.size() > 5); // Smooth curve, takes fewer steps
            double lastV = wave.yData.back();
            std::cout << "Adaptive Transient V(N2) at 0.5s: " << lastV << " V (Expected ~9.93V)" << std::endl;
            assert(std::abs(lastV - 9.9326) < 0.1);
            found = true;
        }
    }
    assert(found);
}

void testSensitivity() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // R2 N2 0 1k
    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    // V2 = V1 * R2 / (R1 + R2)
    // dV2/dR1 = -V1 * R2 / (R1+R2)^2 = -10 * 1000 / (2000)^2 = -10000 / 4000000 = -0.0025
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::Sensitivity;
    config.sensitivityTargetSignal = "V(N2)";
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double sensR1 = results.sensitivities["R1"];
    std::cout << "Sensitivity dV(N2)/dR1: " << sensR1 << " V/Ohm (Expected -0.0025)" << std::endl;
    assert(std::abs(sensR1 + 0.0025) < 1e-6);
}

void testMonteCarlo() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // R2 N2 0 1k (10% tol)
    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    r2.tolerances["resistance"] = { 0.10, ToleranceDistribution::Uniform }; // 10%
    netlist.addComponent(r2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::MonteCarlo;
    config.mcBaseAnalysis = SimAnalysisType::OP;
    config.mcRuns = 10;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    bool found = false;
    for (const auto& wave : results.waveforms) {
        if (wave.name == "MC_V(N2)") {
            std::cout << "Monte Carlo MC_V(N2) [10 runs]: ";
            for (double v : wave.yData) std::cout << v << " ";
            std::cout << std::endl;
            
            assert(wave.yData.size() == 10);
            // Verify there is some variation
            double first = wave.yData[0];
            bool varied = false;
            for (double v : wave.yData) if (v != first) varied = true;
            assert(varied);
            found = true;
        }
    }
    assert(found);
}

void testACFilter() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 AC 1V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["ac_mag"] = 1.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // C1 N2 0 1uF
    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 1e-6; // 1uF
    netlist.addComponent(c1);

    // fc = 1/(2*pi*RC) = 1/(2*pi*1e-3) approx 159 Hz
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::AC;
    config.fStart = 10.0;
    config.fStop = 1000.0;
    config.fPoints = 100;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    bool foundFc = false;
    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            for (size_t i = 0; i < wave.xData.size(); ++i) {
                double f = wave.xData[i];
                double mag = wave.yData[i];
                // Check near 159 Hz, mag should be approx 1/sqrt(2) = 0.707
                if (f > 155 && f < 165) {
                    std::cout << "AC Filter V(N2) at " << f << " Hz: " << mag << " (Expected ~0.707)" << std::endl;
                    assert(mag > 0.65 && mag < 0.75);
                    foundFc = true;
                    break;
                }
            }
        }
    }
    assert(foundFc);
}
void testBJT() {
    SimNetlist netlist;
    int nC = netlist.addNode("nodeC");
    int nB = netlist.addNode("nodeB");
    int nVCC = netlist.addNode("VCC");
    
    // V1 VCC 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {nVCC, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // Rb VCC nodeB 1M
    SimComponentInstance rb;
    rb.name = "RB";
    rb.type = SimComponentType::Resistor;
    rb.nodes = {nVCC, nB};
    rb.params["resistance"] = 1e6;
    netlist.addComponent(rb);

    // Rc VCC nodeC 1k
    SimComponentInstance rc;
    rc.name = "RC";
    rc.type = SimComponentType::Resistor;
    rc.nodes = {nVCC, nC};
    rc.params["resistance"] = 1000.0;
    netlist.addComponent(rc);

    // Q1 nodeC nodeB 0 NPN
    SimComponentInstance q1;
    q1.name = "Q1";
    q1.type = SimComponentType::BJT_NPN;
    q1.nodes = {nC, nB, 0};
    q1.params["Bf"] = 100.0;
    netlist.addComponent(q1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double vc = results.nodeVoltages["nodeC"];
    double vb = results.nodeVoltages["nodeB"];
    
    std::cout << "BJT OP nodeC: " << vc << "V, nodeB: " << vb << "V" << std::endl;
    // With Rb=1M, Ib approx 9.3uA. Ic approx 0.93mA. Vc approx 10 - 0.93 = 9.07V.
    // Base should be approx 0.7V
    assert(vb > 0.6 && vb < 0.8);
    assert(vc > 8.5 && vc < 9.5);
}

void testMOSFET() {
    SimNetlist netlist;
    int nG = netlist.addNode("nodeG");
    int nD = netlist.addNode("nodeD");

    // VGS = 2V
    SimComponentInstance vgs;
    vgs.name = "VGS";
    vgs.type = SimComponentType::VoltageSource;
    vgs.nodes = {nG, 0};
    vgs.params["voltage"] = 2.0;
    netlist.addComponent(vgs);

    // VDD = 10V
    int nVDD = netlist.addNode("VDD");
    SimComponentInstance vddSource;
    vddSource.name = "VDD_SRC";
    vddSource.type = SimComponentType::VoltageSource;
    vddSource.nodes = {nVDD, 0};
    vddSource.params["voltage"] = 10.0;
    netlist.addComponent(vddSource);

    // RD = 1k
    SimComponentInstance rd;
    rd.name = "RD";
    rd.type = SimComponentType::Resistor;
    rd.nodes = {nVDD, nD};
    rd.params["resistance"] = 1000.0;
    netlist.addComponent(rd);

    // NMOS: D=nD, G=nG, S=0
    SimComponentInstance m1;
    m1.name = "M1";
    m1.type = SimComponentType::MOSFET_NMOS;
    m1.nodes = {nD, nG, 0};
    m1.params["Kp"] = 2e-3;
    m1.params["Vt"] = 1.0;
    netlist.addComponent(m1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double vd = results.nodeVoltages["nodeD"];
    // Vgs = 2V, Vt = 1V. Vgs-Vt = 1V. Saturation if Vds > 1V.
    // Ids = 0.5 * 2e-3 * (2-1)^2 = 1mA.
    // Vd = 10 - 1mA * 1k = 9V.
    std::cout << "MOSFET OP nodeD: " << vd << "V" << std::endl;
    assert(vd > 8.8 && vd < 9.2);
}

void testPMOSFET() {
    SimNetlist netlist;
    int nGate = netlist.addNode("gate");
    int nDrain = netlist.addNode("drain");
    int nVDD = netlist.addNode("VDD");

    // PMOS gate at 0V => device should turn on as high-side pull-up.
    SimComponentInstance vg;
    vg.name = "VG";
    vg.type = SimComponentType::VoltageSource;
    vg.nodes = {nGate, 0};
    vg.params["voltage"] = 0.0;
    netlist.addComponent(vg);

    SimComponentInstance vdd;
    vdd.name = "VDD_SRC";
    vdd.type = SimComponentType::VoltageSource;
    vdd.nodes = {nVDD, 0};
    vdd.params["voltage"] = 10.0;
    netlist.addComponent(vdd);

    SimComponentInstance rload;
    rload.name = "RLOAD";
    rload.type = SimComponentType::Resistor;
    rload.nodes = {nDrain, 0};
    rload.params["resistance"] = 1000.0;
    netlist.addComponent(rload);

    SimComponentInstance mp1;
    mp1.name = "MP1";
    mp1.type = SimComponentType::MOSFET_PMOS;
    mp1.nodes = {nDrain, nGate, nVDD}; // D, G, S
    mp1.params["Kp"] = 2e-3;
    mp1.params["Vt"] = 1.0;
    netlist.addComponent(mp1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double vd = results.nodeVoltages["drain"];
    std::cout << "PMOS OP drain: " << vd << "V" << std::endl;
    assert(vd > 8.0 && vd <= 10.1);
}

void testControlledSources() {
    SimNetlist netlist;
    // VCVS Test (Ideal Amp)
    int nIn = netlist.addNode("nIn");
    int nOut = netlist.addNode("nOut");

    SimComponentInstance vin;
    vin.name = "Vin";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {nIn, 0};
    vin.params["voltage"] = 1.0;
    netlist.addComponent(vin);

    SimComponentInstance e1;
    e1.name = "E1";
    e1.type = SimComponentType::VCVS;
    e1.nodes = {nOut, 0, nIn, 0};
    e1.params["gain"] = 5.0;
    netlist.addComponent(e1);

    // CCVS Test (Current to Voltage)
    int nPath = netlist.addNode("nPath");
    int nOutI = netlist.addNode("nOutI");
    
    // Rload for InP path
    SimComponentInstance rIn;
    rIn.name = "RIn";
    rIn.type = SimComponentType::Resistor;
    rIn.nodes = {nIn, nPath};
    rIn.params["resistance"] = 1000.0;
    netlist.addComponent(rIn);

    // H1: OutP=nOutI, OutN=0, InP=nPath, InN=0. Gain=2000.
    SimComponentInstance h1;
    h1.name = "H1";
    h1.type = SimComponentType::CCVS;
    h1.nodes = {nOutI, 0, nPath, 0};
    h1.params["gain"] = 2000.0;
    netlist.addComponent(h1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double vOut = results.nodeVoltages["nOut"];
    double vOutI = results.nodeVoltages["nOutI"];
    
    std::cout << "VCVS Out: " << vOut << "V (Expected 5V)" << std::endl;
    // Iin = (Vin - Vpath)/RIn. Since Path is shorted to 0 by CCVS input, Iin = 1V/1k = 1mA.
    // VoutI = k * Iin = 2000 * 1mA = 2V.
    std::cout << "CCVS Out: " << vOutI << "V (Expected 2V)" << std::endl;
    
    assert(std::abs(vOut - 5.0) < 1e-6);
    assert(std::abs(vOutI - 2.0) < 1e-6);
}

void testGearTransient() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["v_init"] = 0.0;
    v1.params["v_step"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    // C1 N2 0 1uF
    SimComponentInstance c1;
    c1.name = "C1";
    c1.type = SimComponentType::Capacitor;
    c1.nodes = {n2, 0};
    c1.params["capacitance"] = 1e-6; 
    netlist.addComponent(c1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::Transient;
    config.tStart = 0;
    config.tStop = 0.005; // 5ms
    config.tStep = 1e-4;  // 100us
    config.integrationMethod = SimIntegrationMethod::Gear2;
    config.useAdaptiveStep = false;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    // After 5ms (5*RC), Vc should be ~9.9326V
    bool found = false;
    for (const auto& wave : results.waveforms) {
        if (wave.name == "V(N2)") {
            double startV = wave.yData.front();
            double finalV = wave.yData.back();
            std::cout << "Gear2 Transient V(N2) Start: " << startV << "V, End (5ms): " << finalV << "V (Expected ~9.93V)" << std::endl;
            assert(std::abs(startV) < 1e-6);
            assert(finalV > 9.8 && finalV < 10.0); // Should not overshoot 10V
            found = true;
        }
    }
    assert(found);
}

void testOpAmpSubcircuit() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1"); // Input
    int n2 = netlist.addNode("N2"); // Inverting Input
    int n3 = netlist.addNode("N3"); // Output
    int nGnd = 0;
    
    // Create Op-Amp Subcircuit Model (Ideal-ish)
    SimSubcircuit opamp;
    opamp.name = "IdealOpAmp";
    opamp.pinNames = {"IN+", "IN-", "OUT"}; // Pins 1, 2, 3
    
    // Input resistance (1Meg) between IN+ and IN-
    SimComponentInstance rin;
    rin.name = "Rin";
    rin.type = SimComponentType::Resistor;
    rin.nodes = {1, 2}; 
    rin.params["resistance"] = 1e6;
    opamp.components.push_back(rin);
    
    // VCVS Gain (1e5) from IN+, IN- to OUT
    SimComponentInstance vcvs;
    vcvs.name = "E1";
    vcvs.type = SimComponentType::VCVS;
    vcvs.nodes = {4, 0, 1, 2}; // OUT+, OUT-, IN+, IN-
    vcvs.params["gain"] = 1e5;
    opamp.components.push_back(vcvs);
    
    // Output resistance (50 ohms)
    SimComponentInstance rout;
    rout.name = "Rout";
    rout.type = SimComponentType::Resistor;
    rout.nodes = {4, 3}; // Internal to OUT
    rout.params["resistance"] = 50.0;
    opamp.components.push_back(rout);
    
    netlist.addSubcircuit(opamp);
    
    // Top-level Circuit: Inverting Amplifier with Gain = -2
    // Vin N1 0 1V
    SimComponentInstance vin;
    vin.name = "Vin";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {n1, nGnd};
    vin.params["voltage"] = 1.0;
    netlist.addComponent(vin);
    
    // R1 N1 N2 10k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 10e3;
    netlist.addComponent(r1);
    
    // R2 N2 N3 20k (Feedback)
    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, n3};
    r2.params["resistance"] = 20e3;
    netlist.addComponent(r2);
    
    // X1 0 N2 N3 IdealOpAmp
    SimComponentInstance x1;
    x1.name = "X1";
    x1.subcircuitName = "IdealOpAmp";
    x1.nodes = {nGnd, n2, n3}; // IN+ = 0, IN- = N2, OUT = N3
    netlist.addComponent(x1);
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);
    
    SimEngine engine;
    SimResults results = engine.run(netlist);
    
    double vOut = results.nodeVoltages["N3"];
    std::cout << "OpAmp Subcircuit N3 (Out): " << vOut << "V (Expected ~-2V)" << std::endl;
    assert(std::abs(vOut + 2.0) < 0.05);
}

void testHierarchicalFlatteningEquivalence() {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;

    // Hierarchical deck: same topology as testOpAmpSubcircuit(), but through X1.
    SimNetlist hierarchical;
    int hN1 = hierarchical.addNode("N1");
    int hN2 = hierarchical.addNode("N2");
    int hN3 = hierarchical.addNode("N3");

    SimSubcircuit opamp;
    opamp.name = "IdealOpAmp";
    opamp.pinNames = {"IN+", "IN-", "OUT"};

    SimComponentInstance rin;
    rin.name = "Rin";
    rin.type = SimComponentType::Resistor;
    rin.nodes = {1, 2};
    rin.params["resistance"] = 1e6;
    opamp.components.push_back(rin);

    SimComponentInstance vcvs;
    vcvs.name = "E1";
    vcvs.type = SimComponentType::VCVS;
    vcvs.nodes = {4, 0, 1, 2};
    vcvs.params["gain"] = 1e5;
    opamp.components.push_back(vcvs);

    SimComponentInstance rout;
    rout.name = "Rout";
    rout.type = SimComponentType::Resistor;
    rout.nodes = {4, 3};
    rout.params["resistance"] = 50.0;
    opamp.components.push_back(rout);

    hierarchical.addSubcircuit(opamp);

    SimComponentInstance hVin;
    hVin.name = "Vin";
    hVin.type = SimComponentType::VoltageSource;
    hVin.nodes = {hN1, 0};
    hVin.params["voltage"] = 1.0;
    hierarchical.addComponent(hVin);

    SimComponentInstance hR1;
    hR1.name = "R1";
    hR1.type = SimComponentType::Resistor;
    hR1.nodes = {hN1, hN2};
    hR1.params["resistance"] = 10e3;
    hierarchical.addComponent(hR1);

    SimComponentInstance hR2;
    hR2.name = "R2";
    hR2.type = SimComponentType::Resistor;
    hR2.nodes = {hN2, hN3};
    hR2.params["resistance"] = 20e3;
    hierarchical.addComponent(hR2);

    SimComponentInstance x1;
    x1.name = "X1";
    x1.subcircuitName = "IdealOpAmp";
    x1.nodes = {0, hN2, hN3};
    hierarchical.addComponent(x1);
    hierarchical.setAnalysis(config);

    // Flattened equivalent deck: directly expanded primitive components.
    SimNetlist flattened;
    int fN1 = flattened.addNode("N1");
    int fN2 = flattened.addNode("N2");
    int fN3 = flattened.addNode("N3");
    int fInternal = flattened.addNode("X1:4");

    SimComponentInstance fVin;
    fVin.name = "Vin";
    fVin.type = SimComponentType::VoltageSource;
    fVin.nodes = {fN1, 0};
    fVin.params["voltage"] = 1.0;
    flattened.addComponent(fVin);

    SimComponentInstance fR1;
    fR1.name = "R1";
    fR1.type = SimComponentType::Resistor;
    fR1.nodes = {fN1, fN2};
    fR1.params["resistance"] = 10e3;
    flattened.addComponent(fR1);

    SimComponentInstance fR2;
    fR2.name = "R2";
    fR2.type = SimComponentType::Resistor;
    fR2.nodes = {fN2, fN3};
    fR2.params["resistance"] = 20e3;
    flattened.addComponent(fR2);

    SimComponentInstance fRin;
    fRin.name = "X1:Rin";
    fRin.type = SimComponentType::Resistor;
    fRin.nodes = {0, fN2};
    fRin.params["resistance"] = 1e6;
    flattened.addComponent(fRin);

    SimComponentInstance fVcvs;
    fVcvs.name = "X1:E1";
    fVcvs.type = SimComponentType::VCVS;
    fVcvs.nodes = {fInternal, 0, 0, fN2};
    fVcvs.params["gain"] = 1e5;
    flattened.addComponent(fVcvs);

    SimComponentInstance fRout;
    fRout.name = "X1:Rout";
    fRout.type = SimComponentType::Resistor;
    fRout.nodes = {fInternal, fN3};
    fRout.params["resistance"] = 50.0;
    flattened.addComponent(fRout);
    flattened.setAnalysis(config);

    SimEngine engine;
    const SimResults hierarchicalResults = engine.run(hierarchical);
    const SimResults flattenedResults = engine.run(flattened);

    const double hOut = hierarchicalResults.nodeVoltages.at("N3");
    const double fOut = flattenedResults.nodeVoltages.at("N3");
    const double hInv = hierarchicalResults.nodeVoltages.at("N2");
    const double fInv = flattenedResults.nodeVoltages.at("N2");

    std::cout << "Hierarchical flatten equivalence N3: hier=" << hOut
              << " flat=" << fOut << std::endl;
    assert(std::abs(hOut - fOut) < 1e-9);
    assert(std::abs(hInv - fInv) < 1e-9);
}

void testDeterministicAssemblyOrdering() {
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;

    SimNetlist ordered;
    int oN1 = ordered.addNode("N1");
    int oN2 = ordered.addNode("N2");

    SimComponentInstance oV1;
    oV1.name = "V1";
    oV1.type = SimComponentType::VoltageSource;
    oV1.nodes = {oN1, 0};
    oV1.params["voltage"] = 10.0;
    ordered.addComponent(oV1);

    SimComponentInstance oV2;
    oV2.name = "V2";
    oV2.type = SimComponentType::VoltageSource;
    oV2.nodes = {oN2, 0};
    oV2.params["voltage"] = 2.0;
    ordered.addComponent(oV2);

    SimComponentInstance oR1;
    oR1.name = "R1";
    oR1.type = SimComponentType::Resistor;
    oR1.nodes = {oN1, oN2};
    oR1.params["resistance"] = 1000.0;
    ordered.addComponent(oR1);

    SimComponentInstance oR2;
    oR2.name = "R2";
    oR2.type = SimComponentType::Resistor;
    oR2.nodes = {oN2, 0};
    oR2.params["resistance"] = 1000.0;
    ordered.addComponent(oR2);
    ordered.setAnalysis(config);

    SimNetlist shuffled;
    int sN1 = shuffled.addNode("N1");
    int sN2 = shuffled.addNode("N2");

    SimComponentInstance sR2;
    sR2.name = "R2";
    sR2.type = SimComponentType::Resistor;
    sR2.nodes = {sN2, 0};
    sR2.params["resistance"] = 1000.0;
    shuffled.addComponent(sR2);

    SimComponentInstance sV2;
    sV2.name = "V2";
    sV2.type = SimComponentType::VoltageSource;
    sV2.nodes = {sN2, 0};
    sV2.params["voltage"] = 2.0;
    shuffled.addComponent(sV2);

    SimComponentInstance sR1;
    sR1.name = "R1";
    sR1.type = SimComponentType::Resistor;
    sR1.nodes = {sN1, sN2};
    sR1.params["resistance"] = 1000.0;
    shuffled.addComponent(sR1);

    SimComponentInstance sV1;
    sV1.name = "V1";
    sV1.type = SimComponentType::VoltageSource;
    sV1.nodes = {sN1, 0};
    sV1.params["voltage"] = 10.0;
    shuffled.addComponent(sV1);
    shuffled.setAnalysis(config);

    SimEngine engine;
    const SimResults orderedResults = engine.run(ordered);
    const SimResults shuffledResults = engine.run(shuffled);

    assert(std::abs(orderedResults.nodeVoltages.at("N1") - shuffledResults.nodeVoltages.at("N1")) < 1e-12);
    assert(std::abs(orderedResults.nodeVoltages.at("N2") - shuffledResults.nodeVoltages.at("N2")) < 1e-12);
    assert(std::abs(orderedResults.branchCurrents.at("V1") - shuffledResults.branchCurrents.at("V1")) < 1e-12);
    assert(std::abs(orderedResults.branchCurrents.at("V2") - shuffledResults.branchCurrents.at("V2")) < 1e-12);
}

void testSingularMatrixGracefulHandling() {
    // Two conflicting ideal voltage sources on the same node should be diagnosed
    // as inconsistent constraints; simulation must fail gracefully without crashing.
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 5.0;
    netlist.addComponent(v1);

    SimComponentInstance v2;
    v2.name = "V2";
    v2.type = SimComponentType::VoltageSource;
    v2.nodes = {n1, 0};
    v2.params["voltage"] = 10.0;
    netlist.addComponent(v2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    // Engine should return structured results even if convergence fails.
    assert(results.nodeVoltages.count("N1") > 0);
    assert(std::isfinite(results.nodeVoltages.at("N1")));
}

void testComplexConvergence() {
    // Tests Gmin and Source Stepping on a nonlinear diode bridge
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    int n3 = netlist.addNode("N3");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);
    
    // R1 N1 N2 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);
    
    // D1 N2 N3
    SimComponentInstance d1;
    d1.name = "D1";
    d1.type = SimComponentType::Diode;
    d1.nodes = {n2, n3};
    netlist.addComponent(d1);
    
    // D2 N3 0
    SimComponentInstance d2;
    d2.name = "D2";
    d2.type = SimComponentType::Diode;
    d2.nodes = {n3, 0};
    netlist.addComponent(d2);
    
    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);
    
    SimEngine engine;
    SimResults results = engine.run(netlist);
    
    double v3 = results.nodeVoltages["N3"];
    std::cout << "Complex Convergence N3: " << v3 << "V (Expected ~0.7V)" << std::endl;
    assert(v3 > 0.6 && v3 < 0.9);
}

void testParametricSweep() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 1k
    // R2 N2 0 1k (Swept)
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::ParametricSweep;
    config.sweepParam = "R2.resistance";
    config.sweepStart = 1000.0;
    config.sweepStop = 5000.0;
    config.sweepPoints = 5;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    // Expected V(N2) = 10 * R2 / (1000 + R2)
    // Points at R2 = 1000, 2000, 3000, 4000, 5000
    // V(N2) = 5.0, 6.66, 7.5, 8.0, 8.33
    
    std::cout << "Parametric Sweep Results [V(N2)]:" << std::endl;
    int count = 0;
    for (const auto& wave : results.waveforms) {
        if (wave.name.find("V(N2)") != std::string::npos) {
            std::cout << "  " << wave.name << ": " << wave.yData[0] << "V" << std::endl;
            count++;
        }
    }
    assert(count == 5);
}

void testParametricSweepParallelDeterministic() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");

    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    SimAnalysisConfig serialCfg;
    serialCfg.type = SimAnalysisType::ParametricSweep;
    serialCfg.sweepParam = "R2.resistance";
    serialCfg.sweepStart = 1000.0;
    serialCfg.sweepStop = 5000.0;
    serialCfg.sweepPoints = 9;
    serialCfg.sweepParallelism = 1;
    netlist.setAnalysis(serialCfg);

    SimEngine engine;
    const SimResults serialResults = engine.run(netlist);

    SimAnalysisConfig parallelCfg = serialCfg;
    parallelCfg.sweepParallelism = 4;
    netlist.setAnalysis(parallelCfg);
    const SimResults parallelResults = engine.run(netlist);

    assert(serialResults.waveforms.size() == parallelResults.waveforms.size());
    for (size_t i = 0; i < serialResults.waveforms.size(); ++i) {
        const auto& a = serialResults.waveforms[i];
        const auto& b = parallelResults.waveforms[i];
        assert(a.name == b.name);
        assert(a.xData.size() == b.xData.size());
        assert(a.yData.size() == b.yData.size());
        for (size_t j = 0; j < a.yData.size(); ++j) {
            assert(std::abs(a.yData[j] - b.yData[j]) < 1e-12);
        }
    }
    std::cout << "Parametric sweep serial/parallel deterministic equivalence: "
              << serialResults.waveforms.size() << " traces" << std::endl;
}

void testParameterExpressions() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    int n2 = netlist.addNode("N2");
    
    // V1 N1 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // R1 N1 N2 {BASE_R * 2}
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.paramExpressions["resistance"] = "{BASE_R * 2}";
    netlist.addComponent(r1);

    // R2 N2 0 1k
    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    netlist.addComponent(r2);

    netlist.setParameter("BASE_R", 500.0); // R1 should be 1000.0

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double v2 = results.nodeVoltages["N2"];
    std::cout << "Parameter Expression Result N2: " << v2 << "V (Expected 5V)" << std::endl;
    assert(std::abs(v2 - 5.0) < 1e-6);
}

void testFFT() {
    SimNetlist netlist;
    int n1 = netlist.addNode("N1");
    
    // V1 N1 0 SIN(0 5 1000) -> 1kHz Sine
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, 0};
    v1.params["wave_type"] = 1; // SIN
    v1.params["v_offset"] = 0.0;
    v1.params["v_ampl"] = 5.0;
    v1.params["v_freq"] = 1000.0;
    netlist.addComponent(v1);

    // R1 N1 0 1k
    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, 0};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::FFT;
    config.tStart = 0;
    config.tStop = 0.01; // 10 cycles
    config.tStep = 1e-5;
    config.fftTargetSignal = "V(N1)";
    config.fftPoints = 1024;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    assert(!results.waveforms.empty());
    const SimWaveform* fftWave = findWave(results, "FFT(V(N1))");
    assert(fftWave);

    // Find peak frequency
    double peakFreq = 0;
    double maxMag = -1e9;
    for (size_t i = 0; i < fftWave->xData.size(); ++i) {
        if (fftWave->yData[i] > maxMag) {
            maxMag = fftWave->yData[i];
            peakFreq = fftWave->xData[i];
        }
    }

    std::cout << "FFT Peak Frequency: " << peakFreq << " Hz (Expected ~1000 Hz), Magnitude: " << maxMag << " dB" << std::endl;
    assert(std::abs(peakFreq - 1000.0) < 150.0); // Resampling/Windowing might shift it slightly
}

void testPNP() {
    SimNetlist netlist;
    int nC = netlist.addNode("nodeC");
    int nB = netlist.addNode("nodeB");
    int nVE = netlist.addNode("VE");
    
    // V1 VE 0 10V
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {nVE, 0};
    v1.params["voltage"] = 10.0;
    netlist.addComponent(v1);

    // Rb nodeB 0 1M
    SimComponentInstance rb;
    rb.name = "RB";
    rb.type = SimComponentType::Resistor;
    rb.nodes = {nB, 0};
    rb.params["resistance"] = 1e6;
    netlist.addComponent(rb);

    // Rc nodeC 0 1k
    SimComponentInstance rc;
    rc.name = "RC";
    rc.type = SimComponentType::Resistor;
    rc.nodes = {nC, 0};
    rc.params["resistance"] = 1000.0;
    netlist.addComponent(rc);

    // Q1 nodeC nodeB nVE PNP
    SimComponentInstance q1;
    q1.name = "Q1";
    q1.type = SimComponentType::BJT_PNP;
    q1.nodes = {nC, nB, nVE};
    q1.params["Bf"] = 100.0;
    netlist.addComponent(q1);

    SimAnalysisConfig config;
    config.type = SimAnalysisType::OP;
    netlist.setAnalysis(config);

    SimEngine engine;
    SimResults results = engine.run(netlist);

    double vc = results.nodeVoltages["nodeC"];
    double vb = results.nodeVoltages["nodeB"];
    double ve = results.nodeVoltages["VE"];
    
    std::cout << "PNP OP nodeC: " << vc << "V, nodeB: " << vb << "V, VE: " << ve << "V" << std::endl;
    
    // If Q1 is PNP:
    // Vb approx 9.35V, Vc approx 0.93V (with Bf=100)
    assert(vb > 9.0 && vb < 9.5);
    assert(vc > 0.8 && vc < 1.1);
}

