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
#include <QColor>
#include <QVector>
#include <vector>
#include <QLabel>
#include <QtCharts/QChart>
#include <QStack>
#include <QSplitter>
#include "measurement_dialog.h"
#include "analysis_dialog.h"
#include "fft_analyzer.h"

QT_USE_NAMESPACE

class VioChartView : public QChartView {
    Q_OBJECT
public:
    VioChartView(QChart *chart, QWidget *parent = nullptr);
Q_SIGNALS:
    void mouseMoved(const QPointF &value);
    void cursorMoved(int id, double x);
    void cursorsMoved();
    void legendCtrlClicked(const QString &seriesName);
    void contextMenuRequested(const QPoint &globalPos);
    void zoomRectCompleted(const QRectF &valueRect);
    void clicked();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    int m_movingCursor = 0; // 0: none, 1: cursor1, 2: cursor2
    double m_c1x = -1, m_c2x = -1;
    double m_c1y = 0, m_c2y = 0;
    QLineSeries *m_activeSeries = nullptr;
    bool m_showCursors = false;
    bool m_crosshairEnabled = false;
    QPointF m_mousePos;
    bool m_panning = false;
    QPointF m_panStart;
    bool m_zoomRectActive = false;
    QPoint m_zoomRectStart;
    QRubberBand *m_rubberBand = nullptr;

public:
    void setCursorsEnabled(bool enabled) { m_showCursors = enabled; viewport()->update(); }
    void setCrosshairEnabled(bool enabled) { m_crosshairEnabled = enabled; viewport()->update(); }
    void setCursorPositions(double c1x, double c1y, double c2x, double c2y, QLineSeries *series = nullptr);
    double snapToSeries(double x, QLineSeries *series);
    bool isCrosshairEnabled() const { return m_crosshairEnabled; }
    double cursor1X() const { return m_c1x; }
    double cursor2X() const { return m_c2x; }
};

class WaveformViewer : public QWidget {
    Q_OBJECT

public:
    enum class PlotQuality {
        HighQuality = 0,
        Balanced,
        Fast
    };

    WaveformViewer(QWidget *parent = nullptr);
    ~WaveformViewer();
    void loadCsv(const QString &fileName);
    void addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values);
    void addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values, const QVector<double>& phase);
    void addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values, const QColor &color);
    void setSignalChecked(const QString& name, bool checked);
    void appendPoint(const QString& name, double x, double y);
    void appendPoints(const QString& name, const std::vector<double>& times, const std::vector<double>& values);
    void removeSignal(const QString& name);
    void beginBatchUpdate() { m_blockUpdates = true; }
    void endBatchUpdate() { m_blockUpdates = false; updatePlot(true); }
    void clear();
    void clearPane(int index = -1);
    void zoomFit();
    void setAcMode(bool enabled);
    bool currentXRange(double& minX, double& maxX) const;
    void preserveXRangeOnce(double minX, double maxX);
    static QString formatValue(double val, const QString &unit = "");
    Q_INVOKABLE void updatePlot(bool autoScale = false);
    void setPlotQuality(PlotQuality quality);
    PlotQuality plotQuality() const { return m_plotQuality; }

    struct SignalExport {
        QString name;
        QVector<double> time;
        QVector<double> values;
        QVector<double> phase;
        bool hasPhase = false;
        bool checked = false;
        bool selected = false;
        QColor customColor;
        double lineWidth = 1.5;
        Qt::PenStyle penStyle = Qt::SolidLine;
        int paneIndex = -1;
    };
    QList<SignalExport> exportSignals() const;
    void importSignals(const QList<SignalExport>& signalExports);
    bool getSignalData(const QString& name, QVector<double>& time, QVector<double>& values);
    QStringList getSignalNames() const;
    int focusedPaneIndex() const;
    int paneCount() const { return m_panes.size(); }
    QString currentSignalName() const;
    void ensurePaneCount(int count);
    void setFocusedPaneIndex(int index);
    void setSignalPaneIndex(const QString& name, int paneIndex);
    void setCurrentSignal(const QString& name);
    QStringList getSignalsInPane(int index) const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private Q_SLOTS:
    void onNodeSelected();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void undoZoom();
    void redoZoom();
    void onMouseMoved(const QPointF &value);
    void onContextMenuRequested(const QPoint &globalPos);
    void toggleCursors();
    void toggleCrosshair();
    void updateCursors();
    void onNodeClicked(QListWidgetItem *item);
    void updateZoomAnalysis();
    void onSubtractRequested();
    void onFftRequested();
    void exportImage();
    void onExpressionSubmitted(const QString &expression, const QColor &color = QColor(), const QString &targetName = QString());
    void onLegendCtrlClicked(const QString &seriesName);
    void onPaneClicked();
    void onZoomRectCompleted(const QRectF &valueRect);

private:
    enum class SignalType { VOLTAGE, CURRENT, POWER, OTHER };

    struct ChartPane {
        VioChartView* view = nullptr;
        QChart* chart = nullptr;
        SignalType type = SignalType::OTHER;
        QValueAxis* axisY = nullptr;
        QValueAxis* axisX = nullptr;
    };

    QList<ChartPane*> m_panes;
    ChartPane* m_focusedPane = nullptr;
    QSplitter* m_splitter;
    QListWidget *m_nodeList;
    QLabel *m_coordLabel;
    QHBoxLayout *m_legendLayout;
    QLabel *m_xAxisTitleLabel;
    QWidget *m_legendContainer;
    MeasurementDialog *m_measureDialog;
    AnalysisDialog *m_analysisDialog = nullptr;
    bool m_cursorsEnabled;
    bool m_blockUpdates = false;
    bool m_acMode = false;
    PlotQuality m_plotQuality = PlotQuality::Balanced;
    double m_cursor1X, m_cursor2X;
    QString m_activeCursorSeries;
    
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
        QColor customColor;
        double lineWidth = 1.5;
        Qt::PenStyle penStyle = Qt::SolidLine;
        int paneIndex = -1;
    };
    
    QMap<QString, SignalData> m_signals;
    QMap<QString, int> m_pointCounters;
    QStringList m_nodeNames;
    
    void setupUi();
    void setupStyle();
    ChartPane* createPane(SignalType type);
    void removePane(int index);
    ChartPane* getPaneForType(SignalType type);
    void syncAxesX(QValueAxis* source);
    void zoomFitYOnly();
    void updateNodeItemStyle(QListWidgetItem* item);
    void updateLegend();
    void showAnalysisForSeries(const QString &seriesName);
    void exportSignalsCsv();
    bool buildValueAtCursor(QString &outText) const;
    void applyPlotQualityToViews();
    bool shouldUseOpenGL() const;
    bool shouldUseAntialiasing() const;
    int visiblePointBudget(int viewportWidth) const;
    bool parseExpression(const QString &expression, QStringList &signalNames, QString &error);
    bool evaluateExpression(const QString &expression, const QStringList &signalNames, QVector<double> &time, QVector<double> &values);
    double evaluateSimpleMath(const QString &expr, bool &ok);
    double evaluateOperand(const QString &operand, const QVector<QVector<double>> &signalVectors, int index);

    struct EdgeTimes {
        double riseMin = 0, riseMax = 0, riseAvg = 0;
        double fallMin = 0, fallMax = 0, fallAvg = 0;
        int riseCount = 0, fallCount = 0;
    };
    EdgeTimes computeEdgeTimes(const QVector<double>& time, const QVector<double>& values) const;
    QVector<double> computeDerivative(const QVector<double>& time, const QVector<double>& values);
    QVector<double> computeIntegral(const QVector<double>& time, const QVector<double>& values);
    void scheduleVisibleRangeRefresh();

    bool m_preserveXRangeOnce = false;
    double m_preserveXMin = 0.0;
    double m_preserveXMax = 0.0;
    int m_holdXRangeCount = 0;
    double m_holdXMin = 0.0;
    double m_holdXMax = 0.0;
    bool m_rebuildQueued = false;
    QPointF m_lastMouseValue;
    bool m_hasLastMouseValue = false;

    struct ZoomState {
        double xMin, xMax;
        QList<QPair<double, double>> yRanges; // One for each pane
    };
    QStack<ZoomState> m_zoomUndo;
    QStack<ZoomState> m_zoomRedo;
    void pushZoomState();
    void applyZoomState(const ZoomState &s);
};
