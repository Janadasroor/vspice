#ifndef SIM_NETLIST_H
#define SIM_NETLIST_H

#include <string>
#include <vector>
#include <map>

enum class SimComponentType {
    Resistor, Capacitor, Inductor,
    VoltageSource, CurrentSource,
    Diode, BJT_NPN, BJT_PNP,
    MOSFET_NMOS, MOSFET_PMOS,
    OpAmpMacro, Switch, TransmissionLine, SubcircuitInstance,
    VCVS, VCCS, CCVS, CCCS,
    B_VoltageSource, B_CurrentSource,
    FluxScript,
    LOGIC_AND, LOGIC_OR, LOGIC_XOR, LOGIC_NAND, LOGIC_NOR, LOGIC_NOT
};

enum class SimAnalysisType {
    OP, Transient, AC, MonteCarlo, Sensitivity, ParametricSweep, Noise, Distortion, Optimization, FFT, RealTime
};

enum class SimIntegrationMethod {
    BackwardEuler,
    Trapezoidal,
    Gear2
};

enum class SimTransientStorageMode {
    Full,         // Store all accepted transient points.
    Strided,      // Store every Nth accepted point.
    AutoDecimate  // Store all points, then decimate when storage cap is exceeded.
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
};

struct SimSubcircuit {
    std::string name;
    std::vector<std::string> pinNames;
    std::vector<SimComponentInstance> components;
    std::map<std::string, SimModel> models;
    std::map<std::string, double> parameters;
};

struct SimAnalysisConfig {
    SimAnalysisType type;
    double tStart = 0, tStop = 0.01, tStep = 1e-5;
    double fStart = 1, fStop = 1e6; 
    int fPoints = 10;
    int mcRuns = 10; // Number of Monte Carlo iterations
    SimAnalysisType mcBaseAnalysis = SimAnalysisType::OP; // Analysis to run under MC

    // Sensitivity analysis settings
    std::string sensitivityTargetSignal; // e.g. "V(N2)"
    std::string sensitivityTargetParam;  // e.g. "resistance" (optional, if empty checks all components)

    // Parametric Sweep settings
    std::string sweepParam; // e.g. "R1.resistance"
    double sweepStart = 0, sweepStop = 1000;
    int sweepPoints = 10;
    bool sweepLog = false;
    int sweepParallelism = 1; // <=0 => auto

    // Noise analysis settings
    std::string noiseOutputSignal;  // e.g. "V(OUT)"
    std::string noiseInputSource;   // e.g. "VIN"
    double noiseTemperatureK = 300.0;

    // Distortion/THD settings
    std::string thdTargetSignal;    // e.g. "V(OUT)"
    double thdFundamentalHz = 1000.0;
    int thdHarmonics = 5;
    int thdSkipCycles = 1;
    int thdCycles = 5;

    // FFT settings
    std::string fftTargetSignal; // e.g. "V(OUT)"
    int fftPoints = 1024;        // Must be power of 2
    double fftStart = 0.0;       // Start time in transient results
    double fftStop = 0.0;        // 0 => use tStop
    std::string fftWindow;       // "rectangular", "hann", "hamming", "blackman"

    // Real-time settings
    int rtIntervalMs = 50;       // UI Update interval
    double rtTimeStep = 1e-3;    // Simulated time per update

    // Optional report output path (JSON-like text emitted by analyses that support it).
    std::string reportFile;

    // Mixed-signal controls
    double digitalThresholdLow = 2.0;
    double digitalThresholdHigh = 3.0;
    double digitalOutputLow = 0.0;
    double digitalOutputHigh = 5.0;
    bool mixedSignalEnableEventRefinement = true;
    double mixedSignalEventStep = 1e-6;
    bool mixedSignalLogEvents = false;

    struct OptimizationParam {
        std::string name;     // e.g. "R1.resistance"
        double start = 0.0;
        double stop = 0.0;
        int points = 1;
        bool logScale = false;
        double sigma = 0.0;   // optional relative 1-sigma for yield perturbation (0 => fallback)
    };

    struct OptimizationConstraint {
        std::string signal;   // e.g. "V(OUT)"
        double minValue = -1e300;
        double maxValue = 1e300;
    };

    std::vector<OptimizationParam> optimizationParams;
    std::string optimizationTargetSignal; // e.g. "V(OUT)"
    double optimizationTargetValue = 0.0;
    double optimizationTargetWeight = 1.0;
    int optimizationMaxEvaluations = 0; // 0 => evaluate full Cartesian product
    std::vector<OptimizationConstraint> optimizationConstraints;
    int optimizationYieldSamples = 0;   // 0 => disabled
    int optimizationSeed = 42;
    double optimizationYieldTargetTolerance = 0.0; // abs tolerance around target; 0 => disabled

    // Numerical settings
    double relTol = 1e-3;
    double absTol = 1e-6;
    bool useAdaptiveStep = true;
    SimIntegrationMethod integrationMethod = SimIntegrationMethod::BackwardEuler;
    bool transientUseOperatingPointInit = true;
    bool transientUseDeviceInitialConditions = true;
    double transientMinStep = 0.0;      // 0 => auto
    double transientMaxStep = 0.0;      // 0 => auto
    double transientMaxGrowth = 2.0;    // max multiplier for next accepted step
    double transientMinShrink = 0.1;    // min multiplier when shrinking step
    int transientMaxRejects = 25;
    bool logTransientStepControl = false;
    SimTransientStorageMode transientStorageMode = SimTransientStorageMode::Full;
    int transientStoreStride = 1;       // Used when transientStorageMode == Strided
    int transientMaxStoredPoints = 0;   // 0 => unbounded

    // Convergence aids
    double gmin = 1e-12;
    bool useSourceStepping = true;
    bool useGminStepping = true;
    bool useCombinedHomotopyStepping = true;

    // Homotopy schedule controls
    double gminSteppingStart = 1e-3;
    int gminSteppingSteps = 6;
    double sourceSteppingInitial = 0.1;
    double sourceSteppingMaxStep = 0.2;
    double sourceSteppingMinStep = 1e-6;
    int combinedHomotopySteps = 8;
    bool logHomotopyProgress = false;

    // Newton-Raphson controls
    int maxNRIterations = 200;
    double nrMinDamping = 0.1;
    double nrMaxVoltageStep = 0.5;
    bool logNRIterationProgress = false;
    bool logNRFailureDiagnostics = true;
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
