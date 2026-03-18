#include "raw_data_parser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <cmath>
#include <algorithm>

bool RawDataParser::loadRawAscii(const QString& path, RawData* out, QString* error) {
    if (!out) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = "Could not open raw file: " + path;
        return false;
    }

    QTextStream in(&file);
    QString line;
    bool collectingData = false;
    int numVariables = 0;
    int numPoints = 0;
    QStringList varNames;

    while (!in.atEnd()) {
        line = in.readLine().trimmed();
        if (line.startsWith("No. Variables:")) {
            numVariables = line.section(':', 1).trimmed().toInt();
        } else if (line.startsWith("No. Points:")) {
            numPoints = line.section(':', 1).trimmed().toInt();
        } else if (line.startsWith("Variables:")) {
            for (int i = 0; i < numVariables; ++i) {
                QString vLine = in.readLine().trimmed();
                QStringList parts = vLine.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 2) varNames << parts[1];
                else if (parts.size() == 1 && i == 0) varNames << "time";
            }
        } else if (line.startsWith("Values:")) {
            collectingData = true;
            break;
        }
    }

    if (!collectingData || varNames.isEmpty() || numVariables <= 0) {
        if (error) *error = "Raw file missing Variables/Values sections: " + path;
        return false;
    }

    RawData data;
    data.numVariables = numVariables;
    data.numPoints = numPoints;
    data.varNames = varNames;
    
    if (numPoints > 0) {
        data.x.reserve(numPoints);
        data.y.resize(numVariables - 1);
        for (int i = 0; i < data.y.size(); ++i) data.y[i].reserve(numPoints);

        auto nextNonEmpty = [&](QString* outLine) -> bool {
            while (!in.atEnd()) {
                const QString l = in.readLine().trimmed();
                if (!l.isEmpty()) {
                    if (outLine) *outLine = l;
                    return true;
                }
            }
            return false;
        };

        for (int p = 0; p < numPoints; ++p) {
            QString xLine;
            if (!nextNonEmpty(&xLine)) break;
            QStringList xParts = xLine.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (xParts.size() < 2) break;
            const double xVal = xParts[1].toDouble();
            data.x.push_back(xVal);

            for (int v = 1; v < numVariables; ++v) {
                QString vLine;
                if (!nextNonEmpty(&vLine)) break;
                // Some formats might have "index value" for every variable, 
                // but usually after the first one it's just the value.
                QStringList vParts = vLine.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                const double yVal = vParts.last().toDouble();
                data.y[v - 1].push_back(yVal);
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
