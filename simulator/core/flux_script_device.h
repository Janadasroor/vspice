#ifndef FLUX_SCRIPT_DEVICE_H
#define FLUX_SCRIPT_DEVICE_H

#include "sim_component.h"
#include <string>
#include <map>

#ifdef slots
#undef slots
#endif
#include <pybind11/pybind11.h>
#define slots Q_SLOTS

namespace py = pybind11;

/**
 * @brief Simulator model for a Python-scripted behavioral block.
 */
class FluxScriptModel : public SimComponentModel {
public:
    void stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
               int& vSourceCounter, double sourceFactor = 1.0) override;

    void stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                       double t, double h, const std::vector<double>& prevSolution, 
                       const std::vector<double>& prev2Solution,
                       SimIntegrationMethod method,
                       int& vSourceCounter) override;

    void stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                        const std::vector<double>& solution, double t, int& vSourceCounter) override;

    bool isNonlinear() const override { return true; }

    bool shouldRollback(const SimComponentInstance& inst, double t, double h, 
                       const std::vector<double>& currentSol, 
                       const std::vector<double>& prevSol) override;

    int voltageSourceCount(const SimComponentInstance& inst) const override;

private:
    void ensurePythonInstance(const SimComponentInstance& inst);
    std::map<std::string, py::object> m_instances; 
    std::map<std::string, std::string> m_loadedScripts;
    
    struct CacheEntry {
        double lastTime = -1.0;
        std::map<std::string, double> lastOutputs;
        double lastSignificantValue = 0.0;
    };
    std::map<std::string, CacheEntry> m_caches;
};

#endif // FLUX_SCRIPT_DEVICE_H
