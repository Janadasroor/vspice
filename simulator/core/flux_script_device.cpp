#include "flux_script_device.h"
#include "sim_matrix.h"
#include "../../core/flux_python.h"
#include "../bridge/flux_script_marshaller.h"
#include <QDebug>

void FluxScriptModel::ensurePythonInstance(const SimComponentInstance& inst) {
    QString code = QString::fromStdString(inst.pythonScript);
    if (code.isEmpty()) {
        code = "class SmartSignal:\n    def update(self, t, inputs):\n        return 0.0";
    }
    const std::string codeText = code.toStdString();

    const bool hasInstance = m_instances.count(inst.name) > 0;
    const auto loadedIt = m_loadedScripts.find(inst.name);
    const bool scriptChanged = (loadedIt == m_loadedScripts.end() || loadedIt->second != codeText);
    if (hasInstance && !scriptChanged) return;

    // Replace stale Python instance when code changed.
    m_instances.erase(inst.name);
    m_caches.erase(inst.name);
    m_loadedScripts.erase(inst.name);

    QString error;
    if (!FluxPython::instance().executeString(code, &error)) {
        qWarning() << "FluxScriptModel: Failed to load code for" << QString::fromStdString(inst.name) << ":" << error;
        return;
    }

    try {
        auto globals = py::globals();
        if (globals.contains("SmartSignal")) {
            m_instances[inst.name] = globals["SmartSignal"]();
            if (py::hasattr(m_instances[inst.name], "init")) {
                m_instances[inst.name].attr("init")();
            }
            m_loadedScripts[inst.name] = codeText;
        }
    } catch (const std::exception& e) {
        qWarning() << "FluxScriptModel: Python init error:" << e.what();
    }
}

int FluxScriptModel::voltageSourceCount(const SimComponentInstance& inst) const {
    // Each output pin is modeled as a dependent voltage source relative to GND
    return static_cast<int>(inst.outputPinNames.size()); 
}

void FluxScriptModel::stamp(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, int& vSourceCounter, double) {
    for (size_t i = 0; i < inst.outputPinNames.size(); ++i) {
        // Output pin is after all input pins in the 'nodes' vector
        size_t nodeIdx = inst.inputPinNames.size() + i;
        if (nodeIdx >= inst.nodes.size()) break;

        int nOut = inst.nodes[nodeIdx];
        int vIdx = vSourceCounter++;
        
        matrix.addB(nOut, vIdx, 1.0);
        matrix.addC(vIdx, nOut, 1.0);
        
        // Add small series resistance to output to prevent MNA singularity
        // Note: done unconditionally to avoid a singular 0=xx row when connected to GND
        matrix.addD(vIdx, vIdx, -1e-3); 
        
        // RHS is set in update() / stampNonlinear()
    }
}

void FluxScriptModel::stampTransient(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst,
                                   double t, double h, const std::vector<double>& prevSolution, 
                                   const std::vector<double>& prev2Solution, SimIntegrationMethod method, int& vSourceCounter) {
    stamp(matrix, netlist, inst, vSourceCounter, 1.0);
}

void FluxScriptModel::stampNonlinear(SimMNAMatrix& matrix, const SimNetlist& netlist, const SimComponentInstance& inst, 
                                   const std::vector<double>& solution, double t, int& vSourceCounter) {
    ensurePythonInstance(inst);
    if (!m_instances.count(inst.name)) {
        vSourceCounter += voltageSourceCount(inst);
        return;
    }

    CacheEntry& cache = m_caches[inst.name];
    std::map<std::string, double> outputs;

    // --- Performance Optimization: Step Caching (Phase 5) ---
    // If we are at the same time point (Newton-Raphson iteration), use cached Python results
    if (std::abs(t - cache.lastTime) < 1e-15 && !cache.lastOutputs.empty()) {
        outputs = cache.lastOutputs;
    } else {
        // 1. Gather inputs from current solution
        std::map<std::string, double> inputs;
        for (size_t i = 0; i < inst.inputPinNames.size(); ++i) {
            if (i >= inst.nodes.size()) break;
            int node = inst.nodes[i];
            double voltage = (node > 0 && (node - 1) < (int)solution.size()) ? solution[node - 1] : 0.0;
            inputs[inst.inputPinNames[i]] = voltage;
        }

        // 2. Call Python update
        QString error;
        py::tuple args = py::make_tuple(t, FluxScriptMarshaller::voltagesToDict(inputs));
        py::object result = FluxPython::instance().safeCall(m_instances[inst.name], "update", args, &error);

        if (!result.is_none()) {
            outputs = FluxScriptMarshaller::pythonToOutputs(result);
            cache.lastOutputs = outputs;
            cache.lastTime = t;
        }
    }

    // 3. Set RHS for each output voltage source
    for (size_t i = 0; i < inst.outputPinNames.size(); ++i) {
        int vIdx = vSourceCounter++;
        double vOut = 0.0;
        
        if (outputs.count(inst.outputPinNames[i])) {
            vOut = outputs[inst.outputPinNames[i]];
        } else if (outputs.count("out") && inst.outputPinNames.size() == 1) {
            vOut = outputs["out"];
        }

        // Boundary Guard
        vOut = std::clamp(vOut, -1000.0, 1000.0);
        matrix.addE(vIdx, vOut);
    }
}

bool FluxScriptModel::shouldRollback(const SimComponentInstance& inst, double t, double h, 
                                   const std::vector<double>& /*currentSol*/, 
                                   const std::vector<double>& /*prevSol*/) {
    // --- Discontinuity Detection: Rollback Logic (Phase 3.2) ---
    if (!m_caches.count(inst.name)) return false;
    
    CacheEntry& cache = m_caches[inst.name];
    if (cache.lastOutputs.empty()) return false;

    // Use the first output as the primary signal for jump detection
    double currentVal = cache.lastOutputs.begin()->second;
    double delta = std::abs(currentVal - cache.lastSignificantValue);
    
    // If signal jumps more than 2.0V in a single step, request a rollback to shrink dt
    if (delta > 2.0 && h > 1e-9) {
        return true; 
    }

    // If step is accepted, update our baseline
    cache.lastSignificantValue = currentVal;
    return false;
}
