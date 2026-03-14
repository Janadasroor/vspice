// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "waveform_viewer.h"
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
#include "../core/theme_manager.h"

QString WaveformViewer::formatValue(double val, const QString &unit) {
    double absVal = std::abs(val);
    if (absVal < 1e-18) return "0" + unit;
    
    static const struct { double mult; const char* sym; } suffixes[] = {
        {1e18, "E"}, {1e15, "P"}, {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"},
        {1.0, ""},
        {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}, {1e-18, "a"}
    };
    
    for (const auto& s : suffixes) {
        if (absVal >= s.mult * 0.999) {
            return QString::number(val / s.mult, 'g', 4) + s.sym + unit;
        }
    }
    return QString::number(val, 'g', 4) + unit;
}

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
    
    toolbar->addAction("Zoom In", this, &WaveformViewer::zoomIn);
    toolbar->addAction("Zoom Out", this, &WaveformViewer::zoomOut);
    toolbar->addAction("Zoom Fit", this, &WaveformViewer::zoomFit);
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
    m_nodeList->setFixedWidth(180);
    connect(m_nodeList, &QListWidget::itemSelectionChanged, this, &WaveformViewer::onNodeSelected);
    connect(m_nodeList, &QListWidget::itemChanged, this, [this](QListWidgetItem*){ updatePlot(false); });
    connect(m_nodeList, &QListWidget::itemClicked, this, &WaveformViewer::onNodeClicked);
    
    m_chart = new QChart();
    m_chart->setBackgroundVisible(false);
    m_chart->legend()->setAlignment(Qt::AlignBottom);

    m_chartView = new VioChartView(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);
    connect(m_chartView, &VioChartView::mouseMoved, this, &WaveformViewer::onMouseMoved);
    connect(m_chartView, &VioChartView::cursorMoved, this, &WaveformViewer::updateCursors);

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
    PCBTheme* theme = ThemeManager::theme();
    if (theme) {
        m_chart->setTheme(theme->type() == PCBTheme::Light ? QChart::ChartThemeLight : QChart::ChartThemeDark);
        m_chart->setBackgroundVisible(true);
        m_chart->setBackgroundBrush(QBrush(theme->panelBackground()));
        m_chart->setTitleBrush(QBrush(theme->textColor()));
        m_chart->legend()->setLabelColor(theme->textColor());
    } else {
        m_chart->setTheme(QChart::ChartThemeDark);
        m_chart->setTitleBrush(QBrush(Qt::white));
    }
}

void WaveformViewer::clear() {
    m_signals.clear();
    m_nodeList->blockSignals(true);
    m_nodeList->clear();
    m_nodeList->blockSignals(false);
    m_chart->removeAllSeries();
    auto axes = m_chart->axes();
    for (auto* a : axes) {
        m_chart->removeAxis(a);
        a->deleteLater();
    }
    m_activeSeriesName.clear();
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
        item->setCheckState(Qt::Unchecked); // Default to unchecked, user clicks to show
        m_nodeList->blockSignals(false);
    }
}

void WaveformViewer::setSignalChecked(const QString& name, bool checked) {
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->text() == name) {
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

    QString coordStr = QString("Time: %1").arg(formatValue(value.x() / m_timeMultiplier, "s"));
    
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
        coordStr += QString(" | V: %1").arg(formatValue(valV / m_vMultiplier, m_vUnit));
    }
    if (hasI) {
        double valI = value.y(); 
        coordStr += QString(" | I: %1").arg(formatValue(valI / m_iMultiplier, m_iUnit));
    }
    if (hasP) {
        double valP = value.y();
        coordStr += QString(" | P: %1").arg(formatValue(valP / m_pMultiplier, m_pUnit));
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
                formatValue(m_cursor1X / m_timeMultiplier, m_timeUnit), "N/A",
                formatValue(m_cursor2X / m_timeMultiplier, m_timeUnit), "N/A",
                formatValue(std::abs(m_cursor2X - m_cursor1X) / m_timeMultiplier, m_timeUnit), "N/A",
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
        m_measureDialog->updateValues(series->name(), 
            formatValue(m_cursor1X, "s"), formatValue(v1, ""),
            formatValue(m_cursor2X, "s"), formatValue(v2, ""),
            formatValue(m_cursor2X - m_cursor1X, "s"), formatValue(v2 - v1, ""),
            "---", "---"
        );
    }
}

void WaveformViewer::updatePlot(bool autoScale) {
    if (m_blockUpdates) return;
    
    m_chart->removeAllSeries();
    auto axes = m_chart->axes();
    for (auto* a : axes) {
        m_chart->removeAxis(a);
        a->deleteLater();
    }

    if (m_signals.isEmpty()) return;

    auto* axisX = new QValueAxis();
    axisX->setTitleText("Time (s)");
    
    auto* axisY = new QValueAxis();
    axisY->setTitleText("Amplitude");

    PCBTheme* theme = ThemeManager::theme();
    if (theme) {
        QPen axisPen(theme->textSecondary());
        axisX->setLinePen(axisPen);
        axisX->setLabelsColor(theme->textColor());
        axisX->setTitleBrush(QBrush(theme->textColor()));
        axisX->setGridLineColor(theme->panelBorder());

        axisY->setLinePen(axisPen);
        axisY->setLabelsColor(theme->textColor());
        axisY->setTitleBrush(QBrush(theme->textColor()));
        axisY->setGridLineColor(theme->panelBorder());
    }

    m_chart->addAxis(axisX, Qt::AlignBottom);
    m_chart->addAxis(axisY, Qt::AlignLeft);

    bool hasAnyChecked = false;
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
                    for (int j = 0; j < totalSize; ++j) {
                        series->append(data.time[j], data.values[j]);
                    }
                } else {
                    int bucketSize = totalSize / maxBuckets;
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
                            series->append(data.time[minIdx], data.values[minIdx]);
                            series->append(data.time[maxIdx], data.values[maxIdx]);
                        } else if (minIdx > maxIdx) {
                            series->append(data.time[maxIdx], data.values[maxIdx]);
                            series->append(data.time[minIdx], data.values[minIdx]);
                        } else {
                            series->append(data.time[minIdx], data.values[minIdx]);
                        }
                    }
                    // Always ensure we include the absolute last point
                    if (series->count() > 0 && series->at(series->count()-1).x() < data.time.back()) {
                        series->append(data.time.back(), data.values.back());
                    }
                }
                m_chart->addSeries(series);
                series->attachAxis(axisX);
                series->attachAxis(axisY);
                hasAnyChecked = true;
            }
        }
    }
    
    if (hasAnyChecked || autoScale) {
        zoomFit();
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

void WaveformViewer::onNodeSelected() {
    auto items = m_nodeList->selectedItems();
    if (!items.isEmpty()) m_activeSeriesName = items.last()->text();
    updateZoomAnalysis();
}

void WaveformViewer::onNodeClicked(QListWidgetItem *item) {
    if (!item) return;
    item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
}

void WaveformViewer::updateZoomAnalysis() {
    if (m_activeSeriesName.isEmpty() || !m_signals.contains(m_activeSeriesName)) return;
    const auto& data = m_signals[m_activeSeriesName];
    if (data.values.isEmpty()) return;
    
    double sum = 0;
    for (double v : data.values) sum += v;
    double avg = sum / data.values.size();
    m_statsLabel->setText(QString("%1 Avg: %2").arg(m_activeSeriesName, formatValue(avg, "")));
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
