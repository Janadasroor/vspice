#include "../core/sim_engine.h"
#include "../core/sim_convergence_assistant.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

bool hasToken(const std::vector<std::string>& msgs, const std::string& token) {
    for (const auto& m : msgs) {
        if (m.find(token) != std::string::npos) return true;
    }
    return false;
}

void testOpConvergenceGuidanceForFloatingCircuit() {
    SimNetlist netlist;
    const int n1 = netlist.addNode("N1");
    const int n2 = netlist.addNode("N2");

    // Deliberately floating network: no ground connection.
    SimComponentInstance v1;
    v1.name = "V1";
    v1.type = SimComponentType::VoltageSource;
    v1.nodes = {n1, n2};
    v1.params["voltage"] = 5.0;
    netlist.addComponent(v1);

    SimComponentInstance r1;
    r1.name = "R1";
    r1.type = SimComponentType::Resistor;
    r1.nodes = {n1, n2};
    r1.params["resistance"] = 1000.0;
    netlist.addComponent(r1);

    SimAnalysisConfig cfg;
    cfg.type = SimAnalysisType::OP;
    cfg.absTol = 1e-12;
    cfg.relTol = 1e-6;
    netlist.setAnalysis(cfg);

    const SimConvergenceAssistantReport report =
        SimConvergenceAssistant::analyze(netlist, cfg, "op_convergence_failure");

    assert(!report.diagnostics.empty());
    assert(!report.suggestions.empty());
    std::cout << "Convergence assistant diagnostics: " << report.diagnostics.front() << "\n";
    std::cout << "Convergence assistant first suggestion: " << report.suggestions.front() << "\n";

    const bool mentionsGround = hasToken(report.suggestions, "ground");
    const bool mentionsFloating = hasToken(report.suggestions, "floating");
    assert(mentionsGround || mentionsFloating);
}

} // namespace

int main() {
    testOpConvergenceGuidanceForFloatingCircuit();
    std::cout << "simulator.convergence_assistant: all tests passed\n";
    return 0;
}
