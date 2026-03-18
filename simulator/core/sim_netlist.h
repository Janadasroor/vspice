#ifndef SIM_NETLIST_H
#define SIM_NETLIST_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "sim_results.h"

enum class SimComponentType {
    Resistor, Capacitor, Inductor,
    VoltageSource, CurrentSource,
    Diode, BJT_NPN, BJT_PNP,
    MOSFET_NMOS, MOSFET_PMOS,
    OpAmpMacro, Switch, TransmissionLine, SubcircuitInstance,
    VCVS, VCCS, CCVS, CCCS,
    B_VoltageSource, B_CurrentSource,
    LOGIC_AND, LOGIC_OR, LOGIC_XOR, LOGIC_NAND, LOGIC_NOR, LOGIC_NOT,
    FluxScript
};

struct SimNode {
    int id;
    std::string name;
};

struct SimModel {
    std::string name;
    SimComponentType type;
    std::map<std::string, double> params;
};

enum class ToleranceDistribution {
    Uniform,
    Gaussian,
    WorstCase
};

struct SimTolerance {
    double value = 0.0; // Relative tolerance (e.g. 0.05 for 5%)
    ToleranceDistribution distribution = ToleranceDistribution::Uniform;
    std::string lotId; // Components with same lotId vary together

    bool operator<(const SimTolerance& other) const {
        if (value != other.value) return value < other.value;
        if (distribution != other.distribution) return static_cast<int>(distribution) < static_cast<int>(other.distribution);
        return lotId < other.lotId;
    }
};

struct SimComponentInstance {
    std::string name;
    SimComponentType type;
    std::vector<int> nodes;
    std::map<std::string, double> params;
    std::map<std::string, std::string> paramExpressions; // e.g. "resistance": "{RVAL*1.2}"
    std::map<std::string, SimTolerance> tolerances;
    std::string modelName; // Link to a SimModel
    std::string subcircuitName; // Link to a SimSubcircuit
    int vIdx = -1; // Branch current index in MNA solver

    // Scripted/Programmable Logic (Phase 3.3)
    std::string pythonScript;
    std::vector<std::string> inputPinNames;
    std::vector<std::string> outputPinNames;

    mutable std::shared_ptr<void> runtimeData; // Simulation cache
};

struct SimSubcircuit {
    std::string name;
    std::vector<std::string> pinNames;
    std::vector<SimComponentInstance> components;
    std::map<std::string, SimModel> models;
    std::map<std::string, double> parameters;
};

class SimNetlist {
public:
    SimNetlist();

    int addNode(const std::string& name);
    int groundNode() const { return 0; }
    
    void addComponent(const SimComponentInstance& comp);
    void addModel(const SimModel& model);
    void addSubcircuit(const SimSubcircuit& sub);
    void setAnalysis(const SimAnalysisConfig& config);
    void setParameter(const std::string& name, double value);
    double getParameter(const std::string& name, double defaultVal = 0.0) const;

    void addAutoProbe(const std::string& signalName) { m_autoProbes.push_back(signalName); }
    const std::vector<std::string>& autoProbes() const { return m_autoProbes; }

    void flatten(); // Expands subcircuits into primitive components
    void evaluateExpressions(); // Resolves parameter expressions into numeric values

    int nodeCount() const;
    const std::vector<SimComponentInstance>& components() const { return m_components; }
    std::vector<SimComponentInstance>& mutableComponents() { return m_components; }
    const SimAnalysisConfig& analysis() const { return m_config; }
    int findNode(const std::string& name) const;
    std::string nodeName(int id) const;

    const SimModel* findModel(const std::string& name) const;
    const SimSubcircuit* findSubcircuit(const std::string& name) const;

    const std::map<std::string, SimModel>& models() const { return m_models; }
    std::map<std::string, SimModel>& mutableModels() { return m_models; }
    const std::map<std::string, SimSubcircuit>& subcircuits() const { return m_subcircuits; }

    void addDiagnostic(const std::string& msg) { m_diagnostics.push_back(msg); }
    const std::vector<std::string>& diagnostics() const { return m_diagnostics; }

private:
    std::vector<SimNode> m_nodes;
    std::vector<SimComponentInstance> m_components;
    std::map<std::string, SimModel> m_models;
    std::map<std::string, SimSubcircuit> m_subcircuits;
    std::map<std::string, double> m_parameters;
    std::vector<std::string> m_autoProbes;
    std::vector<std::string> m_diagnostics;
    SimAnalysisConfig m_config;
};

#endif // SIM_NETLIST_H
