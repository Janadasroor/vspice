// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "waveform_viewer.h"
#include "../core/si_formatter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLogValueAxis>
#include <QLabel>
#include <QMouseEvent>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QGraphicsLayout>
#include <QApplication>
#include <QToolButton>
#include <cmath>
#include <algorithm>
#include <QGraphicsTextItem>
#include <QGraphicsSimpleTextItem>
#include "../core/theme_manager.h"


VioChartView::VioChartView(QChart *chart, QWidget *parent) : QChartView(chart, parent) {
    setMouseTracking(true);
}

void VioChartView::mouseMoveEvent(QMouseEvent *event) {
    m_mousePos = event->pos();
    if (m_movingCursor > 0 && m_showCursors) {
        double x = chart()->mapToValue(event->pos()).x();
        if (m_movingCursor == 1) m_c1x = x;
        else m_c2x = x;
        emit cursorMoved(m_movingCursor, x);
        viewport()->update();
    }
    if (m_crosshairEnabled) {
        viewport()->update();
    }
    QPointF value = chart()->mapToValue(event->pos());
    emit mouseMoved(value);
    QChartView::mouseMoveEvent(event);
}

void VioChartView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        if (auto* legend = chart()->legend(); legend && legend->isVisible()) {
            const QPointF scenePos = mapToScene(event->pos());
            if (legend->sceneBoundingRect().contains(scenePos) && scene()) {
                const auto items = scene()->items(scenePos);
                for (auto* item : items) {
                    QString text;
                    if (auto* t = dynamic_cast<QGraphicsTextItem*>(item)) {
                        text = t->toPlainText().trimmed();
                    } else if (auto* t = dynamic_cast<QGraphicsSimpleTextItem*>(item)) {
                        text = t->text().trimmed();
                    } else if (item->parentItem()) {
                        if (auto* t = dynamic_cast<QGraphicsTextItem*>(item->parentItem())) {
                            text = t->toPlainText().trimmed();
                        } else if (auto* t = dynamic_cast<QGraphicsSimpleTextItem*>(item->parentItem())) {
                            text = t->text().trimmed();
                        }
                    }

                    if (!text.isEmpty()) {
                        emit legendCtrlClicked(text);
                        event->accept();
                        return;
                    }
                }
            }
        }
    }
    if (event->button() == Qt::LeftButton && m_showCursors) {
        double xPos = event->pos().x();
        auto xToPos = [&](double xv) { return chart()->mapToPosition(QPointF(xv, 0)).x(); };
        double tol = 10.0;

        if (std::abs(xPos - xToPos(m_c1x)) < tol) {
            m_movingCursor = 1;
        } else if (std::abs(xPos - xToPos(m_c2x)) < tol) {
            m_movingCursor = 2;
        }

        if (m_movingCursor > 0) {
            viewport()->update();
            return; 
        }
    }
    QChartView::mousePressEvent(event);
}

void VioChartView::mouseReleaseEvent(QMouseEvent *event) {
    m_movingCursor = 0;
    QChartView::mouseReleaseEvent(event);
}

void VioChartView::drawForeground(QPainter *painter, const QRectF &rect) {
    QChartView::drawForeground(painter, rect);
    
    QRectF plot = chart()->plotArea();
    
    // Draw Crosshair if enabled
    if (m_crosshairEnabled && plot.contains(m_mousePos)) {
        painter->setPen(QPen(QColor("#888888"), 1, Qt::DashLine));
        painter->drawLine(m_mousePos.x(), plot.top(), m_mousePos.x(), plot.bottom());
        painter->drawLine(plot.left(), m_mousePos.y(), plot.right(), m_mousePos.y());
    }

    if (!m_showCursors) return;

    if (m_activeSeries) {
        bool stillValid = false;
        for (auto* s : chart()->series()) {
            if (s == m_activeSeries) { stillValid = true; break; }
        }
        if (!stillValid) m_activeSeries = nullptr;
    }

    painter->setRenderHint(QPainter::Antialiasing);
    auto drawC = [&](double x, double y, const QColor &color, const QString &label) {
        QPointF pos;
        if (m_activeSeries) {
            pos = chart()->mapToPosition(QPointF(x, y), m_activeSeries);
        } else {
            pos = chart()->mapToPosition(QPointF(x, y));
        }
        
        if (plot.contains(pos.x(), plot.center().y())) {
            painter->setPen(QPen(color, 1, Qt::DashLine));
            painter->drawLine(pos.x(), plot.top(), pos.x(), plot.bottom());
            
            if (plot.contains(pos)) {
                painter->drawLine(plot.left(), pos.y(), plot.right(), pos.y());
                painter->setPen(QPen(color, 2, Qt::SolidLine));
                painter->drawLine(pos.x() - 5, pos.y(), pos.x() + 5, pos.y());
                painter->drawLine(pos.x(), pos.y() - 5, pos.x(), pos.y() + 5);
            }
            
            painter->setPen(color);
            painter->drawText(pos.x() + 5, plot.top() + 15, label);
        }
    };

    drawC(m_c1x, m_c1y, QColor("#00ffff"), "1");
    drawC(m_c2x, m_c2y, QColor("#ffaa00"), "2");
}

WaveformViewer::WaveformViewer(QWidget *parent) : QWidget(parent), 
    m_measureDialog(nullptr), m_cursorsEnabled(false), m_cursor1X(0), m_cursor2X(0) {
    setupUi();
    setupStyle();
}

WaveformViewer::~WaveformViewer() {
    if (m_measureDialog) m_measureDialog->deleteLater();
    if (m_analysisDialog) m_analysisDialog->deleteLater();
}

void WaveformViewer::setupUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = new QToolBar("Waveform Controls", this);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setMinimumHeight(24);
    toolbar->setStyleSheet(R"(
        QToolBar { spacing: 2px; padding: 0px 2px; }
        QToolButton { padding: 2px 6px; margin: 0px; font-size: 11px; min-height: 20px; }
    )");

    toolbar->addAction("Z+", this, &WaveformViewer::zoomIn);
    toolbar->addAction("Z-", this, &WaveformViewer::zoomOut);
    toolbar->addAction("Fit", this, &WaveformViewer::zoomFit);
    toolbar->addSeparator();
    toolbar->addAction("Diff", this, &WaveformViewer::onSubtractRequested);
    toolbar->addAction("FFT", this, &WaveformViewer::onFftRequested);
    toolbar->addSeparator();
    auto *cursorAct = toolbar->addAction("Cursors", this, &WaveformViewer::toggleCursors);
    cursorAct->setCheckable(true);
    
    auto *crosshairAct = toolbar->addAction("Crosshair", this, &WaveformViewer::toggleCrosshair);
    crosshairAct->setCheckable(true);
    
    layout->addWidget(toolbar);

    auto *mainArea = new QHBoxLayout();
    m_nodeList = new QListWidget(this);
    m_nodeList->setSelectionMode(QAbstractItemView::MultiSelection);
    m_nodeList->setFixedWidth(120);
    connect(m_nodeList, &QListWidget::itemSelectionChanged, this, &WaveformViewer::onNodeSelected);
    connect(m_nodeList, &QListWidget::itemChanged, this, [this](QListWidgetItem* item){
        updateNodeItemStyle(item);
        updatePlot(false);
    });
    connect(m_nodeList, &QListWidget::itemClicked, this, &WaveformViewer::onNodeClicked);
    
    m_chart = new QChart();
    m_chart->setBackgroundVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignTop);
    m_chart->legend()->setMarkerShape(QLegend::MarkerShapeRectangle);
    m_chart->legend()->setBackgroundVisible(false);
    m_chart->legend()->setLabelColor(Qt::white);
    m_chart->setMargins(QMargins(0, 0, 0, 0));
    m_chart->setBackgroundRoundness(0);

    m_chartView = new VioChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    connect(m_chartView, &VioChartView::mouseMoved, this, &WaveformViewer::onMouseMoved);
    connect(m_chartView, &VioChartView::cursorMoved, this, &WaveformViewer::updateCursors);
    connect(m_chartView, &VioChartView::legendCtrlClicked, this, &WaveformViewer::onLegendCtrlClicked);

    // Set up default axes so cursor mapping always works (even before signals are loaded)
    auto *defaultAxisX = new QValueAxis();
    defaultAxisX->setRange(0, 1);
    defaultAxisX->setTitleText("Time (s)");
    auto *defaultAxisY = new QValueAxis();
    defaultAxisY->setRange(-1, 1);
    defaultAxisY->setTitleText("Amplitude");
    m_chart->addAxis(defaultAxisX, Qt::AlignBottom);
    m_chart->addAxis(defaultAxisY, Qt::AlignLeft);
    
    mainArea->addWidget(m_nodeList);
    mainArea->addWidget(m_chartView, 1);
    layout->addLayout(mainArea, 1);
    
    auto *footer = new QHBoxLayout();
    m_coordLabel = new QLabel("Ready");
    m_coordLabel->setStyleSheet("font-family: monospace; color: #00ff00;");
    m_statsLabel = new QLabel("");
    m_statsLabel->setStyleSheet("font-family: monospace; color: #ffaa00;");
    footer->addWidget(m_coordLabel);
    footer->addStretch();
    footer->addWidget(m_statsLabel);
    footer->setContentsMargins(10, 2, 10, 2);
    layout->addLayout(footer);
}

void WaveformViewer::setupStyle() {
    m_chart->setTheme(QChart::ChartThemeDark);
    m_chart->setBackgroundBrush(QBrush(Qt::black));
    m_chart->setPlotAreaBackgroundBrush(QBrush(Qt::black));
    m_chart->setTitleBrush(QBrush(Qt::white));

    m_nodeList->setStyleSheet(R"(
        QListWidget { background: #f3f4f6; color: #111827; border: 1px solid #e5e7eb; }
        QListWidget::item { padding: 2px 4px; color: #374151; }
        QListWidget::item:selected { background: #dbeafe; color: #111827; }
    )");
}

void WaveformViewer::clear() {
    m_signals.clear();
    m_pointCounters.clear();
    m_nodeList->blockSignals(true);
    m_nodeList->clear();
    m_nodeList->blockSignals(false);
    m_chartView->setCursorPositions(m_chartView->cursor1X(), 0, m_chartView->cursor2X(), 0, nullptr);
    m_chart->removeAllSeries();
    auto axes = m_chart->axes();
    for (auto* a : axes) {
        m_chart->removeAxis(a);
        a->deleteLater();
    }
    m_activeSeriesName.clear();
}

bool WaveformViewer::currentXRange(double& minX, double& maxX) const {
    auto axesX = m_chart->axes(Qt::Horizontal);
    if (axesX.isEmpty()) return false;
    auto* axis = qobject_cast<QValueAxis*>(axesX[0]);
    if (!axis) return false;
    minX = axis->min();
    maxX = axis->max();
    return true;
}

void WaveformViewer::preserveXRangeOnce(double minX, double maxX) {
    m_preserveXRangeOnce = true;
    m_preserveXMin = minX;
    m_preserveXMax = maxX;
    m_holdXRangeCount = 3;
    m_holdXMin = minX;
    m_holdXMax = maxX;
}

void WaveformViewer::addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values) {
    if (time.isEmpty() || values.isEmpty()) return;

    SignalData data;
    data.name = name;
    QString lowerName = name.toLower();
    if (lowerName.startsWith("v(") || lowerName.startsWith("v_")) data.type = SignalType::VOLTAGE;
    else if (lowerName.contains("#branch") || lowerName.startsWith("i(")) data.type = SignalType::CURRENT;
    else if (lowerName.startsWith("p(")) data.type = SignalType::POWER;
    else data.type = SignalType::OTHER;
    
    data.time = time;
    data.values = values;
    m_signals[name] = data;
    
    // Check if already in list
    bool exists = false;
    for (int i = 0; i < m_nodeList->count(); ++i) {
        if (m_nodeList->item(i)->text() == name) {
            exists = true;
            // If already checked, update the plot to show new data
            if (m_nodeList->item(i)->checkState() == Qt::Checked) {
                updatePlot(false);
            }
            break;
        }
    }

    if (!exists) {
        m_nodeList->blockSignals(true);
        auto* item = new QListWidgetItem(name, m_nodeList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        item->setCheckState(Qt::Unchecked); // Default to unchecked so only explicit probes show up
        updateNodeItemStyle(item);
        m_nodeList->blockSignals(false);
    }
}

void WaveformViewer::appendPoint(const QString& name, double x, double y) {
    if (!m_signals.contains(name)) {
        addSignal(name, {x}, {y});
        // We don't auto-check by default, but we should check if it's already in the list
    }
    
    auto& sig = m_signals[name];
    sig.time.append(x);
    sig.values.append(y);
    
    if (m_blockUpdates) return;

    // Fast check for signal existence in chart
    QLineSeries* lineSeries = nullptr;
    for (auto* series : m_chart->series()) {
        if (series->name() == name) {
            lineSeries = qobject_cast<QLineSeries*>(series);
            break;
        }
    }

    // If series not in chart but signal is checked, add it now
    if (!lineSeries) {
        bool checked = false;
        for (int i = 0; i < m_nodeList->count(); ++i) {
            if (m_nodeList->item(i)->text() == name) {
                checked = (m_nodeList->item(i)->checkState() == Qt::Checked);
                break;
            }
        }
        
        if (checked) {
            lineSeries = new QLineSeries();
            lineSeries->setName(name);
            m_chart->addSeries(lineSeries);
            auto axesX = m_chart->axes(Qt::Horizontal);
            auto axesY = m_chart->axes(Qt::Vertical);
            if (axesX.isEmpty() || axesY.isEmpty()) {
                updatePlot(true); // Re-init axes
                return; 
            }
            lineSeries->attachAxis(axesX[0]);
            lineSeries->attachAxis(axesY[0]);
        }
    }

    if (lineSeries) {
        lineSeries->append(x, y);
        
        // If it's the very first points, set a reasonable range immediately
        if (lineSeries->count() < 10) {
            auto axesX = m_chart->axes(Qt::Horizontal);
            auto axesY = m_chart->axes(Qt::Vertical);
            if (!axesX.isEmpty() && !axesY.isEmpty()) {
                auto* ax = qobject_cast<QValueAxis*>(axesX[0]);
                auto* ay = qobject_cast<QValueAxis*>(axesY[0]);
                ax->setRange(std::min(0.0, x), std::max(1e-3, x * 1.5));
                ay->setRange(y - 1.0, y + 1.0);
            }
        }

        // Throttled auto-scale: only update every N points
        int& count = m_pointCounters[name];
        if ((++count % 50) == 0) {
            auto axesX = m_chart->axes(Qt::Horizontal);
            auto axesY = m_chart->axes(Qt::Vertical);
            if (!axesX.isEmpty() && !axesY.isEmpty()) {
                auto* ax = qobject_cast<QValueAxis*>(axesX[0]);
                auto* ay = qobject_cast<QValueAxis*>(axesY[0]);
                
                if (x > ax->max()) ax->setMax(x * 1.1);
                if (y > ay->max()) ay->setMax(y > 0 ? y * 1.2 : y * 0.8);
                if (y < ay->min()) ay->setMin(y < 0 ? y * 1.2 : y * 0.8);
            }
        }
    }
}

void WaveformViewer::appendPoints(const QString& name, const std::vector<double>& times, const std::vector<double>& values) {
    if (times.empty() || times.size() != values.size()) return;

    if (!m_signals.contains(name)) {
        QVector<double> tVec; tVec.reserve(times.size());
        QVector<double> vVec; vVec.reserve(values.size());
        for(double t : times) tVec.append(t);
        for(double v : values) vVec.append(v);
        addSignal(name, tVec, vVec);
    } else {
        auto& sig = m_signals[name];
        // Efficient append
        const int oldSize = sig.time.size();
        sig.time.resize(oldSize + times.size());
        sig.values.resize(oldSize + values.size());
        std::copy(times.begin(), times.end(), sig.time.begin() + oldSize);
        std::copy(values.begin(), values.end(), sig.values.begin() + oldSize);
    }
    
    if (m_blockUpdates) return;

    // Throttle the actual chart redraws so we don't spam QLineSeries with millions of points
    // during real-time streaming. We just update the bounds roughly.
    int& count = m_pointCounters[name];
    count += times.size();
    if ((count % 1000) < times.size() || count < 100) { 
        // We only trigger updatePlot periodically to use its fast Min-Max bucket decimation
        QMetaObject::invokeMethod(this, "updatePlot", Qt::QueuedConnection, Q_ARG(bool, false));
    }
}

void WaveformViewer::setSignalChecked(const QString& name, bool checked) {
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->text().compare(name, Qt::CaseInsensitive) == 0) {
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
            return;
        }
    }
}

void WaveformViewer::removeSignal(const QString& name) {
    m_signals.remove(name);
    for (int i = 0; i < m_nodeList->count(); ++i) {
        if (m_nodeList->item(i)->text() == name) {
            delete m_nodeList->takeItem(i);
            break;
        }
    }
    updatePlot(false);
}

void WaveformViewer::onMouseMoved(const QPointF &value) {
    if (m_signals.empty()) {
        m_coordLabel->setText("Ready");
        return;
    }

    QString coordStr = QString("Time: %1").arg(SiFormatter::format(value.x() / m_timeMultiplier, "s"));
    
    // Check what types of signals are currently loaded
    bool hasV = false, hasI = false, hasP = false;
    for (const auto& sig : m_signals) {
        if (sig.type == SignalType::VOLTAGE) hasV = true;
        else if (sig.type == SignalType::CURRENT) hasI = true;
        else if (sig.type == SignalType::POWER) hasP = true;
    }
    
    QPoint pos = m_chartView->mapFromGlobal(QCursor::pos());
    // Note: chart()->mapToValue(pos) is already passed as 'value' but mapped to global cursor might be more accurate for crosshair
    
    if (hasV) {
        double valV = value.y(); // Default to current mouse Y
        coordStr += QString(" | V: %1").arg(SiFormatter::format(valV / m_vMultiplier, m_vUnit));
    }
    if (hasI) {
        double valI = value.y(); 
        coordStr += QString(" | I: %1").arg(SiFormatter::format(valI / m_iMultiplier, m_iUnit));
    }
    if (hasP) {
        double valP = value.y();
        coordStr += QString(" | P: %1").arg(SiFormatter::format(valP / m_pMultiplier, m_pUnit));
    }
    
    m_coordLabel->setText(coordStr);
}

void WaveformViewer::toggleCrosshair() {
    m_chartView->setCrosshairEnabled(!m_chartView->isCrosshairEnabled());
}

void WaveformViewer::toggleCursors() {
    m_cursorsEnabled = !m_cursorsEnabled;
    m_chartView->setCursorsEnabled(m_cursorsEnabled);
    if (m_cursorsEnabled) {
        if (!m_measureDialog) m_measureDialog = new MeasurementDialog(this);
        m_measureDialog->show();

        // Always initialize cursors to visible positions within current axis range
        {
            auto axes = m_chart->axes(Qt::Horizontal);
            if (!axes.isEmpty()) {
                auto *axis = qobject_cast<QValueAxis*>(axes.first());
                if (axis) {
                    double min = axis->min();
                    double max = axis->max();
                    // Only reset if cursors are unset OR out of range
                    bool cursor1Valid = (m_chartView->cursor1X() >= min && m_chartView->cursor1X() <= max);
                    bool cursor2Valid = (m_chartView->cursor2X() >= min && m_chartView->cursor2X() <= max);
                    if (!cursor1Valid || !cursor2Valid) {
                        double range = max - min;
                        m_cursor1X = min + range * 0.25;
                        m_cursor2X = min + range * 0.75;
                        m_chartView->setCursorPositions(m_cursor1X, 0, m_cursor2X, 0);
                    }
                }
            }
        }
        updateCursors();
    } else if (m_measureDialog) {
        m_measureDialog->hide();
    }
}

void WaveformViewer::updateCursors() {
    if (!m_cursorsEnabled) return;

    m_cursor1X = m_chartView->cursor1X();
    m_cursor2X = m_chartView->cursor2X();

    // If there is no series, just update positions without Y interpolation
    if (m_chart->series().isEmpty()) {
        m_chartView->setCursorPositions(m_cursor1X, 0, m_cursor2X, 0);
        if (m_measureDialog) {
            m_measureDialog->updateValues("", 
                SiFormatter::format(m_cursor1X / m_timeMultiplier, m_timeUnit), "N/A",
                SiFormatter::format(m_cursor2X / m_timeMultiplier, m_timeUnit), "N/A",
                SiFormatter::format(std::abs(m_cursor2X - m_cursor1X) / m_timeMultiplier, m_timeUnit), "N/A",
                "---", "---");
        }
        return;
    }

    QLineSeries *series = qobject_cast<QLineSeries*>(m_chart->series().first());
    if (!series) return;

    m_cursor1X = m_chartView->cursor1X();
    m_cursor2X = m_chartView->cursor2X();
    
    auto getY = [&](double x) {
        auto points = series->points();
        if (points.isEmpty()) return 0.0;
        for (int i = 0; i < points.size() - 1; ++i) {
            if (x >= points[i].x() && x <= points[i+1].x()) {
                double t = (x - points[i].x()) / (points[i+1].x() - points[i].x());
                return points[i].y() + t * (points[i+1].y() - points[i].y());
            }
        }
        return points.last().y();
    };

    double v1 = getY(m_cursor1X);
    double v2 = getY(m_cursor2X);
    m_chartView->setCursorPositions(m_cursor1X, v1, m_cursor2X, v2, series);

    if (m_measureDialog) {
        const double dt = m_cursor2X - m_cursor1X;
        const double dv = v2 - v1;
        QString unit = "";
        if (m_signals.contains(series->name())) {
            const auto type = m_signals[series->name()].type;
            if (type == SignalType::VOLTAGE) unit = "V";
            else if (type == SignalType::CURRENT) unit = "A";
            else if (type == SignalType::POWER) unit = "W";
        }

        QString freqStr = "---";
        QString slopeStr = "---";
        if (std::abs(dt) > 1e-15) {
            freqStr = SiFormatter::format(1.0 / std::abs(dt), "Hz");
            slopeStr = SiFormatter::format(dv / dt, unit + "/s");
        }

        m_measureDialog->updateValues(series->name(), 
            SiFormatter::format(m_cursor1X, "s"), SiFormatter::format(v1, ""),
            SiFormatter::format(m_cursor2X, "s"), SiFormatter::format(v2, ""),
            SiFormatter::format(dt, "s"), SiFormatter::format(dv, ""),
            freqStr, slopeStr
        );
    }
}

void WaveformViewer::updatePlot(bool autoScale) {
    if (m_blockUpdates) return;
    
    m_chartView->setCursorPositions(m_chartView->cursor1X(), 0, m_chartView->cursor2X(), 0, nullptr);
    m_chart->removeAllSeries();
    auto axes = m_chart->axes();
    for (auto* a : axes) {
        m_chart->removeAxis(a);
        a->deleteLater();
    }

    if (m_signals.isEmpty()) return;

    auto* axisX = new QValueAxis();
    axisX->setTitleText("Time (s)");
    axisX->setLabelFormat("%.2g");
    
    auto* axisY = new QValueAxis();
    axisY->setTitleText("Amplitude");
    axisY->setLabelFormat("%.2g");

    QPen axisPen(Qt::white);
    axisPen.setWidth(1);
    
    axisX->setLinePen(axisPen);
    axisX->setLabelsBrush(QBrush(Qt::white));
    axisX->setTitleBrush(QBrush(Qt::white));
    axisX->setGridLinePen(QPen(QColor("#404040"), 1, Qt::DotLine));

    axisY->setLinePen(axisPen);
    axisY->setLabelsBrush(QBrush(Qt::white));
    axisY->setTitleBrush(QBrush(Qt::white));
    axisY->setGridLinePen(QPen(QColor("#404040"), 1, Qt::DotLine));

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);

    bool hasAnyChecked = false;
    int colorIdx = 0;
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->text();
            if (m_signals.contains(name)) {
                const auto& data = m_signals[name];
                auto* series = new QLineSeries();
                series->setName(name);
                
                // Optimized loading: use Min-Max decimation to preserve peaks and avoid aliasing
                const int maxBuckets = 5000;
                const int totalSize = static_cast<int>(data.time.size());
                
                if (totalSize <= maxBuckets) {
                    QList<QPointF> points;
                    points.reserve(totalSize);
                    for (int j = 0; j < totalSize; ++j) {
                        points.append(QPointF(data.time[j], data.values[j]));
                    }
                    series->append(points);
                } else {
                    int bucketSize = totalSize / maxBuckets;
                    QList<QPointF> points;
                    points.reserve(maxBuckets * 2 + 1);
                    for (int b = 0; b < maxBuckets; ++b) {
                        int start = b * bucketSize;
                        int end = (b == maxBuckets - 1) ? totalSize : (b + 1) * bucketSize;
                        
                        int minIdx = start;
                        int maxIdx = start;
                        for (int j = start + 1; j < end; ++j) {
                            if (data.values[j] < data.values[minIdx]) minIdx = j;
                            if (data.values[j] > data.values[maxIdx]) maxIdx = j;
                        }
                        
                        // Append min and max in their original time order
                        if (minIdx < maxIdx) {
                            points.append(QPointF(data.time[minIdx], data.values[minIdx]));
                            points.append(QPointF(data.time[maxIdx], data.values[maxIdx]));
                        } else if (minIdx > maxIdx) {
                            points.append(QPointF(data.time[maxIdx], data.values[maxIdx]));
                            points.append(QPointF(data.time[minIdx], data.values[minIdx]));
                        } else {
                            points.append(QPointF(data.time[minIdx], data.values[minIdx]));
                        }
                    }
                    // Always ensure we include the absolute last point
                    if (!points.isEmpty() && points.last().x() < data.time.back()) {
                        points.append(QPointF(data.time.back(), data.values.back()));
                    }
                    series->append(points);
                }
                
                const QList<QColor> colors = {QColor(0, 204, 0), QColor(0, 0, 255), QColor(255, 0, 0), QColor(0, 255, 255), QColor(255, 0, 255), QColor(255, 255, 0)};
                series->setPen(QPen(colors[colorIdx % colors.size()], 1.5));
                m_chart->addSeries(series);
                series->attachAxis(axisX);
                series->attachAxis(axisY);
                hasAnyChecked = true;
                colorIdx++;
            }
        }
    }
    
    if (hasAnyChecked || autoScale) {
        if (m_preserveXRangeOnce) {
            zoomFitYOnly();
            auto axesX = m_chart->axes(Qt::Horizontal);
            if (!axesX.isEmpty()) {
                if (auto* axisX = qobject_cast<QValueAxis*>(axesX[0])) {
                    axisX->setRange(m_preserveXMin, m_preserveXMax);
                }
            }
            m_preserveXRangeOnce = false;
        } else {
            zoomFit();
        }
    }

    if (m_holdXRangeCount > 0) {
        auto axesX = m_chart->axes(Qt::Horizontal);
        if (!axesX.isEmpty()) {
            if (auto* axisX = qobject_cast<QValueAxis*>(axesX[0])) {
                axisX->setRange(m_holdXMin, m_holdXMax);
            }
        }
        m_holdXRangeCount--;
    }
}

void WaveformViewer::zoomIn() { m_chart->zoomIn(); }
void WaveformViewer::zoomOut() { m_chart->zoomOut(); }
void WaveformViewer::zoomFit() {
    auto axesX = m_chart->axes(Qt::Horizontal);
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) return;
    
    double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
    bool found = false;

    // Use internal signal data for reliable range calculation instead of chart points
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->text();
            if (m_signals.contains(name)) {
                const auto& data = m_signals[name];
                if (data.time.isEmpty()) continue;
                
                auto minMaxX = std::minmax_element(data.time.begin(), data.time.end());
                auto minMaxY = std::minmax_element(data.values.begin(), data.values.end());
                
                minX = std::min(minX, *minMaxX.first); maxX = std::max(maxX, *minMaxX.second);
                minY = std::min(minY, *minMaxY.first); maxY = std::max(maxY, *minMaxY.second);
                found = true;
            }
        }
    }

    if (found) {
        // Ensure non-zero range for X
        if (std::abs(maxX - minX) < 1e-15) {
            minX -= 1.0;
            maxX += 1.0;
        }
        // Ensure non-zero range for Y
        if (std::abs(maxY - minY) < 1e-15) {
            double dy = std::abs(minY) < 1e-9 ? 1.0 : std::abs(minY) * 0.1;
            minY -= dy;
            maxY += dy;
        } else {
            double dy = (maxY - minY) * 0.1;
            minY -= dy;
            maxY += dy;
        }
        
        qobject_cast<QValueAxis*>(axesX[0])->setRange(minX, maxX);
        qobject_cast<QValueAxis*>(axesY[0])->setRange(minY, maxY);
    }
}
void WaveformViewer::resetZoom() { m_chart->zoomReset(); }

void WaveformViewer::zoomFitYOnly() {
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesY.isEmpty()) return;

    double minY = 1e30, maxY = -1e30;
    bool found = false;

    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->text();
            if (m_signals.contains(name)) {
                const auto& data = m_signals[name];
                if (data.values.isEmpty()) continue;
                auto minMaxY = std::minmax_element(data.values.begin(), data.values.end());
                minY = std::min(minY, *minMaxY.first);
                maxY = std::max(maxY, *minMaxY.second);
                found = true;
            }
        }
    }

    if (!found) return;

    if (std::abs(maxY - minY) < 1e-15) {
        double dy = std::abs(minY) < 1e-9 ? 1.0 : std::abs(minY) * 0.1;
        minY -= dy;
        maxY += dy;
    } else {
        double dy = (maxY - minY) * 0.1;
        minY -= dy;
        maxY += dy;
    }

    if (auto* axisY = qobject_cast<QValueAxis*>(axesY[0])) {
        axisY->setRange(minY, maxY);
    }
}

void WaveformViewer::onNodeSelected() {
    auto items = m_nodeList->selectedItems();
    if (!items.isEmpty()) m_activeSeriesName = items.last()->text();
    updateZoomAnalysis();
}

void WaveformViewer::onNodeClicked(QListWidgetItem *item) {
    if (!item) return;
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        showAnalysisForSeries(item->text());
        return;
    }
    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void WaveformViewer::updateNodeItemStyle(QListWidgetItem* item) {
    if (!item) return;
    if (item->checkState() == Qt::Checked) {
        item->setForeground(QColor("#111827"));
    } else {
        item->setForeground(QColor("#6b7280"));
    }
}

void WaveformViewer::updateZoomAnalysis() {
    if (m_activeSeriesName.isEmpty() || !m_signals.contains(m_activeSeriesName)) return;
    const auto& data = m_signals[m_activeSeriesName];
    if (data.values.isEmpty()) return;
    
    double sum = 0;
    for (double v : data.values) sum += v;
    double avg = sum / data.values.size();
    m_statsLabel->setText(QString("%1 Avg: %2").arg(m_activeSeriesName, SiFormatter::format(avg, "")));
}

void WaveformViewer::onSubtractRequested() {
    auto selected = m_nodeList->selectedItems();
    if (selected.size() != 2) return;
    addSignal(selected[0]->text() + "-" + selected[1]->text(), m_signals[selected[0]->text()].time, m_signals[selected[0]->text()].values); // Dummy sub
    updatePlot(true);
}

void WaveformViewer::onFftRequested() {
    if (m_activeSeriesName.isEmpty()) return;
    const auto& sig = m_signals[m_activeSeriesName];
    auto res = viospice::FftAnalyzer::compute(std::vector<double>(sig.time.begin(), sig.time.end()), std::vector<double>(sig.values.begin(), sig.values.end()));
    
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("FFT - " + m_activeSeriesName);
    auto* l = new QVBoxLayout(dlg);
    auto* chart = new QChart();
    auto* series = new QLineSeries();
    for (size_t i = 0; i < res.frequencies.size(); ++i) series->append(res.frequencies[i], res.magnitudes[i]);
    chart->addSeries(series);
    auto* ax = new QValueAxis(); ax->setTitleText("Hz"); chart->addAxis(ax, Qt::AlignBottom); series->attachAxis(ax);
    auto* ay = new QValueAxis(); ay->setTitleText("dBV"); chart->addAxis(ay, Qt::AlignLeft); series->attachAxis(ay);
    l->addWidget(new QChartView(chart));
    dlg->resize(600, 400);
    dlg->show();
}

void WaveformViewer::loadCsv(const QString&) {}

void WaveformViewer::onLegendCtrlClicked(const QString &seriesName) {
    if (seriesName.isEmpty()) return;
    if (m_signals.contains(seriesName)) {
        showAnalysisForSeries(seriesName);
        return;
    }
    for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
        if (it.key().compare(seriesName, Qt::CaseInsensitive) == 0) {
            showAnalysisForSeries(it.key());
            return;
        }
    }
}

void WaveformViewer::showAnalysisForSeries(const QString &seriesName) {
    if (seriesName.isEmpty() || !m_signals.contains(seriesName)) return;
    const auto& data = m_signals[seriesName];
    if (data.time.size() < 2 || data.values.size() < 2) return;

    double tStart = data.time.front();
    double tEnd = data.time.back();
    double minX = 0.0, maxX = 0.0;
    if (currentXRange(minX, maxX)) {
        tStart = std::max(tStart, minX);
        tEnd = std::min(tEnd, maxX);
    }
    if (tEnd <= tStart) return;

    double area = 0.0;
    double areaSq = 0.0;
    for (int i = 0; i < data.time.size() - 1; ++i) {
        double x0 = data.time[i];
        double x1 = data.time[i + 1];
        double y0 = data.values[i];
        double y1 = data.values[i + 1];
        if (x1 <= tStart || x0 >= tEnd) continue;

        const double segStart = std::max(x0, tStart);
        const double segEnd = std::min(x1, tEnd);
        const double span = x1 - x0;
        if (span <= 0.0) continue;
        const double u0 = (segStart - x0) / span;
        const double u1 = (segEnd - x0) / span;
        const double yStart = y0 + (y1 - y0) * u0;
        const double yEnd = y0 + (y1 - y0) * u1;
        const double dt = segEnd - segStart;

        area += 0.5 * (yStart + yEnd) * dt;
        areaSq += dt * (yStart * yStart + yStart * yEnd + yEnd * yEnd) / 3.0;
    }

    const double interval = tEnd - tStart;
    const double avg = area / interval;
    const double rms = std::sqrt(std::max(0.0, areaSq / interval));

    QString unit = "";
    QString rmsOrIntegralLabel;
    bool isPower = (data.type == SignalType::POWER);
    if (data.type == SignalType::VOLTAGE) unit = "V";
    else if (data.type == SignalType::CURRENT) unit = "A";
    else if (data.type == SignalType::POWER) unit = "W";

    const QString avgStr = SiFormatter::format(avg, unit);
    if (isPower) {
        rmsOrIntegralLabel = SiFormatter::format(area, "J");
    } else {
        rmsOrIntegralLabel = SiFormatter::format(rms, unit);
    }

    if (!m_analysisDialog) m_analysisDialog = new AnalysisDialog(this);
    m_analysisDialog->setValues(seriesName,
                                SiFormatter::format(tStart, "s"),
                                SiFormatter::format(tEnd, "s"),
                                avgStr,
                                rmsOrIntegralLabel,
                                isPower);
    m_analysisDialog->show();
    m_analysisDialog->raise();
    m_analysisDialog->activateWindow();
}
