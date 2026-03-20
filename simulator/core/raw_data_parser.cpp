#include "raw_data_parser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <QLocale>
#include <cmath>
#include <algorithm>

bool RawDataParser::loadRawAscii(const QString& path, RawData* out, QString* error) {
    if (!out) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "Could not open raw file: " + path;
        return false;
    }

    QByteArray allData = file.readAll();
    file.close();

    if (allData.isEmpty()) {
        if (error) *error = "Raw file is empty: " + path;
        return false;
    }

    const char* dataPtr = allData.constData();
    const char* endPtr = dataPtr + allData.size();

    auto readLine = [&]() -> QByteArray {
        const char* start = dataPtr;
        while (dataPtr < endPtr && *dataPtr != '\n' && *dataPtr != '\r') {
            dataPtr++;
        }
        QByteArray line(start, dataPtr - start);
        // Skip \r\n or \n\r or just \n
        if (dataPtr < endPtr && *dataPtr == '\r') dataPtr++;
        if (dataPtr < endPtr && *dataPtr == '\n') dataPtr++;
        return line.trimmed();
    };

    bool collectingData = false;
    bool isBinary = false;
    bool isComplex = false;
    int numVariables = 0;
    int numPoints = 0;
    QStringList varNames;

    while (dataPtr < endPtr) {
        QByteArray line = readLine();
        if (line.isEmpty()) continue;

        if (line.startsWith("No. Variables:")) {
            numVariables = line.mid(14).trimmed().toInt();
        } else if (line.startsWith("No. Points:")) {
            numPoints = line.mid(11).trimmed().toInt();
        } else if (line.startsWith("Flags:")) {
            const QString flags = QString::fromLatin1(line).toLower();
            if (flags.contains("complex")) isComplex = true;
        } else if (line.startsWith("Variables:")) {
            for (int i = 0; i < numVariables; ++i) {
                QByteArray vLine = readLine();
                QStringList parts = QString::fromLatin1(vLine).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) varNames << parts[1];
                else if (parts.size() == 1 && i == 0) varNames << "time";
            }
        } else if (line.startsWith("Values:")) {
            collectingData = true;
            isBinary = false;
            break;
        } else if (line.startsWith("Binary:")) {
            collectingData = true;
            isBinary = true;
            qDebug() << "RawDataParser: Using binary parsing mode";
            break;
        }
    }

    if (!collectingData || varNames.isEmpty() || numVariables <= 0) {
        if (error) *error = "Raw file missing Variables/Values/Binary sections: " + path;
        return false;
    }

    RawData data;
    data.numVariables = numVariables;
    data.numPoints = numPoints;
    data.varNames = varNames;
    
    if (numPoints > 0) {
        data.x.reserve(numPoints);
        data.y.resize(numVariables - 1);
        data.yPhase.resize(numVariables - 1);
        data.hasPhase.resize(numVariables - 1, false);
        for (int i = 0; i < data.y.size(); ++i) data.y[i].reserve(numPoints);
        for (int i = 0; i < data.yPhase.size(); ++i) data.yPhase[i].reserve(numPoints);

        if (isBinary) {
            qint64 totalDoubles = (qint64)numPoints * numVariables;
            if (isComplex) {
                // Complex data uses real+imag for each variable, including x.
                totalDoubles = (qint64)numPoints * numVariables * 2;
            }
            qint64 remainingBytes = endPtr - dataPtr;
            if (remainingBytes >= totalDoubles * (qint64)sizeof(double)) {
                for (int p = 0; p < numPoints; ++p) {
                    double val;
                    if (isComplex) {
                        double re = 0.0, im = 0.0;
                        memcpy(&re, dataPtr, sizeof(double)); dataPtr += sizeof(double);
                        memcpy(&im, dataPtr, sizeof(double)); dataPtr += sizeof(double);
                        data.x.push_back(re); // freq/time is typically real
                        for (int v = 1; v < numVariables; ++v) {
                            memcpy(&re, dataPtr, sizeof(double)); dataPtr += sizeof(double);
                            memcpy(&im, dataPtr, sizeof(double)); dataPtr += sizeof(double);
                            const double mag = std::hypot(re, im);
                            const double phase = std::atan2(im, re) * 180.0 / std::acos(-1.0);
                            data.y[v - 1].push_back(mag);
                            data.yPhase[v - 1].push_back(phase);
                            data.hasPhase[v - 1] = true;
                        }
                    } else {
                        memcpy(&val, dataPtr, sizeof(double));
                        dataPtr += sizeof(double);
                        data.x.push_back(val);
                        for (int v = 1; v < numVariables; ++v) {
                            memcpy(&val, dataPtr, sizeof(double));
                            dataPtr += sizeof(double);
                            data.y[v - 1].push_back(val);
                        }
                    }
                }
            } else {
                if (error) *error = QString("Raw file binary payload is incomplete: %1 (Expected %2 bytes, got %3)").arg(path).arg(totalDoubles * sizeof(double)).arg(remainingBytes);
                return false;
            }
        } else {
            static const QRegularExpression spaceRe("\\s+");
            auto getNextToken = [&]() -> QByteArray {
                while (dataPtr < endPtr) {
                    const char* start = dataPtr;
                    while (dataPtr < endPtr && !isspace(*dataPtr)) dataPtr++;
                    QByteArray word(start, dataPtr - start);
                    while (dataPtr < endPtr && isspace(*dataPtr)) dataPtr++;
                    if (!word.isEmpty()) return word;
                }
                return {};
            };

            auto parseToken = [&](const QByteArray& token, double& magOut, double& phaseOut, bool& isComplexOut) {
                const QString raw = QString::fromLatin1(token).trimmed();
                if (raw.isEmpty()) return false;

                QString t = raw;
                if (t.startsWith('(') && t.endsWith(')')) {
                    t = t.mid(1, t.size() - 2).trimmed();
                }

                if (t.contains(',')) {
                    const QStringList parts = t.split(',', Qt::SkipEmptyParts);
                    if (parts.size() >= 2) {
                        bool ok1 = false, ok2 = false;
                        const double re = QLocale::c().toDouble(parts[0].trimmed(), &ok1);
                        const double im = QLocale::c().toDouble(parts[1].trimmed(), &ok2);
                        if (ok1 && ok2) {
                            magOut = std::hypot(re, im);
                            phaseOut = std::atan2(im, re) * 180.0 / std::acos(-1.0);
                            isComplexOut = true;
                            return true;
                        }
                    }
                }

                bool ok = false;
                const double val = QLocale::c().toDouble(t, &ok);
                if (!ok) return false;
                magOut = val;
                phaseOut = 0.0;
                isComplexOut = false;
                return true;
            };

            for (int p = 0; p < numPoints; ++p) {
                // In Values: format, the first variable is prefixed by an index
                getNextToken(); // skip index
                {
                    const QByteArray xTok = getNextToken();
                    const QString raw = QString::fromLatin1(xTok).trimmed();
                    double xVal = 0.0;
                    bool ok = false;
                    if (raw.contains(',')) {
                        const QStringList parts = raw.split(',', Qt::SkipEmptyParts);
                        if (!parts.isEmpty()) {
                            xVal = QLocale::c().toDouble(parts[0].trimmed(), &ok);
                        }
                    } else {
                        xVal = QLocale::c().toDouble(raw, &ok);
                    }
                    data.x.push_back(ok ? xVal : 0.0);
                }
                for (int v = 1; v < numVariables; ++v) {
                    const QByteArray tok = getNextToken();
                    double mag = 0.0;
                    double phase = 0.0;
                    bool isComplex = false;
                    if (!parseToken(tok, mag, phase, isComplex)) {
                        mag = 0.0;
                        phase = 0.0;
                        isComplex = false;
                    }
                    data.y[v - 1].push_back(mag);
                    data.yPhase[v - 1].push_back(phase);
                    if (isComplex) data.hasPhase[v - 1] = true;
                }
            }
        }
    }

    *out = std::move(data);
    return true;
}

SimResults RawData::toSimResults() const {
    SimResults res;
    res.xAxisName = varNames.isEmpty() ? "time" : varNames[0].toStdString();
    
    if (numPoints <= 0) return res;

    std::vector<double> stdX(x.begin(), x.end());

    for (int i = 1; i < varNames.size(); ++i) {
        SimWaveform w;
        w.name = varNames[i].toStdString();
        w.xData = stdX;
        if (i - 1 < y.size()) {
            w.yData = std::vector<double>(y[i - 1].begin(), y[i - 1].end());
            if (i - 1 < yPhase.size() && i - 1 < hasPhase.size() && hasPhase[i - 1]) {
                w.yPhase = std::vector<double>(yPhase[i - 1].begin(), yPhase[i - 1].end());
            }
        }
        res.waveforms.push_back(std::move(w));

        // For OP analysis, also populate nodeVoltages/branchCurrents
        if (numPoints == 1) {
            double val = y[i-1].isEmpty() ? 0.0 : y[i-1][0];
            if (QString::fromStdString(w.name).startsWith("V(", Qt::CaseInsensitive)) {
                QString node = QString::fromStdString(w.name).mid(2);
                if (node.endsWith(")")) node.chop(1);
                res.nodeVoltages[node.toStdString()] = val;
            } else if (QString::fromStdString(w.name).startsWith("I(", Qt::CaseInsensitive)) {
                QString branch = QString::fromStdString(w.name).mid(2);
                if (branch.endsWith(")")) branch.chop(1);
                res.branchCurrents[branch.toStdString()] = val;
            }
        }
    }

    return res;
}

SimResults::Snapshot SimResults::interpolateAt(double t) const {
    Snapshot snap;
    for (const auto& wave : waveforms) {
        if (wave.xData.empty()) continue;

        double val = 0.0;
        if (t <= wave.xData.front()) {
            val = wave.yData.front();
        } else if (t >= wave.xData.back()) {
            val = wave.yData.back();
        } else {
            // Binary search for interval
            auto it = std::lower_bound(wave.xData.begin(), wave.xData.end(), t);
            size_t i1 = std::distance(wave.xData.begin(), it);
            if (i1 == 0) {
                val = wave.yData.front();
            } else if (i1 >= wave.xData.size()) {
                val = wave.yData.back();
            } else {
                size_t i0 = i1 - 1;
                double x0 = wave.xData[i0];
                double x1 = wave.xData[i1];
                double y0 = wave.yData[i0];
                double y1 = wave.yData[i1];
                
                if (std::abs(x1 - x0) < 1e-18) {
                    val = y0;
                } else {
                    double factor = (t - x0) / (x1 - x0);
                    val = y0 + factor * (y1 - y0);
                }
            }
        }

        // Map to Snapshot
        if (wave.name.size() > 3 && (wave.name.substr(0, 2) == "V(" || wave.name.substr(0, 2) == "v(")) {
            std::string node = wave.name.substr(2, wave.name.size() - 3);
            snap.nodeVoltages[node] = val;
        } else if (wave.name.size() > 3 && (wave.name.substr(0, 2) == "I(" || wave.name.substr(0, 2) == "i(")) {
            std::string branch = wave.name.substr(2, wave.name.size() - 3);
            snap.branchCurrents[branch] = val;
        }
    }
    return snap;
}
