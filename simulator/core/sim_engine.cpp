#include "sim_engine.h"
#include "sim_convergence_assistant.h"
#include "sim_expression.h"
#include "sim_math.h"
#include "../../core/diagnostics/runtime_diagnostics.h"
#include <iostream>
#include <random>
#include <cmath>
#include <algorithm>
#include <limits>
#include <atomic>
#include <thread>
#include <fstream>
#include <sstream>
#include <cctype>
#include <unordered_map>

namespace {
bool parseVoltageProbe(const std::string& signal, std::string& nodeOut) {
    if (signal.size() < 4) return false;
    if ((signal[0] == 'V' || signal[0] == 'v') && signal[1] == '(' && signal.back() == ')') {
        nodeOut = signal.substr(2, signal.size() - 3);
        return !nodeOut.empty();
    }
    return false;
}

std::string trimCopy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(start, end - start);
}

std::string uppercaseNoSpaces(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (std::isspace(ch)) continue;
        out.push_back(static_cast<char>(std::toupper(ch)));
    }
    return out;
}

double sampleWaveAtX(const SimWaveform& wave, double x) {
    const size_t n = std::min(wave.xData.size(), wave.yData.size());
    if (n == 0) return 0.0;
    if (n == 1) return wave.yData.front();
    if (x <= wave.xData.front()) return wave.yData.front();
    if (x >= wave.xData[n - 1]) return wave.yData[n - 1];

    auto it = std::lower_bound(wave.xData.begin(), wave.xData.begin() + static_cast<long>(n), x);
    if (it == wave.xData.begin()) return wave.yData.front();
    if (it == wave.xData.begin() + static_cast<long>(n)) return wave.yData[n - 1];

    const size_t i1 = static_cast<size_t>(std::distance(wave.xData.begin(), it));
    const size_t i0 = i1 - 1;
    const double x0 = wave.xData[i0];
    const double x1 = wave.xData[i1];
    const double y0 = wave.yData[i0];
    const double y1 = wave.yData[i1];
    const double dx = x1 - x0;
    if (std::abs(dx) < 1e-18) return y0;
    const double t = (x - x0) / dx;
    return y0 + (y1 - y0) * t;
}

const SimWaveform* findWaveformBySignalName(
    const std::unordered_map<std::string, const SimWaveform*>& waveByKey,
    const std::string& signal) {
    auto it = waveByKey.find(uppercaseNoSpaces(signal));
    if (it != waveByKey.end()) return it->second;
    return nullptr;
}

bool parseDerivativeWrapper(const std::string& expression, std::string& innerExpr) {
    const std::string e = trimCopy(expression);
    if (e.size() < 4) return false;

    size_t open = e.find('(');
    if (open == std::string::npos || e.back() != ')' || open == 0) return false;

    const std::string fn = uppercaseNoSpaces(e.substr(0, open));
    if (fn != "D" && fn != "DDT" && fn != "DERIV" && fn != "DERIVATIVE") return false;

    innerExpr = trimCopy(e.substr(open + 1, e.size() - open - 2));
    return !innerExpr.empty();
}

bool evaluateExpressionWaveform(
    const std::string& expression,
    const std::vector<double>& xAxis,
    const std::unordered_map<std::string, const SimWaveform*>& waveByKey,
    std::vector<double>& yOut,
    std::string& errorOut) {
    Sim::Expression expr(expression);
    if (!expr.isValid()) {
        errorOut = "invalid expression";
        return false;
    }

    std::vector<std::string> vars = expr.getVariables();
    std::vector<const SimWaveform*> varWaves;
    varWaves.reserve(vars.size());
    for (const auto& v : vars) {
        const SimWaveform* w = findWaveformBySignalName(waveByKey, v);
        if (!w) {
            errorOut = "unknown signal '" + v + "'";
            return false;
        }
        varWaves.push_back(w);
    }

    yOut.clear();
    yOut.reserve(xAxis.size());
    std::map<std::string, double> variableValues;
    for (size_t i = 0; i < vars.size(); ++i) variableValues[vars[i]] = 0.0;

    for (double x : xAxis) {
        for (size_t i = 0; i < vars.size(); ++i) {
            variableValues[vars[i]] = sampleWaveAtX(*varWaves[i], x);
        }
        yOut.push_back(expr.evaluate(variableValues));
    }
    return true;
}

void computeWaveformMeasurements(SimResults& results) {
    for (auto& wave : results.waveforms) {
        if (wave.yData.empty()) continue;
        
        double minVal = std::numeric_limits<double>::infinity();
        double maxVal = -std::numeric_limits<double>::infinity();
        double sum = 0;
        
        for (double v : wave.yData) {
            if (v < minVal) minVal = v;
            if (v > maxVal) maxVal = v;
            sum += v;
        }
        
        const std::string prefix = "waveform[" + wave.name + "].";
        results.measurements[prefix + "min"] = minVal;
        results.measurements[prefix + "max"] = maxVal;
        results.measurements[prefix + "avg"] = sum / static_cast<double>(wave.yData.size());
        results.measurements[prefix + "pkpk"] = maxVal - minVal;
    }
}

void appendEquationChannels(const SimNetlist& netlist, SimResults& results) {
    if (results.waveforms.empty() || netlist.autoProbes().empty()) return;

    std::unordered_map<std::string, const SimWaveform*> waveByKey;
    waveByKey.reserve(results.waveforms.size() * 2);
    for (const auto& w : results.waveforms) {
        waveByKey[uppercaseNoSpaces(w.name)] = &w;
    }

    const std::vector<double> referenceX = results.waveforms.front().xData;
    if (referenceX.empty()) return;

    for (const std::string& raw : netlist.autoProbes()) {
        const std::string signal = trimCopy(raw);
        if (signal.empty()) continue;

        const std::string signalKey = uppercaseNoSpaces(signal);
        if (waveByKey.count(signalKey)) continue;

        std::string baseExpr = signal;
        bool derivativeChannel = false;
        std::string derivativeInner;
        if (parseDerivativeWrapper(signal, derivativeInner)) {
            baseExpr = derivativeInner;
            derivativeChannel = true;
        }

        std::vector<double> y;
        std::string error;
        if (!evaluateExpressionWaveform(baseExpr, referenceX, waveByKey, y, error)) {
            results.diagnostics.push_back("Probe expression '" + signal + "' skipped: " + error);
            continue;
        }

        if (derivativeChannel) {
            std::vector<double> dydx(y.size(), 0.0);
            for (size_t i = 0; i < y.size(); ++i) {
                if (y.size() == 1) {
                    dydx[i] = 0.0;
                    break;
                }
                if (i == 0) {
                    const double dx = referenceX[1] - referenceX[0];
                    dydx[i] = (std::abs(dx) > 1e-18) ? (y[1] - y[0]) / dx : 0.0;
                } else if (i + 1 == y.size()) {
                    const double dx = referenceX[i] - referenceX[i - 1];
                    dydx[i] = (std::abs(dx) > 1e-18) ? (y[i] - y[i - 1]) / dx : 0.0;
                } else {
                    const double dx = referenceX[i + 1] - referenceX[i - 1];
                    dydx[i] = (std::abs(dx) > 1e-18) ? (y[i + 1] - y[i - 1]) / dx : 0.0;
                }
            }
            y.swap(dydx);
        }

        SimWaveform derived;
        derived.name = signal;
        derived.xData = referenceX;
        derived.yData = std::move(y);
        results.waveforms.push_back(std::move(derived));

        waveByKey[signalKey] = &results.waveforms.back();
    }
}

void maybeWriteReport(const SimAnalysisConfig& config, const SimResults& results, const std::string& analysisName) {
    if (config.reportFile.empty()) return;
    std::ofstream out(config.reportFile);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"analysis\": \"" << analysisName << "\",\n";
    out << "  \"measurements\": {\n";
    bool first = true;
    for (const auto& [k, v] : results.measurements) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << k << "\": " << v;
    }
    out << "\n  }\n";
    out << "}\n";
}
} // namespace

SimResults::Snapshot SimResults::interpolateAt(double t) const {
    Snapshot snap;
    for (const auto& wave : waveforms) {
        if (wave.xData.empty()) continue;

        double val = 0.0;
        if (t <= wave.xData.front()) {
            val = wave.yData.front();
        } else if (t >= wave.xData.back()) {
            val = wave.yData.back();
        } else {
            // Binary search for interval
            auto it = std::lower_bound(wave.xData.begin(), wave.xData.end(), t);
            size_t i1 = std::distance(wave.xData.begin(), it);
            size_t i0 = i1 - 1;
            
            double x0 = wave.xData[i0];
            double x1 = wave.xData[i1];
            double y0 = wave.yData[i0];
            double y1 = wave.yData[i1];
            
            double factor = (t - x0) / (x1 - x0);
            val = y0 + factor * (y1 - y0);
        }

        // Map to Snapshot
        if (wave.name.size() > 3 && (wave.name.substr(0, 2) == "V(" || wave.name.substr(0, 2) == "v(")) {
            std::string node = wave.name.substr(2, wave.name.size() - 3);
            snap.nodeVoltages[node] = val;
        } else if (wave.name.size() > 3 && (wave.name.substr(0, 2) == "I(" || wave.name.substr(0, 2) == "i(")) {
            std::string branch = wave.name.substr(2, wave.name.size() - 3);
            snap.branchCurrents[branch] = val;
        }
    }
    return snap;
}

SimResults SimEngine::run(const SimNetlist& netlist) {
    FLUX_DIAG_SCOPE("SimEngine::run");
    SimNetlist flatNetlist = netlist;
    flatNetlist.flatten();
    flatNetlist.evaluateExpressions();

    // Resolve model types and merge parameters
    for (auto& comp : flatNetlist.mutableComponents()) {
        if (!comp.modelName.empty()) {
            const SimModel* model = flatNetlist.findModel(comp.modelName);
            if (model) {
                // If the model specifies a type that is compatible with the character prefix, use it.
                // For BJTs and MOSFETs, the model defines NPN vs PNP or NMOS vs PMOS.
                if ((comp.type == SimComponentType::BJT_NPN || comp.type == SimComponentType::BJT_PNP) &&
                    (model->type == SimComponentType::BJT_NPN || model->type == SimComponentType::BJT_PNP)) {
                    comp.type = model->type;
                } else if ((comp.type == SimComponentType::MOSFET_NMOS || comp.type == SimComponentType::MOSFET_PMOS) &&
                           (model->type == SimComponentType::MOSFET_NMOS || model->type == SimComponentType::MOSFET_PMOS)) {
                    comp.type = model->type;
                }

                // Merge model parameters into instance (instance-specific params override model defaults)
                for (const auto& [k, v] : model->params) {
                    if (comp.params.find(k) == comp.params.end()) {
                        comp.params[k] = v;
                    }
                }
            }
        }
    }

    // Canonicalize component order so source indexing and stamping are deterministic
    // even when upstream netlist insertion order differs.
    auto& comps = flatNetlist.mutableComponents();
    std::stable_sort(comps.begin(), comps.end(), [](const SimComponentInstance& a, const SimComponentInstance& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
        if (a.nodes != b.nodes) return a.nodes < b.nodes;
        if (a.modelName != b.modelName) return a.modelName < b.modelName;
        if (a.subcircuitName != b.subcircuitName) return a.subcircuitName < b.subcircuitName;
        if (a.params != b.params) return a.params < b.params;
        return a.tolerances < b.tolerances;
    });

    assignIndices(flatNetlist);

    // Prepare Netlist: Assign vIdx to components that need it
    int totalVIdx = 0;
    for (auto& comp : flatNetlist.mutableComponents()) { // Use flatNetlist here
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) {
            comp.vIdx = totalVIdx;
            totalVIdx += model->voltageSourceCount(comp);
        } else if (comp.type != SimComponentType::SubcircuitInstance) {
            flatNetlist.addDiagnostic("Missing internal model for component '" + comp.name + "' of type " + std::to_string(static_cast<int>(comp.type)));
        }
    }

    SimResults results;
    if (flatNetlist.analysis().type == SimAnalysisType::OP) {
        results = solveDCOP(flatNetlist);
        appendEquationChannels(flatNetlist, results);
        results.analysisType = SimAnalysisType::OP;
        results.xAxisName = "index";
        results.yAxisName = "value";
        computeWaveformMeasurements(results);
    } else {
        switch (flatNetlist.analysis().type) {
            case SimAnalysisType::Transient:
            {
                results = solveTransient(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::Transient;
                results.xAxisName = "time_s";
                results.yAxisName = "value";
                computeWaveformMeasurements(results);
                break;
            }
            case SimAnalysisType::AC:
            {
                results = solveAC(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::AC;
                results.xAxisName = "frequency_hz";
                results.yAxisName = "magnitude";
                computeWaveformMeasurements(results);
                break;
            }
            case SimAnalysisType::MonteCarlo:
            {
                results = solveMonteCarlo(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::MonteCarlo;
                results.xAxisName = "run";
                results.yAxisName = "value";
                computeWaveformMeasurements(results);
                break;
            }
            case SimAnalysisType::Sensitivity:
            {
                results = solveSensitivity(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::Sensitivity;
                results.xAxisName = "component";
                results.yAxisName = "sensitivity";
                break;
            }
            case SimAnalysisType::ParametricSweep:
            {
                results = solveParametricSweep(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::ParametricSweep;
                results.xAxisName = "sweep_point";
                results.yAxisName = "value";
                computeWaveformMeasurements(results);
                break;
            }
            case SimAnalysisType::Noise:
            {
                results = solveNoise(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::Noise;
                results.xAxisName = "frequency_hz";
                results.yAxisName = "noise_density";
                break;
            }
            case SimAnalysisType::Distortion:
            {
                results = solveDistortion(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::Distortion;
                results.xAxisName = "harmonic";
                results.yAxisName = "magnitude";
                break;
            }
            case SimAnalysisType::Optimization:
            {
                results = solveOptimization(flatNetlist);
                appendEquationChannels(flatNetlist, results);
                results.analysisType = SimAnalysisType::Optimization;
                results.xAxisName = "evaluation";
                results.yAxisName = "objective";
                break;
            }
            case SimAnalysisType::FFT:
            {
                results = solveFFT(flatNetlist);
                results.analysisType = SimAnalysisType::FFT;
                results.xAxisName = "frequency_hz";
                results.yAxisName = "magnitude_db";
                break;
            }
            default:
                break;
        }
    }

    // Merge diagnostics from netlist build phase
    for (const auto& d : netlist.diagnostics()) results.diagnostics.push_back(d);
    for (const auto& d : flatNetlist.diagnostics()) results.diagnostics.push_back(d);

    return results;
}

SimResults SimEngine::solveDCOP(const SimNetlist& netlist) {
    SimResults results;
    int nodes = netlist.nodeCount();
    
    // Determine solution vector size
    int vSourceCount = 0;
    for (const auto& comp : netlist.components()) {
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) vSourceCount += model->voltageSourceCount(comp);
    }

    std::vector<double> solution(nodes - 1 + vSourceCount, 0.0);

    if (!attemptConvergence(netlist, solution)) {
        std::cerr << "Simulator: DC OP failed to converge after trying all recovery methods." << std::endl;
        results.measurements["convergence_failed"] = 1.0;
        const auto report = SimConvergenceAssistant::analyze(netlist, netlist.analysis(), "op_convergence_failure");
        results.diagnostics = report.diagnostics;
        results.fixSuggestions = report.suggestions;
    }

    // Map solution back to results
    results.nodeVoltages["0"] = 0.0;
    // Expose DC operating-point values as single-point waveforms so probe widgets
    // and the waveform viewer can display a result in .op mode.
    std::vector<SimWaveform> waveforms;
    for (int i = 1; i < nodes; ++i) {
        const std::string node = netlist.nodeName(i);
        const double v = solution[i - 1];
        results.nodeVoltages[node] = v;
        waveforms.push_back({ "V(" + node + ")", { 0.0 }, { v }, {} });
    }
    results.waveforms = waveforms;

    int vIdx = 0;
    for (const auto& comp : netlist.components()) {
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) {
            int count = model->voltageSourceCount(comp);
            if (count > 0) {
                results.branchCurrents[comp.name] = solution[nodes - 1 + vIdx];
                vIdx += count;
            }
        }
    }

    return results;
}

bool SimEngine::solveNR(const SimNetlist& netlist, std::vector<double>& solution, double sourceFactor, double gmin, double t) {
    const auto& config = netlist.analysis();
    int nodes = netlist.nodeCount();
    bool hasNonlinear = false;
    int vSourceCount = 0;
    for (const auto& comp : netlist.components()) {
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) {
            vSourceCount += model->voltageSourceCount(comp);
            if (model->isNonlinear()) hasNonlinear = true;
        }
    }

    const int maxIter = hasNonlinear ? std::max(1, config.maxNRIterations) : 1;
    const double tol = std::max(1e-15, config.absTol);
    SimSparseMatrix matrix;
    matrix.resize(nodes, vSourceCount);

    double damping = 1.0;
    double prevResidual = std::numeric_limits<double>::infinity();
    std::vector<double> residualTrace;
    residualTrace.reserve(static_cast<size_t>(maxIter));

    for (int iter = 0; iter < maxIter; ++iter) {
        matrix.clear();
        
        // Apply Global Gmin to prevent singular matrices
        for (int i = 1; i < nodes; ++i) {
            matrix.addG(i, i, gmin);
        }

        int vIdx = 0;
        for (const auto& comp : netlist.components()) {
            auto* model = SimComponentFactory::getModel(comp.type);
            if (model) {
                int startVIdx = vIdx;
                model->stamp(matrix, netlist, comp, vIdx, sourceFactor);
                if (model->isNonlinear()) {
                    int nlVIdx = startVIdx;
                    model->stampNonlinear(matrix, netlist, comp, solution, t, nlVIdx);
                    // Apply Gmin across nonlinear nodes
                    for (int n : comp.nodes) {
                        if (n > 0) matrix.addG(n, n, gmin);
                    }
                }
            }
        }

        std::vector<double> newSolution = matrix.solveSparse();
        if (newSolution.empty()) return false;

        double maxRawDelta = 0.0;
        double maxAppliedDelta = 0.0;
        const double vLimit = hasNonlinear ? std::max(0.0, config.nrMaxVoltageStep) : 0.0;

        for (size_t i = 0; i < solution.size(); ++i) {
            const double rawDiff = newSolution[i] - solution[i];
            maxRawDelta = std::max(maxRawDelta, std::abs(rawDiff));

            double diff = rawDiff;
            if (hasNonlinear && vLimit > 0.0 && i < static_cast<size_t>(nodes - 1) && std::abs(diff) > vLimit) {
                diff = (diff > 0) ? vLimit : -vLimit;
            }
            if (hasNonlinear) diff *= damping;

            solution[i] += diff;
            maxAppliedDelta = std::max(maxAppliedDelta, std::abs(diff));
        }

        if (config.logNRIterationProgress && hasNonlinear) {
            std::cerr << "Simulator NR: iter=" << iter
                      << " residual=" << maxRawDelta
                      << " applied_step=" << maxAppliedDelta
                      << " damping=" << damping
                      << " srcFactor=" << sourceFactor
                      << " gmin=" << gmin
                      << " t=" << t << std::endl;
        }

        residualTrace.push_back(maxRawDelta);
        if (maxAppliedDelta < tol || !hasNonlinear) return true;

        // Adaptive damping: back off when residual worsens, relax when it improves.
        if (maxRawDelta > prevResidual * 1.25) {
            damping = std::max(config.nrMinDamping, damping * 0.5);
        } else if (maxRawDelta < prevResidual * 0.5) {
            damping = std::min(1.0, damping * 1.2);
        }
        prevResidual = maxRawDelta;
    }

    if (hasNonlinear && config.logNRFailureDiagnostics) {
        std::cerr << "Simulator NR Failure: maxIter=" << maxIter
                  << " srcFactor=" << sourceFactor
                  << " gmin=" << gmin
                  << " t=" << t
                  << " final_damping=" << damping << std::endl;
        std::cerr << "Simulator NR Residual Trace:";
        const size_t tail = std::min<size_t>(8, residualTrace.size());
        for (size_t i = residualTrace.size() - tail; i < residualTrace.size(); ++i) {
            std::cerr << ' ' << residualTrace[i];
        }
        std::cerr << std::endl;
    }
    return false;
}

bool SimEngine::attemptConvergence(const SimNetlist& netlist, std::vector<double>& solution) {
    const auto& config = netlist.analysis();

    auto logHomotopy = [&](const std::string& msg) {
        if (config.logHomotopyProgress) {
            std::cerr << "Simulator Homotopy: " << msg << std::endl;
        }
    };

    auto trySolveCheckpointed = [&](std::vector<double>& state, double sourceFactor, double gmin, const std::string& tag) {
        std::vector<double> candidate = state;
        const bool ok = solveNR(netlist, candidate, sourceFactor, gmin);
        if (ok) {
            state = std::move(candidate);
            logHomotopy(tag + " success src=" + std::to_string(sourceFactor) + " gmin=" + std::to_string(gmin));
        } else {
            logHomotopy(tag + " fail src=" + std::to_string(sourceFactor) + " gmin=" + std::to_string(gmin));
        }
        return ok;
    };

    const auto clampPositive = [](double value, double fallback) {
        return value > 0.0 ? value : fallback;
    };
    const double gminTarget = clampPositive(config.gmin, 1e-12);
    const double gminStart = std::max(gminTarget, clampPositive(config.gminSteppingStart, 1e-3));

    // 1. Initial attempt
    if (trySolveCheckpointed(solution, 1.0, gminTarget, "direct")) return true;

    // 2. Gmin stepping: fixed sourceFactor=1, decrease gmin from high to target.
    if (config.useGminStepping) {
        std::vector<double> tempSol(solution.size(), 0.0);
        bool stepSuccess = true;

        const int steps = std::max(2, config.gminSteppingSteps);
        for (int i = 0; i < steps; ++i) {
            double alpha = static_cast<double>(i) / static_cast<double>(steps - 1);
            double currentGmin = gminStart * std::pow(gminTarget / gminStart, alpha);
            if (!trySolveCheckpointed(tempSol, 1.0, currentGmin, "gmin-step")) {
                stepSuccess = false;
                break;
            }
        }
        if (stepSuccess) {
            solution = tempSol;
            return true;
        }
    }

    // 3. Source stepping: fixed gmin=target, increase source factor to full scale.
    if (config.useSourceStepping) {
        std::vector<double> tempSol(solution.size(), 0.0);
        double factor = std::clamp(config.sourceSteppingInitial, 0.0, 1.0);
        double step = std::max(config.sourceSteppingMinStep, std::min(config.sourceSteppingInitial, config.sourceSteppingMaxStep));
        bool success = true;

        while (factor <= 1.0) {
            double nextFactor = std::min(1.0, factor + step);
            if (trySolveCheckpointed(tempSol, nextFactor, gminTarget, "source-step")) {
                factor = nextFactor;
                step = std::min(config.sourceSteppingMaxStep, step * 1.5);
                if (factor >= 1.0) break;
            } else {
                step *= 0.5;
                if (step < config.sourceSteppingMinStep) {
                    success = false;
                    break;
                }
            }
        }
        if (success) {
            solution = tempSol;
            return true;
        }
    }

    // 4. Combined homotopy: source up + gmin down at the same time.
    if (config.useCombinedHomotopyStepping && config.useSourceStepping && config.useGminStepping) {
        std::vector<double> tempSol(solution.size(), 0.0);
        bool success = true;
        const int steps = std::max(2, config.combinedHomotopySteps);
        const double sourceStart = std::clamp(config.sourceSteppingInitial, 0.0, 1.0);

        for (int i = 0; i < steps; ++i) {
            double alpha = static_cast<double>(i) / static_cast<double>(steps - 1);
            double sourceFactor = sourceStart + alpha * (1.0 - sourceStart);
            double currentGmin = gminStart * std::pow(gminTarget / gminStart, alpha);
            if (!trySolveCheckpointed(tempSol, sourceFactor, currentGmin, "combined-step")) {
                success = false;
                break;
            }
        }

        // Final verification at exact target params avoids false positives.
        if (success && trySolveCheckpointed(tempSol, 1.0, gminTarget, "combined-final")) {
            solution = tempSol;
            return true;
        }
    }

    if (config.logNRFailureDiagnostics) {
        std::cerr << "Simulator: homotopy fallbacks exhausted without convergence (gmin-target="
                  << gminTarget << ")." << std::endl;
    }

    return false;
}

SimResults SimEngine::solveTransient(const SimNetlist& netlist) {
    SimResults results;
    int nodes = netlist.nodeCount();
    auto config = netlist.analysis();

    // 1. Initial condition: optionally reuse DC OP as .tran starting point.
    SimResults dcOp;
    if (config.transientUseOperatingPointInit) {
        dcOp = solveDCOP(netlist);
    }

    const double simDuration = std::max(1e-15, config.tStop - config.tStart);
    const double baseStep = std::max(1e-15, config.tStep);
    const double hMin = (config.transientMinStep > 0.0) ? config.transientMinStep : std::max(1e-15, baseStep * 1e-7);
    
    // cap hMax more strictly. If baseStep is provided, we should generally not exceed it by much.
    // LTspice style: hMax defaults to tStep if not specified.
    const double hMax = (config.transientMaxStep > 0.0) ? config.transientMaxStep : baseStep;

    double t = config.tStart;
    double h = std::clamp(baseStep, hMin, hMax);
    int rejectStreak = 0;
    
    int vSourceCount = 0;
    for (const auto& comp : netlist.components()) {
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) vSourceCount += model->voltageSourceCount(comp);
    }
    
    std::vector<double> prevSolution(nodes - 1 + vSourceCount, 0.0);
    // Initialize node voltages from DC OP.
    if (config.transientUseOperatingPointInit) {
        for (int i = 1; i < nodes; ++i) {
            std::string name = netlist.nodeName(i);
            if (dcOp.nodeVoltages.count(name)) {
                prevSolution[i - 1] = dcOp.nodeVoltages.at(name);
            }
        }
    }

    // Initialize branch-current state from DC OP for components that add branch equations
    // (critical for inductor companion model handoff).
    if (config.transientUseOperatingPointInit) {
        for (const auto& comp : netlist.components()) {
            auto* model = SimComponentFactory::getModel(comp.type);
            if (!model || model->voltageSourceCount(comp) <= 0 || comp.vIdx < 0) continue;
            const auto it = dcOp.branchCurrents.find(comp.name);
            if (it == dcOp.branchCurrents.end()) continue;

            const int idx = nodes - 1 + comp.vIdx;
            if (idx >= 0 && idx < static_cast<int>(prevSolution.size())) {
                prevSolution[idx] = it->second;
            }
        }
    }

    // Explicit device initial-condition overrides take precedence when provided.
    if (config.transientUseDeviceInitialConditions) {
        for (const auto& comp : netlist.components()) {
            if (comp.type == SimComponentType::Inductor && comp.params.count("ic") && comp.vIdx >= 0) {
                const int idx = nodes - 1 + comp.vIdx;
                if (idx >= 0 && idx < static_cast<int>(prevSolution.size())) {
                    prevSolution[idx] = comp.params.at("ic");
                }
            }

            if (comp.type == SimComponentType::Capacitor && comp.params.count("ic") && comp.nodes.size() >= 2) {
                const int n1 = comp.nodes[0];
                const int n2 = comp.nodes[1];
                const double vic = comp.params.at("ic");
                if (n1 > 0 && n2 == 0) prevSolution[n1 - 1] = vic;
                if (n2 > 0 && n1 == 0) prevSolution[n2 - 1] = -vic;
            }
        }
    }

    // Prepare waveforms
    std::vector<SimWaveform> waveforms;
    const int estimatedAcceptedSteps =
        std::max(2, static_cast<int>(std::ceil((config.tStop - config.tStart) / std::max(1e-15, baseStep))) + 2);
    int reservePoints = estimatedAcceptedSteps;
    if (config.transientStorageMode == SimTransientStorageMode::Strided) {
        const int stride = std::max(1, config.transientStoreStride);
        reservePoints = std::max(2, estimatedAcceptedSteps / stride + 2);
    }
    if (config.transientMaxStoredPoints > 0) {
        reservePoints = std::min(reservePoints, std::max(2, config.transientMaxStoredPoints));
    }
    for (int i = 1; i < nodes; ++i) {
        SimWaveform w{ "V(" + netlist.nodeName(i) + ")", { t }, { prevSolution[i-1] } };
        w.xData.reserve(static_cast<size_t>(reservePoints));
        w.yData.reserve(static_cast<size_t>(reservePoints));
        waveforms.push_back(std::move(w));
    }

    auto compactWaveformsByHalf = [&]() {
        for (auto& wave : waveforms) {
            const size_t n = std::min(wave.xData.size(), wave.yData.size());
            if (n <= 2) continue;

            std::vector<double> x2;
            std::vector<double> y2;
            x2.reserve((n + 1) / 2 + 1);
            y2.reserve((n + 1) / 2 + 1);

            x2.push_back(wave.xData.front());
            y2.push_back(wave.yData.front());
            for (size_t i = 1; i + 1 < n; i += 2) {
                x2.push_back(wave.xData[i]);
                y2.push_back(wave.yData[i]);
            }
            if (x2.back() != wave.xData[n - 1]) {
                x2.push_back(wave.xData[n - 1]);
                y2.push_back(wave.yData[n - 1]);
            }

            wave.xData.swap(x2);
            wave.yData.swap(y2);
        }
    };

    auto trimWaveformsToCap = [&](size_t capPoints) {
        for (auto& wave : waveforms) {
            const size_t n = std::min(wave.xData.size(), wave.yData.size());
            if (n <= capPoints) continue;
            const size_t drop = n - capPoints;
            const auto d = static_cast<std::vector<double>::difference_type>(drop);
            wave.xData.erase(wave.xData.begin(), wave.xData.begin() + d);
            wave.yData.erase(wave.yData.begin(), wave.yData.begin() + d);
        }
    };

    auto detectMixedSignalEvent = [&](const std::vector<double>& prev, const std::vector<double>& cur) -> bool {
        const double low = config.digitalThresholdLow;
        const double high = config.digitalThresholdHigh;
        const double mid = 0.5 * (low + high);
        const double nearBand = std::max(1e-6, std::abs(high - low) * 0.25);

        auto nodeV = [](int n, const std::vector<double>& s) -> double {
            return (n > 0 && n - 1 < static_cast<int>(s.size())) ? s[n - 1] : 0.0;
        };

        for (const auto& comp : netlist.components()) {
            const bool isLogic =
                comp.type == SimComponentType::LOGIC_AND ||
                comp.type == SimComponentType::LOGIC_OR ||
                comp.type == SimComponentType::LOGIC_XOR ||
                comp.type == SimComponentType::LOGIC_NAND ||
                comp.type == SimComponentType::LOGIC_NOR ||
                comp.type == SimComponentType::LOGIC_NOT;
            if (!isLogic || comp.nodes.size() < 2) continue;

            const int inputCount = static_cast<int>(comp.nodes.size()) - 1;
            for (int i = 0; i < inputCount; ++i) {
                const int n = comp.nodes[static_cast<size_t>(i)];
                const double vp = nodeV(n, prev);
                const double vc = nodeV(n, cur);
                const bool crossedMid = ((vp < mid && vc >= mid) || (vp > mid && vc <= mid));
                const bool nearThreshold = (std::abs(vc - low) <= nearBand) || (std::abs(vc - high) <= nearBand);
                if (crossedMid || nearThreshold) return true;
            }
        }
        return false;
    };
    
    std::vector<double> prev2Solution;
    std::vector<double> currentSolution = prevSolution;
    size_t acceptedSteps = 0;
    bool transientConvergenceFailure = false;
    std::string transientFailureContext;

    // Time loop
    SimMNAMatrix matrix;
    matrix.resize(nodes, vSourceCount);
    while (t < config.tStop) {
        if (t + h > config.tStop) h = config.tStop - t;
        bool mixedSignalEventCapped = false;

        bool converged = false;
        std::vector<double> trialSolution = prevSolution;
        // Newton-Raphson for current step
        for (int iter = 0; iter < std::max(1, config.maxNRIterations); ++iter) {
            matrix.clear();
            
            // Apply Global Gmin
            for (int i = 1; i < nodes; ++i) {
                matrix.addG(i, i, config.gmin);
            }

            int vIdx = 0;
            for (const auto& comp : netlist.components()) {
                auto* model = SimComponentFactory::getModel(comp.type);
                if (model) {
                    int compVIdx = vIdx;
                    model->stampTransient(matrix, netlist, comp, t + h, h, prevSolution, prev2Solution, config.integrationMethod, vIdx); 
                    if (model->isNonlinear()) {
                        model->stampNonlinear(matrix, netlist, comp, trialSolution, t + h, compVIdx);
                    }
                }
            }
            
            std::vector<double> nextGuess = matrix.solve();
            if (nextGuess.empty()) break;
            
            double maxError = 0;
            for (size_t i = 0; i < trialSolution.size(); ++i) {
                maxError = std::max(maxError, std::abs(nextGuess[i] - trialSolution[i]));
            }

            trialSolution = nextGuess;
            if (maxError < std::max(1e-12, config.absTol)) {
                converged = true;
                break;
            }
        }

        if (!converged) {
            // Convergence failed, reduce timestep and retry from last accepted solution.
            const double shrink = std::clamp(0.25, config.transientMinShrink, 0.5);
            h *= shrink;
            rejectStreak++;
            currentSolution = prevSolution;
            if (config.logTransientStepControl) {
                std::cerr << "Simulator TRAN: reject(nonconverged) t=" << t << " new_h=" << h
                          << " streak=" << rejectStreak << std::endl;
            }
            if (h < hMin) {
                std::cerr << "Transient simulation failed to converge at t=" << t
                          << " (step dropped below hMin=" << hMin << ")" << std::endl;
                transientConvergenceFailure = true;
                transientFailureContext = "step below minimum at t=" + std::to_string(t);
                break;
            }
            if (rejectStreak > config.transientMaxRejects) {
                std::cerr << "Transient simulation aborted at t=" << t
                          << " after too many rejected steps (" << rejectStreak << ")" << std::endl;
                transientConvergenceFailure = true;
                transientFailureContext = "too many nonconverged rejects at t=" + std::to_string(t);
                break;
            }
            continue;
        }

        currentSolution = trialSolution;

        // Step converged, estimate normalized error for adaptive control.
        bool stepAccepted = true;
        double ratio = 1.0;
        double maxErrorFactor = 0.0;

        for (size_t i = 0; i < currentSolution.size(); ++i) {
            const double cur = currentSolution[i];
            const double prev = prevSolution[i];
            const double scale = std::max({1.0, std::abs(cur), std::abs(prev)});
            const double tol = config.absTol + config.relTol * scale;
            if (tol > 0.0) {
                maxErrorFactor = std::max(maxErrorFactor, std::abs(cur - prev) / tol);
            }
        }

        if (!prev2Solution.empty()) {
            int vIdxLTE = 0;
            for (const auto& comp : netlist.components()) {
                auto* model = SimComponentFactory::getModel(comp.type);
                if (!model) continue;
                const double lte = model->calculateLTE(comp, h, currentSolution, prevSolution, prev2Solution, nodes, vIdxLTE);
                if (lte <= 0.0) continue;
                const double tol = config.absTol + config.relTol;
                if (tol > 0.0) {
                    maxErrorFactor = std::max(maxErrorFactor, lte / tol);
                }
            }
        }

        if (config.useAdaptiveStep) {
            // Check for component-level rollback requests (e.g. from FluxScript)
            bool componentRequestedRollback = false;
            for (const auto& comp : netlist.components()) {
                auto* model = SimComponentFactory::getModel(comp.type);
                if (model && model->shouldRollback(comp, t + h, h, currentSolution, prevSolution)) {
                    componentRequestedRollback = true;
                    break;
                }
            }

            if (maxErrorFactor > 1.0 || componentRequestedRollback) {
                stepAccepted = false;
                if (componentRequestedRollback) {
                    ratio = 0.5; // Aggressive shrink on scripted discontinuity
                } else {
                    ratio = 0.9 * std::pow(1.0 / std::max(maxErrorFactor, 1e-12), 0.5);
                }
                ratio = std::clamp(ratio, config.transientMinShrink, 0.9);
            } else {
                if (maxErrorFactor > 0.0) {
                    ratio = 0.9 * std::pow(1.0 / maxErrorFactor, 0.5);
                } else {
                    ratio = config.transientMaxGrowth;
                }
                ratio = std::clamp(ratio, 1.05, config.transientMaxGrowth);
            }
        }

        if (stepAccepted) {
            rejectStreak = 0;
            t += h;
            acceptedSteps++;
            const bool mixedSignalEvent = config.mixedSignalEnableEventRefinement &&
                                          detectMixedSignalEvent(prevSolution, currentSolution);

            bool shouldStore = true;
            if (config.transientStorageMode == SimTransientStorageMode::Strided) {
                const size_t stride = static_cast<size_t>(std::max(1, config.transientStoreStride));
                shouldStore = (((acceptedSteps - 1) % stride) == 0) || (t >= config.tStop);
            }

            if (shouldStore) {
                for (int i = 1; i < nodes; ++i) {
                    waveforms[i - 1].xData.push_back(t);
                    waveforms[i - 1].yData.push_back(currentSolution[i - 1]);
                }

                if (config.transientMaxStoredPoints > 0 && !waveforms.empty()) {
                    const size_t cap = static_cast<size_t>(std::max(2, config.transientMaxStoredPoints));
                    while (!waveforms.empty() && waveforms.front().xData.size() > cap) {
                        if (config.transientStorageMode == SimTransientStorageMode::AutoDecimate ||
                            config.transientStorageMode == SimTransientStorageMode::Strided) {
                            compactWaveformsByHalf();
                        } else {
                            trimWaveformsToCap(cap);
                            break;
                        }
                    }
                }
            }
            prev2Solution = prevSolution;
            prevSolution = currentSolution;

            if (mixedSignalEvent) {
                const double eventH = std::max(hMin, config.mixedSignalEventStep);
                h = std::min(h, eventH);
                mixedSignalEventCapped = true;
                if (config.mixedSignalLogEvents) {
                    std::cerr << "Simulator TRAN MixedSignal: event near threshold at t=" << t
                              << ", next_h capped to " << h << std::endl;
                }
            }
        } else {
            rejectStreak++;
            currentSolution = prevSolution;
            if (config.logTransientStepControl) {
                std::cerr << "Simulator TRAN: reject(error) t=" << t << " h=" << h
                          << " err=" << maxErrorFactor << " ratio=" << ratio
                          << " streak=" << rejectStreak << std::endl;
            }
            if (rejectStreak > config.transientMaxRejects) {
                std::cerr << "Transient simulation aborted at t=" << t
                          << " after too many LTE rejections (" << rejectStreak << ")" << std::endl;
                transientConvergenceFailure = true;
                transientFailureContext = "too many LTE rejects at t=" + std::to_string(t);
                break;
            }
        }

        if (config.useAdaptiveStep) {
            h *= ratio;
            if (h > hMax) h = hMax;
            if (h < hMin) h = hMin;
            if (mixedSignalEventCapped) {
                h = std::min(h, std::max(hMin, config.mixedSignalEventStep));
            }
            if (config.logTransientStepControl && stepAccepted) {
                std::cerr << "Simulator TRAN: accept t=" << t << " err=" << maxErrorFactor
                          << " next_h=" << h << std::endl;
            }
        } else {
            h = std::clamp(baseStep, hMin, hMax);
            if (mixedSignalEventCapped) {
                h = std::min(h, std::max(hMin, config.mixedSignalEventStep));
            }
        }
    }

    results.waveforms = waveforms;
    if (transientConvergenceFailure) {
        results.measurements["convergence_failed"] = 1.0;
        const auto report = SimConvergenceAssistant::analyze(
            netlist, netlist.analysis(), "transient_convergence_failure", transientFailureContext);
        results.diagnostics = report.diagnostics;
        results.fixSuggestions = report.suggestions;
    }
    return results;
}

SimResults SimEngine::solveAC(const SimNetlist& netlist) {
    SimResults results;
    int nodes = netlist.nodeCount();
    auto config = netlist.analysis();

    int vSourceCount = 0;
    for (const auto& comp : netlist.components()) {
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) vSourceCount += model->voltageSourceCount(comp);
    }

    // Prepare waveforms
    std::vector<SimWaveform> waveforms;
    for (int i = 1; i < nodes; ++i) {
        waveforms.push_back({ "V(" + netlist.nodeName(i) + ")", {}, {}, {} });
    }

    double f = config.fStart;
    double logStep = std::pow(config.fStop / config.fStart, 1.0 / (config.fPoints - 1));
    SimSparseComplexMatrix matrix;
    matrix.resize(nodes, vSourceCount);

    for (int p = 0; p < config.fPoints; ++p) {
        double omega = 2.0 * M_PI * f;
        matrix.clear();

        int vIdx = 0;
        for (const auto& comp : netlist.components()) {
            auto* model = SimComponentFactory::getModel(comp.type);
            if (model) {
                model->stampAC(matrix, netlist, comp, omega, vIdx);
            }
        }

        std::vector<std::complex<double>> solution = matrix.solveSparse();
        if (solution.empty()) break;

        // Record data (magnitude and phase)
        for (int i = 1; i < nodes; ++i) {
            std::complex<double> v = solution[i - 1];
            waveforms[i - 1].xData.push_back(f);
            waveforms[i - 1].yData.push_back(std::abs(v));
            waveforms[i - 1].yPhase.push_back(std::arg(v) * 180.0 / M_PI);
        }

        if (config.fPoints > 1) f *= logStep;
        else break;
    }

    results.waveforms = waveforms;
    return results;
}

SimResults SimEngine::solveMonteCarlo(const SimNetlist& netlist) {
    SimResults aggregateResults;
    auto config = netlist.analysis();
    
    std::mt19937 gen(42); 
    std::uniform_real_distribution<> uniformDis(-1.0, 1.0);
    std::normal_distribution<> gaussianDis(0.0, 1.0);

    for (int mc_run = 0; mc_run < config.mcRuns; ++mc_run) {
        SimNetlist perturbed = netlist;
        
        // Map LotId -> variation factor
        std::map<std::string, double> lotVariations;

        auto& components = perturbed.mutableComponents();
        for (auto& comp : components) {
            for (auto const& [param, tol] : comp.tolerances) {
                if (comp.params.count(param)) {
                    double variationFactor = 0.0;
                    
                    if (!tol.lotId.empty()) {
                        if (lotVariations.count(tol.lotId)) {
                            variationFactor = lotVariations[tol.lotId];
                        } else {
                            if (tol.distribution == ToleranceDistribution::Gaussian) variationFactor = gaussianDis(gen);
                            else variationFactor = uniformDis(gen);
                            lotVariations[tol.lotId] = variationFactor;
                        }
                    } else {
                        if (tol.distribution == ToleranceDistribution::Gaussian) variationFactor = gaussianDis(gen);
                        else variationFactor = uniformDis(gen);
                    }

                    double variation = 1.0 + variationFactor * tol.value;
                    comp.params[param] *= variation;
                }
            }
        }

        SimAnalysisConfig baseConfig = config;
        baseConfig.type = config.mcBaseAnalysis;
        perturbed.setAnalysis(baseConfig);

        SimResults runResults = this->run(perturbed);

        if (baseConfig.type == SimAnalysisType::OP) {
            for (const auto& [name, val] : runResults.nodeVoltages) {
                bool found = false;
                for (auto& wave : aggregateResults.waveforms) {
                    if (wave.name == "MC_V(" + name + ")") {
                        wave.xData.push_back(mc_run);
                        wave.yData.push_back(val);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    aggregateResults.waveforms.push_back({ "MC_V(" + name + ")", {(double)mc_run}, {val}, {} });
                }
            }
        } else {
            for (auto& wave : runResults.waveforms) {
                wave.name += " [Run " + std::to_string(mc_run) + "]";
                aggregateResults.waveforms.push_back(wave);
            }
        }
    }

    return aggregateResults;
}

SimResults SimEngine::solveSensitivity(const SimNetlist& netlist) {
    SimResults results;
    auto config = netlist.analysis();
    std::string target = config.sensitivityTargetSignal;
    if (target.empty()) return results;

    // 1. Initial baseline analysis
    SimResults baseline = solveDCOP(netlist);
    double baselineVal = 0;

    // Parse target (e.g. V(N2))
    if (target.find("V(") == 0 && target.back() == ')') {
        std::string nodeName = target.substr(2, target.size() - 3);
        if (baseline.nodeVoltages.count(nodeName)) {
            baselineVal = baseline.nodeVoltages[nodeName];
        }
    }

    // 2. Perturb each component
    double epsilon = 1e-4; // 0.01% perturbation

    for (const auto& comp : netlist.components()) {
        SimNetlist perturbed = netlist;
        
        auto& components = perturbed.mutableComponents();
        for (auto& pComp : components) {
            if (pComp.name == comp.name) {
                // Perturb the primary parameter (e.g. resistance)
                std::string param = "resistance";
                if (pComp.type == SimComponentType::Capacitor) param = "capacitance";
                else if (pComp.type == SimComponentType::Inductor) param = "inductance";
                else if (pComp.type == SimComponentType::VoltageSource) param = "voltage";
                else if (pComp.type == SimComponentType::CurrentSource) param = "current";

                if (pComp.params.count(param)) {
                    double original = pComp.params[param];
                    double delta = std::abs(original) * epsilon + 1e-9;
                    pComp.params[param] += delta;

                    SimResults pertResults = solveDCOP(perturbed);
                    double pertVal = 0;
                    if (target.find("V(") == 0) {
                        std::string nodeName = target.substr(2, target.size() - 3);
                        if (pertResults.nodeVoltages.count(nodeName)) pertVal = pertResults.nodeVoltages[nodeName];
                    }

                    double sensitivity = (pertVal - baselineVal) / delta;
                    results.sensitivities[comp.name] = sensitivity;
                }
                break;
            }
        }
    }

    return results;
}

int SimEngine::assignIndices(SimNetlist& netlist) {
    int vIdx = 0;
    m_compVIdxMap.clear();
    for (auto& comp : netlist.mutableComponents()) {
        auto* model = SimComponentFactory::getModel(comp.type);
        if (model) {
            int count = model->voltageSourceCount(comp);
            if (count > 0) {
                comp.vIdx = vIdx;
                m_compVIdxMap[comp.name] = vIdx;
            } else {
                comp.vIdx = -1;
            }
            vIdx += count;
        } else {
            comp.vIdx = -1;
        }
    }
    return vIdx;
}

SimResults SimEngine::solveParametricSweep(const SimNetlist& netlist) {
    SimResults aggregateResults;
    auto config = netlist.analysis();
    
    // Parse sweepParam (e.g., "R1.resistance")
    size_t dotPos = config.sweepParam.find('.');
    if (dotPos == std::string::npos) return aggregateResults;

    const std::string compName = config.sweepParam.substr(0, dotPos);
    const std::string paramName = config.sweepParam.substr(dotPos + 1);
    const int points = std::max(1, config.sweepPoints);

    std::vector<double> sweepValues;
    sweepValues.reserve(static_cast<size_t>(points));
    if (config.sweepLog && config.sweepStart > 0.0 && config.sweepStop > 0.0 && points > 1) {
        const double logStep = std::pow(config.sweepStop / config.sweepStart, 1.0 / static_cast<double>(points - 1));
        double val = config.sweepStart;
        for (int i = 0; i < points; ++i) {
            sweepValues.push_back(val);
            val *= logStep;
        }
    } else {
        const double step = (points > 1) ? (config.sweepStop - config.sweepStart) / static_cast<double>(points - 1) : 0.0;
        for (int i = 0; i < points; ++i) {
            sweepValues.push_back(config.sweepStart + step * static_cast<double>(i));
        }
    }

    const auto runSweepPoint = [&](SimEngine& localEngine, int idx, double val, SimResults& out) -> bool {
        (void)idx;
        SimNetlist perturbed = netlist;
        auto& components = perturbed.mutableComponents();

        bool found = false;
        for (auto& comp : components) {
            if (comp.name == compName) {
                comp.params[paramName] = val;
                found = true;
                break;
            }
        }
        if (!found) return false;

        SimAnalysisConfig baseCfg = config;
        baseCfg.type = SimAnalysisType::OP;
        perturbed.setAnalysis(baseCfg);

        out = localEngine.run(perturbed);
        return true;
    };

    std::vector<SimResults> pointResults(static_cast<size_t>(points));
    std::vector<bool> ok(static_cast<size_t>(points), false);

    int workers = config.sweepParallelism;
    if (workers <= 0) {
        workers = static_cast<int>(std::thread::hardware_concurrency());
        if (workers <= 0) workers = 2;
    }
    workers = std::max(1, std::min(workers, points));

    if (workers == 1) {
        SimEngine serialEngine;
        for (int i = 0; i < points; ++i) {
            ok[static_cast<size_t>(i)] =
                runSweepPoint(serialEngine, i, sweepValues[static_cast<size_t>(i)], pointResults[static_cast<size_t>(i)]);
        }
    } else {
        std::atomic<int> next{0};
        std::vector<std::thread> pool;
        pool.reserve(static_cast<size_t>(workers));
        for (int w = 0; w < workers; ++w) {
            pool.emplace_back([&]() {
                SimEngine workerEngine;
                while (true) {
                    const int i = next.fetch_add(1);
                    if (i >= points) break;
                    ok[static_cast<size_t>(i)] =
                        runSweepPoint(workerEngine, i, sweepValues[static_cast<size_t>(i)], pointResults[static_cast<size_t>(i)]);
                }
            });
        }
        for (auto& th : pool) th.join();
    }

    for (int i = 0; i < points; ++i) {
        if (!ok[static_cast<size_t>(i)]) break;
        const double val = sweepValues[static_cast<size_t>(i)];
        for (auto wave : pointResults[static_cast<size_t>(i)].waveforms) {
            wave.name += " [" + config.sweepParam + "=" + std::to_string(val) + "]";
            aggregateResults.waveforms.push_back(std::move(wave));
        }
    }

    return aggregateResults;
}

SimResults SimEngine::solveNoise(const SimNetlist& netlist) {
    SimResults results;
    const auto cfg = netlist.analysis();
    const double kBoltzmann = 1.380649e-23;
    const double tempK = std::max(1.0, cfg.noiseTemperatureK);

    std::string outNode;
    if (!cfg.noiseOutputSignal.empty()) {
        (void)parseVoltageProbe(cfg.noiseOutputSignal, outNode);
    }
    if (outNode.empty() && netlist.nodeCount() > 1) {
        outNode = netlist.nodeName(1);
    }
    const int outNodeId = outNode.empty() ? -1 : netlist.findNode(outNode);

    const int points = std::max(1, cfg.fPoints);
    const double fStart = std::max(1.0, cfg.fStart);
    const double fStop = std::max(fStart, cfg.fStop);
    const double logStep = (points > 1) ? std::pow(fStop / fStart, 1.0 / static_cast<double>(points - 1)) : 1.0;

    SimWaveform onoise;
    onoise.name = outNode.empty() ? "ONOISE" : ("ONOISE(V(" + outNode + "))");
    SimWaveform inoise;
    if (!cfg.noiseInputSource.empty()) {
        inoise.name = "INOISE(" + cfg.noiseInputSource + ")";
    }

    double totalPSD = 0.0;
    for (const auto& comp : netlist.components()) {
        if (comp.type != SimComponentType::Resistor || comp.nodes.size() < 2) continue;
        if (outNodeId <= 0) continue;

        const int n1 = comp.nodes[0];
        const int n2 = comp.nodes[1];
        if (n1 != outNodeId && n2 != outNodeId) continue;

        double r = comp.params.count("resistance") ? std::abs(comp.params.at("resistance")) : 0.0;
        if (r < 1e-9) r = 1e-9;

        // First-order model:
        // - Direct shunt to ground contributes full 4kTR (V^2/Hz) at node.
        // - Non-shunt resistor contributes reduced local share.
        const bool shuntToGround = ((n1 == outNodeId && n2 == 0) || (n2 == outNodeId && n1 == 0));
        const double scale = shuntToGround ? 1.0 : 0.25;
        totalPSD += scale * (4.0 * kBoltzmann * tempK * r);
    }

    double inputAcMag = 1.0;
    if (!cfg.noiseInputSource.empty()) {
        for (const auto& comp : netlist.components()) {
            if (comp.name == cfg.noiseInputSource && comp.type == SimComponentType::VoltageSource) {
                inputAcMag = comp.params.count("ac_mag") ? std::max(1e-12, std::abs(comp.params.at("ac_mag"))) : 1.0;
                break;
            }
        }
    }

    double f = fStart;
    double integratedNoisePower = 0.0;
    double prevF = fStart;
    double prevDens = std::sqrt(std::max(0.0, totalPSD));
    for (int i = 0; i < points; ++i) {
        const double dens = std::sqrt(std::max(0.0, totalPSD));
        onoise.xData.push_back(f);
        onoise.yData.push_back(dens);
        if (!inoise.name.empty()) {
            inoise.xData.push_back(f);
            inoise.yData.push_back(dens / inputAcMag);
        }

        if (i > 0) {
            const double df = f - prevF;
            integratedNoisePower += 0.5 * (prevDens * prevDens + dens * dens) * df;
        }
        prevF = f;
        prevDens = dens;
        f *= logStep;
    }

    results.waveforms.push_back(std::move(onoise));
    if (!inoise.name.empty()) results.waveforms.push_back(std::move(inoise));
    results.measurements["onoise_rms_v"] = std::sqrt(std::max(0.0, integratedNoisePower));
    if (!cfg.noiseInputSource.empty()) {
        results.measurements["inoise_rms_v"] = results.measurements["onoise_rms_v"] / std::max(1e-12, inputAcMag);
    }
    maybeWriteReport(cfg, results, "noise");
    return results;
}

SimResults SimEngine::solveDistortion(const SimNetlist& netlist) {
    SimResults results;
    auto cfg = netlist.analysis();

    const double fundamentalHz = std::max(1.0, cfg.thdFundamentalHz);
    const int harmonics = std::max(2, cfg.thdHarmonics);
    const int skipCycles = std::max(0, cfg.thdSkipCycles);
    const int cycles = std::max(2, cfg.thdCycles);
    const double period = 1.0 / fundamentalHz;

    SimNetlist transientNetlist = netlist;
    SimAnalysisConfig tcfg = cfg;
    tcfg.type = SimAnalysisType::Transient;
    if (tcfg.tStep <= 0.0) tcfg.tStep = period / 200.0;
    tcfg.tStart = 0.0;
    tcfg.tStop = static_cast<double>(skipCycles + cycles) * period;
    transientNetlist.setAnalysis(tcfg);

    const SimResults tr = solveTransient(transientNetlist);
    if (tr.waveforms.empty()) return results;

    std::string targetName = cfg.thdTargetSignal;
    if (targetName.empty()) {
        targetName = tr.waveforms.front().name;
    } else {
        std::string node;
        if (parseVoltageProbe(targetName, node)) {
            targetName = "V(" + node + ")";
        }
    }

    const SimWaveform* target = nullptr;
    for (const auto& w : tr.waveforms) {
        if (w.name == targetName) {
            target = &w;
            break;
        }
    }
    if (!target) return results;
    if (target->xData.size() < 32 || target->yData.size() < 32) return results;

    const double t0 = static_cast<double>(skipCycles) * period;
    const double t1 = static_cast<double>(skipCycles + cycles) * period;
    const int sampleCount = std::max(512, cycles * 256);
    std::vector<double> y;
    y.reserve(static_cast<size_t>(sampleCount));

    auto interp = [&](double t) -> double {
        const auto& x = target->xData;
        const auto& v = target->yData;
        if (t <= x.front()) return v.front();
        if (t >= x.back()) return v.back();
        auto it = std::lower_bound(x.begin(), x.end(), t);
        if (it == x.end()) return v.back();
        const size_t idx = static_cast<size_t>(it - x.begin());
        if (idx == 0) return v.front();
        const double x0 = x[idx - 1], x1 = x[idx];
        const double y0 = v[idx - 1], y1 = v[idx];
        const double a = (t - x0) / std::max(1e-18, x1 - x0);
        return y0 + (y1 - y0) * a;
    };

    for (int i = 0; i < sampleCount; ++i) {
        const double t = t0 + (t1 - t0) * static_cast<double>(i) / static_cast<double>(sampleCount - 1);
        y.push_back(interp(t));
    }

    auto harmonicAmplitude = [&](int h) -> double {
        const double omega = 2.0 * M_PI * fundamentalHz * static_cast<double>(h);
        double a = 0.0;
        double b = 0.0;
        for (int i = 0; i < sampleCount; ++i) {
            const double t = t0 + (t1 - t0) * static_cast<double>(i) / static_cast<double>(sampleCount - 1);
            a += y[static_cast<size_t>(i)] * std::sin(omega * t);
            b += y[static_cast<size_t>(i)] * std::cos(omega * t);
        }
        const double scale = 2.0 / static_cast<double>(sampleCount);
        return scale * std::sqrt(a * a + b * b);
    };

    const double h1 = std::max(1e-18, harmonicAmplitude(1));
    double sumSq = 0.0;

    SimWaveform harmonicsWave;
    harmonicsWave.name = "HARMONIC_MAG(" + targetName + ")";
    for (int h = 1; h <= harmonics; ++h) {
        const double mag = harmonicAmplitude(h);
        harmonicsWave.xData.push_back(static_cast<double>(h));
        harmonicsWave.yData.push_back(mag);
        results.measurements["harmonic_" + std::to_string(h)] = mag;
        if (h >= 2) sumSq += mag * mag;
    }

    const double thd = std::sqrt(sumSq) / h1;
    results.measurements["thd_ratio"] = thd;
    results.measurements["thd_percent"] = thd * 100.0;
    results.waveforms.push_back(std::move(harmonicsWave));
    maybeWriteReport(cfg, results, "distortion");
    return results;
}

SimResults SimEngine::solveOptimization(const SimNetlist& netlist) {
    SimResults results;
    const auto cfg = netlist.analysis();
    if (cfg.optimizationParams.empty() || cfg.optimizationTargetSignal.empty()) {
        return results;
    }

    struct ParsedParam {
        std::string fullName;
        std::string compName;
        std::string paramName;
        std::vector<double> values;
        double sigma = 0.0;
    };

    std::vector<ParsedParam> params;
    params.reserve(cfg.optimizationParams.size());
    for (const auto& p : cfg.optimizationParams) {
        const size_t dot = p.name.find('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= p.name.size()) {
            continue;
        }
        ParsedParam pp;
        pp.fullName = p.name;
        pp.compName = p.name.substr(0, dot);
        pp.paramName = p.name.substr(dot + 1);
        pp.sigma = std::max(0.0, p.sigma);

        const int points = std::max(1, p.points);
        pp.values.reserve(static_cast<size_t>(points));
        if (points == 1) {
            pp.values.push_back(p.start);
        } else if (p.logScale && p.start > 0.0 && p.stop > 0.0) {
            const double ratio = std::pow(p.stop / p.start, 1.0 / static_cast<double>(points - 1));
            double v = p.start;
            for (int i = 0; i < points; ++i) {
                pp.values.push_back(v);
                v *= ratio;
            }
        } else {
            const double step = (p.stop - p.start) / static_cast<double>(points - 1);
            for (int i = 0; i < points; ++i) {
                pp.values.push_back(p.start + step * static_cast<double>(i));
            }
        }
        params.push_back(std::move(pp));
    }
    if (params.empty()) return results;

    std::string targetNode;
    if (!parseVoltageProbe(cfg.optimizationTargetSignal, targetNode)) return results;

    auto evaluateNodeSignal = [&](const SimResults& op, const std::string& signal, double& out) -> bool {
        std::string node;
        if (!parseVoltageProbe(signal, node)) return false;
        const auto it = op.nodeVoltages.find(node);
        if (it == op.nodeVoltages.end()) return false;
        out = it->second;
        return true;
    };

    auto applyParamsToNetlist = [&](SimNetlist& perturbed, const std::vector<double>& values) -> bool {
        auto& comps = perturbed.mutableComponents();
        for (size_t i = 0; i < params.size(); ++i) {
            bool found = false;
            for (auto& c : comps) {
                if (c.name == params[i].compName) {
                    c.params[params[i].paramName] = values[i];
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    };

    auto runCandidate = [&](const std::vector<double>& values, double& objective, bool& feasible, SimResults& opOut) -> bool {
        SimNetlist perturbed = netlist;
        if (!applyParamsToNetlist(perturbed, values)) return false;
        SimAnalysisConfig base = cfg;
        base.type = SimAnalysisType::OP;
        perturbed.setAnalysis(base);

        SimEngine local;
        opOut = local.run(perturbed);

        double target = 0.0;
        if (!evaluateNodeSignal(opOut, cfg.optimizationTargetSignal, target)) return false;
        const double weight = std::max(1e-12, cfg.optimizationTargetWeight);
        objective = weight * std::abs(target - cfg.optimizationTargetValue);
        feasible = true;

        for (const auto& c : cfg.optimizationConstraints) {
            double val = 0.0;
            if (!evaluateNodeSignal(opOut, c.signal, val)) {
                feasible = false;
                objective += 1e6;
                continue;
            }
            if (val < c.minValue) {
                feasible = false;
                objective += 1e3 * (c.minValue - val);
            } else if (val > c.maxValue) {
                feasible = false;
                objective += 1e3 * (val - c.maxValue);
            }
        }
        return true;
    };

    std::vector<SimWaveform> paramWaves;
    paramWaves.reserve(params.size() + 1);
    paramWaves.push_back({"OPT_OBJECTIVE", {}, {}, {}});
    paramWaves.push_back({"OPT_FEASIBLE", {}, {}, {}});
    for (const auto& p : params) {
        paramWaves.push_back({"OPT_PARAM(" + p.fullName + ")", {}, {}, {}});
    }

    const size_t dims = params.size();
    std::vector<size_t> index(dims, 0);
    bool done = false;
    int evalCount = 0;

    bool haveBest = false;
    bool bestFeasible = false;
    double bestObjective = std::numeric_limits<double>::infinity();
    std::vector<double> bestValues(dims, 0.0);
    SimResults bestOp;

    while (!done) {
        std::vector<double> values(dims, 0.0);
        for (size_t d = 0; d < dims; ++d) values[d] = params[d].values[index[d]];

        double objective = 0.0;
        bool feasible = false;
        SimResults op;
        const bool ok = runCandidate(values, objective, feasible, op);
        if (ok) {
            paramWaves[0].xData.push_back(static_cast<double>(evalCount));
            paramWaves[0].yData.push_back(objective);
            paramWaves[1].xData.push_back(static_cast<double>(evalCount));
            paramWaves[1].yData.push_back(feasible ? 1.0 : 0.0);
            for (size_t d = 0; d < dims; ++d) {
                paramWaves[d + 2].xData.push_back(static_cast<double>(evalCount));
                paramWaves[d + 2].yData.push_back(values[d]);
            }

            const bool better =
                !haveBest ||
                (feasible && !bestFeasible) ||
                (feasible == bestFeasible && objective < bestObjective - 1e-15);
            if (better) {
                haveBest = true;
                bestFeasible = feasible;
                bestObjective = objective;
                bestValues = values;
                bestOp = op;
            }
            ++evalCount;
        }

        if (cfg.optimizationMaxEvaluations > 0 && evalCount >= cfg.optimizationMaxEvaluations) {
            break;
        }

        for (size_t d = 0; d < dims; ++d) {
            index[d]++;
            if (index[d] < params[d].values.size()) {
                break;
            }
            index[d] = 0;
            if (d == dims - 1) done = true;
        }
    }

    if (!haveBest) return results;

    results.waveforms = std::move(paramWaves);
    results.measurements["optimization_evaluations"] = static_cast<double>(evalCount);
    results.measurements["optimization_best_objective"] = bestObjective;
    results.measurements["optimization_best_feasible"] = bestFeasible ? 1.0 : 0.0;
    for (size_t d = 0; d < dims; ++d) {
        results.measurements["optimization_best_" + params[d].fullName] = bestValues[d];
    }

    double bestTarget = 0.0;
    if (evaluateNodeSignal(bestOp, cfg.optimizationTargetSignal, bestTarget)) {
        results.measurements["optimization_best_target"] = bestTarget;
    }

    if (cfg.optimizationYieldSamples > 0) {
        std::mt19937 rng(cfg.optimizationSeed);
        std::normal_distribution<double> normal(0.0, 1.0);
        int passCount = 0;
        int validCount = 0;

        for (int s = 0; s < cfg.optimizationYieldSamples; ++s) {
            std::vector<double> perturbed = bestValues;
            for (size_t d = 0; d < dims; ++d) {
                double sigma = params[d].sigma;
                if (sigma <= 0.0) {
                    sigma = 0.0;
                    for (const auto& c : netlist.components()) {
                        if (c.name == params[d].compName && c.tolerances.count(params[d].paramName)) {
                            sigma = c.tolerances.at(params[d].paramName).value / 3.0;
                            break;
                        }
                    }
                }
                if (sigma > 0.0) {
                    const double rel = sigma * normal(rng);
                    perturbed[d] *= (1.0 + rel);
                }
            }

            double objective = 0.0;
            bool feasible = false;
            SimResults op;
            if (!runCandidate(perturbed, objective, feasible, op)) continue;
            ++validCount;
            if (!feasible) continue;

            if (cfg.optimizationYieldTargetTolerance > 0.0) {
                double target = 0.0;
                if (!evaluateNodeSignal(op, cfg.optimizationTargetSignal, target)) continue;
                if (std::abs(target - cfg.optimizationTargetValue) > cfg.optimizationYieldTargetTolerance) continue;
            }
            ++passCount;
        }

        results.measurements["optimization_yield_valid_samples"] = static_cast<double>(validCount);
        results.measurements["optimization_yield_pass_samples"] = static_cast<double>(passCount);
        results.measurements["optimization_yield_ratio"] =
            (validCount > 0) ? static_cast<double>(passCount) / static_cast<double>(validCount) : 0.0;
    }

    maybeWriteReport(cfg, results, "optimization");
    return results;
}

SimResults SimEngine::solveFFT(const SimNetlist& netlist) {
    const auto cfg = netlist.analysis();
    
    // 1. Run standard transient simulation first
    SimNetlist tranNet = netlist;
    SimAnalysisConfig tranCfg = cfg;
    tranCfg.type = SimAnalysisType::Transient;
    tranNet.setAnalysis(tranCfg);
    SimResults tranResults = solveTransient(tranNet);

    SimResults fftResults;
    fftResults.analysisType = SimAnalysisType::FFT;
    fftResults.xAxisName = "frequency_hz";
    fftResults.yAxisName = "magnitude_db";

    if (tranResults.waveforms.empty()) return fftResults;

    // 2. Find target waveform
    const SimWaveform* target = nullptr;
    for (const auto& w : tranResults.waveforms) {
        if (w.name == cfg.fftTargetSignal) {
            target = &w;
            break;
        }
    }

    if (!target) {
        fftResults.diagnostics.push_back("FFT Error: Target signal '" + cfg.fftTargetSignal + "' not found in transient results.");
        return fftResults;
    }

    // 3. Resample to power of 2
    int n = cfg.fftPoints;
    if ((n & (n - 1)) != 0 || n < 16) n = 1024; // Ensure power of 2

    std::vector<double> samples = SimMath::resample(target->xData, target->yData, n);
    
    // 4. Apply window (Hann)
    std::vector<std::complex<double>> complexSamples(n);
    for (int i = 0; i < n; ++i) {
        double window = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (n - 1)));
        complexSamples[i] = std::complex<double>(samples[i] * window, 0.0);
    }

    // 5. Compute FFT
    std::vector<std::complex<double>> spectrum = SimMath::fft(complexSamples);

    // 6. Format results (Positive frequencies only)
    SimWaveform magWave;
    magWave.name = "FFT(" + cfg.fftTargetSignal + ")";
    
    double timeSpan = target->xData.back() - target->xData.front();
    if (timeSpan <= 0) timeSpan = 1e-9;
    double fs = (double)(n - 1) / timeSpan;

    for (int i = 0; i <= n / 2; ++i) {
        double freq = i * fs / n;
        double mag = std::abs(spectrum[i]) * 2.0 / n; // Normalize
        
        magWave.xData.push_back(freq);
        // Store in dB for better visualization
        magWave.yData.push_back(20.0 * std::log10(std::max(1e-12, mag)));
    }
    
    fftResults.waveforms.push_back(std::move(magWave));
    return fftResults;
}
