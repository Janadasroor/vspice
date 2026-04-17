#ifndef WAVEFORM_ENGINE_H
#define WAVEFORM_ENGINE_H

#include <QVector>
#include <QPointF>
#include <QString>

namespace WaveformEngine {

    // Standard Generators
    QVector<QPointF> generateSine(int points = 100);
    QVector<QPointF> generateSquare(double dutyCycle = 0.5);
    QVector<QPointF> generateTriangle();
    QVector<QPointF> generateSawtooth();
    QVector<QPointF> generatePulse(double v1, double v2, double delay, double width, double riseFall = 0.01);

    // Expression Parser
    double evaluateFormula(const QString& formula, double x, bool* ok = nullptr);
    QVector<QPointF> generateFromFormula(const QString& formula, int points = 200, bool* ok = nullptr);

    // Transformations
    void smooth(QVector<QPointF>& points);
    void addNoise(QVector<QPointF>& points, double factor = 0.1);
}

#endif // WAVEFORM_ENGINE_H
