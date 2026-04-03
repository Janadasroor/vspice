#ifndef SIM_RESULTS_H
#define SIM_RESULTS_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <functional>
#include <complex>

enum class SimAnalysisType {
    OP, Transient, AC, DC, MonteCarlo, Sensitivity, ParametricSweep, Noise, Distortion, Optimization, FFT, RealTime, SParameter
};

struct SParameterPoint {
    double frequency;
    std::complex<double> s11, s12, s21, s22;
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

struct SimAnalysisConfig {
    SimAnalysisType type;
    double tStart = 0, tStop = 0.01, tStep = 1e-5;
    bool transientStopAtSteadyState = false;
    double transientSteadyStateTol = 0.0;
    double transientSteadyStateDelay = 0.0;
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

    // S-Parameter settings
    std::string rfPort1Source; // e.g. "V1"
    std::string rfPort2Node;   // e.g. "N1"
    double rfZ0 = 50.0;        // Reference impedance (Ohms)

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

struct SimWaveform {
    std::string name;
    std::vector<double> xData; // Time or Frequency
    std::vector<double> yData; // Voltage/Current magnitude
    std::vector<double> yPhase; // Phase (for AC analysis)
};

struct SimMeasurementMetadata {
    std::string quantityLabel;
    std::string displayUnit;
};

/**
 * @brief Thread-safe control flags for simulation execution.
 */
struct SimControl {
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> pauseRequested{false};
    std::function<void(double t, const std::vector<double>& solution)> streamingCallback;
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
    std::map<std::string, SimMeasurementMetadata> measurementMetadata;
    std::vector<std::string> diagnostics;
    std::vector<std::string> fixSuggestions;

    std::vector<SParameterPoint> sParameterResults;
    double rfZ0 = 50.0; // Reference impedance used for this set of results

    struct Snapshot {
        std::map<std::string, double> nodeVoltages;
        std::map<std::string, double> branchCurrents;
    };
    Snapshot interpolateAt(double t) const;
};

#endif // SIM_RESULTS_H
