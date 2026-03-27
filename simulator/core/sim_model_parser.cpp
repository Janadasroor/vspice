#include "sim_model_parser.h"
#include "sim_value_parser.h"
#include "sim_meas_evaluator.h"

#include <QString>
#include <QRegularExpression>
#include <QDebug>

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

QString sanitizeMeasName(const QString& raw) {
    QString s = raw;
    s.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    s.replace(QRegularExpression("_+"), "_");
    s.remove(QRegularExpression("^_+"));
    s.remove(QRegularExpression("_+$"));
    return s.isEmpty() ? QString("m") : s.left(48);
}

bool convertMeanToMeasLine(const std::string& meanLine, int lineNo, std::string& outMeasLine) {
    const QString qLine = QString::fromStdString(meanLine).trimmed();
    static const QRegularExpression re(
        "^\\s*\\.mean\\s+(?:(avg|max|min|rms)\\s+)?([^\\s]+)(?:\\s+from\\s*=\\s*([^\\s]+))?(?:\\s+to\\s*=\\s*([^\\s]+))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(qLine);
    if (!m.hasMatch()) return false;

    const QString mode = m.captured(1).isEmpty() ? QString("avg") : m.captured(1).toLower();
    const QString signal = m.captured(2).trimmed();
    const QString from = m.captured(3).trimmed();
    const QString to = m.captured(4).trimmed();
    if (signal.isEmpty()) return false;

    const QString name = QString("mean_%1_%2_l%3")
        .arg(mode, sanitizeMeasName(signal))
        .arg(lineNo);

    QString meas = QString(".meas tran %1 %2 %3").arg(name, mode, signal);
    if (!from.isEmpty()) meas += QString(" from=%1").arg(from);
    if (!to.isEmpty()) meas += QString(" to=%1").arg(to);
    outMeasLine = meas.toStdString();
    return true;
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
    if (typeStr == "VDMOS") return SimComponentType::MOSFET_NMOS;
    if (typeStr == "NMF") return SimComponentType::MOSFET_NMOS;
    if (typeStr == "PMF") return SimComponentType::MOSFET_PMOS;
    if (typeStr == "NJF") return SimComponentType::JFET_NJF;
    if (typeStr == "PJF") return SimComponentType::JFET_PJF;
    if (typeStr == "SW" || typeStr == "VSWITCH") return SimComponentType::Switch;
    if (typeStr == "CSW") return SimComponentType::CSW;
    if (typeStr == "RES" || typeStr == "R") return SimComponentType::Resistor;
    if (typeStr == "CAP" || typeStr == "C") return SimComponentType::Capacitor;
    if (typeStr == "IND" || typeStr == "L") return SimComponentType::Inductor;
    
    // Support for common digital logic models (XSpice) - handle silently
    // Check if it starts with D_ (e.g. D_AND, D_DFF) or matches common names directly
    if (startsWithNoCase(typeStr, "D_") || typeStr == "DFF" || typeStr == "JKFF" || 
        typeStr == "INV" || typeStr == "BUF" || typeStr == "AND" || typeStr == "NAND" ||
        typeStr == "OR" || typeStr == "NOR" || typeStr == "XOR" || typeStr == "XNOR") {
        ok = true;
        return SimComponentType::SubcircuitInstance; 
    }

    ok = false;
    return SimComponentType::SubcircuitInstance;
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
    } catch (...) {
        // Alphanumeric node name (common in LTspice)
    }

    auto it = localNodeToId.find(token);
    if (it != localNodeToId.end()) return it->second;

    const int id = nextInternalId++;
    localNodeToId[token] = id;
    return id;
}

bool isLikelyEncrypted(const std::string& content) {
    if (content.empty()) return false;
    
    // Check for common LTspice encrypted headers or binary markers
    if (content.find("<Binary File>") != std::string::npos) return true;
    if (content.find("* LTspice Encrypted File") != std::string::npos) return true;
    
    // Scan the first few hundred characters for excessive non-printable bytes
    size_t scanLimit = std::min<size_t>(content.size(), 1024);
    size_t controlChars = 0;
    for (size_t i = 0; i < scanLimit; ++i) {
        unsigned char c = static_cast<unsigned char>(content[i]);
        // Only count true control characters (less than space)
        // \r, \n, \t and ^Z (26) are allowed in text files
        if (c < 32 && c != '\r' && c != '\n' && c != '\t' && c != 26) {
            controlChars++;
        }
        // Characters > 127 are NOT necessarily encrypted (could be UTF-8 or legacy encoding)
    }
    
    // If more than 5% of characters are true control characters in the preamble, it's likely binary
    return (controlChars > scanLimit / 20);
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

    std::string lineToSplit = tLine;
    bool inBraces = false;
    for (char &c : lineToSplit) {
        if (c == '{') inBraces = true;
        else if (c == '}') inBraces = false;
        if (!inBraces && (c == '(' || c == ')' || c == ',')) {
            c = ' ';
        }
    }

    auto tokens = split(lineToSplit);
    if (tokens.size() < 3) {
        addDiag(diagnostics, Severity::Error, lineNumber, sourceName, "invalid .model card (expected '.model <name> <type-or-base> ...')", tLine);
        return false;
    }

    SimModel model;
    model.name = tokens[1];
    std::string typeOrBase = tokens[2];

    if (startsWithNoCase(typeOrBase, "AKO:")) {
        typeOrBase = typeOrBase.substr(4);
    }

    bool typeOk = false;
    model.type = inferModelType(typeOrBase, typeOk);
    if (!typeOk) {
        auto it = outModels.find(typeOrBase);
        if (it != outModels.end()) {
            model.type = it->second.type;
            model.params = it->second.params;
            typeOk = true;
        } else {
            const SimModel* base = netlist.findModel(typeOrBase);
            if (base) {
                model.type = base->type;
                model.params = base->params;
                typeOk = true;
            }
        }
    }

    if (!typeOk) {
        addDiag(diagnostics, Severity::Warning, lineNumber, sourceName, "unknown model type or base model '" + typeOrBase + "'", tLine);
        model.type = SimComponentType::SubcircuitInstance; 
    }

    bool vdmosPchan = false;
    for (size_t i = 3; i < tokens.size(); ++i) {
        std::string token = stripParensComma(tokens[i]);
        if (token.empty()) continue;

        std::string tokenLower = token;
        std::transform(tokenLower.begin(), tokenLower.end(), tokenLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tokenLower == "pchan") {
            vdmosPchan = true;
            continue;
        }

        const size_t eq = token.find('=');
        if (eq == std::string::npos) continue;

        std::string key = token.substr(0, eq);
        const std::string valStr = token.substr(eq + 1);
        if (key.empty()) continue;

        double parsed = 0.0;
        if (parseNumeric(valStr, parsed)) {
            model.params[key] = parsed;
        }
    }

    std::string typeUpper = typeOrBase;
    std::transform(typeUpper.begin(), typeUpper.end(), typeUpper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (typeUpper == "VDMOS" && vdmosPchan) {
        model.type = SimComponentType::MOSFET_PMOS;
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
    if (isLikelyEncrypted(content)) {
        addDiag(diagnostics, Severity::Error, 1, options.sourceName, 
                "Encrypted or binary LTspice model detected. These models are proprietary and cannot be simulated in Viospice.", "");
        return false;
    }

    struct LogicalLine {
        int lineNo = 0;
        std::string text;
    };

    std::vector<LogicalLine> logicalLines;
    {
        const size_t kMaxLogicalLines = 200000;
        const size_t kMaxLineLength = 65536;

        std::istringstream stream(content);
        std::string raw;
        int lineNo = 0;
        while (std::getline(stream, raw)) {
            ++lineNo;
            if (raw.size() > kMaxLineLength) {
                if (diagnostics) {
                    SimParseDiagnostic d;
                    d.severity = SimParseDiagnosticSeverity::Warning;
                    d.line = lineNo;
                    d.message = "Line too long (truncated).";
                    diagnostics->push_back(d);
                }
                raw.resize(kMaxLineLength);
            }

            const std::string t = trim(raw);
            if (!logicalLines.empty() && !t.empty() && t[0] == '+') {
                if (logicalLines.back().text.size() + t.size() < kMaxLineLength * 2) {
                     logicalLines.back().text += " " + trim(t.substr(1));
                }
            } else {
                logicalLines.push_back({lineNo, raw});
                if (logicalLines.size() > kMaxLogicalLines) {
                     if (diagnostics) {
                         SimParseDiagnostic d;
                         d.severity = SimParseDiagnosticSeverity::Warning;
                         d.line = lineNo;
                         d.message = "Library exceeds maximum logical line limit. Truncating.";
                         diagnostics->push_back(d);
                     }
                     break;
                }
            }
        }
    }

    bool inLibBlock = false;
    bool parseLibBlock = true;
    bool hadErrors = false;
    
    struct SubcktContext {
        SimSubcircuit sub;
        std::map<std::string, int> pinToId;
        std::map<std::string, int> localNodeToId;
        int nextInternalNodeId = 1;
    };
    std::vector<SubcktContext> subcktStack;

    auto fail = [&](int lineNo, const std::string& msg, const std::string& lineText) {
        addDiag(diagnostics, Severity::Error, lineNo, options.sourceName, msg, lineText);
        hadErrors = true;
    };

    for (const LogicalLine& ll : logicalLines) {
        const std::string tLine = trim(ll.text);
        if (tLine.empty() || tLine[0] == '*' || tLine[0] == '[' || tLine[0] == '#') continue;

        auto tokens = split(tLine);
        if (tokens.empty()) continue;
        const std::string card = tokens[0];

        std::string cardLower = card;
        std::transform(cardLower.begin(), cardLower.end(), cardLower.begin(), [](unsigned char c) { return std::tolower(c); });

        if (inLibBlock && !parseLibBlock && cardLower != ".endl") {
            continue;
        }

        if (cardLower == ".subckt") {
            if (tokens.size() < 2) {
                fail(ll.lineNo, "malformed .subckt card", tLine);
                continue;
            }
            
            SubcktContext ctx;
            ctx.sub.name = tokens[1];
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (tokens[i].find('=') != std::string::npos) break; // End of pins, start of params
                ctx.sub.pinNames.push_back(tokens[i]);
                ctx.pinToId[tokens[i]] = static_cast<int>(i - 1);
            }
            ctx.nextInternalNodeId = static_cast<int>(ctx.sub.pinNames.size()) + 1;
            subcktStack.push_back(ctx);
            continue;
        }

        if (cardLower == ".ends") {
            if (subcktStack.empty()) {
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, ".ends found without active .subckt", tLine);
                continue;
            }
            netlist.addSubcircuit(subcktStack.back().sub);
            subcktStack.pop_back();
            continue;
        }

        if (cardLower == ".model") {
            if (!subcktStack.empty()) {
                parseModelLine(netlist, subcktStack.back().sub.models, tLine, ll.lineNo, options.sourceName, diagnostics);
            } else {
                parseModelLine(netlist, netlist.mutableModels(), tLine, ll.lineNo, options.sourceName, diagnostics);
            }
            continue;
        }

        if (cardLower == ".param" || cardLower == ".params") {
            for (size_t i = 1; i < tokens.size(); ++i) {
                std::string token = tokens[i];
                size_t eq = token.find('=');
                
                std::string key, val;
                if (eq != std::string::npos) {
                    key = token.substr(0, eq);
                    val = token.substr(eq + 1);
                    if (val.empty() && i + 1 < tokens.size()) {
                        val = tokens[++i];
                    }
                } else {
                    if (i + 1 < tokens.size() && (tokens[i+1] == "=" || tokens[i+1].front() == '=')) {
                        key = token;
                        if (tokens[i+1] == "=") {
                            i += 2;
                            if (i < tokens.size()) val = tokens[i];
                        } else {
                            val = tokens[++i].substr(1);
                        }
                    } else {
                        continue;
                    }
                }

                if (key.empty()) continue;
                double parsed = 0.0;
                if (parseNumeric(val, parsed)) {
                    if (!subcktStack.empty()) subcktStack.back().sub.parameters[key] = parsed;
                    else netlist.setParameter(key, parsed);
                } else if (!val.empty()) {
                    // Formula or expression
                } else {
                    addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "empty value for parameter '" + key + "'", tLine);
                }
            }
            continue;
        }

        if (cardLower == ".lib") {
            inLibBlock = true;
            std::string section = (tokens.size() >= 2) ? tokens[1] : "";
            parseLibBlock = options.activeLibSection.empty() || section == options.activeLibSection;
            if (!parseLibBlock) {
                addDiag(diagnostics, Severity::Info, ll.lineNo, options.sourceName, "skipping .lib section '" + section + "'", tLine);
            }
            continue;
        }

        if (cardLower == ".endl") {
            inLibBlock = false;
            parseLibBlock = true;
            continue;
        }

        if (cardLower == ".include" || cardLower == ".inc") {
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

        if (cardLower == ".noise" || cardLower == ".four" || cardLower == ".tf" ||
            cardLower == ".disto" || cardLower == ".sens" || cardLower == ".fft" ||
            cardLower == ".meas" || cardLower == ".measur" || cardLower == ".measure" ||
            cardLower == ".mean" || cardLower == ".step" || cardLower == ".options" || 
            cardLower == ".option" || cardLower == ".temp" || cardLower == ".temperature" ||
            cardLower == ".ic" || cardLower == ".nodeset" || cardLower == ".global" ||
            cardLower == ".func" || cardLower == ".function" || cardLower == ".print" ||
            cardLower == ".plot" || cardLower == ".probe" || cardLower == ".width" ||
            cardLower == ".title" || cardLower == ".save" || cardLower == ".control" ||
            cardLower == ".endc" || cardLower == ".alter" || cardLower == ".let" ||
            cardLower == ".csdf" || cardLower == ".timedomain" || cardLower == ".machine" || 
            cardLower == ".state" || cardLower == ".rule" || cardLower == ".output" || 
            cardLower == ".endmachine" || cardLower == ".backanno" || cardLower == ".net") {
            continue; 
        }

        std::string lineToSplit = tLine;
        bool inBraces = false;
        for (char &c : lineToSplit) {
            if (c == '{') inBraces = true;
            else if (c == '}') inBraces = false;
            if (!inBraces && (c == '(' || c == ')' || c == ',')) {
                c = ' ';
            }
        }
        auto compTokens = split(lineToSplit);

        if (compTokens.size() < 2) continue;

        SimComponentInstance inst;
        inst.name = compTokens[0];
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
            case 'J': inst.type = SimComponentType::JFET_NJF; nodeCount = 3; break;
            case 'Z': inst.type = SimComponentType::JFET_NJF; nodeCount = 3; break;
            case 'M': inst.type = SimComponentType::MOSFET_NMOS; nodeCount = 4; break;
            case 'V': inst.type = SimComponentType::VoltageSource; nodeCount = 2; break;
            case 'I': inst.type = SimComponentType::CurrentSource; nodeCount = 2; break;
            case 'E': {
                bool isBehavioral = false;
                for (const auto& t : compTokens) {
                    std::string up = t; std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                    if (up.find("VALUE") != std::string::npos || up.find("POLY") != std::string::npos || 
                        up.find("TABLE") != std::string::npos || up.find("{") != std::string::npos) {
                        isBehavioral = true; break;
                    }
                }
                inst.type = SimComponentType::VCVS;
                nodeCount = isBehavioral ? 2 : 4;
                break;
            }
            case 'G': {
                bool isBehavioral = false;
                for (const auto& t : compTokens) {
                    std::string up = t; std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                    if (up.find("VALUE") != std::string::npos || up.find("POLY") != std::string::npos || 
                        up.find("TABLE") != std::string::npos || up.find("{") != std::string::npos) {
                        isBehavioral = true; break;
                    }
                }
                inst.type = SimComponentType::VCCS;
                nodeCount = isBehavioral ? 2 : 4;
                break;
            }
            case 'F': inst.type = SimComponentType::CCCS; nodeCount = 2; break;
            case 'H': inst.type = SimComponentType::CCVS; nodeCount = 2; break;
            case 'S': inst.type = SimComponentType::Switch; nodeCount = 4; break;
            case 'W': inst.type = SimComponentType::CSW; nodeCount = 4; break;
            case 'B': inst.type = SimComponentType::B_VoltageSource; nodeCount = 2; break;
            case 'A': {
                inst.type = SimComponentType::LOGIC_AND;
                size_t endOfNodes = 1;
                for (; endOfNodes < compTokens.size(); ++endOfNodes) {
                    if (compTokens[endOfNodes].find('=') != std::string::npos) break;
                }
                if (endOfNodes > 2) {
                    inst.modelName = compTokens[endOfNodes - 1];
                    nodeCount = endOfNodes - 2;
                } else {
                    nodeCount = 0;
                }
                break;
            }
            case 'X': isSubcktCall = true; break;
            default:
                if (!inst.name.empty() && inst.name[0] == '.') continue;
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "unsupported primitive '" + inst.name + "'", tLine);
                continue;
        }

        if (isSubcktCall) {
            size_t eqPos = compTokens.size();
            for (size_t i = 1; i < compTokens.size(); ++i) {
                if (compTokens[i].find('=') != std::string::npos) {
                    eqPos = i;
                    break;
                }
            }
            if (eqPos <= 2) {
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "invalid X-call line", tLine);
                continue;
            }
            inst.subcircuitName = compTokens[eqPos - 1];
            for (size_t i = nodeStart; i + 1 < eqPos; ++i) {
                if (!subcktStack.empty()) {
                    auto& ctx = subcktStack.back();
                    inst.nodes.push_back(mapSubcktNodeToken(compTokens[i], ctx.pinToId, ctx.localNodeToId, ctx.nextInternalNodeId));
                } else {
                    inst.nodes.push_back(netlist.addNode(compTokens[i]));
                }
            }
        } else {
            if (typeChar != 'A' && compTokens.size() <= nodeStart + nodeCount) {
                addDiag(diagnostics, Severity::Warning, ll.lineNo, options.sourceName, "insufficient node tokens for '" + inst.name + "'", tLine);
                continue;
            }
            for (size_t i = nodeStart; i < nodeStart + nodeCount; ++i) {
                if (!subcktStack.empty()) {
                    auto& ctx = subcktStack.back();
                    inst.nodes.push_back(mapSubcktNodeToken(compTokens[i], ctx.pinToId, ctx.localNodeToId, ctx.nextInternalNodeId));
                } else {
                    inst.nodes.push_back(netlist.addNode(compTokens[i]));
                }
            }

            size_t dataIdx = nodeStart + nodeCount;
            if (typeChar == 'A') dataIdx = inst.nodes.size() + 2; 

            for (size_t i = dataIdx; i < compTokens.size(); ++i) {
                std::string token = compTokens[i];
                if (token.empty()) continue;

                const size_t eq = token.find('=');
                if (eq == std::string::npos) {
                    if (i == dataIdx) {
                        if (typeChar == 'R' || typeChar == 'C' || typeChar == 'L') {
                            std::string key = (typeChar == 'R') ? "resistance" : (typeChar == 'C' ? "capacitance" : "inductance");
                            double v = 0.0;
                            if (parseNumeric(token, v)) inst.params[key] = v;
                            else inst.paramExpressions[key] = token;
                        } else if (typeChar == 'D' || typeChar == 'Q' || typeChar == 'M' || typeChar == 'J' || typeChar == 'Z') {
                            inst.modelName = token;
                        } else if (typeChar == 'V' || typeChar == 'I' || typeChar == 'E' || typeChar == 'G') {
                            std::string key = (typeChar == 'V') ? "voltage" : (typeChar == 'I' ? "current" : "gain");
                            double v = 0.0;
                            if (parseNumeric(token, v)) inst.params[key] = v;
                            else inst.paramExpressions[key] = token;
                        }
                    }
                    continue;
                }

                std::string key = token.substr(0, eq);
                const std::string val = token.substr(eq + 1);
                if (key.empty()) continue;

                double parsed = 0.0;
                if (parseNumeric(val, parsed)) {
                    inst.params[key] = parsed;
                } else {
                    inst.paramExpressions[key] = val;
                }
            }
        }

        if (!subcktStack.empty()) {
            subcktStack.back().sub.components.push_back(inst);
        } else {
            netlist.addComponent(inst);
        }
    }

    if (!subcktStack.empty()) {
        fail(logicalLines.empty() ? 0 : logicalLines.back().lineNo, "unterminated .subckt (missing .ends)", subcktStack.back().sub.name);
    }

    return options.strict ? !hadErrors : true;
}
