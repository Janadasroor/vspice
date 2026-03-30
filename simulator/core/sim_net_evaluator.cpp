#include "sim_net_evaluator.h"

#include "sim_expression.h"
#include "sim_value_parser.h"

#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <complex>
#include <cmath>

namespace {

QString qFromStd(const std::string& s) { return QString::fromStdString(s); }

std::string lowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

const SimWaveform* findWaveform(const SimResults& results, const std::string& signal) {
    const QString raw = qFromStd(signal).trimmed();
    if (raw.isEmpty()) return nullptr;
    QStringList candidates;
    candidates << raw.toUpper();
    const QRegularExpression bareNodeRe("^[A-Za-z0-9_.$:+-]+$");
    if (bareNodeRe.match(raw).hasMatch()) candidates << QString("V(%1)").arg(raw.toUpper());
    for (const auto& w : results.waveforms) {
        const QString name = QString::fromStdString(w.name).trimmed().toUpper();
        if (candidates.contains(name)) return &w;
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
    const QRegularExpression re("^V\\(\\s*([^,()]+)\\s*(?:,\\s*([^()]+)\\s*)?\\)$", QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(qFromStd(spec).trimmed());
    if (!m.hasMatch()) return false;
    posNode = m.captured(1).trimmed().toUpper().toStdString();
    negNode = m.captured(2).trimmed().toUpper().toStdString();
    return true;
}

bool parseCurrentOutputSpec(const std::string& spec, std::string& currentSignal) {
    const QRegularExpression re("^I\\(\\s*([^()]+)\\s*\\)$", QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(qFromStd(spec).trimmed());
    if (!m.hasMatch()) return false;
    currentSignal = QString("I(%1)").arg(m.captured(1).trimmed().toUpper()).toStdString();
    return true;
}

std::complex<double> complexFromMagPhase(double mag, double phaseDeg) {
    return std::polar(mag, phaseDeg * std::acos(-1.0) / 180.0);
}

void appendComplexSample(SimWaveform& w, const std::complex<double>& value) {
    w.yData.push_back(std::abs(value));
    w.yPhase.push_back(std::arg(value) * 180.0 / std::acos(-1.0));
}

} // namespace

bool SimNetEvaluator::parse(const std::string& line, int lineNumber, const std::string& sourceName, NetStatement& out) {
    const QString qline = qFromStd(line).trimmed();
    if (!qline.startsWith(".net", Qt::CaseInsensitive)) return false;

    QStringList tokens = qline.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (tokens.size() < 2) return false;

    out = NetStatement();
    out.lineNumber = lineNumber;
    out.sourceName = sourceName;

    int idx = 1;
    const QString first = tokens.value(idx);
    if (first.startsWith("V(", Qt::CaseInsensitive) || first.startsWith("I(", Qt::CaseInsensitive)) {
        out.hasOutput = true;
        out.outputSpec = first.toStdString();
        ++idx;
    }
    if (idx >= tokens.size()) return false;
    out.inputSource = tokens.value(idx).toUpper().toStdString();
    ++idx;

    for (; idx < tokens.size(); ++idx) {
        const QString tok = tokens.value(idx);
        if (tok.startsWith("Rin=", Qt::CaseInsensitive)) {
            double value = 0.0;
            if (!SimValueParser::parseSpiceNumber(tok.mid(4).trimmed(), value)) return false;
            out.hasRin = true;
            out.rin = value;
        } else if (tok.startsWith("Rout=", Qt::CaseInsensitive)) {
            double value = 0.0;
            if (!SimValueParser::parseSpiceNumber(tok.mid(5).trimmed(), value)) return false;
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
    if (lowerCopy(analysisType) != "ac") return eval;

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
        const bool outputIsVoltage = hasOutput && qFromStd(stmt.outputSpec).trimmed().startsWith("V(", Qt::CaseInsensitive);
        const bool outputIsCurrent = hasOutput && qFromStd(stmt.outputSpec).trimmed().startsWith("I(", Qt::CaseInsensitive);
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
