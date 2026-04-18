#include "waveform_engine.h"
#include <QtMath>
#include <cmath>
#include <random>

namespace WaveformEngine {

struct Parser {
    QString s;
    int pos = 0;
    double x_val;
    bool error = false;

    double parseExpression() {
        double res = parseTerm();
        while (pos < s.length() && !error) {
            if (s[pos] == '+') { pos++; res += parseTerm(); }
            else if (s[pos] == '-') { pos++; res -= parseTerm(); }
            else break;
        }
        return res;
    }

    double parseTerm() {
        double res = parseFactor();
        while (pos < s.length() && !error) {
            if (s[pos] == '*') { pos++; res *= parseFactor(); }
            else if (s[pos] == '/') {
                pos++;
                double d = parseFactor();
                if (d != 0) res /= d;
                else error = true;
            }
            else break;
        }
        return res;
    }

    double parseFactor() {
        if (pos >= s.length()) { error = true; return 0; }
        if (s[pos] == '(') {
            pos++;
            double res = parseExpression();
            if (pos < s.length() && s[pos] == ')') pos++;
            else error = true;
            return res;
        }
        if (s[pos] == '-') { pos++; return -parseFactor(); }
        
        if (s[pos].isDigit() || s[pos] == '.') {
            int start = pos;
            while (pos < s.length() && (s[pos].isDigit() || s[pos] == '.' || s[pos] == 'e')) pos++;
            return s.mid(start, pos - start).toDouble();
        }

        if (s.mid(pos).startsWith("x")) { pos++; return x_val; }
        if (s.mid(pos).startsWith("sin(")) { pos += 4; double v = std::sin(parseExpression()); if (pos < s.length() && s[pos] == ')') pos++; return v; }
        if (s.mid(pos).startsWith("cos(")) { pos += 4; double v = std::cos(parseExpression()); if (pos < s.length() && s[pos] == ')') pos++; return v; }
        if (s.mid(pos).startsWith("exp(")) { pos += 4; double v = std::exp(parseExpression()); if (pos < s.length() && s[pos] == ')') pos++; return v; }
        if (s.mid(pos).startsWith("log(")) { pos += 4; double v = std::log(parseExpression()); if (pos < s.length() && s[pos] == ')') pos++; return v; }
        if (s.mid(pos).startsWith("sqrt(")) { pos += 5; double v = std::sqrt(parseExpression()); if (pos < s.length() && s[pos] == ')') pos++; return v; }
        if (s.mid(pos).startsWith("tan(")) { pos += 4; double v = std::tan(parseExpression()); if (pos < s.length() && s[pos] == ')') pos++; return v; }

        error = true;
        return 0;
    }
};

QVector<QPointF> generateSine(int points) {
    QVector<QPointF> res;
    for (int i = 0; i <= points; ++i) {
        double x = double(i) / points;
        res.append(QPointF(x, std::sin(2.0 * M_PI * x)));
    }
    return res;
}

QVector<QPointF> generateSquare(double dutyCycle) {
    QVector<QPointF> res;
    double d = qBound(0.001, dutyCycle, 0.999);
    res.append({0, 1});
    res.append({d - 0.0001, 1});
    res.append({d, -1});
    res.append({0.9999, -1});
    res.append({1.0, 1});
    return res;
}

QVector<QPointF> generateTriangle() {
    return {{0, 0}, {0.25, 1}, {0.75, -1}, {1.0, 0}};
}

QVector<QPointF> generateSawtooth() {
    return {{0, -1}, {0.999, 1}, {1.0, -1}};
}

QVector<QPointF> generatePulse(double v1, double v2, double delay, double width, double riseFall) {
    QVector<QPointF> res;
    res.append({0, v1});
    if (delay > 0) res.append({delay, v1});
    res.append({delay + riseFall, v2});
    res.append({qMin(0.999, delay + riseFall + width), v2});
    res.append({qMin(1.0, delay + riseFall + width + riseFall), v1});
    if (delay + 2*riseFall + width < 1.0) res.append({1.0, v1});
    
    // Auto-normalize if v1/v2 are outside [-1, 1]
    double maxV = qMax(qAbs(v1), qAbs(v2));
    if (maxV > 1.0) {
        for (auto& p : res) p.setY(p.y() / maxV);
    }
    return res;
}

QVector<QPointF> generateBitstream(const QString& bits) {
    QVector<QPointF> res;
    if (bits.isEmpty()) return res;

    QString filtered;
    for (const QChar& c : bits) {
        if (c == '0' || c == '1') filtered.append(c);
    }
    if (filtered.isEmpty()) return res;

    double dt = 1.0 / filtered.length();
    for (int i = 0; i < filtered.length(); ++i) {
        double val = (filtered[i] == '1') ? 1.0 : -1.0;
        // One point at the exact start of each bit
        res.append(QPointF(i * dt, val));
    }
    // Final closure point at the very end
    res.append(QPointF(1.0, (filtered.at(filtered.length() - 1) == '1') ? 1.0 : -1.0));

    return res;
}

double evaluateFormula(const QString& formula, double x, bool* ok) {
    Parser p;
    p.s = formula.toLower().replace(" ", "").replace("pi", QString::number(M_PI, 'g', 15));
    p.x_val = x;
    double res = p.parseExpression();
    if (ok) *ok = !p.error;
    return res;
}

QVector<QPointF> generateFromFormula(const QString& formula, int points, bool* ok) {
    QVector<QPointF> res;
    for (int i = 0; i <= points; ++i) {
        double x = double(i) / points;
        bool localOk;
        double y = evaluateFormula(formula, x, &localOk);
        if (!localOk) {
            if (ok) *ok = false;
            return {};
        }
        res.append({x, y});
    }
    if (ok) *ok = true;
    return res;
}

void smooth(QVector<QPointF>& points) {
    if (points.size() < 3) return;
    QVector<QPointF> next;
    next.append(points.first());
    for (int i = 1; i < points.size() - 1; ++i) {
        double avgY = (points[i-1].y() + points[i].y() + points[i+1].y()) / 3.0;
        next.append({points[i].x(), avgY});
    }
    next.append(points.last());
    points = next;
}

void addNoise(QVector<QPointF>& points, double factor) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(-factor, factor);
    for (auto& p : points) {
        p.setY(qBound(-1.0, p.y() + dis(gen), 1.0));
    }
}

QString convertToPwl(QVector<QPointF> points, const ExportParams& params) {
    if (points.isEmpty()) return "0 0 1 0";

    // Ensure sorted and bounded [0, 1]
    std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x();
    });

    if (points.first().x() > 0.0) points.prepend(QPointF(0.0, points.first().y()));
    if (points.last().x() < 1.0) points.append(QPointF(1.0, points.last().y()));

    QStringList tokens;

    if (params.resample) {
        double lastY = -9e9;
        for (int i = 0; i < params.sampleCount; ++i) {
            double x = double(i) / (params.sampleCount - 1);
            double y = 0;
            for (int j = 1; j < points.size(); ++j) {
                if (x <= points[j].x()) {
                    const QPointF a = points[j - 1];
                    const QPointF b = points[j];
                    if (params.isStepMode) y = a.y();
                    else {
                        double t = (b.x() - a.x()) <= 1e-12 ? 0.0 : (x - a.x()) / (b.x() - a.x());
                        y = a.y() + t * (b.y() - a.y());
                    }
                    break;
                }
            }
            double currentY = params.offset + params.amplitude * y;
            if (i == 0 || i == params.sampleCount - 1 || qAbs(currentY - lastY) > 1e-9) {
                tokens << QString::number(x * params.period, 'g', 15) << QString::number(currentY, 'g', 15);
                lastY = currentY;
            }
        }
    } else {
        double lastT = -1.0;
        double lastY = -9e9;
        const double tr = 1e-9; // 1ns fixed transition

        for (int i = 0; i < points.size(); ++i) {
            double rawT = points[i].x() * params.period;
            double rawY = params.offset + params.amplitude * points[i].y();

            if (i > 0 && qAbs(rawT - lastT) < 1e-15 && qAbs(rawY - lastY) < 1e-12) continue;

            if (i > 0 && params.isStepMode) {
                if (qAbs(rawY - lastY) > 1e-9) {
                    double jumpStart = std::max(lastT + 1e-15, rawT - tr);
                    tokens << QString::number(jumpStart, 'g', 15) << QString::number(lastY, 'g', 15);
                } else if (i < points.size() - 1) {
                    continue; 
                }
            }
            tokens << QString::number(rawT, 'g', 15) << QString::number(rawY, 'g', 15);
            lastT = rawT;
            lastY = rawY;
        }
    }
    return tokens.join(' ');
}

} // namespace WaveformEngine
