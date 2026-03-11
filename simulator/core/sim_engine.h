#ifndef SIM_ENGINE_H
#define SIM_ENGINE_H

#include "sim_netlist.h"
#include "sim_matrix.h"
#include "sim_component.h"
#include <map>
#include <string>

struct SimWaveform {
    std::string name;
    std::vector<double> xData; // Time or Frequency
    std::vector<double> yData; // Voltage/Current magnitude
    std::vector<double> yPhase; // Phase (for AC analysis)
};

class SimResults {
public:
    static constexpr int kSchemaVersion = 1;
    int schemaVersion = kSchemaVersion;
    SimAnalysisType analysisType = SimAnalysisType::OP;
    std::string xAxisName = "time_s";
    std::string yAxisName = "value";
    bool isSchemaCompatible() const { return schemaVersion == kSchemaVersion; }

    std::vector<SimWaveform> waveforms;
    std::map<std::string, double> nodeVoltages;
    std::map<std::string, double> branchCurrents;
    std::map<std::string, double> sensitivities; // Component Name -> Delta V / Delta P
    std::map<std::string, double> measurements;  // THD, noise-integrated metrics, etc.
    std::vector<std::string> diagnostics;
    std::vector<std::string> fixSuggestions;

    /**
     * @brief Interpolate all waveforms to get a snapshot at time 't'.
     */
    struct Snapshot {
        std::map<std::string, double> nodeVoltages;
        std::map<std::string, double> branchCurrents;
    };
    Snapshot interpolateAt(double t) const;
};

class SimEngine {
public:
    SimResults run(const SimNetlist& netlist);

private:
    SimResults solveDCOP(const SimNetlist& netlist);
    SimResults solveTransient(const SimNetlist& netlist);
    SimResults solveAC(const SimNetlist& netlist);
    SimResults solveMonteCarlo(const SimNetlist& netlist);
    SimResults solveSensitivity(const SimNetlist& netlist);
    SimResults solveParametricSweep(const SimNetlist& netlist);
    SimResults solveNoise(const SimNetlist& netlist);
    SimResults solveDistortion(const SimNetlist& netlist);
    SimResults solveOptimization(const SimNetlist& netlist);
    SimResults solveFFT(const SimNetlist& netlist);

    // Convergence helpers
    bool solveNR(const SimNetlist& netlist, std::vector<double>& solution, double sourceFactor, double gmin, double t = 0.0);
    bool attemptConvergence(const SimNetlist& netlist, std::vector<double>& solution);
    int assignIndices(SimNetlist& netlist);

    std::map<std::string, int> m_compVIdxMap;
};

#endif // SIM_ENGINE_H
