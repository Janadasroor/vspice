#include "sim_convergence_assistant.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

bool isNonlinear(SimComponentType t) {
    return t == SimComponentType::Diode ||
           t == SimComponentType::BJT_NPN ||
           t == SimComponentType::BJT_PNP ||
           t == SimComponentType::MOSFET_NMOS ||
           t == SimComponentType::MOSFET_PMOS ||
           t == SimComponentType::B_VoltageSource ||
           t == SimComponentType::B_CurrentSource ||
           t == SimComponentType::OpAmpMacro ||
           t == SimComponentType::Switch ||
           t == SimComponentType::LOGIC_AND ||
           t == SimComponentType::LOGIC_OR ||
           t == SimComponentType::LOGIC_XOR ||
           t == SimComponentType::LOGIC_NAND ||
           t == SimComponentType::LOGIC_NOR ||
           t == SimComponentType::LOGIC_NOT;
}

void pushUnique(std::vector<std::string>& out, const std::string& msg) {
    if (msg.empty()) return;
    if (std::find(out.begin(), out.end(), msg) == out.end()) out.push_back(msg);
}

} // namespace

SimConvergenceAssistantReport SimConvergenceAssistant::analyze(
    const SimNetlist& netlist,
    const SimAnalysisConfig& config,
    const std::string& failureKind,
    const std::string& context
) {
    SimConvergenceAssistantReport report;

    int nonlinearCount = 0;
    int voltageSourceCount = 0;
    int resistorCount = 0;
    int groundedComponents = 0;
    bool hasFastPulse = false;
    std::unordered_map<int, int> nodeDegree;

    for (const auto& comp : netlist.components()) {
        if (isNonlinear(comp.type)) nonlinearCount++;
        if (comp.type == SimComponentType::VoltageSource) voltageSourceCount++;
        if (comp.type == SimComponentType::Resistor) resistorCount++;

        bool touchesGround = false;
        for (int n : comp.nodes) {
            if (n == 0) touchesGround = true;
            if (n > 0) nodeDegree[n]++;
        }
        if (touchesGround) groundedComponents++;

        if (comp.type == SimComponentType::VoltageSource && comp.params.count("wave_type")) {
            const int w = static_cast<int>(comp.params.at("wave_type"));
            if (w == 2) {
                const double tr = comp.params.count("pulse_tr") ? comp.params.at("pulse_tr") : 1e-9;
                const double tf = comp.params.count("pulse_tf") ? comp.params.at("pulse_tf") : 1e-9;
                if (tr < config.tStep * 0.1 || tf < config.tStep * 0.1) hasFastPulse = true;
            }
        }
    }

    int weakNodes = 0;
    for (const auto& kv : nodeDegree) {
        if (kv.second <= 1) weakNodes++;
    }

    {
        std::ostringstream ss;
        ss << "Convergence assistant failure_kind=" << failureKind
           << ", nonlinear_components=" << nonlinearCount
           << ", weak_nodes=" << weakNodes
           << ", grounded_components=" << groundedComponents;
        if (!context.empty()) ss << ", context=" << context;
        report.diagnostics.push_back(ss.str());
    }

    if (groundedComponents == 0) {
        pushUnique(report.suggestions, "No component is tied to ground; add a clear reference node (0/GND).");
    }
    if (weakNodes > 0) {
        pushUnique(report.suggestions, "Detected likely floating/weakly constrained nodes; check opens and add a DC return path.");
    }
    if (voltageSourceCount > 1 && resistorCount == 0) {
        pushUnique(report.suggestions, "Multiple ideal sources without resistive paths can cause singular constraints; add realistic series resistance.");
    }
    if (nonlinearCount > 0) {
        pushUnique(report.suggestions, "Nonlinear deck detected; increase maxNRIterations and enable gmin/source stepping for robust bias convergence.");
        pushUnique(report.suggestions, "For hard nonlinear cases, reduce nrMaxVoltageStep and increase nrMinDamping.");
    }
    if (failureKind.find("transient") != std::string::npos) {
        pushUnique(report.suggestions, "Transient convergence failed; reduce tStep and cap transientMaxStep.");
        if (hasFastPulse) {
            pushUnique(report.suggestions, "Pulse edges are much faster than timestep; increase pulse rise/fall time or reduce timestep.");
        }
    }
    if (config.absTol < 1e-10 || config.relTol < 1e-5) {
        pushUnique(report.suggestions, "Solver tolerances are very tight; relax absTol/relTol slightly to improve convergence stability.");
    }

    if (report.suggestions.empty()) {
        pushUnique(report.suggestions, "No specific pattern matched; enable NR/homotopy logging and inspect component mapping warnings.");
    }

    return report;
}
