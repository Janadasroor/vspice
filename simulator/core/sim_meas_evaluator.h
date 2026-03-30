#ifndef SIM_MEAS_EVALUATOR_H
#define SIM_MEAS_EVALUATOR_H

#include "sim_results.h"
#include <string>
#include <vector>
#include <map>

enum class MeasFunction {
    FIND, DERIV, PARAM,
    // Direct (single-signal) functions
    MAX, MIN, PP, AVG, RMS, N, INTEG,
    MIN_AT, MAX_AT,
    FIRST, LAST,
    DUTY, SLEWRATE, SLEWRATE_FALL, SLEWRATE_RISE,
    FREQ, PERIOD,

    // Conditional (trig/targ) functions
    TRIG_TARG,

    // Invalid
    Unknown
};

struct MeasTrigger {
    std::string signal;
    std::string lhsExpr;
    std::string rhsExpr;
    double value = 0.0;
    int index = 1;       // Which crossing (1=first, 2=second, etc.)
    bool rising = true;  // RISE=true, FALL=false
    bool useRiseFall = false; // Was RISE/FALL specified?
    bool useCross = false;
    bool useLast = false;
    double td = 0.0;     // Delay before searching
    double cross = 0.0;  // Cross number (alternative to index)
};

struct MeasStatement {
    std::string analysisType; // "tran", "ac", "dc", "noise", etc. (case-insensitive)
    std::string name;         // Measurement name
    MeasFunction function = MeasFunction::Unknown;
    std::string signal;       // Signal expression e.g. "V(out)"
    std::string expr;         // General expression for FIND/DERIV/PARAM
    bool hasAt = false;
    double at = 0.0;
    bool hasWhen = false;
    MeasTrigger when;

    // For conditional measurements (TRIG/TARG form)
    MeasTrigger trig;
    MeasTrigger targ;
    bool hasTrigTarg = false;

    // Optional time/frequency window for direct functions.
    bool hasFrom = false;
    bool hasTo = false;
    double from = 0.0;
    double to = 0.0;

    // Parsed source line info
    int lineNumber = 0;
    std::string sourceName;
};

struct MeasResult {
    std::string name;
    double value = 0.0;
    bool valid = false;
    std::string error; // Error message if evaluation failed
};

class SimMeasEvaluator {
public:
    // Parse a single .meas line. Returns true if parsed successfully.
    static bool parse(
        const std::string& line,
        int lineNumber,
        const std::string& sourceName,
        MeasStatement& out
    );

    // Evaluate a list of .meas statements against simulation results.
    // Only evaluates statements matching the given analysis type.
    static std::vector<MeasResult> evaluate(
        const std::vector<MeasStatement>& statements,
        const SimResults& results,
        const std::string& analysisType
    );

private:
    static MeasFunction parseFunction(const std::string& token);
    static std::string toLower(const std::string& s);
    static bool parseTrigger(
        const std::vector<std::string>& tokens,
        size_t& pos,
        MeasTrigger& trig
    );

    // Waveform signal lookup (handles V(name), I(name), bare name)
    static const SimWaveform* findWaveform(
        const SimResults& results,
        const std::string& signal
    );

    // Evaluation helpers
    static double evalMAX(const SimWaveform& w);
    static double evalMIN(const SimWaveform& w);
    static double evalPP(const SimWaveform& w);
    static double evalAVG(const SimWaveform& w);
    static double evalRMS(const SimWaveform& w);
    static double evalINTEG(const SimWaveform& w);
    static double evalFIRST(const SimWaveform& w);
    static double evalLAST(const SimWaveform& w);

    // Find time at crossing for trig/targ evaluation
    static bool findCrossingTime(
        const SimWaveform& w,
        const MeasTrigger& trigger,
        double& outTime
    );

    static bool findConditionCrossingTime(
        const SimResults& results,
        const MeasTrigger& trigger,
        const std::map<std::string, double>& priorMeasurements,
        double& outTime
    );

    static bool resolveTriggerTime(
        const SimResults& results,
        const MeasTrigger& trigger,
        const std::map<std::string, double>& priorMeasurements,
        double& outTime
    );
};

#endif // SIM_MEAS_EVALUATOR_H
