#ifndef SIM_NET_EVALUATOR_H
#define SIM_NET_EVALUATOR_H

#include "sim_results.h"

#include <map>
#include <string>
#include <vector>

struct NetStatement {
    std::string inputSource;
    std::string outputSpec;
    bool hasOutput = false;
    bool hasRin = false;
    bool hasRout = false;
    double rin = 1.0;
    double rout = 1.0;
    int lineNumber = 0;
    std::string sourceName;
};

struct NetSourceInfo {
    std::string positiveNode;
    std::string negativeNode;
};

struct NetEvaluation {
    std::vector<SimWaveform> waveforms;
    std::vector<std::string> diagnostics;
};

class SimNetEvaluator {
public:
    static bool parse(
        const std::string& line,
        int lineNumber,
        const std::string& sourceName,
        NetStatement& out
    );

    static NetEvaluation evaluate(
        const std::vector<NetStatement>& statements,
        const std::map<std::string, NetSourceInfo>& sources,
        const SimResults& results,
        const std::string& analysisType
    );
};

#endif // SIM_NET_EVALUATOR_H
