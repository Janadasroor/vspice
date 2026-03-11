#include "../core/sim_engine.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

SimNetlist buildOptimizationDeck() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    SimComponentInstance vin;
    vin.name = "VIN";
    vin.type = SimComponentType::VoltageSource;
    vin.nodes = {n1, 0};
    vin.params["voltage"] = 10.0;
    netlist.addComponent(vin);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    r1.tolerances["resistance"] = { 0.06, ToleranceDistribution::Uniform };
    netlist.addComponent(r1);

    SimComponentInstance r2;
    r2.name = "R2";
    r2.type = SimComponentType::Resistor;
    r2.nodes = {n2, 0};
    r2.params["resistance"] = 1000.0;
    r2.tolerances["resistance"] = { 0.06, ToleranceDistribution::Uniform };
    netlist.addComponent(r2);

    return netlist;
}

void testOptimizationDeterministicBest() {
    SimNetlist netlist = buildOptimizationDeck();
    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Optimization;
    cfg.optimizationTargetSignal = "V(N2)";
    cfg.optimizationTargetValue = 6.0;
    cfg.optimizationTargetWeight = 1.0;
    cfg.optimizationMaxEvaluations = 0;
    cfg.optimizationYieldSamples = 80;
    cfg.optimizationYieldTargetTolerance = 0.2;
    cfg.optimizationSeed = 1234;

    SimAnalysisConfig::OptimizationParam p1;
    p1.name = "R1.resistance";
    p1.start = 1000.0;
    p1.stop = 3000.0;
    p1.points = 5;
    cfg.optimizationParams.push_back(p1);

    SimAnalysisConfig::OptimizationParam p2;
    p2.name = "R2.resistance";
    p2.start = 1000.0;
    p2.stop = 3000.0;
    p2.points = 5;
    cfg.optimizationParams.push_back(p2);

    SimAnalysisConfig::OptimizationConstraint c1;
    c1.signal = "V(N2)";
    c1.minValue = 5.5;
    c1.maxValue = 6.5;
    cfg.optimizationConstraints.push_back(c1);

    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults a = engine.run(netlist);
    const SimResults b = engine.run(netlist);

    const auto objA = a.measurements.at("optimization_best_objective");
    const auto objB = b.measurements.at("optimization_best_objective");
    const auto r1A = a.measurements.at("optimization_best_R1.resistance");
    const auto r1B = b.measurements.at("optimization_best_R1.resistance");
    const auto r2A = a.measurements.at("optimization_best_R2.resistance");
    const auto r2B = b.measurements.at("optimization_best_R2.resistance");
    const auto bestTarget = a.measurements.at("optimization_best_target");

    std::cout << "Optimization best objective (run A/B): " << objA << " / " << objB << "\n";
    std::cout << "Optimization best params R1/R2: " << r1A << " / " << r2A << "\n";
    std::cout << "Optimization best target: " << bestTarget << "V\n";

    assert(std::abs(objA - objB) < 1e-12);
    assert(std::abs(r1A - r1B) < 1e-12);
    assert(std::abs(r2A - r2B) < 1e-12);
    assert(std::abs(bestTarget - 6.0) < 0.3);
}

void testOptimizationYieldSummary() {
    SimNetlist netlist = buildOptimizationDeck();
    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::Optimization;
    cfg.optimizationTargetSignal = "V(N2)";
    cfg.optimizationTargetValue = 6.0;
    cfg.optimizationYieldSamples = 120;
    cfg.optimizationYieldTargetTolerance = 0.25;
    cfg.optimizationSeed = 7;

    SimAnalysisConfig::OptimizationParam p1;
    p1.name = "R1.resistance";
    p1.start = 1000.0;
    p1.stop = 3000.0;
    p1.points = 5;
    cfg.optimizationParams.push_back(p1);

    SimAnalysisConfig::OptimizationParam p2;
    p2.name = "R2.resistance";
    p2.start = 1000.0;
    p2.stop = 3000.0;
    p2.points = 5;
    cfg.optimizationParams.push_back(p2);

    netlist.setAnalysis(cfg);

    SimEngine engine;
    const SimResults results = engine.run(netlist);
    const double yieldRatio = results.measurements.at("optimization_yield_ratio");
    const double validSamples = results.measurements.at("optimization_yield_valid_samples");
    std::cout << "Optimization yield ratio: " << yieldRatio
              << " over valid samples: " << validSamples << "\n";
    assert(validSamples > 0.0);
    assert(yieldRatio >= 0.0 && yieldRatio <= 1.0);
}

} // namespace

int main() {
    testOptimizationDeterministicBest();
    testOptimizationYieldSummary();
    std::cout << "simulator.optimization: all tests passed\n";
    return 0;
}
