#include "sim_meas_evaluator.h"
#include "sim_value_parser.h"
#include "sim_expression.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <cctype>
#include <limits>
#include <map>

namespace {

QString qFromStd(const std::string& s) { return QString::fromStdString(s); }

const std::vector<double>* commonXAxis(const SimResults& results);

double interpolateWaveformValue(const SimWaveform& w, double x);

bool evaluateExpression(
    const SimResults& results,
    const std::string& expr,
    double x,
    bool useInterpolation,
    size_t sampleIndex,
    const std::map<std::string, double>& priorMeasurements,
    double& out
);

std::string lowerCopy(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return r;
}

bool parseSpiceNum(const std::string& s, double& out) {
    return SimValueParser::parseSpiceNumber(qFromStd(s), out);
}

std::string trimCopy(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::vector<std::string> tokenizeMeas(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    int parenDepth = 0;
    int braceDepth = 0;
    for (char c : line) {
        if (std::isspace(static_cast<unsigned char>(c)) && parenDepth == 0 && braceDepth == 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        if (c == '(') ++parenDepth;
        else if (c == ')' && parenDepth > 0) --parenDepth;
        else if (c == '{') ++braceDepth;
        else if (c == '}' && braceDepth > 0) --braceDepth;
        current.push_back(c);
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

bool splitCondition(const std::string& text, std::string& lhs, std::string& rhs) {
    int parenDepth = 0;
    int braceDepth = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(') ++parenDepth;
        else if (c == ')' && parenDepth > 0) --parenDepth;
        else if (c == '{') ++braceDepth;
        else if (c == '}' && braceDepth > 0) --braceDepth;
        else if (c == '=' && parenDepth == 0 && braceDepth == 0) {
            lhs = text.substr(0, i);
            rhs = text.substr(i + 1);
            return true;
        }
    }
    return false;
}

bool isTriggerQualifier(const std::string& token) {
    const std::string lower = lowerCopy(token);
    return lower == "rise" || lower.rfind("rise=", 0) == 0 ||
           lower == "fall" || lower.rfind("fall=", 0) == 0 ||
           lower == "cross" || lower.rfind("cross=", 0) == 0 ||
           lower == "td" || lower.rfind("td=", 0) == 0 ||
           lower == "targ" || lower == "trig" || lower == "from" ||
           lower == "to" || lower.rfind("from=", 0) == 0 ||
           lower.rfind("to=", 0) == 0;
}

bool parseTriggerConditionToken(const std::string& text, MeasTrigger& trig) {
    std::string lhs;
    std::string rhs;
    if (splitCondition(text, lhs, rhs)) {
        trig.lhsExpr = trimCopy(lhs);
        trig.rhsExpr = trimCopy(rhs);
        if (trig.signal.empty()) trig.signal = trig.lhsExpr;
        if (!trig.rhsExpr.empty()) {
            double parsed = 0.0;
            if (parseSpiceNum(trig.rhsExpr, parsed)) trig.value = parsed;
        }
        return !trig.lhsExpr.empty() && !trig.rhsExpr.empty();
    }

    trig.signal = text;
    trig.lhsExpr = text;
    return !trig.lhsExpr.empty();
}

bool buildExpressionWaveform(
    const SimResults& results,
    const std::string& expr,
    const std::map<std::string, double>& priorMeasurements,
    SimWaveform& out
) {
    const std::vector<double>* xAxis = commonXAxis(results);
    if (!xAxis || xAxis->empty()) return false;

    out.name = expr;
    out.xData = *xAxis;
    out.yData.clear();
    out.yData.reserve(xAxis->size());
    for (size_t i = 0; i < xAxis->size(); ++i) {
        double value = 0.0;
        if (!evaluateExpression(results, expr, (*xAxis)[i], false, i, priorMeasurements, value)) {
            return false;
        }
        out.yData.push_back(value);
    }
    return true;
}

std::string rewriteWrappedWaveformCalls(
    const std::string& expr,
    std::map<std::string, std::string>& placeholderToExpr
) {
    std::string out;
    size_t i = 0;
    int counter = 0;
    while (i < expr.size()) {
        if (!std::isalpha(static_cast<unsigned char>(expr[i]))) {
            out.push_back(expr[i++]);
            continue;
        }

        const size_t identStart = i;
        while (i < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[i])) || expr[i] == '_')) ++i;
        const std::string ident = expr.substr(identStart, i - identStart);
        const std::string identLower = lowerCopy(ident);
        if (i < expr.size() && expr[i] == '(' &&
            (identLower == "mag" || identLower == "db" || identLower == "ph" || identLower == "phase" ||
             identLower == "re" || identLower == "real" || identLower == "im" || identLower == "imag")) {
            const size_t callStart = identStart;
            int depth = 0;
            size_t j = i;
            for (; j < expr.size(); ++j) {
                if (expr[j] == '(') ++depth;
                else if (expr[j] == ')') {
                    --depth;
                    if (depth == 0) {
                        ++j;
                        break;
                    }
                }
            }
            if (depth == 0 && j > callStart) {
                const std::string original = expr.substr(callStart, j - callStart);
                const std::string placeholder = "MEASVAR_" + std::to_string(counter++);
                placeholderToExpr[placeholder] = original;
                out += placeholder;
                i = j;
                continue;
            }
        }

        out += ident;
    }
    return out;
}

const std::vector<double>* commonXAxis(const SimResults& results) {
    for (const auto& w : results.waveforms) {
        if (!w.xData.empty()) return &w.xData;
    }
    return nullptr;
}

const SimWaveform* lookupWaveform(const SimResults& results, const std::string& signal) {
    if (signal.empty() || results.waveforms.empty()) return nullptr;
    const std::string sl = lowerCopy(signal);
    for (const auto& w : results.waveforms) {
        const std::string wn = w.name;
        if (wn == signal || lowerCopy(wn) == sl) return &w;
        if (wn.length() > 2 && wn[0] == 'V' && wn[1] == '(' && wn.back() == ')') {
            const std::string bare = wn.substr(2, wn.length() - 3);
            if (lowerCopy(bare) == sl || bare == signal) return &w;
        }
        const std::string wrapped = "V(" + signal + ")";
        if (lowerCopy(wn) == lowerCopy(wrapped)) return &w;
    }
    return nullptr;
}

bool splitWrapperCall(const std::string& text, std::string& name, std::string& arg) {
    const size_t open = text.find('(');
    if (open == std::string::npos || text.empty() || text.back() != ')') return false;
    name = lowerCopy(trimCopy(text.substr(0, open)));
    arg = trimCopy(text.substr(open + 1, text.size() - open - 2));
    return !name.empty() && !arg.empty();
}

double waveformSamplePhaseDeg(const SimWaveform& w, double x, bool useInterpolation, size_t sampleIndex) {
    if (w.yPhase.empty()) return 0.0;
    const size_t n = std::min(w.xData.size(), w.yPhase.size());
    if (n == 0) return 0.0;
    if (!useInterpolation) return (sampleIndex < n) ? w.yPhase[sampleIndex] : w.yPhase.back();
    if (n == 1 || x <= w.xData.front()) return w.yPhase.front();
    if (x >= w.xData[n - 1]) return w.yPhase[n - 1];
    auto it = std::lower_bound(w.xData.begin(), w.xData.begin() + static_cast<long>(n), x);
    size_t idx = static_cast<size_t>(std::distance(w.xData.begin(), it));
    if (idx == 0) return w.yPhase.front();
    if (idx >= n) return w.yPhase[n - 1];
    const double x0 = w.xData[idx - 1], x1 = w.xData[idx];
    const double p0 = w.yPhase[idx - 1], p1 = w.yPhase[idx];
    if (std::abs(x1 - x0) < 1e-30) return p0;
    const double frac = (x - x0) / (x1 - x0);
    return p0 + frac * (p1 - p0);
}

bool evaluateWaveformVariable(
    const SimResults& results,
    const std::string& var,
    double x,
    bool useInterpolation,
    size_t sampleIndex,
    double& out
) {
    std::string funcName;
    std::string arg;
    const SimWaveform* w = nullptr;
    if (splitWrapperCall(var, funcName, arg) &&
        (funcName == "mag" || funcName == "db" || funcName == "ph" || funcName == "phase" ||
         funcName == "re" || funcName == "real" || funcName == "im" || funcName == "imag")) {
        w = lookupWaveform(results, arg);
        if (!w) return false;
        const double mag = useInterpolation ? interpolateWaveformValue(*w, x)
                                            : ((sampleIndex < std::min(w->xData.size(), w->yData.size())) ? w->yData[sampleIndex]
                                                                                                             : (w->yData.empty() ? 0.0 : w->yData.back()));
        const double phaseDeg = waveformSamplePhaseDeg(*w, x, useInterpolation, sampleIndex);
        const double phaseRad = phaseDeg * std::acos(-1.0) / 180.0;
        if (funcName == "mag") out = mag;
        else if (funcName == "db") out = (mag > 0.0) ? 20.0 * std::log10(mag) : -300.0;
        else if (funcName == "ph" || funcName == "phase") out = phaseDeg;
        else if (funcName == "im" || funcName == "imag") out = mag * std::sin(phaseRad);
        else out = mag * std::cos(phaseRad);
        return true;
    }

    w = lookupWaveform(results, var);
    if (!w) return false;
    const double mag = useInterpolation ? interpolateWaveformValue(*w, x)
                                        : ((sampleIndex < std::min(w->xData.size(), w->yData.size())) ? w->yData[sampleIndex]
                                                                                                         : (w->yData.empty() ? 0.0 : w->yData.back()));
    if (results.analysisType == SimAnalysisType::AC && !w->yPhase.empty()) {
        const double phaseDeg = waveformSamplePhaseDeg(*w, x, useInterpolation, sampleIndex);
        out = mag * std::cos(phaseDeg * std::acos(-1.0) / 180.0);
    } else {
        out = mag;
    }
    return true;
}

double interpolateWaveformValue(const SimWaveform& w, double x) {
    const size_t n = std::min(w.xData.size(), w.yData.size());
    if (n == 0) return 0.0;
    if (n == 1 || x <= w.xData.front()) return w.yData.front();
    if (x >= w.xData[n - 1]) return w.yData[n - 1];
    auto it = std::lower_bound(w.xData.begin(), w.xData.begin() + static_cast<long>(n), x);
    size_t idx = static_cast<size_t>(std::distance(w.xData.begin(), it));
    if (idx == 0) return w.yData.front();
    if (idx >= n) return w.yData[n - 1];
    const double x0 = w.xData[idx - 1], x1 = w.xData[idx];
    const double y0 = w.yData[idx - 1], y1 = w.yData[idx];
    if (std::abs(x1 - x0) < 1e-30) return y0;
    const double frac = (x - x0) / (x1 - x0);
    return y0 + frac * (y1 - y0);
}

bool evaluateExpression(
    const SimResults& results,
    const std::string& expr,
    double x,
    bool useInterpolation,
    size_t sampleIndex,
    const std::map<std::string, double>& priorMeasurements,
    double& out
) {
    std::map<std::string, std::string> placeholderToExpr;
    const std::string rewrittenExpr = rewriteWrappedWaveformCalls(expr, placeholderToExpr);
    Sim::Expression parsed(rewrittenExpr);
    if (!parsed.isValid()) return false;
    std::map<std::string, double> vars = priorMeasurements;
    vars["time"] = x;
    for (const auto& var : parsed.getVariables()) {
        if (vars.count(var)) continue;
        const std::string resolvedVar = placeholderToExpr.count(var) ? placeholderToExpr.at(var) : var;
        double waveformValue = 0.0;
        if (!evaluateWaveformVariable(results, resolvedVar, x, useInterpolation, sampleIndex, waveformValue)) {
            vars[var] = 0.0;
            continue;
        }
        vars[var] = waveformValue;
    }
    std::vector<double> values;
    for (const auto& var : parsed.getVariables()) values.push_back(vars.count(var) ? vars.at(var) : 0.0);
    out = parsed.evaluate(values.data(), static_cast<int>(values.size()));
    return true;
}

// Find the crossing index in a waveform where signal crosses 'value'
// starting from index 'startIdx'. Returns -1 if not found.
// 'rising': true = look for upward crossing, false = downward
int findCrossingIdx(
    const std::vector<double>& y,
    const std::vector<double>& x,
    double value,
    int startIdx,
    bool rising
) {
    for (int i = std::max(1, startIdx); i < static_cast<int>(y.size()); ++i) {
        double prev = y[i - 1];
        double curr = y[i];
        if (rising) {
            if (prev < value && curr >= value) return i;
        } else {
            if (prev > value && curr <= value) return i;
        }
    }
    return -1;
}

// Interpolate x-value at crossing point
double interpolateX(
    const std::vector<double>& x,
    const std::vector<double>& y,
    int idx,
    double value
) {
    if (idx <= 0 || idx >= static_cast<int>(x.size())) return x.empty() ? 0.0 : x[std::max(0, idx)];
    double y0 = y[idx - 1], y1 = y[idx];
    double x0 = x[idx - 1], x1 = x[idx];
    if (std::abs(y1 - y0) < 1e-30) return x0;
    double frac = (value - y0) / (y1 - y0);
    return x0 + frac * (x1 - x0);
}

} // namespace

std::string SimMeasEvaluator::toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return r;
}

MeasFunction SimMeasEvaluator::parseFunction(const std::string& token) {
    std::string t = toLower(token);
    if (t == "find") return MeasFunction::FIND;
    if (t == "deriv") return MeasFunction::DERIV;
    if (t == "param") return MeasFunction::PARAM;
    if (t == "max") return MeasFunction::MAX;
    if (t == "min") return MeasFunction::MIN;
    if (t == "pp" || t == "peak-to-peak" || t == "peak_to_peak") return MeasFunction::PP;
    if (t == "avg" || t == "average") return MeasFunction::AVG;
    if (t == "rms") return MeasFunction::RMS;
    if (t == "n") return MeasFunction::N;
    if (t == "integ" || t == "integral") return MeasFunction::INTEG;
    if (t == "min_at") return MeasFunction::MIN_AT;
    if (t == "max_at") return MeasFunction::MAX_AT;
    if (t == "first") return MeasFunction::FIRST;
    if (t == "last") return MeasFunction::LAST;
    if (t == "duty") return MeasFunction::DUTY;
    if (t == "slewrate" || t == "slew_rate") return MeasFunction::SLEWRATE;
    if (t == "slewrate_fall") return MeasFunction::SLEWRATE_FALL;
    if (t == "slewrate_rise") return MeasFunction::SLEWRATE_RISE;
    if (t == "freq" || t == "frequency") return MeasFunction::FREQ;
    if (t == "period") return MeasFunction::PERIOD;
    if (t == "trig") return MeasFunction::TRIG_TARG; // start of trig/targ
    return MeasFunction::Unknown;
}

bool SimMeasEvaluator::parseTrigger(
    const std::vector<std::string>& tokens,
    size_t& pos,
    MeasTrigger& trig
) {
    if (pos >= tokens.size()) return false;

    std::string tok = toLower(tokens[pos]);

    // Expect "val=" or "val" keyword
    if (tok == "val") {
        pos++;
        if (pos < tokens.size() && tokens[pos] == "=") pos++;
        if (pos >= tokens.size()) return false;
        trig.rhsExpr = tokens[pos];
        if (!parseSpiceNum(tokens[pos], trig.value) && trig.rhsExpr.empty()) return false;
        pos++;
    } else if (tok.substr(0, 4) == "val=") {
        std::string valStr = tokens[pos].substr(4);
        trig.rhsExpr = valStr;
        if (!parseSpiceNum(valStr, trig.value) && trig.rhsExpr.empty()) return false;
        pos++;
    } else {
        // Direct numeric or expression value (no "val=" keyword)
        if (pos < tokens.size() && !isTriggerQualifier(tokens[pos])) {
            trig.rhsExpr = tokens[pos];
            double v = 0.0;
            if (parseSpiceNum(tokens[pos], v)) {
                trig.value = v;
            }
            pos++;
        }
    }

    // Parse optional RISE/FALL, TD=, CROSS=
    while (pos < tokens.size()) {
        tok = toLower(tokens[pos]);
        if (tok == "rise" || tok.rfind("rise=", 0) == 0) {
            trig.rising = true;
            trig.useRiseFall = true;
            if (tok.find('=') != std::string::npos) {
                const std::string rhs = tok.substr(tok.find('=') + 1);
                if (toLower(rhs) == "last") trig.useLast = true;
                else {
                    int idx = std::stoi(rhs);
                    if (idx > 0) trig.index = idx;
                }
            }
            pos++;
        } else if (tok == "fall" || tok.rfind("fall=", 0) == 0) {
            trig.rising = false;
            trig.useRiseFall = true;
            if (tok.find('=') != std::string::npos) {
                const std::string rhs = tok.substr(tok.find('=') + 1);
                if (toLower(rhs) == "last") trig.useLast = true;
                else {
                    int idx = std::stoi(rhs);
                    if (idx > 0) trig.index = idx;
                }
            }
            pos++;
        } else if (tok.substr(0, 3) == "td=") {
            parseSpiceNum(tok.substr(3), trig.td);
            pos++;
        } else if (tok == "td") {
            pos++;
            if (pos < tokens.size()) {
                parseSpiceNum(tokens[pos], trig.td);
                pos++;
            }
        } else if (tok.substr(0, 6) == "cross=") {
            const std::string rhs = tok.substr(6);
            if (toLower(rhs) == "last") trig.useLast = true;
            else parseSpiceNum(rhs, trig.cross);
            trig.useCross = true;
            pos++;
        } else if (tok == "cross") {
            pos++;
            if (pos < tokens.size()) {
                if (toLower(tokens[pos]) == "last") trig.useLast = true;
                else parseSpiceNum(tokens[pos], trig.cross);
                trig.useCross = true;
                pos++;
            }
        } else {
            break;
        }
    }

    return true;
}

bool SimMeasEvaluator::parse(
    const std::string& line,
    int lineNumber,
    const std::string& sourceName,
    MeasStatement& out
) {
    std::vector<std::string> tokens = tokenizeMeas(line);

    if (tokens.size() < 4) return false;

    size_t pos = 0;

    // Skip ".meas" / ".measur" / ".measure"
    std::string cmd = toLower(tokens[pos]);
    if (cmd == ".meas" || cmd == ".measur" || cmd == ".measure") {
        pos++;
    }
    if (pos >= tokens.size()) return false;

    out.lineNumber = lineNumber;
    out.sourceName = sourceName;

    // Analysis type: tran, ac, dc, noise, etc. (or "cur" for "current")
    out.analysisType = toLower(tokens[pos]);
    pos++;
    if (pos >= tokens.size()) return false;

    // Measurement name
    out.name = tokens[pos];
    pos++;
    if (pos >= tokens.size()) return false;

    // Function or TRIG keyword
    std::string funcTok = toLower(tokens[pos]);

    if (funcTok == "trig") {
        // Conditional measurement: .meas <type> <name> TRIG <signal> VAL=<v> RISE=<n> TARG <signal> VAL=<v> RISE=<n>
        out.hasTrigTarg = true;
        out.function = MeasFunction::TRIG_TARG;
        pos++;

        // Parse TRIG signal
        if (pos >= tokens.size()) return false;
        if (!parseTriggerConditionToken(tokens[pos], out.trig)) return false;
        pos++;

        // Parse TRIG conditions (VAL=, RISE=, etc.)
        if (pos < tokens.size() && toLower(tokens[pos]) != "targ") {
            parseTrigger(tokens, pos, out.trig);
        }

        // Expect TARG keyword
        if (pos >= tokens.size() || toLower(tokens[pos]) != "targ") {
            // Some variants use AT instead of TARG
            if (pos < tokens.size() && toLower(tokens[pos]) == "at") {
                pos++;
            } else {
                return false;
            }
        } else {
            pos++;
        }

        // Parse TARG signal
        if (pos >= tokens.size()) return false;
        if (!parseTriggerConditionToken(tokens[pos], out.targ)) return false;
        pos++;

        // Parse TARG conditions
        if (pos < tokens.size()) {
            parseTrigger(tokens, pos, out.targ);
        }

        return true;
    }

    // Direct function form: .meas <type> <name> <func> <signal>
    MeasFunction func = parseFunction(funcTok);
    if (func == MeasFunction::Unknown && funcTok == "when") {
        func = MeasFunction::FIND;
        out.hasWhen = true;
        out.expr.clear();
    }
    if (func == MeasFunction::Unknown) return false;
    out.function = func;
    pos++;

    if (func == MeasFunction::PARAM) {
        std::ostringstream joined;
        while (pos < tokens.size()) {
            if (joined.tellp() > 0) joined << ' ';
            joined << tokens[pos++];
        }
        out.expr = joined.str();
        return !out.expr.empty();
    }

    if (func == MeasFunction::FIND || func == MeasFunction::DERIV) {
        if (!out.hasWhen) {
            if (pos >= tokens.size()) return false;
            out.expr = tokens[pos++];
        } else {
            if (pos >= tokens.size()) return false;
            std::string lhs, rhs;
            if (!splitCondition(tokens[pos], lhs, rhs)) return false;
            out.when.lhsExpr = lhs;
            out.when.rhsExpr = rhs;
            pos++;
            parseTrigger(tokens, pos, out.when);
        }
        out.signal = out.expr;
    } else {
        if (pos >= tokens.size()) return false;
        out.signal = tokens[pos];
        out.expr = out.signal;
        pos++;
    }

    while (pos < tokens.size()) {
        const std::string t = toLower(tokens[pos]);
        if (t.rfind("at=", 0) == 0) {
            out.hasAt = parseSpiceNum(tokens[pos].substr(3), out.at);
            pos++;
            continue;
        }
        if (t == "at") {
            pos++;
            if (pos < tokens.size()) out.hasAt = parseSpiceNum(tokens[pos++], out.at);
            continue;
        }
        if (t == "when") {
            out.hasWhen = true;
            pos++;
            if (pos >= tokens.size()) return false;
            std::string lhs, rhs;
            if (!splitCondition(tokens[pos], lhs, rhs)) return false;
            out.when.lhsExpr = lhs;
            out.when.rhsExpr = rhs;
            pos++;
            parseTrigger(tokens, pos, out.when);
            continue;
        }
        if (t == "trig") {
            out.hasTrigTarg = true;
            pos++;
            if (pos >= tokens.size()) return false;
            if (!parseTriggerConditionToken(tokens[pos], out.trig)) return false;
            pos++;
            parseTrigger(tokens, pos, out.trig);
            continue;
        }
        if (t == "targ") {
            out.hasTrigTarg = true;
            pos++;
            if (pos >= tokens.size()) return false;
            if (!parseTriggerConditionToken(tokens[pos], out.targ)) return false;
            pos++;
            parseTrigger(tokens, pos, out.targ);
            continue;
        }
        break;
    }

    // Optional range window: FROM=<x> TO=<x> (or FROM <x> TO <x>)
    while (pos < tokens.size()) {
        const std::string t = toLower(tokens[pos]);
        if (t.rfind("from=", 0) == 0) {
            const std::string value = tokens[pos].substr(5);
            double parsed = 0.0;
            if (parseSpiceNum(value, parsed)) {
                out.hasFrom = true;
                out.from = parsed;
            }
            pos++;
            continue;
        }
        if (t == "from") {
            pos++;
            if (pos < tokens.size()) {
                double parsed = 0.0;
                if (parseSpiceNum(tokens[pos], parsed)) {
                    out.hasFrom = true;
                    out.from = parsed;
                }
                pos++;
            }
            continue;
        }
        if (t.rfind("to=", 0) == 0) {
            const std::string value = tokens[pos].substr(3);
            double parsed = 0.0;
            if (parseSpiceNum(value, parsed)) {
                out.hasTo = true;
                out.to = parsed;
            }
            pos++;
            continue;
        }
        if (t == "to") {
            pos++;
            if (pos < tokens.size()) {
                double parsed = 0.0;
                if (parseSpiceNum(tokens[pos], parsed)) {
                    out.hasTo = true;
                    out.to = parsed;
                }
                pos++;
            }
            continue;
        }
        pos++;
    }

    return true;
}

const SimWaveform* SimMeasEvaluator::findWaveform(
    const SimResults& results,
    const std::string& signal
) {
    if (signal.empty() || results.waveforms.empty()) return nullptr;

    std::string s = signal;
    // Strip outer parens if present: V(out) -> V(out), out -> V(out)
    // Also handle I(V1) format

    for (const auto& w : results.waveforms) {
        std::string wn = w.name;
        // Exact match
        if (wn == s) return &w;
        // Case-insensitive match
        std::string wnl = toLower(wn);
        std::string sl = toLower(s);
        if (wnl == sl) return &w;

        // Bare name matching V(name)
        if (wn.length() > 2 && wn[0] == 'V' && wn[1] == '(' && wn.back() == ')') {
            std::string bare = wn.substr(2, wn.length() - 3);
            if (toLower(bare) == sl || bare == s) return &w;
        }
        // Try with V() wrapper
        std::string wrapped = "V(" + s + ")";
        if (toLower(wn) == toLower(wrapped)) return &w;
    }
    return nullptr;
}

double SimMeasEvaluator::evalMAX(const SimWaveform& w) {
    if (w.yData.empty()) return 0.0;
    return *std::max_element(w.yData.begin(), w.yData.end());
}

double SimMeasEvaluator::evalMIN(const SimWaveform& w) {
    if (w.yData.empty()) return 0.0;
    return *std::min_element(w.yData.begin(), w.yData.end());
}

double SimMeasEvaluator::evalPP(const SimWaveform& w) {
    if (w.yData.empty()) return 0.0;
    auto [mn, mx] = std::minmax_element(w.yData.begin(), w.yData.end());
    return *mx - *mn;
}

double SimMeasEvaluator::evalAVG(const SimWaveform& w) {
    if (w.yData.empty()) return 0.0;
    double sum = 0.0;
    for (double v : w.yData) sum += v;
    return sum / static_cast<double>(w.yData.size());
}

double SimMeasEvaluator::evalRMS(const SimWaveform& w) {
    if (w.yData.empty()) return 0.0;
    double sumSq = 0.0;
    for (double v : w.yData) sumSq += v * v;
    return std::sqrt(sumSq / static_cast<double>(w.yData.size()));
}

double SimMeasEvaluator::evalINTEG(const SimWaveform& w) {
    if (w.xData.size() < 2 || w.yData.size() < 2) return 0.0;
    double integral = 0.0;
    size_t n = std::min(w.xData.size(), w.yData.size());
    for (size_t i = 1; i < n; ++i) {
        double dt = w.xData[i] - w.xData[i - 1];
        integral += 0.5 * (w.yData[i] + w.yData[i - 1]) * dt;
    }
    return integral;
}

double SimMeasEvaluator::evalFIRST(const SimWaveform& w) {
    return w.yData.empty() ? 0.0 : w.yData.front();
}

double SimMeasEvaluator::evalLAST(const SimWaveform& w) {
    return w.yData.empty() ? 0.0 : w.yData.back();
}

bool SimMeasEvaluator::findCrossingTime(
    const SimWaveform& w,
    const MeasTrigger& trigger,
    double& outTime
) {
    if (w.xData.empty() || w.yData.empty()) return false;
    if (w.xData.size() != w.yData.size()) return false;

    int startIdx = 0;
    // Apply time delay
    if (trigger.td > 0.0) {
        for (size_t i = 0; i < w.xData.size(); ++i) {
            if (w.xData[i] >= trigger.td) {
                startIdx = static_cast<int>(i);
                break;
            }
        }
    }

    bool rising = trigger.rising;
    int count = 0;
    int targetIdx = trigger.useCross && trigger.cross > 0.0 ? static_cast<int>(trigger.cross)
                    : (trigger.useRiseFall ? trigger.index : 1);
    int searchFrom = startIdx;
    std::vector<double> crossings;

    while (searchFrom < static_cast<int>(w.yData.size())) {
        int idx = findCrossingIdx(w.yData, w.xData, trigger.value, searchFrom, rising);
        if (idx < 0) break;
        const double t = interpolateX(w.xData, w.yData, idx, trigger.value);
        crossings.push_back(t);
        count++;
        searchFrom = idx + 1;
    }
    if (crossings.empty()) return false;
    if (trigger.useLast) {
        outTime = crossings.back();
        return true;
    }
    if (targetIdx <= 0 || targetIdx > static_cast<int>(crossings.size())) return false;
    outTime = crossings[static_cast<size_t>(targetIdx - 1)];
    return true;
}

bool SimMeasEvaluator::findConditionCrossingTime(
    const SimResults& results,
    const MeasTrigger& trigger,
    const std::map<std::string, double>& priorMeasurements,
    double& outTime
) {
    const std::vector<double>* xAxis = commonXAxis(results);
    if (!xAxis || xAxis->size() < 2) return false;

    std::vector<double> diff;
    diff.reserve(xAxis->size());
    for (size_t i = 0; i < xAxis->size(); ++i) {
        double lhs = 0.0, rhs = 0.0;
        if (!evaluateExpression(results, trigger.lhsExpr, (*xAxis)[i], false, i, priorMeasurements, lhs)) return false;
        if (!evaluateExpression(results, trigger.rhsExpr, (*xAxis)[i], false, i, priorMeasurements, rhs)) return false;
        diff.push_back(lhs - rhs);
    }

    std::vector<double> crossings;
    for (size_t i = 1; i < diff.size(); ++i) {
        if ((*xAxis)[i] < trigger.td) continue;
        const double prev = diff[i - 1];
        const double curr = diff[i];
        bool hit = false;
        if (trigger.useRiseFall) {
            hit = trigger.rising ? (prev < 0.0 && curr >= 0.0) : (prev > 0.0 && curr <= 0.0);
        } else {
            hit = (prev <= 0.0 && curr >= 0.0) || (prev >= 0.0 && curr <= 0.0);
        }
        if (!hit) continue;
        crossings.push_back(interpolateX(*xAxis, diff, static_cast<int>(i), 0.0));
    }

    if (crossings.empty()) return false;
    if (trigger.useLast) {
        outTime = crossings.back();
        return true;
    }
    int index = 1;
    if (trigger.useCross && trigger.cross > 0.0) index = static_cast<int>(trigger.cross);
    else if (trigger.useRiseFall && trigger.index > 0) index = trigger.index;
    if (index <= 0 || index > static_cast<int>(crossings.size())) return false;
    outTime = crossings[static_cast<size_t>(index - 1)];
    return true;
}

bool SimMeasEvaluator::resolveTriggerTime(
    const SimResults& results,
    const MeasTrigger& trigger,
    const std::map<std::string, double>& priorMeasurements,
    double& outTime
) {
    if (!trigger.rhsExpr.empty()) {
        return findConditionCrossingTime(results, trigger, priorMeasurements, outTime);
    }

    const SimWaveform* w = findWaveform(results, trigger.signal);
    if (!w && !trigger.lhsExpr.empty()) w = findWaveform(results, trigger.lhsExpr);
    if (w) return findCrossingTime(*w, trigger, outTime);

    MeasTrigger exprTrigger = trigger;
    exprTrigger.lhsExpr = trigger.lhsExpr.empty() ? trigger.signal : trigger.lhsExpr;
    exprTrigger.rhsExpr = std::to_string(trigger.value);
    return findConditionCrossingTime(results, exprTrigger, priorMeasurements, outTime);
}

std::vector<MeasResult> SimMeasEvaluator::evaluate(
    const std::vector<MeasStatement>& statements,
    const SimResults& results,
    const std::string& analysisType
) {
    std::vector<MeasResult> results_out;
    std::string atype = toLower(analysisType);
    std::map<std::string, double> priorMeasurements;

    for (const auto& stmt : statements) {
        if (toLower(stmt.analysisType) != atype) continue;

        MeasResult mr;
        mr.name = stmt.name;

        if (stmt.function == MeasFunction::PARAM) {
            double value = 0.0;
            double x = 0.0;
            const auto* axis = commonXAxis(results);
            if (axis && !axis->empty()) x = axis->back();
            if (!evaluateExpression(results, stmt.expr, x, true, axis ? axis->size() - 1 : 0, priorMeasurements, value)) {
                mr.error = "Could not evaluate PARAM expression";
            } else {
                mr.value = value;
                mr.valid = true;
                priorMeasurements[mr.name] = mr.value;
            }
            results_out.push_back(mr);
            continue;
        }

        if (stmt.function == MeasFunction::FIND || stmt.function == MeasFunction::DERIV) {
            double sampleTime = 0.0;
            if (stmt.hasAt) sampleTime = stmt.at;
            else if (stmt.hasWhen) {
                if (!findConditionCrossingTime(results, stmt.when, priorMeasurements, sampleTime)) {
                    mr.error = "WHEN crossing not found";
                    results_out.push_back(mr);
                    continue;
                }
            } else {
                const auto* axis = commonXAxis(results);
                sampleTime = (axis && !axis->empty()) ? axis->back() : 0.0;
            }

            if (stmt.function == MeasFunction::FIND && stmt.expr.empty()) {
                mr.value = sampleTime;
                mr.valid = true;
                priorMeasurements[mr.name] = mr.value;
                results_out.push_back(mr);
                continue;
            }

            if (stmt.function == MeasFunction::FIND) {
                double value = 0.0;
                if (!evaluateExpression(results, stmt.expr, sampleTime, true, 0, priorMeasurements, value)) {
                    mr.error = "Could not evaluate FIND expression";
                } else {
                    mr.value = value;
                    mr.valid = true;
                    priorMeasurements[mr.name] = mr.value;
                }
            } else {
                const double delta = 1e-9;
                double left = 0.0, right = 0.0;
                if (!evaluateExpression(results, stmt.expr, sampleTime - delta, true, 0, priorMeasurements, left) ||
                    !evaluateExpression(results, stmt.expr, sampleTime + delta, true, 0, priorMeasurements, right)) {
                    mr.error = "Could not evaluate DERIV expression";
                } else {
                    mr.value = (right - left) / (2.0 * delta);
                    mr.valid = true;
                    priorMeasurements[mr.name] = mr.value;
                }
            }
            results_out.push_back(mr);
            continue;
        }

        if (stmt.function == MeasFunction::TRIG_TARG) {
            double trigTime = 0.0, targTime = 0.0;
            if (!resolveTriggerTime(results, stmt.trig, priorMeasurements, trigTime)) {
                mr.error = "TRIG crossing not found";
                results_out.push_back(mr);
                continue;
            }
            if (!resolveTriggerTime(results, stmt.targ, priorMeasurements, targTime)) {
                mr.error = "TARG crossing not found";
                results_out.push_back(mr);
                continue;
            }

            mr.value = targTime - trigTime;
            mr.valid = true;
            priorMeasurements[mr.name] = mr.value;
            results_out.push_back(mr);
            continue;
        }

        // Direct function evaluation
        SimWaveform exprWaveform;
        const SimWaveform* w = findWaveform(results, stmt.signal);
        if (w) {
            exprWaveform = *w;
        } else if (buildExpressionWaveform(results, stmt.expr, priorMeasurements, exprWaveform)) {
            w = &exprWaveform;
        } else {
            mr.error = "Signal not found: " + stmt.signal;
            results_out.push_back(mr);
            continue;
        }

        SimWaveform view = *w;
        bool hasFrom = stmt.hasFrom;
        bool hasTo = stmt.hasTo;
        double from = stmt.from;
        double to = stmt.to;
        if (stmt.hasTrigTarg) {
            double fromX = view.xData.empty() ? 0.0 : view.xData.front();
            double toX = view.xData.empty() ? 0.0 : view.xData.back();
            if (!stmt.trig.signal.empty()) {
                if (!resolveTriggerTime(results, stmt.trig, priorMeasurements, fromX)) {
                    mr.error = "TRIG crossing not found";
                    results_out.push_back(mr);
                    continue;
                }
            }
            if (!stmt.targ.signal.empty()) {
                if (!resolveTriggerTime(results, stmt.targ, priorMeasurements, toX)) {
                    mr.error = "TARG crossing not found";
                    results_out.push_back(mr);
                    continue;
                }
            }
            hasFrom = true;
            from = fromX;
            hasTo = true;
            to = toX;
        }
        if (hasFrom || hasTo) {
            const double lo = hasFrom ? from : -std::numeric_limits<double>::infinity();
            const double hi = hasTo ? to : std::numeric_limits<double>::infinity();
            std::vector<double> xWin;
            std::vector<double> yWin;
            const size_t n = std::min(view.xData.size(), view.yData.size());
            xWin.reserve(n);
            yWin.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                const double x = view.xData[i];
                if (x < lo || x > hi) continue;
                xWin.push_back(x);
                yWin.push_back(view.yData[i]);
            }
            view.xData.swap(xWin);
            view.yData.swap(yWin);
            if (view.xData.empty() || view.yData.empty()) {
                mr.error = "No points in requested FROM/TO window";
                results_out.push_back(mr);
                continue;
            }
        }

        switch (stmt.function) {
            case MeasFunction::MAX:
                mr.value = evalMAX(view);
                mr.valid = true;
                break;
            case MeasFunction::MIN:
                mr.value = evalMIN(view);
                mr.valid = true;
                break;
            case MeasFunction::PP:
                mr.value = evalPP(view);
                mr.valid = true;
                break;
            case MeasFunction::AVG:
                mr.value = evalAVG(view);
                mr.valid = true;
                break;
            case MeasFunction::RMS:
                mr.value = evalRMS(view);
                mr.valid = true;
                break;
            case MeasFunction::N:
                mr.value = static_cast<double>(view.yData.size());
                mr.valid = true;
                break;
            case MeasFunction::INTEG:
                mr.value = evalINTEG(view);
                mr.valid = true;
                break;
            case MeasFunction::FIRST:
                mr.value = evalFIRST(view);
                mr.valid = true;
                break;
            case MeasFunction::LAST:
                mr.value = evalLAST(view);
                mr.valid = true;
                break;
            case MeasFunction::MIN_AT: {
                if (view.yData.empty() || view.xData.empty()) {
                    mr.error = "Empty waveform";
                    break;
                }
                auto mnIt = std::min_element(view.yData.begin(), view.yData.end());
                size_t idx = static_cast<size_t>(std::distance(view.yData.begin(), mnIt));
                mr.value = view.xData[idx];
                mr.valid = true;
                break;
            }
            case MeasFunction::MAX_AT: {
                if (view.yData.empty() || view.xData.empty()) {
                    mr.error = "Empty waveform";
                    break;
                }
                auto mxIt = std::max_element(view.yData.begin(), view.yData.end());
                size_t idx = static_cast<size_t>(std::distance(view.yData.begin(), mxIt));
                mr.value = view.xData[idx];
                mr.valid = true;
                break;
            }
            case MeasFunction::FREQ: {
                if (view.xData.size() < 2 || view.yData.empty()) {
                    mr.error = "Insufficient data for FREQ";
                    break;
                }
                // Estimate frequency by counting zero crossings (rising)
                double midY = evalAVG(view);
                int crossings = 0;
                for (size_t i = 1; i < view.yData.size(); ++i) {
                    if (view.yData[i - 1] < midY && view.yData[i] >= midY) crossings++;
                }
                double totalTime = view.xData.back() - view.xData.front();
                if (totalTime > 0 && crossings > 0) {
                    mr.value = static_cast<double>(crossings) / totalTime;
                    mr.valid = true;
                } else {
                    mr.error = "No crossings found";
                }
                break;
            }
            case MeasFunction::PERIOD: {
                if (view.xData.size() < 2 || view.yData.empty()) {
                    mr.error = "Insufficient data for PERIOD";
                    break;
                }
                double midY = evalAVG(view);
                double firstCross = 0.0, secondCross = 0.0;
                int found = 0;
                for (size_t i = 1; i < view.yData.size(); ++i) {
                    if (view.yData[i - 1] < midY && view.yData[i] >= midY) {
                        double t = interpolateX(view.xData, view.yData, static_cast<int>(i), midY);
                        if (found == 0) firstCross = t;
                        else if (found == 1) { secondCross = t; break; }
                        found++;
                    }
                }
                if (found >= 2) {
                    mr.value = secondCross - firstCross;
                    mr.valid = true;
                } else {
                    mr.error = "Could not find two periods";
                }
                break;
            }
            case MeasFunction::DUTY: {
                if (view.xData.size() < 2 || view.yData.empty()) {
                    mr.error = "Insufficient data for DUTY";
                    break;
                }
                double midY = evalAVG(view);
                double highTime = 0.0;
                for (size_t i = 1; i < view.yData.size(); ++i) {
                    if (view.yData[i] >= midY) {
                        highTime += view.xData[i] - view.xData[i - 1];
                    }
                }
                double totalTime = view.xData.back() - view.xData.front();
                if (totalTime > 0) {
                    mr.value = highTime / totalTime;
                    mr.valid = true;
                } else {
                    mr.error = "Zero time span";
                }
                break;
            }
            case MeasFunction::SLEWRATE:
            case MeasFunction::SLEWRATE_RISE:
            case MeasFunction::SLEWRATE_FALL: {
                if (view.xData.size() < 2 || view.yData.empty()) {
                    mr.error = "Insufficient data for SLEWRATE";
                    break;
                }
                double maxSlew = 0.0;
                for (size_t i = 1; i < std::min(view.xData.size(), view.yData.size()); ++i) {
                    double dt = view.xData[i] - view.xData[i - 1];
                    if (dt <= 0) continue;
                    double slew = (view.yData[i] - view.yData[i - 1]) / dt;
                    if (stmt.function == MeasFunction::SLEWRATE) {
                        if (std::abs(slew) > std::abs(maxSlew)) maxSlew = slew;
                    } else if (stmt.function == MeasFunction::SLEWRATE_RISE) {
                        if (slew > maxSlew) maxSlew = slew;
                    } else {
                        if (slew < maxSlew) maxSlew = slew;
                    }
                }
                mr.value = maxSlew;
                mr.valid = true;
                break;
            }
            default:
                mr.error = "Unsupported function";
                break;
        }

        results_out.push_back(mr);
        if (mr.valid) priorMeasurements[mr.name] = mr.value;
    }

    return results_out;
}
