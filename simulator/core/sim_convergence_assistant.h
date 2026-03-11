#ifndef SIM_CONVERGENCE_ASSISTANT_H
#define SIM_CONVERGENCE_ASSISTANT_H

#include "sim_netlist.h"
#include <string>
#include <vector>

struct SimConvergenceAssistantReport {
    std::vector<std::string> diagnostics;
    std::vector<std::string> suggestions;
};

class SimConvergenceAssistant {
public:
    static SimConvergenceAssistantReport analyze(
        const SimNetlist& netlist,
        const SimAnalysisConfig& config,
        const std::string& failureKind,
        const std::string& context = ""
    );
};

#endif // SIM_CONVERGENCE_ASSISTANT_H
