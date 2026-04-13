#include "sim_net_evaluator.h"

#include "sim_expression.h"
#include "sim_value_parser.h"

#include <algorithm>
#include <cctype>
#include <complex>
#include <cmath>
#include <regex>
#include <sstream>

namespace {

std::string toLower(std::string_view s) {
    std::string out(s.size(), '\0');
    std::transform(s.begin(), s.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string toUpper(std::string_view s) {
    std::string out(s.size(), '\0');
    std::transform(s.begin(), s.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) ++start;
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(start, end);
}

bool startsWithIgnoreCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    return toLower(s.substr(0, prefix.size())) == toLower(prefix);
}

const SimWaveform* findWaveform(const SimResults& results, const std::string& signal) {
    std::string sig = trim(signal);
    if (sig.empty()) return nullptr;

    std::string upper = toUpper(sig);
    std::vector<std::string> candidates;
    candidates.push_back(upper);

    // If it looks like a bare node name, also try V(NODENAME)
    static const std::regex bareNodeRe("^[A-Za-z0-9_.$:+-]+$");
    if (std::regex_match(upper, bareNodeRe)) {
        candidates.push_back("V(" + upper + ")");
    }

    for (const auto& w : results.waveforms) {
        std::string wName = toUpper(trim(w.name));
        for (const auto& c : candidates) {
            if (wName == c) return &w;
        }
    }
    return nullptr;
}

std::complex<double> sampleComplex(const SimWaveform& w, size_t i) {
    if (i >= w.yData.size()) return {0.0, 0.0};
    const double mag = w.yData[i];
    const double phaseDeg = i < w.yPhase.size() ? w.yPhase[i] : 0.0;
    const double phaseRad = phaseDeg * std::acos(-1.0) / 180.0;
    return std::polar(mag, phaseRad);
}

std::string netBaseName(const NetStatement& stmt) {
    return stmt.inputSource;
}

bool parseVoltageOutputSpec(const std::string& spec, std::string& posNode, std::string& negNode) {
    static const std::regex re(
        "^[Vv]\\(\\s*([^,()]+)\\s*(?:,\\s*([^()]+)\\s*)?\\)$");
    std::smatch m;
    std::string trimmed = trim(spec);
    if (!std::regex_match(trimmed, m, re)) return false;
    posNode = toUpper(trim(m[1].str()));
    negNode = m[2].matched ? toUpper(trim(m[2].str())) : std::string();
    return true;
}

bool parseCurrentOutputSpec(const std::string& spec, std::string& currentSignal) {
    static const std::regex re(
        "^[Ii]\\(\\s*([^()]+)\\s*\\)$");
    std::smatch m;
    std::string trimmed = trim(spec);
    if (!std::regex_match(trimmed, m, re)) return false;
    currentSignal = "I(" + toUpper(trim(m[1].str())) + ")";
    return true;
}

void appendComplexSample(SimWaveform& w, const std::complex<double>& value) {
    w.yData.push_back(std::abs(value));
    w.yPhase.push_back(std::arg(value) * 180.0 / std::acos(-1.0));
}

} // namespace

bool SimNetEvaluator::parse(const std::string& line, int lineNumber, const std::string& sourceName, NetStatement& out) {
    std::string trimmed = trim(line);
    if (!startsWithIgnoreCase(trimmed, ".net")) return false;

    // Tokenize
    std::istringstream iss(trimmed);
    std::string token;
    std::vector<std::string> tokens;
    while (iss >> token) tokens.push_back(token);
    if (tokens.size() < 2) return false;

    out = NetStatement();
    out.lineNumber = lineNumber;
    out.sourceName = sourceName;

    size_t idx = 1;
    const std::string& first = tokens[idx];
    if (startsWithIgnoreCase(first, "V(") || startsWithIgnoreCase(first, "I(")) {
        out.hasOutput = true;
        out.outputSpec = first;
        ++idx;
    }
    if (idx >= tokens.size()) return false;
    out.inputSource = toUpper(tokens[idx]);
    ++idx;

    for (; idx < tokens.size(); ++idx) {
        const std::string& tok = tokens[idx];
        if (startsWithIgnoreCase(tok, "Rin=")) {
            double value = 0.0;
            if (!SimValueParser::parseSpiceNumber(tok.substr(4), value)) return false;
            out.hasRin = true;
            out.rin = value;
        } else if (startsWithIgnoreCase(tok, "Rout=")) {
            double value = 0.0;
            if (!SimValueParser::parseSpiceNumber(tok.substr(5), value)) return false;
            out.hasRout = true;
            out.rout = value;
        }
    }

    return !out.inputSource.empty();
}

NetEvaluation SimNetEvaluator::evaluate(const std::vector<NetStatement>& statements,
                                        const std::map<std::string, NetSourceInfo>& sources,
                                        const SimResults& results,
                                        const std::string& analysisType) {
    NetEvaluation eval;
    if (toLower(analysisType) != "ac") return eval;

    for (const NetStatement& stmt : statements) {
        const auto sourceIt = sources.find(stmt.inputSource);
        if (sourceIt == sources.end()) {
            eval.diagnostics.push_back(".net source not found in netlist: " + stmt.inputSource);
            continue;
        }

        const SimWaveform* current = findWaveform(results, "I(" + stmt.inputSource + ")");
        const SimWaveform* vp = findWaveform(results, sourceIt->second.positiveNode);
        const SimWaveform* vn = sourceIt->second.negativeNode.empty() ? nullptr : findWaveform(results, sourceIt->second.negativeNode);
        if (!current || !vp) {
            eval.diagnostics.push_back(".net requires AC source current and source terminal voltages for " + stmt.inputSource);
            continue;
        }

        const std::string base = netBaseName(stmt);
        SimWaveform zin;
        SimWaveform yin;
        zin.name = "Zin(" + base + ")";
        yin.name = "Yin(" + base + ")";
        zin.xData = current->xData;
        yin.xData = current->xData;
        zin.yData.reserve(current->yData.size());
        zin.yPhase.reserve(current->yData.size());
        yin.yData.reserve(current->yData.size());
        yin.yPhase.reserve(current->yData.size());

        SimWaveform transfer;
        SimWaveform s11;
        SimWaveform s21;
        const bool hasOutput = stmt.hasOutput;
        const bool canComputeS = hasOutput && stmt.rin > 0.0 && stmt.rout > 0.0;
        const bool outputIsVoltage = hasOutput && startsWithIgnoreCase(trim(stmt.outputSpec), "V(");
        const bool outputIsCurrent = hasOutput && startsWithIgnoreCase(trim(stmt.outputSpec), "I(");
        const std::string outputLabel = stmt.outputSpec;
        if (hasOutput) {
            transfer.name = (outputIsVoltage ? "Vtransfer(" : "Itransfer(") + base + "->" + outputLabel + ")";
            transfer.xData = current->xData;
            transfer.yData.reserve(current->yData.size());
            transfer.yPhase.reserve(current->yData.size());
            if (canComputeS) {
                s11.name = "S11(" + base + ")";
                s21.name = "S21(" + base + "->" + outputLabel + ")";
                s11.xData = current->xData;
                s21.xData = current->xData;
                s11.yData.reserve(current->yData.size());
                s11.yPhase.reserve(current->yData.size());
                s21.yData.reserve(current->yData.size());
                s21.yPhase.reserve(current->yData.size());
            }
        }

        const SimWaveform* outputCurrent = nullptr;
        const SimWaveform* outputPos = nullptr;
        const SimWaveform* outputNeg = nullptr;
        std::string outputPosNode;
        std::string outputNegNode;
        if (outputIsVoltage) {
            if (!parseVoltageOutputSpec(stmt.outputSpec, outputPosNode, outputNegNode)) {
                eval.diagnostics.push_back(".net could not parse output voltage spec: " + stmt.outputSpec);
                continue;
            }
            outputPos = findWaveform(results, outputPosNode);
            outputNeg = outputNegNode.empty() ? nullptr : findWaveform(results, outputNegNode);
            if (!outputPos) {
                eval.diagnostics.push_back(".net output voltage waveform not found: " + stmt.outputSpec);
                continue;
            }
            if (!stmt.hasRout) {
                eval.diagnostics.push_back(".net " + stmt.outputSpec + " is using default Rout=1 ohm; specify Rout= for more meaningful two-port results.");
            }
        } else if (outputIsCurrent) {
            std::string outputCurrentSignal;
            if (!parseCurrentOutputSpec(stmt.outputSpec, outputCurrentSignal)) {
                eval.diagnostics.push_back(".net could not parse output current spec: " + stmt.outputSpec);
                continue;
            }
            outputCurrent = findWaveform(results, outputCurrentSignal);
            if (!outputCurrent) {
                eval.diagnostics.push_back(".net output current waveform not found: " + stmt.outputSpec);
                continue;
            }
        } else if (hasOutput) {
            eval.diagnostics.push_back(".net output specification is not implemented yet: " + stmt.outputSpec);
            continue;
        }

        const size_t count = std::min({current->xData.size(), current->yData.size(), vp->yData.size()});
        for (size_t i = 0; i < count; ++i) {
            std::complex<double> vin = sampleComplex(*vp, i);
            if (vn) vin -= sampleComplex(*vn, i);
            const std::complex<double> iin = sampleComplex(*current, i);
            if (std::abs(iin) < 1e-30) {
                zin.yData.push_back(1e300);
                zin.yPhase.push_back(0.0);
                yin.yData.push_back(0.0);
                yin.yPhase.push_back(0.0);
                if (hasOutput) {
                    transfer.yData.push_back(0.0);
                    transfer.yPhase.push_back(0.0);
                    if (canComputeS) {
                        s11.yData.push_back(0.0);
                        s11.yPhase.push_back(0.0);
                        s21.yData.push_back(0.0);
                        s21.yPhase.push_back(0.0);
                    }
                }
                continue;
            }
            const std::complex<double> z = vin / iin;
            const std::complex<double> y = std::complex<double>(1.0, 0.0) / z;
            appendComplexSample(zin, z);
            appendComplexSample(yin, y);

            if (hasOutput) {
                std::complex<double> vout(0.0, 0.0);
                std::complex<double> iout(0.0, 0.0);
                if (outputIsVoltage) {
                    vout = sampleComplex(*outputPos, i);
                    if (outputNeg) vout -= sampleComplex(*outputNeg, i);
                    appendComplexSample(transfer, vin == std::complex<double>(0.0, 0.0) ? std::complex<double>(0.0, 0.0) : vout / vin);
                    if (canComputeS) {
                        iout = -vout / stmt.rout;
                    }
                } else if (outputIsCurrent) {
                    iout = sampleComplex(*outputCurrent, i);
                    appendComplexSample(transfer, iin == std::complex<double>(0.0, 0.0) ? std::complex<double>(0.0, 0.0) : iout / iin);
                    if (canComputeS) {
                        vout = iout * stmt.rout;
                    }
                }

                if (canComputeS) {
                    const std::complex<double> a1 = (vin + stmt.rin * iin) / (2.0 * std::sqrt(stmt.rin));
                    const std::complex<double> b1 = (vin - stmt.rin * iin) / (2.0 * std::sqrt(stmt.rin));
                    const std::complex<double> b2 = (vout - stmt.rout * iout) / (2.0 * std::sqrt(stmt.rout));
                    appendComplexSample(s11, std::abs(a1) < 1e-30 ? std::complex<double>(0.0, 0.0) : b1 / a1);
                    appendComplexSample(s21, std::abs(a1) < 1e-30 ? std::complex<double>(0.0, 0.0) : b2 / a1);
                }
            }
        }

        eval.waveforms.push_back(std::move(zin));
        eval.waveforms.push_back(std::move(yin));
        if (hasOutput) {
            eval.waveforms.push_back(std::move(transfer));
            if (canComputeS) {
                eval.waveforms.push_back(std::move(s11));
                eval.waveforms.push_back(std::move(s21));
            }
        }
    }

    return eval;
}
