// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#pragma once

#include <QWidget>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QListWidget>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <vector>
#include <QLabel>
#include <QtCharts/QChart>
#include "measurement_dialog.h"
#include "analysis_dialog.h"
#include "fft_analyzer.h"

QT_USE_NAMESPACE

class VioChartView : public QChartView {
    Q_OBJECT
public:
    VioChartView(QChart *chart, QWidget *parent = nullptr);
signals:
    void mouseMoved(const QPointF &value);
    void cursorMoved(int id, double x);
    void legendCtrlClicked(const QString &seriesName);

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    int m_movingCursor = 0; // 0: none, 1: cursor1, 2: cursor2
    double m_c1x = -1, m_c2x = -1;
    double m_c1y = 0, m_c2y = 0;
    QLineSeries *m_activeSeries = nullptr;
    bool m_showCursors = false;
    bool m_crosshairEnabled = false;
    QPointF m_mousePos;

public:
    void setCursorsEnabled(bool enabled) { m_showCursors = enabled; viewport()->update(); }
    void setCrosshairEnabled(bool enabled) { m_crosshairEnabled = enabled; viewport()->update(); }
    void setCursorPositions(double c1x, double c1y, double c2x, double c2y, QLineSeries *series = nullptr) { 
        m_c1x = c1x; m_c1y = c1y; m_c2x = c2x; m_c2y = c2y; 
        m_activeSeries = series;
        viewport()->update(); 
    }
    double cursor1X() const { return m_c1x; }
    double cursor2X() const { return m_c2x; }
    bool isCrosshairEnabled() const { return m_crosshairEnabled; }
};

class WaveformViewer : public QWidget {
    Q_OBJECT

public:
    WaveformViewer(QWidget *parent = nullptr);
    ~WaveformViewer();
    void loadCsv(const QString &fileName);
    void addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values);
    void addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values, const QVector<double>& phase);
    void setSignalChecked(const QString& name, bool checked);
    void appendPoint(const QString& name, double x, double y);
    void appendPoints(const QString& name, const std::vector<double>& times, const std::vector<double>& values);
    void removeSignal(const QString& name);
    void beginBatchUpdate() { m_blockUpdates = true; }
    void endBatchUpdate() { m_blockUpdates = false; updatePlot(true); }
    void clear();
    void zoomFit();
    void setAcMode(bool enabled);
    bool currentXRange(double& minX, double& maxX) const;
    void preserveXRangeOnce(double minX, double maxX);
    static QString formatValue(double val, const QString &unit = "");
    void updatePlot(bool autoScale = false);

    struct SignalExport {
        QString name;
        QVector<double> time;
        QVector<double> values;
        QVector<double> phase;
        bool hasPhase = false;
        bool checked = false;
    };
    QList<SignalExport> exportSignals() const;
    void importSignals(const QList<SignalExport>& signalExports);

private slots:
    void onNodeSelected();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void onMouseMoved(const QPointF &value);
    void toggleCursors();
    void toggleCrosshair();
    void updateCursors();
    void onNodeClicked(QListWidgetItem *item);
    void updateZoomAnalysis();
    void onSubtractRequested();
    void onFftRequested();
    void onLegendCtrlClicked(const QString &seriesName);

private:
    VioChartView *m_chartView;
    QChart *m_chart;
    QListWidget *m_nodeList;
    QLabel *m_coordLabel;
    MeasurementDialog *m_measureDialog;
    AnalysisDialog *m_analysisDialog = nullptr;
    bool m_cursorsEnabled;
    bool m_blockUpdates = false;
    bool m_acMode = false;
    double m_cursor1X, m_cursor2X;
    QString m_activeCursorSeries;
    enum class SignalType { VOLTAGE, CURRENT, POWER, OTHER };
    
    QLabel *m_statsLabel;
    QString m_activeSeriesName;
    SignalType m_activeSeriesType;
    double m_timeMultiplier = 1.0;
    QString m_timeUnit = "s";
    double m_vMultiplier = 1.0, m_iMultiplier = 1.0, m_pMultiplier = 1.0;
    QString m_vUnit = "V", m_iUnit = "A", m_pUnit = "W";
    
    struct SignalData {
        QString name;
        SignalType type;
        QVector<double> time;
        QVector<double> values;
        QVector<double> phase;
        bool hasPhase = false;
    };
    
    QMap<QString, SignalData> m_signals;
    QMap<QString, int> m_pointCounters;
    QStringList m_nodeNames;
    
    void setupUi();
    void setupStyle();
    void zoomFitYOnly();
    void updateNodeItemStyle(QListWidgetItem* item);
    void showAnalysisForSeries(const QString &seriesName);

    bool m_preserveXRangeOnce = false;
    double m_preserveXMin = 0.0;
    double m_preserveXMax = 0.0;
    int m_holdXRangeCount = 0;
    double m_holdXMin = 0.0;
    double m_holdXMax = 0.0;
};
