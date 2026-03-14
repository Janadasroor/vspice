#include "sim_model_parser.h"
#include "sim_value_parser.h"

#include <QString>

#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>

namespace {

using Severity = SimParseDiagnosticSeverity;

void addDiag(
    std::vector<SimParseDiagnostic>* diagnostics,
    Severity severity,
    int line,
    const std::string& source,
    const std::string& message,
    const std::string& text
) {
    if (!diagnostics) return;
    diagnostics->push_back({severity, line, source, message, text});
}

bool startsWithNoCase(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

std::string stripParensComma(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '('), s.end());
    s.erase(std::remove(s.begin(), s.end(), ')'), s.end());
    s.erase(std::remove(s.begin(), s.end(), ','), s.end());
    return s;
}

bool parseNumeric(const std::string& text, double& out) {
    return SimValueParser::parseSpiceNumber(QString::fromStdString(text), out);
}

SimComponentType inferModelType(const std::string& typeToken, bool& ok) {
    std::string typeStr = typeToken;
    std::transform(typeStr.begin(), typeStr.end(), typeStr.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    ok = true;
    if (typeStr == "NPN") return SimComponentType::BJT_NPN;
    if (typeStr == "PNP") return SimComponentType::BJT_PNP;
    if (typeStr == "D") return SimComponentType::Diode;
    if (typeStr == "NMOS") return SimComponentType::MOSFET_NMOS;
    if (typeStr == "PMOS") return SimComponentType::MOSFET_PMOS;
    ok = false;
    return SimComponentType::Resistor;
}

int mapSubcktNodeToken(
    const std::string& token,
    const std::map<std::string, int>& pinToId,
    std::map<std::string, int>& localNodeToId,
    int& nextInternalId
) {
    if (token.empty()) return 0;
    if (token == "0" || token == "GND" || token == "gnd") return 0;

    auto pinIt = pinToId.find(token);
    if (pinIt != pinToId.end()) return pinIt->second;

    try {
        const int n = std::stoi(token);
        if (n >= 0) return n;
    } catch (...) {}

    auto it = localNodeToId.find(token);
    if (it != localNodeToId.end()) return it->second;

    const int id = nextInternalId++;
    localNodeToId[token] = id;
    return id;
}

} // namespace

std::string SimModelParser::trim(const std::string& s) {
    auto start = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); });
    auto end = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    return (start < end) ? std::string(start, end) : "";
}

std::vector<std::string> SimModelParser::split(const std::string& s) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (tokenStream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool SimModelParser::parseModelLine(
    SimNetlist& netlist,
    std::map<std::string, SimModel>& outModels,
    const std::string& line,
    int lineNumber,
    const std::string& sourceName,
    std::vector<SimParseDiagnostic>* diagnostics
) {
    const std::string tLine = trim(line);
    if (tLine.empty() || tLine[0] == '*') return true;
    if (!startsWithNoCase(tLine, ".model")) return false;

    auto tokens = split(tLine);
    if (tokens.size() < 3) {
        addDiag(diagnostics, Severity::Error, lineNumber, sourceName, "invalid .model card (expected '.model <name> <type-or-base> ...')", tLine);
        return false;
    }

    SimModel model;
    model.name = tokens[1];
    const std::string typeOrBase = tokens[2];

    bool typeOk = false;
    model.type = inferModelType(typeOrBase, typeOk);
    if (!typeOk) {
        const SimModel* base = netlist.findModel(typeOrBase);
        if (!base) {
            addDiag(diagnostics, Severity::Error, lineNumber, sourceName, "unknown model type or base model '" + typeOrBase + "'", tLine);
            return false;
        }
        model.type = base->type;
        model.params = base->params;
    }

    for (size_t i = 3; i < tokens.size(); ++i) {
        std::string token = stripParensComma(tokens[i]);
        if (token.empty()) continue;
        const size_t eq = token.find('=');
        if (eq == std::string::npos) continue;

        std::string key = token.substr(0, eq);
        const std::string valStr = token.substr(eq + 1);
        if (key.empty()) continue;

        double parsed = 0.0;
        if (parseNumeric(valStr, parsed)) {
            model.params[key] = parsed;
        } else {
            addDiag(diagnostics, Severity::Warning, lineNumber, sourceName, "invalid numeric model parameter '" + key + "=" + valStr + "'", tLine);
        }
    }

    outModels[model.name] = model;
    return true;
}

bool SimModelParser::parseLibrary(
    SimNetlist& netlist,
    const std::string& content,
    const SimModelParseOptions& options,
    std::vector<SimParseDiagnostic>* diagnostics
) {
    struct LogicalLine {
        int lineNo = 0;
        std::string text;
    };

    std::vector<LogicalLine> logicalLines;
    {
        std::istringstream stream(content);
        std::string raw;
        int lineNo = 0;
        while (std::getline(stream, raw)) {
            ++lineNo;
            const std::string t = trim(raw);
            if (!logicalLines.empty() && !t.empty() && t[0] == '+') {
                logicalLines.back().text += " " + trim(t.substr(1));
            } else {
                logicalLines.push_back({lineNo, raw});
            }
        }
    }

    bool inSubckt = false;
    bool inLibBlock = false;
    bool parseLibBlock = true;
    bool hadErrors = false;
    SimSubcircuit currentSub;
    std::map<std::string, int> currentPinToId;
    std::map<std::string, int> currentLocalNodeToId;
    int nextInternalNodeId = 1;

    auto fail = [&](int lineNo, const std::string& msg, const std::string& lineText) {
        addDiag(diagnostics, Severity::Error, lineNo, options.sourceName, msg, lineText);
        hadErrors = true;
    };

    for (const LogicalLine& ll : logicalLines) {
        const std::string tLine = trim(ll.text);
        if (tLine.empty() || tLine[0] == '*') continue;

        auto tokens = split(tLine);
        if (tokens.empty()) continue;
        const std::string card = tokens[0];

        if (startsWithNoCase(card, ".include") || startsWithNoCase(card, ".inc")) {
            if (tokens.size() < 2) {
                fail(ll.lineNo, "malformed .include card", tLine);
                continue;
            }
            std::string path = tokens[1];
            if (!path.empty() && (path.front() == '"' || path.front() == '\'')) {
                path.erase(path.begin());
            }
            if (!path.empty() && (path.back() == '"' || path.back() == '\'')) {
                path.pop_back();
            }
            if (!options.includeResolver) {
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "no include resolver set; skipped include '" + path + "'", tLine);
                continue;
            }
            std::string includedContent;
            if (!options.includeResolver(path, includedContent)) {
                fail(ll.lineNo, "failed to resolve include '" + path + "'", tLine);
                continue;
            }
            SimModelParseOptions childOpts = options;
            if (childOpts.sourceName.empty()) childOpts.sourceName = path;
            const bool ok = parseLibrary(netlist, includedContent, childOpts, diagnostics);
            hadErrors = hadErrors || (options.strict && !ok);
            continue;
        }

        if (startsWithNoCase(card, ".lib")) {
            inLibBlock = true;
            std::string section = (tokens.size() >= 2) ? tokens[1] : "";
            parseLibBlock = options.activeLibSection.empty() || section == options.activeLibSection;
            if (!parseLibBlock) {
                addDiag(diagnostics, Severity::Info, ll.lineNo, options.sourceName, "skipping .lib section '" + section + "'", tLine);
            }
            continue;
        }
        if (startsWithNoCase(card, ".endl")) {
            inLibBlock = false;
            parseLibBlock = true;
            continue;
        }
        if (inLibBlock && !parseLibBlock) {
            continue;
        }

        if (startsWithNoCase(card, ".param")) {
            for (size_t i = 1; i < tokens.size(); ++i) {
                std::string token = stripParensComma(tokens[i]);
                const size_t eq = token.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = token.substr(0, eq);
                const std::string value = token.substr(eq + 1);
                double parsed = 0.0;
                if (key.empty()) continue;
                if (parseNumeric(value, parsed)) {
                    if (inSubckt) currentSub.parameters[key] = parsed;
                    else netlist.setParameter(key, parsed);
                } else {
                    addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "invalid .param numeric value '" + token + "'", tLine);
                }
            }
            continue;
        }

        if (startsWithNoCase(card, ".subckt")) {
            if (tokens.size() < 2) {
                fail(ll.lineNo, "malformed .subckt card", tLine);
                continue;
            }
            inSubckt = true;
            currentSub = SimSubcircuit();
            currentSub.name = tokens[1];
            currentPinToId.clear();
            currentLocalNodeToId.clear();
            currentSub.pinNames.clear();
            currentSub.components.clear();
            for (size_t i = 2; i < tokens.size(); ++i) {
                currentSub.pinNames.push_back(tokens[i]);
                currentPinToId[tokens[i]] = static_cast<int>(i - 1); // pin 1..N
            }
            nextInternalNodeId = static_cast<int>(currentSub.pinNames.size()) + 1;
            continue;
        }

        if (startsWithNoCase(card, ".ends")) {
            if (!inSubckt) {
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, ".ends found without active .subckt", tLine);
                continue;
            }
            netlist.addSubcircuit(currentSub);
            inSubckt = false;
            continue;
        }

        if (inSubckt) {
            if (tokens.size() < 2) {
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "ignored malformed subcircuit line", tLine);
                continue;
            }

            SimComponentInstance inst;
            inst.name = tokens[0];
            const char typeChar = static_cast<char>(std::toupper(static_cast<unsigned char>(inst.name[0])));

            size_t nodeStart = 1;
            size_t nodeCount = 0;
            bool isSubcktCall = false;

            switch (typeChar) {
                case 'R': inst.type = SimComponentType::Resistor; nodeCount = 2; break;
                case 'C': inst.type = SimComponentType::Capacitor; nodeCount = 2; break;
                case 'L': inst.type = SimComponentType::Inductor; nodeCount = 2; break;
                case 'D': inst.type = SimComponentType::Diode; nodeCount = 2; break;
                case 'Q': inst.type = SimComponentType::BJT_NPN; nodeCount = 3; break;
                case 'M': inst.type = SimComponentType::MOSFET_NMOS; nodeCount = 4; break;
                case 'V': inst.type = SimComponentType::VoltageSource; nodeCount = 2; break;
                case 'I': inst.type = SimComponentType::CurrentSource; nodeCount = 2; break;
                case 'E': inst.type = SimComponentType::VCVS; nodeCount = 4; break;
                case 'G': inst.type = SimComponentType::VCCS; nodeCount = 4; break;
                case 'F': inst.type = SimComponentType::CCCS; nodeCount = 2; break;
                case 'H': inst.type = SimComponentType::CCVS; nodeCount = 2; break;
                case 'S': inst.type = SimComponentType::Switch; nodeCount = 4; break;
                case 'B': inst.type = SimComponentType::B_VoltageSource; nodeCount = 2; break;
                case 'A': {
                    // XSPICE: Logic gates. Find the type at the end of node list.
                    inst.type = SimComponentType::LOGIC_AND; // Default
                    nodeCount = 0;
                    for (size_t i = 1; i < tokens.size(); ++i) {
                        if (tokens[i].find('=') != std::string::npos) break;
                        std::string up = tokens[i];
                        std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                        if (up == "AND")  { inst.type = SimComponentType::LOGIC_AND; break; }
                        if (up == "OR")   { inst.type = SimComponentType::LOGIC_OR; break; }
                        if (up == "XOR")  { inst.type = SimComponentType::LOGIC_XOR; break; }
                        if (up == "NAND") { inst.type = SimComponentType::LOGIC_NAND; break; }
                        if (up == "NOR")  { inst.type = SimComponentType::LOGIC_NOR; break; }
                        if (up == "NOT")  { inst.type = SimComponentType::LOGIC_NOT; break; }
                        if (up == "BUF")  { inst.type = SimComponentType::LOGIC_OR; break; } // Map BUF to OR
                        if (up == "SCHMITT") { inst.type = SimComponentType::LOGIC_OR; break; }
                        if (up == "DFLOP") { inst.type = SimComponentType::LOGIC_XOR; break; } // Placeholder
                        nodeCount++;
                    }
                    break;
                }
                case 'X': isSubcktCall = true; break;
                default:
                    addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "unsupported subcircuit primitive '" + inst.name + "'", tLine);
                    continue;
            }

            if (isSubcktCall) {
                size_t eqPos = tokens.size();
                for (size_t i = 1; i < tokens.size(); ++i) {
                    if (tokens[i].find('=') != std::string::npos) {
                        eqPos = i;
                        break;
                    }
                }
                if (eqPos <= 2) {
                    addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "invalid X-call line (missing nodes/subckt name)", tLine);
                    continue;
                }
                inst.subcircuitName = tokens[eqPos - 1];
                for (size_t i = nodeStart; i + 1 < eqPos; ++i) {
                    inst.nodes.push_back(mapSubcktNodeToken(tokens[i], currentPinToId, currentLocalNodeToId, nextInternalNodeId));
                }
            } else {
                if (tokens.size() <= nodeStart + nodeCount) {
                    addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "insufficient node tokens for primitive '" + inst.name + "'", tLine);
                    continue;
                }
                for (size_t i = nodeStart; i < nodeStart + nodeCount; ++i) {
                    inst.nodes.push_back(mapSubcktNodeToken(tokens[i], currentPinToId, currentLocalNodeToId, nextInternalNodeId));
                }

                size_t dataIdx = nodeStart + nodeCount;
                for (size_t i = dataIdx; i < tokens.size(); ++i) {
                    std::string token = tokens[i];
                    if (token.empty()) continue;

                    const size_t eq = token.find('=');
                    if (eq == std::string::npos) {
                        // Positional parameter (e.g., R1 1 0 1k)
                        if (i == dataIdx) {
                            if (typeChar == 'R' || typeChar == 'C' || typeChar == 'L') {
                                std::string key = (typeChar == 'R') ? "resistance" : (typeChar == 'C' ? "capacitance" : "inductance");
                                if (token.front() == '{' && token.back() == '}') {
                                    inst.paramExpressions[key] = token;
                                } else {
                                    double v = 0.0;
                                    if (parseNumeric(token, v)) inst.params[key] = v;
                                }
                            } else if (typeChar == 'D' || typeChar == 'Q' || typeChar == 'M') {
                                inst.modelName = token;
                            } else if (typeChar == 'V' || typeChar == 'I') {
                                std::string key = (typeChar == 'V') ? "voltage" : "current";
                                double v = 0.0;
                                if (parseNumeric(token, v)) inst.params[key] = v;
                                else inst.paramExpressions[key] = token; // SIN(...) etc
                            } else if (typeChar == 'E' || typeChar == 'G') {
                                double g = 0.0;
                                if (parseNumeric(token, g)) inst.params["gain"] = g;
                            }
                        }
                        continue;
                    }

                    std::string key = token.substr(0, eq);
                    const std::string val = token.substr(eq + 1);
                    if (key.empty()) continue;

                    // Support tol=... dist=... lot=...
                    if (key == "tol") {
                        std::string target = (typeChar == 'R') ? "resistance" : (typeChar == 'C' ? "capacitance" : "inductance");
                        double t = 0.0;
                        if (parseNumeric(val, t)) inst.tolerances[target].value = t;
                        continue;
                    } else if (key == "dist") {
                        std::string target = (typeChar == 'R') ? "resistance" : (typeChar == 'C' ? "capacitance" : "inductance");
                        if (val == "gaussian") inst.tolerances[target].distribution = ToleranceDistribution::Gaussian;
                        else inst.tolerances[target].distribution = ToleranceDistribution::Uniform;
                        continue;
                    } else if (key == "lot") {
                        std::string target = (typeChar == 'R') ? "resistance" : (typeChar == 'C' ? "capacitance" : "inductance");
                        inst.tolerances[target].lotId = val;
                        continue;
                    }

                    if (val.front() == '{' && val.back() == '}') {
                        inst.paramExpressions[key] = val;
                    } else {
                        double parsed = 0.0;
                        if (parseNumeric(val, parsed)) {
                            inst.params[key] = parsed;
                        } else {
                            // Store non-numeric (like table=...) as expression/string
                            inst.paramExpressions[key] = val;
                        }
                    }
                }
            }

            currentSub.components.push_back(inst);
            continue;
        }

        if (startsWithNoCase(card, ".model")) {
            if (inSubckt) {
                if (!parseModelLine(netlist, currentSub.models, tLine, ll.lineNo, options.sourceName, diagnostics)) {
                    hadErrors = true;
                }
            } else {
                std::map<std::string, SimModel> dummy; // Not ideal, but parseModelLine usually adds to netlist.
                // Re-obtaining current Netlist models as a map reference isn't direct via API,
                // so let's adjust the call to pass netlist.models() if we can.
                // Actually, I'll just use a local temporary and add it.
                if (!parseModelLine(netlist, netlist.mutableModels(), tLine, ll.lineNo, options.sourceName, diagnostics)) {
                    hadErrors = true;
                }
            }
            continue;
        }

        if (!card.empty() && card[0] == '.') {
            addDiag(diagnostics, Severity::Info, ll.lineNo, options.sourceName, "ignored unsupported control card '" + card + "'", tLine);
        }
    }

    if (inSubckt) {
        fail(logicalLines.empty() ? 0 : logicalLines.back().lineNo, "unterminated .subckt (missing .ends)", currentSub.name);
    }

    return options.strict ? !hadErrors : true;
}
