// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "waveform_viewer.h"
#include "waveform_expression_dialog.h"
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
#include <QWheelEvent>
#include <QKeyEvent>
#include <QRubberBand>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QGraphicsLayout>
#include <QApplication>
#include <QToolButton>
#include <QRegularExpression>
#include <QDebug>
#include <QMenu>
#include <QClipboard>
#include <QColorDialog>
#include <QSvgGenerator>
#include <QPrinter>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <numeric>
#include <QGraphicsTextItem>
#include <QGraphicsSimpleTextItem>
#include "../core/theme_manager.h"

namespace {
constexpr double kDbFloor = 1e-15;

double toDb(double value) {
    const double mag = std::max(std::abs(value), kDbFloor);
    return 20.0 * std::log10(mag);
}

QString formatDb(double value) {
    return QString::number(value, 'g', 4) + " dB";
}
} // namespace


VioChartView::VioChartView(QChart *chart, QWidget *parent) : QChartView(chart, parent) {
    setMouseTracking(true);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
}

void VioChartView::mouseMoveEvent(QMouseEvent *event) {
    m_mousePos = event->pos();

    if (m_panning) {
        auto *c = chart();
        auto axesX = c->axes(Qt::Horizontal);
        auto axesY = c->axes(Qt::Vertical);
        if (!axesX.isEmpty() && !axesY.isEmpty()) {
            auto *axX = qobject_cast<QValueAxis*>(axesX.first());
            auto *axY = qobject_cast<QValueAxis*>(axesY.first());
            if (axX && axY) {
                QPointF startVal = c->mapToValue(m_panStart.toPoint());
                QPointF curVal = c->mapToValue(event->position().toPoint());
                double dx = startVal.x() - curVal.x();
                double dy = startVal.y() - curVal.y();
                axX->setRange(axX->min() + dx, axX->max() + dx);
                axY->setRange(axY->min() + dy, axY->max() + dy);
                m_panStart = event->position();
            }
        }
        event->accept();
        return;
    }

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

    if (m_zoomRectActive) {
        m_rubberBand->setGeometry(QRect(m_zoomRectStart, event->pos()).normalized());
    }

    QPointF value = chart()->mapToValue(event->pos());
    emit mouseMoved(value);
    QChartView::mouseMoveEvent(event);
}

void VioChartView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = event->position();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    // Handle Ctrl+left-click or right-click on legend
    bool isRightClick = (event->button() == Qt::RightButton);
    bool isCtrlLeftClick = (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier));
    if (isRightClick || isCtrlLeftClick) {
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
    if (event->button() == Qt::RightButton) {
        emit contextMenuRequested(mapToGlobal(event->pos()));
        event->accept();
        return;
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
    // Start zoom rectangle on plain left-click in plot area
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        QRectF plotArea = chart()->plotArea();
        if (plotArea.contains(event->pos())) {
            m_zoomRectActive = true;
            m_zoomRectStart = event->pos();
            m_rubberBand->setGeometry(QRect(m_zoomRectStart, QSize()).normalized());
            m_rubberBand->show();
            event->accept();
            return;
        }
    }
    QChartView::mousePressEvent(event);
}

void VioChartView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_zoomRectActive && event->button() == Qt::LeftButton) {
        m_zoomRectActive = false;
        m_rubberBand->hide();
        QRect pixelRect = QRect(m_zoomRectStart, event->pos()).normalized();
        if (pixelRect.width() > 5 && pixelRect.height() > 5) {
            QPointF topLeft = chart()->mapToValue(pixelRect.topLeft());
            QPointF bottomRight = chart()->mapToValue(pixelRect.bottomRight());
            double xMin = std::min(topLeft.x(), bottomRight.x());
            double xMax = std::max(topLeft.x(), bottomRight.x());
            double yMin = std::min(topLeft.y(), bottomRight.y());
            double yMax = std::max(topLeft.y(), bottomRight.y());
            emit zoomRectCompleted(QRectF(QPointF(xMin, yMin), QPointF(xMax, yMax)));
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    m_movingCursor = 0;
    QChartView::mouseReleaseEvent(event);
}

void VioChartView::wheelEvent(QWheelEvent *event) {
    auto *c = chart();
    if (!c) { QChartView::wheelEvent(event); return; }

    auto axesX = c->axes(Qt::Horizontal);
    auto axesY = c->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) { QChartView::wheelEvent(event); return; }

    auto *axX = qobject_cast<QValueAxis*>(axesX.first());
    auto *axY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axX || !axY) { QChartView::wheelEvent(event); return; }

    QPointF cursorVal = c->mapToValue(event->position().toPoint());

    const double factor = 1.15;
    double zoom = (event->angleDelta().y() > 0) ? 1.0 / factor : factor;

    double xMin = axX->min(), xMax = axX->max();
    double yMin = axY->min(), yMax = axY->max();

    double newXRange = (xMax - xMin) * zoom;
    double newYRange = (yMax - yMin) * zoom;

    double xRatio = (cursorVal.x() - xMin) / (xMax - xMin);
    double yRatio = (cursorVal.y() - yMin) / (yMax - yMin);

    double nxMin = cursorVal.x() - xRatio * newXRange;
    double nxMax = nxMin + newXRange;
    double nyMin = cursorVal.y() - yRatio * newYRange;
    double nyMax = nyMin + newYRange;

    axX->setRange(nxMin, nxMax);
    axY->setRange(nyMin, nyMax);

    event->accept();
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

void WaveformViewer::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_F:
        pushZoomState();
        zoomFit();
        event->accept();
        return;
    case Qt::Key_R:
        pushZoomState();
        resetZoom();
        event->accept();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomOut();
        event->accept();
        return;
    case Qt::Key_1:
        toggleCursors();
        event->accept();
        return;
    case Qt::Key_2:
        toggleCrosshair();
        event->accept();
        return;
    case Qt::Key_Left:
    case Qt::Key_Right:
        if (m_cursorsEnabled) {
            auto axesX = m_chart->axes(Qt::Horizontal);
            if (!axesX.isEmpty()) {
                auto *ax = qobject_cast<QValueAxis*>(axesX.first());
                if (ax) {
                    double step = (ax->max() - ax->min()) / 100.0;
                    if (event->key() == Qt::Key_Left) step = -step;
                    m_cursor1X += step;
                    updateCursors();
                }
            }
        }
        event->accept();
        return;
    case Qt::Key_Z:
        if (event->modifiers() & Qt::ControlModifier) {
            undoZoom();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Y:
        if (event->modifiers() & Qt::ControlModifier) {
            redoZoom();
            event->accept();
            return;
        }
        break;
    case Qt::Key_C:
        if (event->modifiers() & Qt::ControlModifier) {
            QString text;
            if (buildValueAtCursor(text)) {
                QClipboard *cb = QApplication::clipboard();
                if (cb) cb->setText(text);
            }
            event->accept();
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
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
    toolbar->addAction("Save", this, &WaveformViewer::exportImage);
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
    connect(m_chartView, &VioChartView::zoomRectCompleted, this, &WaveformViewer::onZoomRectCompleted);
    connect(m_chartView, &VioChartView::contextMenuRequested, this, &WaveformViewer::onContextMenuRequested);

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
    QVector<double> emptyPhase;
    addSignal(name, time, values, emptyPhase);
}

void WaveformViewer::addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values, const QColor &color) {
    QVector<double> emptyPhase;
    addSignal(name, time, values, emptyPhase);
    if (color.isValid() && m_signals.contains(name)) {
        m_signals[name].customColor = color;
    }
}

void WaveformViewer::addSignal(const QString& name, const QVector<double>& time, const QVector<double>& values, const QVector<double>& phase) {
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
    if (!phase.isEmpty() && phase.size() == values.size()) {
        data.phase = phase;
        data.hasPhase = true;
    }
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

    // Fast path: if the series is already in the chart, append to it directly
    QLineSeries* lineSeries = nullptr;
    for (auto* s : m_chart->series()) {
        if (s->name() == name) {
            lineSeries = qobject_cast<QLineSeries*>(s);
            break;
        }
    }

    if (lineSeries) {
        const int batchSize = static_cast<int>(times.size());
        const int maxBucketsInChart = 5000;
        
        // If the batch is very large, decimate it before appending
        if (batchSize > 200) {
            int step = batchSize / 100;
            if (step < 1) step = 1;
            QList<QPointF> decimatedPoints;
            decimatedPoints.reserve(batchSize / step + 1);
            for (int i = 0; i < batchSize; i += step) {
                decimatedPoints.append(QPointF(times[i], m_acMode ? toDb(values[i]) : values[i]));
            }
            lineSeries->append(decimatedPoints);
        } else {
            for (size_t i = 0; i < times.size(); ++i) {
                lineSeries->append(times[i], m_acMode ? toDb(values[i]) : values[i]);
            }
        }

        // If the series has grown too large, trigger a full decimation to keep UI responsive
        if (lineSeries->count() > maxBucketsInChart * 1.5) {
            QMetaObject::invokeMethod(this, "updatePlot", Qt::QueuedConnection, Q_ARG(bool, false));
        } else {
            // Throttled auto-scale check (rough)
            double tLast = times.back();
            auto axesX = m_chart->axes(Qt::Horizontal);
            if (!axesX.isEmpty()) {
                if (auto* ax = qobject_cast<QValueAxis*>(axesX[0])) {
                    if (tLast > ax->max()) ax->setMax(tLast * 1.05);
                }
            }
        }
        return;
    }

    // Fallback: trigger updatePlot if not yet in chart or complex state
    int& count = m_pointCounters[name];
    count += times.size();
    if ((count % 2000) < times.size() || count < 100) { 
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

    m_lastMouseValue = value;
    m_hasLastMouseValue = true;

    if (m_acMode) {
        const QString coordStr = QString("Freq: %1 | Mag: %2")
                                     .arg(SiFormatter::format(value.x(), "Hz"))
                                     .arg(formatDb(value.y()));
        m_coordLabel->setText(coordStr);
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
        m_measureDialog->setAcMode(m_acMode);
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
            const QString h1 = m_acMode ? SiFormatter::format(m_cursor1X, "Hz")
                                        : SiFormatter::format(m_cursor1X / m_timeMultiplier, m_timeUnit);
            const QString h2 = m_acMode ? SiFormatter::format(m_cursor2X, "Hz")
                                        : SiFormatter::format(m_cursor2X / m_timeMultiplier, m_timeUnit);
            const QString dh = m_acMode ? SiFormatter::format(std::abs(m_cursor2X - m_cursor1X), "Hz")
                                        : SiFormatter::format(std::abs(m_cursor2X - m_cursor1X) / m_timeMultiplier, m_timeUnit);
            if (m_acMode) {
                m_measureDialog->updateAcValues("",
                    h1, "---", "---", "---",
                    h2, "---", "---", "---",
                    dh, "---", "---", "---");
            } else {
                m_measureDialog->updateValues("", 
                    h1, "N/A",
                    h2, "N/A",
                    dh, "N/A",
                    "---", "---");
            }
        }
        return;
    }

    QLineSeries *series = qobject_cast<QLineSeries*>(m_chart->series().first());
    if (!series) return;

    m_cursor1X = m_chartView->cursor1X();
    m_cursor2X = m_chartView->cursor2X();
    
    auto getY = [&](QLineSeries* s, double x) {
        if (!s) return 0.0;
        auto points = s->points();
        if (points.isEmpty()) return 0.0;
        for (int i = 0; i < points.size() - 1; ++i) {
            if (x >= points[i].x() && x <= points[i+1].x()) {
                double t = (x - points[i].x()) / (points[i+1].x() - points[i].x());
                return points[i].y() + t * (points[i+1].y() - points[i].y());
            }
        }
        return points.last().y();
    };

    auto getPhaseSeries = [&](const QString& baseName) -> QLineSeries* {
        const QString phaseName = baseName + " (Phase)";
        for (auto* s : m_chart->series()) {
            if (auto* ls = qobject_cast<QLineSeries*>(s)) {
                if (ls->name() == phaseName) return ls;
            }
        }
        return nullptr;
    };

    auto getYLegacy = [&](double x) {
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

    double v1 = getYLegacy(m_cursor1X);
    double v2 = getYLegacy(m_cursor2X);
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

        if (m_acMode) {
            QString ph1 = "---";
            QString ph2 = "---";
            QString dph = "---";
            QString gd = "---";
            QLineSeries* phaseSeries = getPhaseSeries(series->name());
            bool hasPhase = (phaseSeries != nullptr);
            if (hasPhase) {
                const double p1 = getY(phaseSeries, m_cursor1X);
                const double p2 = getY(phaseSeries, m_cursor2X);
                ph1 = QString::number(p1, 'g', 4) + " deg";
                ph2 = QString::number(p2, 'g', 4) + " deg";
                dph = QString::number(p2 - p1, 'g', 4) + " deg";
                if (std::abs(dt) > 1e-12) {
                    const double gdVal = -((p2 - p1) / 360.0) / dt;
                    gd = SiFormatter::format(gdVal, "s");
                }
            }

            m_measureDialog->updateAcValues(series->name(),
                SiFormatter::format(m_cursor1X, "Hz"), formatDb(v1), ph1, "---",
                SiFormatter::format(m_cursor2X, "Hz"), formatDb(v2), ph2, "---",
                SiFormatter::format(dt, "Hz"), formatDb(dv), dph, gd);
        } else {
            m_measureDialog->updateValues(series->name(), 
                SiFormatter::format(m_cursor1X, "s"), SiFormatter::format(v1, ""),
                SiFormatter::format(m_cursor2X, "s"), SiFormatter::format(v2, ""),
                SiFormatter::format(dt, "s"), SiFormatter::format(dv, ""),
                freqStr, slopeStr
            );
        }
    }
}

void WaveformViewer::updatePlot(bool autoScale) {
    if (m_blockUpdates) return;
    
    // Save current X/Y ranges unless autoScale is requested
    double xMin = 0, xMax = 1, yMin = -1, yMax = 1;
    bool hasRange = false;
    auto oldAxesX = m_chart->axes(Qt::Horizontal);
    auto oldAxesY = m_chart->axes(Qt::Vertical);
    if (!autoScale && !oldAxesX.isEmpty() && !oldAxesY.isEmpty()) {
        auto* ax = qobject_cast<QValueAxis*>(oldAxesX[0]);
        auto* ay = qobject_cast<QValueAxis*>(oldAxesY[0]);
        if (ax && ay) {
            xMin = ax->min(); xMax = ax->max();
            yMin = ay->min(); yMax = ay->max();
            hasRange = true;
        }
    }

    m_chartView->setCursorPositions(m_chartView->cursor1X(), 0, m_chartView->cursor2X(), 0, nullptr);
    m_chart->removeAllSeries();
    auto axes = m_chart->axes();
    for (auto* a : axes) {
        m_chart->removeAxis(a);
        a->deleteLater();
    }
    // Explicitly clear pointer counters to prevent stale data logic
    m_pointCounters.clear();

    if (m_signals.isEmpty()) return;

    auto* axisX = new QValueAxis();
    axisX->setTitleText(m_acMode ? "Frequency (Hz)" : "Time (s)");
    axisX->setLabelFormat("%.2g");
    
    auto* axisY = new QValueAxis();
    axisY->setTitleText(m_acMode ? "Magnitude (dB)" : "Amplitude");
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

    bool needsPhaseAxis = false;
    double minPhase = 1e30;
    double maxPhase = -1e30;

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
                
                auto yForPlot = [&](double v) {
                    return m_acMode ? toDb(v) : v;
                };

                QVector<double> sortedTime;
                QVector<double> sortedValues;
                QVector<double> sortedPhase;
                if (m_acMode && totalSize > 1) {
                    QVector<int> order(totalSize);
                    std::iota(order.begin(), order.end(), 0);
                    std::sort(order.begin(), order.end(), [&](int a, int b) {
                        return data.time[a] < data.time[b];
                    });
                    sortedTime.resize(totalSize);
                    sortedValues.resize(totalSize);
                    if (data.hasPhase && data.phase.size() == data.values.size()) {
                        sortedPhase.resize(totalSize);
                    }
                    for (int j = 0; j < totalSize; ++j) {
                        sortedTime[j] = data.time[order[j]];
                        sortedValues[j] = data.values[order[j]];
                        if (!sortedPhase.isEmpty()) {
                            sortedPhase[j] = data.phase[order[j]];
                        }
                    }
                } else {
                    sortedTime = data.time;
                    sortedValues = data.values;
                    if (data.hasPhase && data.phase.size() == data.values.size()) {
                        sortedPhase = data.phase;
                    }
                }

                if (totalSize <= maxBuckets) {
                    QList<QPointF> points;
                    points.reserve(totalSize);
                    for (int j = 0; j < totalSize; ++j) {
                        points.append(QPointF(sortedTime[j], yForPlot(sortedValues[j])));
                    }
                    series->append(points);
                } else {
                    // Optimized: only decimate visible range if possible
                    int startIdx = 0;
                    int endIdx = totalSize;
                    if (hasRange && !autoScale) {
                        auto itStart = std::lower_bound(sortedTime.begin(), sortedTime.end(), xMin);
                        auto itEnd = std::upper_bound(sortedTime.begin(), sortedTime.end(), xMax);
                        startIdx = std::distance(sortedTime.begin(), itStart);
                        endIdx = std::distance(sortedTime.begin(), itEnd);
                        if (startIdx > 0) startIdx--; // include one point before for continuity
                        if (endIdx < totalSize) endIdx++; // include one point after
                    }

                    const int rangeSize = endIdx - startIdx;
                    if (rangeSize <= maxBuckets) {
                        QList<QPointF> points;
                        points.reserve(rangeSize);
                        for (int j = startIdx; j < endIdx; ++j) {
                            points.append(QPointF(sortedTime[j], yForPlot(sortedValues[j])));
                        }
                        series->append(points);
                    } else {
                        int bucketSize = rangeSize / maxBuckets;
                        if (bucketSize < 1) bucketSize = 1;
                        QList<QPointF> points;
                        points.reserve(maxBuckets * 2 + 1);
                        for (int b = 0; b < maxBuckets; ++b) {
                            int start = startIdx + b * bucketSize;
                            int end = (b == maxBuckets - 1) ? endIdx : start + bucketSize;
                            if (start >= endIdx) break;
                            
                            int minIdx = start;
                            int maxIdx = start;
                            double minVal = yForPlot(sortedValues[start]);
                            double maxVal = minVal;
                            for (int j = start + 1; j < std::min(end, endIdx); ++j) {
                                const double y = yForPlot(sortedValues[j]);
                                if (y < minVal) { minVal = y; minIdx = j; }
                                if (y > maxVal) { maxVal = y; maxIdx = j; }
                            }
                            
                            if (minIdx < maxIdx) {
                                points.append(QPointF(sortedTime[minIdx], yForPlot(sortedValues[minIdx])));
                                points.append(QPointF(sortedTime[maxIdx], yForPlot(sortedValues[maxIdx])));
                            } else if (minIdx > maxIdx) {
                                points.append(QPointF(sortedTime[maxIdx], yForPlot(sortedValues[maxIdx])));
                                points.append(QPointF(sortedTime[minIdx], yForPlot(sortedValues[minIdx])));
                            } else {
                                points.append(QPointF(sortedTime[minIdx], yForPlot(sortedValues[minIdx])));
                            }
                        }
                        series->append(points);
                    }
                }
                
                const QList<QColor> colors = {QColor(0, 204, 0), QColor(0, 0, 255), QColor(255, 0, 0), QColor(0, 255, 255), QColor(255, 0, 255), QColor(255, 255, 0)};
                QColor seriesColor = data.customColor.isValid() ? data.customColor : colors[colorIdx % colors.size()];
                QPen pen(seriesColor, data.lineWidth, data.penStyle);
                series->setPen(pen);
                m_chart->addSeries(series);
                series->attachAxis(axisX);
                series->attachAxis(axisY);

                if (m_acMode && data.hasPhase && !sortedPhase.isEmpty()) {
                    auto* phaseSeries = new QLineSeries();
                    phaseSeries->setName(name + " (Phase)");
                    QPen phasePen(colors[colorIdx % colors.size()], 1.2, Qt::DashLine);
                    phaseSeries->setPen(phasePen);

                    if (totalSize <= maxBuckets) {
                        QList<QPointF> points;
                        points.reserve(totalSize);
                        for (int j = 0; j < totalSize; ++j) {
                            points.append(QPointF(sortedTime[j], sortedPhase[j]));
                        }
                        phaseSeries->append(points);
                    } else {
                        int bucketSize = totalSize / maxBuckets;
                        QList<QPointF> points;
                        points.reserve(maxBuckets * 2 + 1);
                        for (int b = 0; b < maxBuckets; ++b) {
                            int start = b * bucketSize;
                            int end = (b == maxBuckets - 1) ? totalSize : (b + 1) * bucketSize;

                            int minIdx = start;
                            int maxIdx = start;
                            double minVal = sortedPhase[start];
                            double maxVal = minVal;
                            for (int j = start + 1; j < end; ++j) {
                                const double y = sortedPhase[j];
                                if (y < minVal) { minVal = y; minIdx = j; }
                                if (y > maxVal) { maxVal = y; maxIdx = j; }
                            }

                            if (minIdx < maxIdx) {
                                points.append(QPointF(sortedTime[minIdx], sortedPhase[minIdx]));
                                points.append(QPointF(sortedTime[maxIdx], sortedPhase[maxIdx]));
                            } else if (minIdx > maxIdx) {
                                points.append(QPointF(sortedTime[maxIdx], sortedPhase[maxIdx]));
                                points.append(QPointF(sortedTime[minIdx], sortedPhase[minIdx]));
                            } else {
                                points.append(QPointF(sortedTime[minIdx], sortedPhase[minIdx]));
                            }
                        }
                        if (!points.isEmpty() && points.last().x() < sortedTime.back()) {
                            points.append(QPointF(sortedTime.back(), sortedPhase.back()));
                        }
                        phaseSeries->append(points);
                    }

                    if (sortedPhase.size() == sortedTime.size()) {
                        for (double v : sortedPhase) {
                            minPhase = std::min(minPhase, v);
                            maxPhase = std::max(maxPhase, v);
                        }
                        needsPhaseAxis = true;
                    }

                    m_chart->addSeries(phaseSeries);
                    phaseSeries->attachAxis(axisX);
                    // axisYPhase will be added after series loop if needed
                }

                hasAnyChecked = true;
                colorIdx++;
            }
        }
    }

    QValueAxis* axisYPhase = nullptr;
    if (m_acMode && needsPhaseAxis) {
        axisYPhase = new QValueAxis();
        axisYPhase->setTitleText("Phase (deg)");
        axisYPhase->setLabelFormat("%.0f");
        axisYPhase->setGridLineVisible(false);
        if (!std::isfinite(minPhase) || !std::isfinite(maxPhase) || minPhase > maxPhase) {
            minPhase = -180.0;
            maxPhase = 180.0;
        }
        if (std::abs(maxPhase - minPhase) < 1e-6) {
            minPhase -= 10.0;
            maxPhase += 10.0;
        } else {
            const double pad = (maxPhase - minPhase) * 0.1;
            minPhase -= pad;
            maxPhase += pad;
        }
        axisYPhase->setRange(minPhase, maxPhase);
        axisYPhase->setLinePen(axisPen);
        axisYPhase->setLabelsBrush(QBrush(Qt::white));
        axisYPhase->setTitleBrush(QBrush(Qt::white));
        m_chart->addAxis(axisYPhase, Qt::AlignRight);

        // Attach phase series to the phase axis
        for (auto* s : m_chart->series()) {
            if (s->name().endsWith("(Phase)")) {
                s->attachAxis(axisYPhase);
            }
        }
    }
    
    if (hasAnyChecked || autoScale) {
        if (m_preserveXRangeOnce) {
            zoomFitYOnly();
            auto axesX = m_chart->axes(Qt::Horizontal);
            if (!axesX.isEmpty()) {
                if (auto* axX = qobject_cast<QValueAxis*>(axesX[0])) {
                    axX->setRange(m_preserveXMin, m_preserveXMax);
                }
            }
            m_preserveXRangeOnce = false;
        } else if (hasRange && !autoScale) {
            // Restore saved ranges
            auto axesX = m_chart->axes(Qt::Horizontal);
            auto axesY = m_chart->axes(Qt::Vertical);
            if (!axesX.isEmpty()) qobject_cast<QValueAxis*>(axesX[0])->setRange(xMin, xMax);
            if (!axesY.isEmpty()) qobject_cast<QValueAxis*>(axesY[0])->setRange(yMin, yMax);
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
                double localMinY = 1e30;
                double localMaxY = -1e30;
                for (double v : data.values) {
                    const double y = m_acMode ? toDb(v) : v;
                    localMinY = std::min(localMinY, y);
                    localMaxY = std::max(localMaxY, y);
                }
                
                minX = std::min(minX, *minMaxX.first);
                maxX = std::max(maxX, *minMaxX.second);
                minY = std::min(minY, localMinY);
                maxY = std::max(maxY, localMaxY);
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

void WaveformViewer::pushZoomState() {
    auto axesX = m_chart->axes(Qt::Horizontal);
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) return;
    auto *axX = qobject_cast<QValueAxis*>(axesX.first());
    auto *axY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axX || !axY) return;
    ZoomState s = {axX->min(), axX->max(), axY->min(), axY->max()};
    // Avoid duplicate pushes
    if (!m_zoomUndo.isEmpty()) {
        const auto &top = m_zoomUndo.top();
        if (qFuzzyCompare(top.xMin, s.xMin) && qFuzzyCompare(top.xMax, s.xMax) &&
            qFuzzyCompare(top.yMin, s.yMin) && qFuzzyCompare(top.yMax, s.yMax))
            return;
    }
    m_zoomUndo.push(s);
    m_zoomRedo.clear();
}

void WaveformViewer::applyZoomState(const ZoomState &s) {
    auto axesX = m_chart->axes(Qt::Horizontal);
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) return;
    auto *axX = qobject_cast<QValueAxis*>(axesX.first());
    auto *axY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axX || !axY) return;
    axX->setRange(s.xMin, s.xMax);
    axY->setRange(s.yMin, s.yMax);
}

void WaveformViewer::onZoomRectCompleted(const QRectF &valueRect) {
    pushZoomState();
    auto axesX = m_chart->axes(Qt::Horizontal);
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) return;
    auto *axX = qobject_cast<QValueAxis*>(axesX.first());
    auto *axY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axX || !axY) return;
    axX->setRange(valueRect.left(), valueRect.right());
    axY->setRange(valueRect.top(), valueRect.bottom());
}

void WaveformViewer::undoZoom() {
    if (m_zoomUndo.isEmpty()) return;
    auto axesX = m_chart->axes(Qt::Horizontal);
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) return;
    auto *axX = qobject_cast<QValueAxis*>(axesX.first());
    auto *axY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axX || !axY) return;
    // Push current state to redo
    m_zoomRedo.push({axX->min(), axX->max(), axY->min(), axY->max()});
    applyZoomState(m_zoomUndo.pop());
}

void WaveformViewer::redoZoom() {
    if (m_zoomRedo.isEmpty()) return;
    auto axesX = m_chart->axes(Qt::Horizontal);
    auto axesY = m_chart->axes(Qt::Vertical);
    if (axesX.isEmpty() || axesY.isEmpty()) return;
    auto *axX = qobject_cast<QValueAxis*>(axesX.first());
    auto *axY = qobject_cast<QValueAxis*>(axesY.first());
    if (!axX || !axY) return;
    // Push current state to undo
    m_zoomUndo.push({axX->min(), axX->max(), axY->min(), axY->max()});
    applyZoomState(m_zoomRedo.pop());
}

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
                double localMinY = 1e30;
                double localMaxY = -1e30;
                for (double v : data.values) {
                    const double y = m_acMode ? toDb(v) : v;
                    localMinY = std::min(localMinY, y);
                    localMaxY = std::max(localMaxY, y);
                }
                minY = std::min(minY, localMinY);
                maxY = std::max(maxY, localMaxY);
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

void WaveformViewer::setAcMode(bool enabled) {
    if (m_acMode == enabled) return;
    m_acMode = enabled;
    if (m_measureDialog) {
        m_measureDialog->setAcMode(m_acMode);
    }
    updatePlot(false);
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

void WaveformViewer::onContextMenuRequested(const QPoint &globalPos) {
    QMenu menu(this);

    QAction* fitAct = menu.addAction("Fit");
    QAction* fitYAct = menu.addAction("Fit Y Only");
    QAction* resetAct = menu.addAction("Reset Zoom");
    menu.addSeparator();

    QAction* cursorsAct = menu.addAction("Toggle Cursors");
    cursorsAct->setCheckable(true);
    cursorsAct->setChecked(m_cursorsEnabled);

    QAction* crosshairAct = menu.addAction("Toggle Crosshair");
    crosshairAct->setCheckable(true);
    crosshairAct->setChecked(m_chartView->isCrosshairEnabled());

    menu.addSeparator();

    QAction* copyAct = menu.addAction("Copy Value at Cursor");
    QString copyText;
    if (!buildValueAtCursor(copyText)) {
        copyAct->setEnabled(false);
    }

    QAction* exportAct = menu.addAction("Export CSV (Current Signals)");

    menu.addSeparator();
    QAction* exportImgAct = menu.addAction("Export Image...");

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == fitAct) zoomFit();
    else if (chosen == fitYAct) { zoomFitYOnly(); }
    else if (chosen == resetAct) resetZoom();
    else if (chosen == cursorsAct) toggleCursors();
    else if (chosen == crosshairAct) toggleCrosshair();
    else if (chosen == copyAct) {
        QClipboard* cb = QApplication::clipboard();
        if (cb) cb->setText(copyText);
    } else if (chosen == exportAct) {
        exportSignalsCsv();
    } else if (chosen == exportImgAct) {
        exportImage();
    }
}

bool WaveformViewer::buildValueAtCursor(QString &outText) const {
    if (!m_hasLastMouseValue) return false;
    if (m_activeSeriesName.isEmpty()) return false;
    if (!m_signals.contains(m_activeSeriesName)) return false;

    const auto& data = m_signals[m_activeSeriesName];
    if (data.time.isEmpty() || data.values.isEmpty()) return false;

    // Find nearest time index to current mouse X
    const double targetX = m_lastMouseValue.x();
    int bestIdx = 0;
    double bestDist = std::abs(data.time[0] - targetX);
    for (int i = 1; i < data.time.size(); ++i) {
        const double d = std::abs(data.time[i] - targetX);
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    }

    double y = data.values[bestIdx];
    if (m_acMode) {
        outText = QString("%1 @ %2 : %3")
                      .arg(m_activeSeriesName)
                      .arg(SiFormatter::format(data.time[bestIdx], "Hz"))
                      .arg(formatDb(y));
        return true;
    }

    QString unit;
    double scale = 1.0;
    switch (data.type) {
    case SignalType::VOLTAGE: unit = m_vUnit; scale = m_vMultiplier; break;
    case SignalType::CURRENT: unit = m_iUnit; scale = m_iMultiplier; break;
    case SignalType::POWER: unit = m_pUnit; scale = m_pMultiplier; break;
    case SignalType::OTHER: unit = ""; scale = 1.0; break;
    }

    outText = QString("%1 @ %2 : %3")
                  .arg(m_activeSeriesName)
                  .arg(SiFormatter::format(data.time[bestIdx] / m_timeMultiplier, "s"))
                  .arg(SiFormatter::format(y / scale, unit));
    return true;
}

void WaveformViewer::exportSignalsCsv() {
    QString path = QFileDialog::getSaveFileName(this, "Export Signals CSV", QString(), "CSV Files (*.csv)");
    if (path.isEmpty()) return;

    QStringList names;
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->checkState() == Qt::Checked) names << item->text();
    }
    if (names.isEmpty()) {
        for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
            names << it.key();
        }
    }
    if (names.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    QTextStream out(&f);

    out << "index";
    for (const auto& n : names) out << "," << n << "_x," << n << "_y";
    out << "\n";

    int maxSize = 0;
    for (const auto& n : names) {
        const auto& sig = m_signals[n];
        maxSize = std::max(maxSize, static_cast<int>(sig.time.size()));
    }

    for (int i = 0; i < maxSize; ++i) {
        out << i;
        for (const auto& n : names) {
            const auto& sig = m_signals[n];
            if (i < sig.time.size() && i < sig.values.size()) out << "," << sig.time[i] << "," << sig.values[i];
            else out << ",,";
        }
        out << "\n";
    }
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

void WaveformViewer::onExpressionSubmitted(const QString &expression, const QColor &color, const QString &targetName) {
    qDebug() << "onExpressionSubmitted called with:" << expression << "color:" << color << "color.isValid:" << color.isValid();
    qDebug() << "Available signals:" << m_signals.keys();
    
    QString newSignalName = expression;
    if (newSignalName.isEmpty()) {
        newSignalName = targetName.isEmpty() ? "result" : targetName;
    }
    
    qDebug() << "New signal name:" << newSignalName;
    
    QStringList signalNames;
    QString error;
    QRegularExpression exprRe("(V|I|P)\\([^)]+\\)|(derivative|integral)\\([^)]+\\)", QRegularExpression::CaseInsensitiveOption);
    bool hasExpression = expression.contains(exprRe);
    
    auto isBaseProbeName = [](const QString &name) -> bool {
        QRegularExpression baseProbeRe("^[VIP]\\([^)]+\\)$", QRegularExpression::CaseInsensitiveOption);
        return baseProbeRe.match(name).hasMatch();
    };

    if (hasExpression) {
        if (!parseExpression(expression, signalNames, error)) {
            qDebug() << "parseExpression failed:" << error;
            return;
        }
        
        qDebug() << "parseExpression succeeded, signalNames:" << signalNames;
        
        QVector<double> time, values;
        if (!evaluateExpression(expression, signalNames, time, values)) {
            qDebug() << "evaluateExpression failed";
            return;
        }
        
        qDebug() << "evaluateExpression succeeded, time size:" << time.size() << "values size:" << values.size();
        
        if (!targetName.isEmpty() && targetName != newSignalName && m_signals.contains(targetName)) {
            const bool preserveSourceSignal = isBaseProbeName(targetName);

            SignalData data = m_signals[targetName];
            data.name = newSignalName;
            data.time = time;
            data.values = values;
            m_signals[newSignalName] = data;

            if (!preserveSourceSignal) {
                m_signals.remove(targetName);
            }

            for (int i = 0; i < m_nodeList->count(); ++i) {
                if (m_nodeList->item(i)->text() == targetName) {
                    m_nodeList->item(i)->setText(newSignalName);
                    break;
                }
            }
        } else if (m_signals.contains(newSignalName)) {
            m_signals[newSignalName].name = newSignalName;
            m_signals[newSignalName].time = time;
            m_signals[newSignalName].values = values;
        } else {
            SignalData data;
            data.time = time;
            data.values = values;
            data.name = expression;
            data.type = SignalType::VOLTAGE;
            m_signals[newSignalName] = data;
            
            auto* item = new QListWidgetItem(newSignalName, m_nodeList);
            item->setCheckState(Qt::Checked);
        }
    } else {
        qDebug() << "No expression with V() found, keeping existing signal data";
    }
    
    qDebug() << "Signal updated:" << newSignalName;
    
    if (color.isValid()) {
        qDebug() << "Storing custom color:" << color << "for signal:" << newSignalName;
        if (m_signals.contains(newSignalName)) {
            m_signals[newSignalName].customColor = color;
        }
    } else {
        qDebug() << "Color is not valid, skipping color storage";
    }
    
    updatePlot(true);
}

bool WaveformViewer::parseExpression(const QString &expression, QStringList &signalNames, QString &error) {
    QRegularExpression vRe("V\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression iRe("I\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression pRe("P\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    
    QMap<QString, QStringList> exprPatterns;
    exprPatterns["V"] = QStringList();
    exprPatterns["I"] = QStringList();
    exprPatterns["P"] = QStringList();
    
    auto extractSignal = [&signalNames, this](const QString &type, const QString &net) -> bool {
        QString prefix = QString("%1(%2)").arg(type).arg(net);
        for (auto it2 = m_signals.keyValueBegin(); it2 != m_signals.keyValueEnd(); ++it2) {
            QString key = it2->first;
            if (key.toLower() == prefix.toLower() || key.toLower() == net.toLower()) {
                QString storedNetName;
                QRegularExpression extractRe(QString("^%1\\((.+)\\)$").arg(type), QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch m = extractRe.match(key);
                if (m.hasMatch()) {
                    storedNetName = m.captured(1);
                } else {
                    storedNetName = key;
                }
                if (!signalNames.contains(storedNetName)) {
                    signalNames.append(storedNetName);
                    qDebug() << "parseExpression: added" << storedNetName << "to signalNames";
                }
                return true;
            }
        }
        return false;
    };
    
    QRegularExpressionMatchIterator itV = vRe.globalMatch(expression);
    while (itV.hasNext()) {
        QRegularExpressionMatch match = itV.next();
        QString net = match.captured(1);
        qDebug() << "parseExpression: found V(" << net << ")";
        if (!extractSignal("V", net)) {
            error = QString("Signal 'V(%1)' not found").arg(net);
            return false;
        }
    }
    
    QRegularExpressionMatchIterator itI = iRe.globalMatch(expression);
    while (itI.hasNext()) {
        QRegularExpressionMatch match = itI.next();
        QString net = match.captured(1);
        qDebug() << "parseExpression: found I(" << net << ")";
        if (!extractSignal("I", net)) {
            error = QString("Signal 'I(%1)' not found").arg(net);
            return false;
        }
    }
    
    QRegularExpressionMatchIterator itP = pRe.globalMatch(expression);
    while (itP.hasNext()) {
        QRegularExpressionMatch match = itP.next();
        QString net = match.captured(1);
        qDebug() << "parseExpression: found P(" << net << ")";
        if (!extractSignal("P", net)) {
            error = QString("Signal 'P(%1)' not found").arg(net);
            return false;
        }
    }
    
    QRegularExpression funcRe("(derivative|integral)\\(([VI])\\(([^)]+)\\)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator itFunc = funcRe.globalMatch(expression);
    while (itFunc.hasNext()) {
        QRegularExpressionMatch match = itFunc.next();
        QString func = match.captured(1).toLower();
        QString type = match.captured(2).toUpper();
        QString net = match.captured(3);
        qDebug() << "parseExpression: found" << func << "(" << type << "(" << net << "))";
        if (!extractSignal(type, net)) {
            error = QString("Signal '%1(%2)' not found").arg(type).arg(net);
            return false;
        }
    }
    
    qDebug() << "Parsed expression:" << expression << "found signals:" << signalNames;
    return true;
}

QVector<double> WaveformViewer::computeDerivative(const QVector<double> &time, const QVector<double> &values) {
    QVector<double> derivative;
    int n = qMin(time.size(), values.size());
    if (n < 2) return derivative;
    
    derivative.resize(n);
    derivative[0] = 0.0;
    
    for (int i = 1; i < n; ++i) {
        double dt = time[i] - time[i-1];
        if (qFuzzyIsNull(dt)) {
            derivative[i] = 0.0;
        } else {
            derivative[i] = (values[i] - values[i-1]) / dt;
        }
    }
    return derivative;
}

QVector<double> WaveformViewer::computeIntegral(const QVector<double> &time, const QVector<double> &values) {
    QVector<double> integral;
    int n = qMin(time.size(), values.size());
    if (n < 2) return integral;
    
    integral.resize(n);
    integral[0] = 0.0;
    
    for (int i = 1; i < n; ++i) {
        double dt = time[i] - time[i-1];
        double avgVal = (values[i] + values[i-1]) / 2.0;
        integral[i] = integral[i-1] + avgVal * dt;
    }
    return integral;
}

bool WaveformViewer::evaluateExpression(const QString &expression, const QStringList &signalNames, QVector<double> &time, QVector<double> &values) {
    qDebug() << "evaluateExpression: expression=" << expression << "signalNames=" << signalNames;
    if (signalNames.isEmpty()) {
        qDebug() << "evaluateExpression: no signal names provided, returning false";
        return false;
    }
    
    QMap<QString, QPair<QVector<double>, QVector<double>>> signalData;
    for (const QString &sig : signalNames) {
        QStringList prefixes = {sig, QString("V(%1)").arg(sig), QString("I(%1)").arg(sig), QString("P(%1)").arg(sig)};
        QString foundKey;
        for (const QString &key : prefixes) {
            if (m_signals.contains(key)) {
                foundKey = key;
                break;
            }
        }
        if (foundKey.isEmpty()) {
            qDebug() << "evaluateExpression: signal" << sig << "not found with any prefix";
            return false;
        }
        signalData[sig] = qMakePair(m_signals[foundKey].time, m_signals[foundKey].values);
        qDebug() << "evaluateExpression: found key" << foundKey << "with" << m_signals[foundKey].values.size() << "values";
    }
    
    int minSize = std::numeric_limits<int>::max();
    for (const QString &sig : signalNames) {
        if (!signalData.contains(sig)) {
            qDebug() << "evaluateExpression: signalData missing key" << sig;
            return false;
        }
        minSize = qMin(minSize, signalData[sig].first.size());
        minSize = qMin(minSize, signalData[sig].second.size());
    }
    
    if (minSize == 0 || minSize == std::numeric_limits<int>::max()) {
        qDebug() << "evaluateExpression: invalid minSize" << minSize;
        return false;
    }
    
    time = signalData[signalNames[0]].first.mid(0, minSize);
    values.resize(minSize);
    
    QMap<QString, QVector<double>> funcResults;
    QString expr = expression;
    
    QRegularExpression funcRe("(derivative|integral)\\(([VI])\\(([^)]+)\\)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator itFunc = funcRe.globalMatch(expr);
    QVector<QPair<QString, QString>> funcPlaceholders;
    int funcIdx = 1000;
    while (itFunc.hasNext()) {
        QRegularExpressionMatch match = itFunc.next();
        QString func = match.captured(1).toLower();
        QString type = match.captured(2);
        QString net = match.captured(3);
        QString funcKey = match.captured(0);
        
        if (funcResults.contains(funcKey)) continue;
        
        QStringList prefixes = {net, QString("%1(%2)").arg(type).arg(net)};
        QString sigKey;
        for (const QString &k : prefixes) {
            if (signalData.contains(k)) {
                sigKey = k;
                break;
            }
        }
        if (!sigKey.isEmpty()) {
            QVector<double> result;
            if (func == "derivative") {
                result = computeDerivative(signalData[sigKey].first, signalData[sigKey].second);
            } else if (func == "integral") {
                result = computeIntegral(signalData[sigKey].first, signalData[sigKey].second);
            }
            if (!result.isEmpty()) {
                funcResults[funcKey] = result;
                QString placeholder = QString("f%1").arg(funcIdx++);
                funcPlaceholders.append({funcKey, placeholder});
            }
        }
    }
    
    for (const auto &p : funcPlaceholders) {
        expr.replace(p.first, p.second);
    }
    
    for (int i = 0; i < signalNames.size(); ++i) {
        QStringList patterns = {
            QString("V\\(%1\\)").arg(QRegularExpression::escape(signalNames[i])),
            QString("I\\(%1\\)").arg(QRegularExpression::escape(signalNames[i])),
            QString("P\\(%1\\)").arg(QRegularExpression::escape(signalNames[i]))
        };
        for (const QString &pattern : patterns) {
            QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
            expr.replace(re, QString("s%1").arg(i));
        }
    }
    
    qDebug() << "evaluateExpression: transformed expr=" << expr;
    
    qDebug() << "evaluateExpression: minSize=" << minSize << "time.size=" << time.size();
    
    QVector<QVector<double>> signalVectors;
    signalVectors.reserve(signalNames.size());
    for (int idx = 0; idx < signalNames.size(); ++idx) {
        const QString &sig = signalNames[idx];
        if (!signalData.contains(sig)) {
            qDebug() << "evaluateExpression: ERROR - signalData missing during vector creation";
            return false;
        }
        signalVectors.append(signalData[sig].second.mid(0, minSize));
        if (signalVectors.last().size() != minSize) {
            qDebug() << "evaluateExpression: WARNING - vector size mismatch";
        }
        qDebug() << "evaluateExpression: signal" << idx << "(" << sig << ") size=" << signalVectors.last().size() 
                 << "first 3 values:" << (signalVectors.last().size() > 0 ? signalVectors.last()[0] : 0) 
                 << (signalVectors.last().size() > 1 ? signalVectors.last()[1] : 0)
                 << (signalVectors.last().size() > 2 ? signalVectors.last()[2] : 0);
    }
    
    for (int i = 0; i < minSize; ++i) {
        QString eval = expr;
        
        // Simple left-to-right evaluation for basic arithmetic
        QStringList tokens;
        QList<QChar> operators;
        
        // Tokenize the expression
        QString current = "";
        for (int pos = 0; pos < eval.length(); ++pos) {
            QChar c = eval[pos];
            if (c == '+' || c == '-' || c == '*' || c == '/') {
                if (!current.isEmpty()) {
                    tokens.append(current.trimmed());
                    current = "";
                }
                operators.append(c);
            } else {
                current += c;
            }
        }
        if (!current.isEmpty()) {
            tokens.append(current.trimmed());
        }
        
        // Evaluate tokens
        if (tokens.isEmpty()) {
            values[i] = 0.0;
            continue;
        }
        
        // First pass: handle * and /
        QList<double> numbers;
        QList<QChar> remainingOps;
        
        if (tokens.isEmpty()) {
            values[i] = 0.0;
            continue;
        }
        
        // Validate token/operator consistency
        if (operators.size() >= tokens.size()) {
            qDebug() << "evaluateExpression: ERROR - operators.size()" << operators.size() 
                     << ">= tokens.size()" << tokens.size() << "for eval=" << eval;
            values[i] = 0.0;
            continue;
        }
        
        auto resolveToken = [&](const QString &token) -> double {
            bool ok;
            double val = token.toDouble(&ok);
            if (ok) return val;
            
            QRegularExpression sigRe("s(\\d+)");
            QRegularExpressionMatch match = sigRe.match(token);
            if (match.hasMatch()) {
                int idx = match.captured(1).toInt();
                if (idx >= 0 && idx < signalVectors.size() && i < signalVectors[idx].size()) {
                    return signalVectors[idx][i];
                }
            }
            
            QRegularExpression funcRe("f(\\d+)");
            match = funcRe.match(token);
            if (match.hasMatch()) {
                int idx = match.captured(1).toInt() - 1000;
                if (idx >= 0 && idx < funcResults.size()) {
                    return funcResults.values()[idx][qMin(i, funcResults.values()[idx].size() - 1)];
                }
            }
            return 0.0;
        };
        
        numbers.append(resolveToken(tokens[0]));
        
        for (int k = 0; k < operators.size(); ++k) {
            QChar op = operators[k];
            double nextNum = (k + 1 < tokens.size()) ? resolveToken(tokens[k+1]) : 0.0;
            
            if (op == '*') {
                double prev = numbers.last();
                numbers.removeLast();
                numbers.append(prev * nextNum);
            } else if (op == '/') {
                if (qFuzzyIsNull(nextNum)) {
                    numbers.append(0.0);
                } else {
                    double prev = numbers.last();
                    numbers.removeLast();
                    numbers.append(prev / nextNum);
                }
            } else {
                numbers.append(nextNum);
                remainingOps.append(op);
            }
        }
        
        // Second pass: handle + and -
        double result = numbers.isEmpty() ? 0.0 : numbers[0];
        for (int k = 0; k < remainingOps.size(); ++k) {
            if (k + 1 < numbers.size()) {
                if (remainingOps[k] == '+') {
                    result += numbers[k+1];
                } else if (remainingOps[k] == '-') {
                    result -= numbers[k+1];
                }
            }
        }
        
        if (!std::isfinite(result)) {
            result = 0.0;
        }
        
        if (i < 3) {
            qDebug() << "evaluateExpression: i=" << i << "eval=" << eval << "result=" << result;
        }
        values[i] = result;
    }
    
    return true;
}
    
double WaveformViewer::evaluateSimpleMath(const QString &expr, bool &ok) {
    qDebug() << "evaluateSimpleMath called with:" << expr;
    ok = true;
    QString e = expr.trimmed();
    
    if (e.isEmpty()) {
        ok = false;
        qDebug() << "evaluateSimpleMath: empty expression";
        return 0;
    }
    
    // First, look for + or - (lowest precedence), leftmost for left-to-right associativity
    int pos = -1;
    QChar op;
    for (int i = 0; i < e.length(); ++i) {
        if (e[i] == '+' || e[i] == '-') {
            // Skip if it's the first character (could be a negative number)
            if (i == 0) continue;
            pos = i;
            op = e[i];
            break; // Take the first (leftmost) + or -
        }
    }
    
    if (pos != -1) {
        QString left = e.left(pos).trimmed();
        QString right = e.mid(pos + 1).trimmed();
        
        bool leftOk, rightOk;
        double leftVal = evaluateSimpleMath(left, leftOk);
        double rightVal = evaluateSimpleMath(right, rightOk);
        
        if (!leftOk || !rightOk) {
            ok = false;
            qDebug() << "evaluateSimpleMath: failed to evaluate left or right side of" << op;
            return 0;
        }
        
        if (op == '+') {
            double result = leftVal + rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " + " << rightVal << " = " << result;
            return result;
        } else { // '-'
            double result = leftVal - rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " - " << rightVal << " = " << result;
            return result;
        }
    }
    
    // No + or - found, look for * or / (higher precedence), leftmost for left-to-right associativity
    for (int i = 0; i < e.length(); ++i) {
        if (e[i] == '*' || e[i] == '/') {
            pos = i;
            op = e[i];
            break; // Take the first (leftmost) * or /
        }
    }
    
    if (pos != -1) {
        QString left = e.left(pos).trimmed();
        QString right = e.mid(pos + 1).trimmed();
        
        bool leftOk, rightOk;
        double leftVal = evaluateSimpleMath(left, leftOk);
        double rightVal = evaluateSimpleMath(right, rightOk);
        
        if (!leftOk || !rightOk) {
            ok = false;
            qDebug() << "evaluateSimpleMath: failed to evaluate left or right side of" << op;
            return 0;
        }
        
        if (op == '*') {
            double result = leftVal * rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " * " << rightVal << " = " << result;
            return result;
        } else { // '/'
            double denominator = rightVal;
            if (qFuzzyIsNull(denominator)) {
                ok = false;
                qDebug() << "evaluateSimpleMath: division by zero";
                return 0;
            }
            double result = leftVal / rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " / " << rightVal << " = " << result;
            return result;
        }
    }
    
    // No operators found, must be a number
    bool conversionOk;
    double val = e.toDouble(&conversionOk);
    if (!conversionOk) {
        ok = false;
        qDebug() << "evaluateSimpleMath: not a valid number:" << e;
        return 0;
    }
    qDebug() << "evaluateSimpleMath: parsed number" << val;
    return val;
}

void WaveformViewer::exportImage() {
    QString path = QFileDialog::getSaveFileName(this, "Export Chart Image", QString(),
        "PNG Image (*.png);;SVG Vector (*.svg);;PDF Document (*.pdf)");
    if (path.isEmpty()) return;

    if (path.endsWith(".svg", Qt::CaseInsensitive)) {
        QRectF plotRect = m_chartView->rect();
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(plotRect.size().toSize());
        generator.setViewBox(plotRect);
        generator.setTitle("VioSpice Waveform");
        QPainter painter(&generator);
        painter.setRenderHint(QPainter::Antialiasing);
        m_chartView->render(&painter);
        painter.end();
    } else if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        printer.setPageSize(QPageSize(m_chartView->size(), QPageSize::Point));
        QPainter painter(&printer);
        painter.setRenderHint(QPainter::Antialiasing);
        m_chartView->render(&painter);
        painter.end();
    } else {
        QPixmap pixmap = m_chartView->grab();
        if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
        pixmap.save(path, "PNG");
    }
}

WaveformViewer::EdgeTimes WaveformViewer::computeEdgeTimes(const QVector<double>& time, const QVector<double>& values) const {
    EdgeTimes result;
    if (time.size() < 3 || time.size() != values.size()) return result;

    double minVal = *std::min_element(values.begin(), values.end());
    double maxVal = *std::max_element(values.begin(), values.end());
    double range = maxVal - minVal;
    if (range < 1e-15) return result;

    double lo = minVal + range * 0.1;
    double hi = minVal + range * 0.9;

    QVector<double> riseTimes, fallTimes;

    auto interpTime = [&](int idx, double threshold) -> double {
        double y0 = values[idx], y1 = values[idx + 1];
        if (std::abs(y1 - y0) < 1e-15) return time[idx];
        double t = (threshold - y0) / (y1 - y0);
        return time[idx] + t * (time[idx + 1] - time[idx]);
    };

    for (int i = 0; i < values.size() - 1; ++i) {
        double y0 = values[i], y1 = values[i + 1];
        // Rising edge: crosses 10% going up
        if (y0 <= lo && y1 > lo) {
            double t10 = interpTime(i, lo);
            // Find next crossing of 90%
            for (int j = i + 1; j < values.size() - 1; ++j) {
                if (values[j] <= hi && values[j + 1] > hi) {
                    double t90 = interpTime(j, hi);
                    riseTimes.append(t90 - t10);
                    i = j;
                    break;
                }
                if (values[j + 1] < lo) break;
            }
        }
        // Falling edge: crosses 90% going down
        if (y0 >= hi && y1 < hi) {
            double t90 = interpTime(i, hi);
            for (int j = i + 1; j < values.size() - 1; ++j) {
                if (values[j] >= lo && values[j + 1] < lo) {
                    double t10 = interpTime(j, lo);
                    fallTimes.append(t10 - t90);
                    i = j;
                    break;
                }
                if (values[j + 1] > hi) break;
            }
        }
    }

    auto computeStats = [](const QVector<double>& v, double& mn, double& mx, double& avg) {
        if (v.isEmpty()) return;
        mn = *std::min_element(v.begin(), v.end());
        mx = *std::max_element(v.begin(), v.end());
        double sum = 0;
        for (double d : v) sum += d;
        avg = sum / v.size();
    };

    result.riseCount = riseTimes.size();
    result.fallCount = fallTimes.size();
    if (result.riseCount > 0) computeStats(riseTimes, result.riseMin, result.riseMax, result.riseAvg);
    if (result.fallCount > 0) computeStats(fallTimes, result.fallMin, result.fallMax, result.fallAvg);
    return result;
}

void WaveformViewer::loadCsv(const QString&) {}

void WaveformViewer::onLegendCtrlClicked(const QString &seriesName) {
    if (seriesName.isEmpty()) return;

    QString actualName = seriesName;
    if (!m_signals.contains(seriesName)) {
        for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
            if (it.key().compare(seriesName, Qt::CaseInsensitive) == 0) {
                actualName = it.key();
                break;
            }
        }
    }

    if (!m_signals.contains(actualName)) return;

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        showAnalysisForSeries(actualName);
        return;
    }

    auto &sig = m_signals[actualName];

    QMenu menu;

    // Color submenu
    QMenu *colorMenu = menu.addMenu("Color");
    colorMenu->addAction("Pick Color...");
    colorMenu->addSeparator();
    const QList<QPair<QString, QColor>> presetColors = {
        {"Green", QColor(0, 204, 0)}, {"Blue", QColor(0, 0, 255)},
        {"Red", QColor(255, 0, 0)}, {"Cyan", QColor(0, 255, 255)},
        {"Magenta", QColor(255, 0, 255)}, {"Yellow", QColor(255, 255, 0)},
        {"White", QColor(255, 255, 255)}, {"Orange", QColor(255, 165, 0)}
    };
    for (auto &pc : presetColors)
        colorMenu->addAction(pc.first);

    // Width submenu
    QMenu *widthMenu = menu.addMenu("Line Width");
    for (double w : {1.0, 1.5, 2.0, 3.0, 4.0}) {
        QAction *a = widthMenu->addAction(QString::number(w));
        a->setCheckable(true);
        if (qFuzzyCompare(sig.lineWidth, w)) a->setChecked(true);
    }

    // Style submenu
    QMenu *styleMenu = menu.addMenu("Line Style");
    struct StyleOpt { QString name; Qt::PenStyle style; };
    QList<StyleOpt> styles = {{"Solid", Qt::SolidLine}, {"Dash", Qt::DashLine},
                              {"Dot", Qt::DotLine}, {"DashDot", Qt::DashDotLine}};
    for (auto &s : styles) {
        QAction *a = styleMenu->addAction(s.name);
        a->setCheckable(true);
        if (sig.penStyle == s.style) a->setChecked(true);
    }

    menu.addSeparator();
    QAction *exprAct = menu.addAction("Expression...");

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    // Handle color
    if (chosen->text() == "Pick Color...") {
        QColor c = QColorDialog::getColor(sig.customColor.isValid() ? sig.customColor : Qt::white, this, "Signal Color");
        if (c.isValid()) { sig.customColor = c; updatePlot(); }
    } else {
        for (auto &pc : presetColors) {
            if (chosen->text() == pc.first) {
                sig.customColor = pc.second;
                updatePlot();
                return;
            }
        }
    }

    // Handle width
    bool isWidth = false;
    double w = chosen->text().toDouble(&isWidth);
    if (isWidth) { sig.lineWidth = w; updatePlot(); return; }

    // Handle style
    for (auto &s : styles) {
        if (chosen->text() == s.name) {
            sig.penStyle = s.style;
            updatePlot();
            return;
        }
    }

    // Expression
    if (chosen == exprAct) {
        QStringList signalNames;
        for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it)
            signalNames << it.key();
        QColor existingColor = sig.customColor;
        WaveformExpressionDialog dlg(actualName, signalNames, existingColor, actualName, this);
        if (dlg.exec() == QDialog::Accepted)
            onExpressionSubmitted(dlg.expression(), dlg.signalColor(), actualName);
    }
}

void WaveformViewer::showAnalysisForSeries(const QString &seriesName) {
    if (seriesName.isEmpty() || !m_signals.contains(seriesName)) return;
    const auto& data = m_signals[seriesName];
    if (data.time.size() < 2 || data.values.size() < 2) return;

    if (m_acMode) {
        double fStart = data.time.front();
        double fEnd = data.time.back();
        if (currentXRange(fStart, fEnd)) {
            if (fEnd < fStart) std::swap(fStart, fEnd);
        } else {
            fStart = data.time.front();
            fEnd = data.time.back();
        }

        // Build magnitude in dB
        QVector<double> mags;
        mags.reserve(data.values.size());
        for (double v : data.values) mags.push_back(toDb(v));

        // Reference at start frequency (nearest point)
        double refDb = mags.front();
        {
            // find nearest index to fStart
            int idx = 0;
            double best = std::abs(data.time[0] - fStart);
            for (int i = 1; i < data.time.size(); ++i) {
                double d = std::abs(data.time[i] - fStart);
                if (d < best) { best = d; idx = i; }
            }
            refDb = mags[idx];
        }

        const double target = refDb - 3.0;
        QString bwStr = "---";
        for (int i = 0; i < data.time.size(); ++i) {
            if (data.time[i] < fStart || data.time[i] > fEnd) continue;
            if (mags[i] <= target) {
                bwStr = SiFormatter::format(data.time[i], "Hz");
                break;
            }
        }

        if (!m_analysisDialog) m_analysisDialog = new AnalysisDialog(this);
        m_analysisDialog->setAcValues(seriesName,
                                      SiFormatter::format(fStart, "Hz"),
                                      SiFormatter::format(fEnd, "Hz"),
                                      QString("%1 @ %2").arg(formatDb(refDb), SiFormatter::format(fStart, "Hz")),
                                      bwStr,
                                      bwStr);
        m_analysisDialog->show();
        m_analysisDialog->raise();
        m_analysisDialog->activateWindow();
        return;
    }

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

    // Compute rise/fall times (only for non-AC, non-power signals with enough data)
    if (!m_acMode && data.type != SignalType::POWER && data.time.size() >= 3) {
        auto edges = computeEdgeTimes(data.time, data.values);
        auto fmtTime = [](double t) -> QString {
            return SiFormatter::format(t, "s");
        };
        m_analysisDialog->setEdgeTimes(
            edges.riseCount,
            edges.riseCount > 0 ? fmtTime(edges.riseMin) : "---",
            edges.riseCount > 0 ? fmtTime(edges.riseAvg) : "---",
            edges.riseCount > 0 ? fmtTime(edges.riseMax) : "---",
            edges.fallCount,
            edges.fallCount > 0 ? fmtTime(edges.fallMin) : "---",
            edges.fallCount > 0 ? fmtTime(edges.fallAvg) : "---",
            edges.fallCount > 0 ? fmtTime(edges.fallMax) : "---"
        );

        // Signal properties: vpp, period, frequency, duty cycle, pulse width, overshoot, undershoot
        double minVal = *std::min_element(data.values.begin(), data.values.end());
        double maxVal = *std::max_element(data.values.begin(), data.values.end());
        double vpp = maxVal - minVal;
        double mid = (maxVal + minVal) / 2.0;

        // Find rising edge crossings at midpoint to measure period
        QVector<double> periods, highTimes;
        double lastRiseTime = -1;
        bool aboveMid = false;
        auto interp = [&](int idx, double thresh) -> double {
            double y0 = data.values[idx], y1 = data.values[idx + 1];
            if (std::abs(y1 - y0) < 1e-15) return data.time[idx];
            double t = (thresh - y0) / (y1 - y0);
            return data.time[idx] + t * (data.time[idx + 1] - data.time[idx]);
        };

        for (int i = 0; i < data.values.size() - 1; ++i) {
            if (!aboveMid && data.values[i] <= mid && data.values[i + 1] > mid) {
                double riseT = interp(i, mid);
                if (lastRiseTime > 0)
                    periods.append(riseT - lastRiseTime);
                lastRiseTime = riseT;
                aboveMid = true;
            } else if (aboveMid && data.values[i] >= mid && data.values[i + 1] < mid) {
                double fallT = interp(i, mid);
                if (lastRiseTime > 0)
                    highTimes.append(fallT - lastRiseTime);
                aboveMid = false;
            }
        }

        QString vppStr = SiFormatter::format(vpp, unit);
        QString periodStr = "---", freqStr = "---", dutyStr = "---", pulseStr = "---";
        if (!periods.isEmpty()) {
            double avgPeriod = std::accumulate(periods.begin(), periods.end(), 0.0) / periods.size();
            periodStr = SiFormatter::format(avgPeriod, "s");
            freqStr = SiFormatter::format(1.0 / avgPeriod, "Hz");
            if (!highTimes.isEmpty()) {
                double avgHigh = std::accumulate(highTimes.begin(), highTimes.end(), 0.0) / highTimes.size();
                double duty = avgHigh / avgPeriod * 100.0;
                dutyStr = QString::number(duty, 'f', 1) + " %";
                pulseStr = SiFormatter::format(avgHigh, "s");
            }
        }

        // Overshoot / undershoot: compare max/min to steady-state levels
        // Steady-state = median of values in upper/lower half
        QVector<double> sorted = data.values;
        std::sort(sorted.begin(), sorted.end());
        double median = sorted[sorted.size() / 2];
        double highLevel = 0, lowLevel = 0;
        int highCnt = 0, lowCnt = 0;
        for (double v : data.values) {
            if (v >= median) { highLevel += v; highCnt++; }
            else { lowLevel += v; lowCnt++; }
        }
        if (highCnt > 0) highLevel /= highCnt;
        if (lowCnt > 0) lowLevel /= lowCnt;

        QString overshootStr = "---", undershootStr = "---";
        double swing = highLevel - lowLevel;
        if (swing > 1e-15) {
            double os = (maxVal - highLevel) / swing * 100.0;
            double us = (lowLevel - minVal) / swing * 100.0;
            if (os > 0.01) overshootStr = QString::number(os, 'f', 2) + " %";
            else overshootStr = "0.00 %";
            if (us > 0.01) undershootStr = QString::number(us, 'f', 2) + " %";
            else undershootStr = "0.00 %";
        }

        m_analysisDialog->setSignalProperties(vppStr, periodStr, freqStr, dutyStr, pulseStr, overshootStr, undershootStr);
    }

    m_analysisDialog->show();
    m_analysisDialog->raise();
    m_analysisDialog->activateWindow();
}

QList<WaveformViewer::SignalExport> WaveformViewer::exportSignals() const {
    QList<SignalExport> result;
    for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
        SignalExport exp;
        exp.name = it->name;
        exp.time = it->time;
        exp.values = it->values;
        exp.phase = it->phase;
        exp.hasPhase = it->hasPhase;
        exp.customColor = it->customColor;
        exp.lineWidth = it->lineWidth;
        exp.penStyle = it->penStyle;
        exp.checked = false;
        for (int i = 0; i < m_nodeList->count(); ++i) {
            if (m_nodeList->item(i)->text() == it->name) {
                exp.checked = (m_nodeList->item(i)->checkState() == Qt::Checked);
                break;
            }
        }
        result.append(exp);
    }
    return result;
}

void WaveformViewer::importSignals(const QList<SignalExport>& signalExports) {
    for (const auto& sig : signalExports) {
        if (sig.hasPhase) {
            addSignal(sig.name, sig.time, sig.values, sig.phase);
        } else {
            addSignal(sig.name, sig.time, sig.values);
        }
        if (m_signals.contains(sig.name)) {
            auto &sd = m_signals[sig.name];
            sd.customColor = sig.customColor;
            sd.lineWidth = sig.lineWidth;
            sd.penStyle = sig.penStyle;
        }
        setSignalChecked(sig.name, sig.checked);
    }
    updatePlot(true);
}

bool WaveformViewer::getSignalData(const QString& name, QVector<double>& time, QVector<double>& values) {
    if (!m_signals.contains(name)) {
        return false;
    }
    const auto& data = m_signals[name];
    time = data.time;
    values = data.values;
    return true;
}

QStringList WaveformViewer::getSignalNames() const {
    QStringList names;
    for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
        names.append(it.key());
    }
    return names;
}
