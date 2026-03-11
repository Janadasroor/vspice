#ifndef SIM_MODEL_PARSER_H
#define SIM_MODEL_PARSER_H

#include "sim_netlist.h"
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Parser for standard SPICE .model and .subckt definitions.
 */
enum class SimParseDiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct SimParseDiagnostic {
    SimParseDiagnosticSeverity severity = SimParseDiagnosticSeverity::Info;
    int line = 0;
    std::string source;
    std::string message;
    std::string text;
};

struct SimModelParseOptions {
    std::string sourceName;
    std::string activeLibSection;
    std::function<bool(const std::string& path, std::string& outContent)> includeResolver;
    bool strict = false;
};

class SimModelParser {
public:
    static bool parseModelLine(
        SimNetlist& netlist,
        const std::string& line,
        int lineNumber = 0,
        const std::string& sourceName = "",
        std::vector<SimParseDiagnostic>* diagnostics = nullptr
    );
    static bool parseLibrary(
        SimNetlist& netlist,
        const std::string& content,
        const SimModelParseOptions& options = SimModelParseOptions(),
        std::vector<SimParseDiagnostic>* diagnostics = nullptr
    );

private:
    static std::string trim(const std::string& s);
    static std::vector<std::string> split(const std::string& s);
};

#endif // SIM_MODEL_PARSER_H
