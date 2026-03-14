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
    void setSignalChecked(const QString& name, bool checked);
    void removeSignal(const QString& name);
    void beginBatchUpdate() { m_blockUpdates = true; }
    void endBatchUpdate() { m_blockUpdates = false; updatePlot(true); }
    void clear();
    void zoomFit();
    static QString formatValue(double val, const QString &unit = "");

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

private:
    VioChartView *m_chartView;
    QChart *m_chart;
    QListWidget *m_nodeList;
    QLabel *m_coordLabel;
    MeasurementDialog *m_measureDialog;
    AnalysisDialog *m_analysisDialog = nullptr;
    bool m_cursorsEnabled;
    bool m_blockUpdates = false;
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
    };
    
    QMap<QString, SignalData> m_signals;
    QStringList m_nodeNames;
    
    void setupUi();
    void setupStyle();
    void updatePlot(bool autoScale = false);
};
