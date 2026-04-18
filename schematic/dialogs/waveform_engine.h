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
    QVector<QPointF> generateBitstream(const QString& bits);

    // Expression Parser
    double evaluateFormula(const QString& formula, double x, bool* ok = nullptr);
    QVector<QPointF> generateFromFormula(const QString& formula, int points = 200, bool* ok = nullptr);

    // Transformations
    void smooth(QVector<QPointF>& points);
    void addNoise(QVector<QPointF>& points, double factor = 0.1);

    // Export & Sampling
    struct ExportParams {
        double period = 1.0;
        double amplitude = 1.0;
        double offset = 0.0;
        bool isStepMode = false;
        bool resample = false;
        int sampleCount = 64;
    };

    QString convertToPwl(QVector<QPointF> points, const ExportParams& params);
}

#endif // WAVEFORM_ENGINE_H
