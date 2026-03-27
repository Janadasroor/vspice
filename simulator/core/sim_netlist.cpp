#include "sim_netlist.h"
#include "sim_expression.h"
#include <algorithm>
#include <iostream>
#include <functional>
#include <set>

SimNetlist::SimNetlist() {
    // Node 0 is always ground
    m_nodes.push_back({0, "0"});
}

int SimNetlist::addNode(const std::string& name) {
    int existing = findNode(name);
    if (existing != -1) return existing;

    int newId = (int)m_nodes.size();
    m_nodes.push_back({newId, name});
    return newId;
}

void SimNetlist::addComponent(const SimComponentInstance& comp) {
    m_components.push_back(comp);
}

void SimNetlist::setAnalysis(const SimAnalysisConfig& config) {
    m_config = config;
}

int SimNetlist::nodeCount() const {
    return (int)m_nodes.size();
}

int SimNetlist::findNode(const std::string& name) const {
    // SPICE ground aliases - expanded for industry standards
    static const std::set<std::string> gndAliases = {
        "0", "GND", "gnd", "AGND", "agnd", "DGND", "dgnd", "PGND", "pgnd", 
        "VSS", "vss", "COM", "com", "GROUND", "ground"
    };
    
    if (gndAliases.count(name)) return 0;

    for (const auto& node : m_nodes) {
        if (node.name == name) return node.id;
    }
    return -1;
}

std::string SimNetlist::nodeName(int id) const {
    if (id >= 0 && id < (int)m_nodes.size()) return m_nodes[id].name;
    return "?";
}

void SimNetlist::setParameter(const std::string& name, double value) {
    m_parameters[name] = value;
}

double SimNetlist::getParameter(const std::string& name, double defaultVal) const {
    auto it = m_parameters.find(name);
    return (it != m_parameters.end()) ? it->second : defaultVal;
}

void SimNetlist::addModel(const SimModel& model) {
    m_models[model.name] = model;
}

void SimNetlist::addSubcircuit(const SimSubcircuit& sub) {
    m_subcircuits[sub.name] = sub;
}

const SimModel* SimNetlist::findModel(const std::string& name) const {
    auto it = m_models.find(name);
    return (it != m_models.end()) ? &it->second : nullptr;
}

const SimSubcircuit* SimNetlist::findSubcircuit(const std::string& name) const {
    auto it = m_subcircuits.find(name);
    if (it != m_subcircuits.end()) return &it->second;

    // Fallback to case-insensitive lookup to match SPICE's loose case conventions.
    std::string needle = name;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (const auto& [subName, sub] : m_subcircuits) {
        std::string hay = subName;
        std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (hay == needle) return &sub;
    }
    return nullptr;
}

void SimNetlist::flatten() {
    std::vector<SimComponentInstance> flatComponents;
    
    // Recursive expansion function
    // Recursive expansion function
    std::function<void(const std::vector<SimComponentInstance>&, const std::string&, const std::map<int, int>&, std::map<std::string, double>, const SimSubcircuit*, int)> expand;
    
    expand = [&](const std::vector<SimComponentInstance>& comps, const std::string& prefix, const std::map<int, int>& nodeMapping, std::map<std::string, double> scopeParams, const SimSubcircuit* currentSubDef, int depth) {
        if (depth > 50) {
            addDiagnostic("Subcircuit expansion depth limit reached at '" + prefix + "'. Possible circular reference.");
            return;
        }
        for (const auto& inst : comps) {
            if (!inst.subcircuitName.empty()) {
                const SimSubcircuit* sub = findSubcircuit(inst.subcircuitName);
                if (!sub) {
                    addDiagnostic("Subcircuit definition '" + inst.subcircuitName + "' NOT FOUND for component '" + inst.name + "'");
                    continue;
                }

                std::map<std::string, double> subParams = scopeParams;
                for (auto const& [k, v] : sub->parameters) subParams[k] = v;
                for (auto const& [k, v] : inst.params) subParams[k] = v;

                for (auto const& [mName, model] : sub->models) {
                    SimModel globalModel = model;
                    globalModel.name = prefix + inst.name + ":" + mName;
                    addModel(globalModel);
                }

                std::map<int, int> subNodeMapping;
                for (size_t i = 0; i < sub->pinNames.size() && i < inst.nodes.size(); ++i) {
                    subNodeMapping[i + 1] = nodeMapping.at(inst.nodes[i]);
                }
                subNodeMapping[0] = 0;

                std::set<int> subInternalNodes;
                for (const auto& sc : sub->components) {
                    for (int n : sc.nodes) if (n > (int)sub->pinNames.size()) subInternalNodes.insert(n);
                }
                for (int internalNode : subInternalNodes) {
                    std::string newName = prefix + inst.name + ":" + std::to_string(internalNode);
                    subNodeMapping[internalNode] = addNode(newName);
                }

                expand(sub->components, prefix + inst.name + ":", subNodeMapping, subParams, sub, depth + 1);
            } else {
                SimComponentInstance flatInst = inst;
                flatInst.name = prefix + inst.name;
                
                for (size_t i = 0; i < flatInst.nodes.size(); ++i) {
                    if (nodeMapping.count(flatInst.nodes[i])) {
                        flatInst.nodes[i] = nodeMapping.at(flatInst.nodes[i]);
                    }
                }

                // Resolve model name
                if (!flatInst.modelName.empty()) {
                    if (currentSubDef && currentSubDef->models.count(flatInst.modelName)) {
                        // It's a model local to the current subcircuit
                        flatInst.modelName = prefix + flatInst.modelName; 
                    } else if (findModel(flatInst.modelName)) {
                        // It's a global model - leave as is
                    } else {
                        // Warning: model not found anywhere
                        addDiagnostic("Model '" + flatInst.modelName + "' NOT FOUND for component '" + flatInst.name + "'");
                    }
                }

                flatComponents.push_back(flatInst);
            }
        }
    };

    // Initial identity mapping for global nodes
    std::map<int, int> identityMap;
    for (size_t i = 0; i < m_nodes.size(); ++i) identityMap[i] = (int)i;

    expand(m_components, "", identityMap, m_parameters, nullptr, 0);
    m_components = flatComponents;
}

void SimNetlist::evaluateExpressions() {
    for (auto& comp : m_components) {
        for (auto const& [name, exprStr] : comp.paramExpressions) {
            std::string clean = exprStr;
            // Remove curly braces if present
            if (clean.size() >= 2 && clean.front() == '{' && clean.back() == '}') {
                clean = clean.substr(1, clean.size() - 2);
            }
            
            try {
                Sim::Expression expr(clean);
                comp.params[name] = expr.evaluate(m_parameters);
            } catch (const std::exception& e) {
                addDiagnostic("Expression error in " + comp.name + "." + name + ": " + std::string(e.what()));
            }
        }
    }
}
