#include "../core/sim_engine.h"
#include "../core/sim_matrix.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void testDenseMatrixSolve() {
    SimMNAMatrix m;
    m.resize(3, 0); // node equations for nodes 1 and 2 (node 0 is ground)

    // [ 2 -1 ] [v1] = [1]
    // [-1  2 ] [v2]   [0]
    m.addG(1, 1, 2.0);
    m.addG(1, 2, -1.0);
    m.addG(2, 1, -1.0);
    m.addG(2, 2, 2.0);
    m.addI(1, 1.0);
    m.addI(2, 0.0);

    const std::vector<double> x = m.solve();
    assert(x.size() == 2);
    assert(std::abs(x[0] - (2.0 / 3.0)) < 1e-12);
    assert(std::abs(x[1] - (1.0 / 3.0)) < 1e-12);
}

void testDenseWithVoltageSource() {
    SimMNAMatrix m;
    m.resize(2, 1); // one node + one ideal voltage source branch current

    // Node 1 to ground through 1k resistor, and ideal source forcing node1=5V.
    m.addG(1, 1, 1.0 / 1000.0);
    m.addB(1, 0, 1.0);
    m.addC(0, 1, 1.0);
    m.addE(0, 5.0);

    const std::vector<double> x = m.solve();
    assert(x.size() == 2);
    const double v1 = x[0];
    const double iV = x[1];
    assert(std::abs(v1 - 5.0) < 1e-12);
    assert(std::abs(std::abs(iV) - 5e-3) < 1e-9);
}

void testSparseMatchesDense() {
    SimMNAMatrix dense;
    SimSparseMatrix sparse;
    dense.resize(4, 0);
    sparse.resize(4, 0);

    auto stamp = [&](int r, int c, double v) {
        dense.addG(r, c, v);
        sparse.addG(r, c, v);
    };

    // 3x3 SPD-like matrix.
    stamp(1, 1, 4.0); stamp(1, 2, -1.0); stamp(1, 3, -1.0);
    stamp(2, 1, -1.0); stamp(2, 2, 4.0); stamp(2, 3, -1.0);
    stamp(3, 1, -1.0); stamp(3, 2, -1.0); stamp(3, 3, 4.0);
    dense.addI(1, 1.0); sparse.addI(1, 1.0);
    dense.addI(2, 2.0); sparse.addI(2, 2.0);
    dense.addI(3, 3.0); sparse.addI(3, 3.0);

    const std::vector<double> xd = dense.solve();
    const std::vector<double> xs = sparse.solve();
    assert(xd.size() == xs.size());
    for (size_t i = 0; i < xd.size(); ++i) {
        assert(std::abs(xd[i] - xs[i]) < 1e-9);
    }
}

void testSingularHandling() {
    SimMNAMatrix m;
    m.resize(2, 0);
    // No stamps => singular.
    const std::vector<double> x = m.solve();
    assert(x.empty());
}

SimResults runDividerWithConvergenceToggles(bool sourceStep, bool gminStep, bool combined) {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

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

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    cfg.useSourceStepping = sourceStep;
    cfg.useGminStepping = gminStep;
    cfg.useCombinedHomotopyStepping = combined;
    cfg.logNRFailureDiagnostics = false;
    netlist.setAnalysis(cfg);

    SimEngine engine;
    return engine.run(netlist);
}

void testConvergenceAidToggleConsistency() {
    const SimResults baseline = runDividerWithConvergenceToggles(true, true, true);
    const SimResults noAids = runDividerWithConvergenceToggles(false, false, false);

    const double vBase = baseline.nodeVoltages.at("N2");
    const double vNoAids = noAids.nodeVoltages.at("N2");
    assert(std::abs(vBase - 5.0) < 1e-12);
    assert(std::abs(vNoAids - 5.0) < 1e-12);
    assert(std::abs(vBase - vNoAids) < 1e-12);
}

} // namespace

int main() {
    testDenseMatrixSolve();
    testDenseWithVoltageSource();
    testSparseMatchesDense();
    testSingularHandling();
    testConvergenceAidToggleConsistency();
    std::cout << "simulator.solver_core: all tests passed" << std::endl;
    return 0;
}
