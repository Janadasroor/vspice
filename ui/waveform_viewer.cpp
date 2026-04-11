// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Janada Sroor

#include "waveform_viewer.h"
#include "waveform_expression_dialog.h"
#include "../core/theme_manager.h"
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
#include <QApplication>
#include <QToolButton>
#include <QMenu>
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

QList<QPointF> buildVisibleMinMaxSeries(const QVector<double>& time,
                                        const QVector<double>& values,
                                        double minX,
                                        double maxX,
                                        int maxColumns,
                                        bool acMode) {
    const int n = std::min(time.size(), values.size());
    if (n <= 0) return {};

    if (maxColumns < 8) maxColumns = 8;
    if (!(minX < maxX)) {
        minX = time.first();
        maxX = time.last();
    }

    auto lower = std::lower_bound(time.begin(), time.end(), minX);
    auto upper = std::upper_bound(time.begin(), time.end(), maxX);

    int begin = std::max(0, static_cast<int>(std::distance(time.begin(), lower)) - 1);
    int end = std::min(n, static_cast<int>(std::distance(time.begin(), upper)) + 1);
    if (begin >= end) {
        begin = 0;
        end = n;
    }

    const int visibleCount = end - begin;
    QList<QPointF> points;
    if (visibleCount <= 0) return points;

    auto pointAt = [&](int idx) {
        const double y = acMode ? toDb(values[idx]) : values[idx];
        return QPointF(time[idx], y);
    };

    if (visibleCount <= maxColumns) {
        points.reserve(visibleCount);
        for (int i = begin; i < end; ++i) {
            points.append(pointAt(i));
        }
        return points;
    }

    const int bucketCount = std::max(1, maxColumns / 2);
    const double bucketSpan = static_cast<double>(visibleCount) / static_cast<double>(bucketCount);

    points.reserve(bucketCount * 2 + 2);
    points.append(pointAt(begin));

    for (int bucket = 0; bucket < bucketCount; ++bucket) {
        const int bucketBegin = begin + static_cast<int>(std::floor(bucket * bucketSpan));
        int bucketEnd = begin + static_cast<int>(std::floor((bucket + 1) * bucketSpan));
        if (bucket == bucketCount - 1 || bucketEnd > end) bucketEnd = end;
        if (bucketBegin >= bucketEnd) continue;

        int minIdx = bucketBegin;
        int maxIdx = bucketBegin;
        double minVal = acMode ? toDb(values[bucketBegin]) : values[bucketBegin];
        double maxVal = minVal;

        for (int i = bucketBegin + 1; i < bucketEnd; ++i) {
            const double y = acMode ? toDb(values[i]) : values[i];
            if (y < minVal) {
                minVal = y;
                minIdx = i;
            }
            if (y > maxVal) {
                maxVal = y;
                maxIdx = i;
            }
        }

        if (minIdx == maxIdx) {
            if (points.isEmpty() || points.back().x() != time[minIdx] || points.back().y() != minVal) {
                points.append(QPointF(time[minIdx], minVal));
            }
        } else if (minIdx < maxIdx) {
            points.append(QPointF(time[minIdx], minVal));
            points.append(QPointF(time[maxIdx], maxVal));
        } else {
            points.append(QPointF(time[maxIdx], maxVal));
            points.append(QPointF(time[minIdx], minVal));
        }
    }

    const QPointF lastPoint = pointAt(end - 1);
    if (points.isEmpty() || points.back() != lastPoint) {
        points.append(lastPoint);
    }
    return points;
}
} // namespace


VioChartView::VioChartView(QChart *chart, QWidget *parent) : QChartView(chart, parent) {
    setMouseTracking(true);
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
}

void VioChartView::setCursorPositions(double c1x, double c1y, double c2x, double c2y, QLineSeries *series) {
    m_c1x = c1x; m_c1y = c1y; m_c2x = c2x; m_c2y = c2y;
    m_activeSeries = series;
    viewport()->update();
}

double VioChartView::snapToSeries(double x, QLineSeries *series) {
    if (!series || series->points().isEmpty()) return x;
    auto points = series->points();
    
    // Binary search for nearest point
    auto it = std::lower_bound(points.begin(), points.end(), QPointF(x, 0), [](const QPointF& p1, const QPointF& p2){
        return p1.x() < p2.x();
    });
    
    if (it == points.end()) return points.last().x();
    if (it == points.begin()) return points.first().x();
    
    auto prev = std::prev(it);
    if (std::abs(x - prev->x()) < std::abs(x - it->x())) return prev->x();
    return it->x();
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
        if (m_activeSeries) {
            x = snapToSeries(x, m_activeSeries);
        }
        
        if (m_movingCursor == 1) m_c1x = x;
        else m_c2x = x;
        Q_EMIT cursorMoved(m_movingCursor, x);
        Q_EMIT cursorsMoved();
        viewport()->update();
    }
    if (m_crosshairEnabled) {
        viewport()->update();
    }

    if (m_zoomRectActive) {
        m_rubberBand->setGeometry(QRect(m_zoomRectStart, event->pos()).normalized());
    }

    QPointF value = chart()->mapToValue(event->pos());
    Q_EMIT mouseMoved(value);
    QChartView::mouseMoveEvent(event);
}

void VioChartView::mousePressEvent(QMouseEvent *event) {
    Q_EMIT clicked();
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
                        Q_EMIT legendCtrlClicked(text);
                        event->accept();
                        return;
                    }
                }
            }
        }
    }
    if (event->button() == Qt::RightButton) {
        Q_EMIT contextMenuRequested(mapToGlobal(event->pos()));
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
            Q_EMIT zoomRectCompleted(QRectF(QPointF(xMin, yMin), QPointF(xMax, yMax)));
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
    if (event->button() == Qt::RightButton) {
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

    // Draw Delta Overlay
    if (m_c1x != -1 && m_c2x != -1) {
        QPointF p1 = chart()->mapToPosition(QPointF(m_c1x, m_c1y), m_activeSeries);
        QPointF p2 = chart()->mapToPosition(QPointF(m_c2x, m_c2y), m_activeSeries);
        
        if (plot.contains(p1.x(), plot.center().y()) && plot.contains(p2.x(), plot.center().y())) {
            painter->setPen(QPen(Qt::white, 1, Qt::SolidLine));
            painter->drawLine(p1, p2);
            
            double dx = std::abs(m_c2x - m_c1x);
            double dy = m_c2y - m_c1y;
            QString dxStr = SiFormatter::format(dx, "s");
            QString dyStr = SiFormatter::format(dy, "");
            QString freqStr = dx > 1e-15 ? SiFormatter::format(1.0/dx, "Hz") : "---";
            
            QString label = QString("dX: %1 | dY: %2 | 1/dX: %3").arg(dxStr, dyStr, freqStr);
            painter->setPen(Qt::white);
            painter->drawText(QRectF(plot.left(), plot.bottom() - 25, plot.width(), 20), Qt::AlignCenter, label);
        }
    }
}

WaveformViewer::WaveformViewer(QWidget *parent) : QWidget(parent), 
    m_measureDialog(nullptr), m_cursorsEnabled(false), m_cursor1X(0), m_cursor2X(0) {
    setupUi();
    setupStyle();
    applyPlotQualityToViews();
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
        if (m_cursorsEnabled && !m_panes.isEmpty()) {
            auto *ax = m_panes.first()->axisX;
            if (ax) {
                double step = (ax->max() - ax->min()) / 100.0;
                if (event->key() == Qt::Key_Left) step = -step;
                m_cursor1X += step;
                updateCursors();
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
    toolbar->setObjectName("waveformToolbar");
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setMinimumHeight(24);

    toolbar->addAction("Z+", QKeySequence(), this, &WaveformViewer::zoomIn);
    toolbar->addAction("Z-", QKeySequence(), this, &WaveformViewer::zoomOut);
    toolbar->addAction("Fit", QKeySequence(), this, &WaveformViewer::zoomFit);
    toolbar->addSeparator();
    toolbar->addAction("Diff", QKeySequence(), this, &WaveformViewer::onSubtractRequested);
    toolbar->addAction("FFT", QKeySequence(), this, &WaveformViewer::onFftRequested);
    toolbar->addAction("Save", QKeySequence(), this, &WaveformViewer::exportImage);
    toolbar->addSeparator();
    auto *cursorAct = toolbar->addAction("Cursors", QKeySequence(), this, &WaveformViewer::toggleCursors);
    cursorAct->setCheckable(true);
    
    auto *crosshairAct = toolbar->addAction("Crosshair", QKeySequence(), this, &WaveformViewer::toggleCrosshair);
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
    
    mainArea->addWidget(m_nodeList);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setHandleWidth(1);
    mainArea->addWidget(m_splitter, 1);

    layout->addLayout(mainArea);
 
    m_legendContainer = new QWidget(this);
    m_legendLayout = new QHBoxLayout(m_legendContainer);
    m_legendLayout->setContentsMargins(10, 2, 10, 2);
    m_legendLayout->setSpacing(15);

    m_xAxisTitleLabel = new QLabel("Time (s)", this);
    m_legendLayout->addWidget(m_xAxisTitleLabel);
    m_legendLayout->addStretch();

    layout->addWidget(m_legendContainer);
 
    auto *footer = new QHBoxLayout();
    m_coordLabel = new QLabel("Ready");
    m_statsLabel = new QLabel("");
    footer->addWidget(m_coordLabel);
    footer->addStretch();
    footer->addWidget(m_statsLabel);
    footer->setContentsMargins(10, 2, 10, 2);
    layout->addLayout(footer);
}

void WaveformViewer::setupStyle() {
    PCBTheme* theme = ThemeManager::theme();
    bool isDark = !theme || theme->type() != PCBTheme::Light;
    const QString panelBg = theme ? theme->panelBackground().name() : "#18181b";
    const QString windowBg = theme ? theme->windowBackground().name() : "#111111";
    const QString border = theme ? theme->panelBorder().name() : "#3f3f46";
    const QString text = theme ? theme->textColor().name() : "#f4f4f5";
    const QString secondary = theme ? theme->textSecondary().name() : "#a1a1aa";
    const QString accent = theme ? theme->accentColor().name() : "#2563eb";
    const QString hover = isDark ? "#333333" : "#e5e7eb";

    if (auto* toolbar = findChild<QToolBar*>("waveformToolbar")) {
        toolbar->setStyleSheet(QString(
            "QToolBar { spacing: 2px; padding: 0px 2px; background: %1; border-bottom: 1px solid %2; }"
            "QToolButton { padding: 2px 6px; margin: 0px; font-size: 11px; min-height: 20px; color: %3; border-radius: 4px; }"
            "QToolButton:hover { background: %4; }"
            "QToolButton:checked { background: %5; color: white; }"
        ).arg(panelBg, border, text, hover, accent));
    }

    m_splitter->setStyleSheet(QString("QSplitter::handle { background: %1; }").arg(border));
    m_legendContainer->setStyleSheet(QString("background: %1; border-top: 1px solid %2;").arg(panelBg, border));
    m_xAxisTitleLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 10px; text-transform: uppercase;").arg(secondary));
    m_coordLabel->setStyleSheet(QString("font-family: monospace; color: %1;").arg(accent));
    m_statsLabel->setStyleSheet(QString("font-family: monospace; color: %1;").arg(isDark ? "#f59e0b" : "#b45309"));
    
    for (auto* p : m_panes) {
        if (isDark) {
            p->chart->setTheme(QChart::ChartThemeDark);
            p->chart->setBackgroundBrush(QBrush(QColor(24, 24, 27))); 
            p->chart->setPlotAreaBackgroundBrush(QBrush(QColor(15, 15, 18)));
            p->chart->setTitleBrush(QBrush(QColor("#f4f4f5")));
        } else {
            p->chart->setTheme(QChart::ChartThemeLight);
            p->chart->setBackgroundBrush(QBrush(Qt::white));
            p->chart->setPlotAreaBackgroundBrush(QBrush(Qt::white));
            p->chart->setTitleBrush(QBrush(QColor("#111827")));
        }
        
        QColor labelColor = isDark ? QColor("#a1a1aa") : QColor("#4b5563");
        QColor titleColor = isDark ? QColor("#e4e4e7") : QColor("#1f2937");
        QColor gridColor = isDark ? QColor(63, 63, 70, 80) : QColor(209, 213, 219, 120);

        if (p->axisX) {
            p->axisX->setLabelsColor(labelColor);
            p->axisX->setTitleBrush(QBrush(titleColor));
            p->axisX->setGridLineColor(gridColor);
        }
        if (p->axisY) {
            p->axisY->setLabelsColor(labelColor);
            p->axisY->setTitleBrush(QBrush(titleColor));
            p->axisY->setGridLineColor(gridColor);
        }
    }
    
    if (isDark) {
        m_nodeList->setStyleSheet(QString(
            "QListWidget { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; }"
            "QListWidget::item { padding: 6px 8px; border-bottom: 1px solid %4; }"
            "QListWidget::item:selected { background: %5; color: white; }"
        ).arg(panelBg, text, border, windowBg, accent));
    } else {
        m_nodeList->setStyleSheet(QString(
            "QListWidget { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; }"
            "QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #f3f4f6; }"
            "QListWidget::item:selected { background: #dbeafe; color: #1e40af; }"
        ).arg(windowBg, text, border));
    }
}

void WaveformViewer::clear() {
    m_signals.clear();
    m_pointCounters.clear();
    m_nodeList->blockSignals(true);
    m_nodeList->clear();
    m_nodeList->blockSignals(false);
    
    for (auto* p : m_panes) {
        if (p->chart) p->chart->removeAllSeries();
        if (p->view) p->view->setCursorPositions(m_cursor1X, 0, m_cursor2X, 0, nullptr);
    }
    m_activeSeriesName.clear();
}

void WaveformViewer::clearPane(int index) {
    if (index == -1 && m_focusedPane) {
        index = m_panes.indexOf(m_focusedPane);
    }
    if (index < 0 || index >= m_panes.size()) return;

    // Remove all signals belonging to this pane
    QList<QString> toRemove;
    for (auto it = m_signals.begin(); it != m_signals.end(); ++it) {
        if (it.value().paneIndex == index) {
            toRemove.append(it.key());
        }
    }

    m_nodeList->blockSignals(true);
    for (const QString& name : toRemove) {
        m_signals.remove(name);
        m_pointCounters.remove(name);
        // Find and remove from m_nodeList
        for (int i = 0; i < m_nodeList->count(); ++i) {
            if (m_nodeList->item(i)->text() == name) {
                delete m_nodeList->takeItem(i);
                break;
            }
        }
    }
    m_nodeList->blockSignals(false);

    auto* p = m_panes[index];
    if (p->chart) {
        p->chart->removeAllSeries();
    }
    if (p->view) {
        p->view->setCursorPositions(m_cursor1X, 0, m_cursor2X, 0, nullptr);
    }
    
    if (m_activeSeriesName.isEmpty() || toRemove.contains(m_activeSeriesName)) {
        m_activeSeriesName.clear();
    }
}

void WaveformViewer::setPlotQuality(WaveformViewer::PlotQuality quality) {
    if (m_plotQuality == quality) return;
    m_plotQuality = quality;
    applyPlotQualityToViews();
    updatePlot(false);
}

void WaveformViewer::applyPlotQualityToViews() {
    const bool antialias = shouldUseAntialiasing();
    for (auto* pane : m_panes) {
        if (pane && pane->view) {
            pane->view->setRenderHint(QPainter::Antialiasing, antialias);
        }
    }
}

bool WaveformViewer::shouldUseOpenGL() const {
    return m_plotQuality != PlotQuality::HighQuality;
}

bool WaveformViewer::shouldUseAntialiasing() const {
    return m_plotQuality == PlotQuality::HighQuality;
}

int WaveformViewer::visiblePointBudget(int viewportWidth) const {
    const int width = std::max(64, viewportWidth);
    switch (m_plotQuality) {
    case PlotQuality::HighQuality:
        return width * 3;
    case PlotQuality::Fast:
        return width;
    case PlotQuality::Balanced:
    default:
        return width * 2;
    }
}

bool WaveformViewer::currentXRange(double& minX, double& maxX) const {
    if (m_panes.isEmpty()) return false;
    auto* axis = m_panes.first()->axisX;
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
    if (lowerName.startsWith("v(") || lowerName.startsWith("v_") || lowerName == "v") data.type = SignalType::VOLTAGE;
    else if (lowerName.contains("#branch") || lowerName.startsWith("i(")) data.type = SignalType::CURRENT;
    else if (lowerName.startsWith("p(")) data.type = SignalType::POWER;
    else data.type = SignalType::OTHER;
    
    if (m_focusedPane) {
        data.paneIndex = m_panes.indexOf(m_focusedPane);
    } else if (m_panes.size() == 1) {
        data.paneIndex = 0;
    } else {
        data.paneIndex = -1; // Default: automatic
    }
    
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
        item->setCheckState(Qt::Unchecked);
        updateNodeItemStyle(item);
        m_nodeList->blockSignals(false);
    }
}

void WaveformViewer::appendPoint(const QString& name, double x, double y) {
    if (!m_signals.contains(name)) {
        addSignal(name, {x}, {y});
    }
    
    auto& sig = m_signals[name];
    sig.time.append(x);
    sig.values.append(y);
    
    if (m_blockUpdates) return;

    ChartPane* pane = getPaneForType(sig.type);
    QLineSeries* lineSeries = nullptr;
    for (auto* series : pane->chart->series()) {
        if (series->name() == name) {
            lineSeries = qobject_cast<QLineSeries*>(series);
            break;
        }
    }

    if (lineSeries) {
        lineSeries->append(x, m_acMode ? toDb(y) : y);
        
        // Throttled auto-scale
        int& count = m_pointCounters[name];
        if ((++count % 50) == 0) {
            if (x > pane->axisX->max()) pane->axisX->setMax(x * 1.1);
            if (y > pane->axisY->max()) pane->axisY->setMax(y > 0 ? y * 1.2 : y * 0.8);
            if (y < pane->axisY->min()) pane->axisY->setMin(y < 0 ? y * 1.2 : y * 0.8);
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

    // Find the correct pane and series
    ChartPane* pane = getPaneForType(m_signals[name].type);
    QLineSeries* lineSeries = nullptr;
    for (auto* s : pane->chart->series()) {
        if (s->name() == name) {
            lineSeries = qobject_cast<QLineSeries*>(s);
            break;
        }
    }

    if (lineSeries) {
        const int batchSize = static_cast<int>(times.size());
        const int maxBucketsInChart = 5000;
        
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

        if (lineSeries->count() > maxBucketsInChart * 1.5) {
            QMetaObject::invokeMethod(this, "updatePlot", Qt::QueuedConnection, Q_ARG(bool, false));
        } else {
            double tLast = times.back();
            if (tLast > pane->axisX->max()) pane->axisX->setMax(tLast * 1.05);
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

    auto* item = new QListWidgetItem(name, m_nodeList);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    updateNodeItemStyle(item);
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
        bool isChecked = false;
        for (int i = 0; i < m_nodeList->count(); ++i) {
            if (m_nodeList->item(i)->text() == sig.name && m_nodeList->item(i)->checkState() == Qt::Checked) {
                isChecked = true;
                break;
            }
        }
        if (!isChecked) continue;
        
        if (sig.type == SignalType::VOLTAGE) hasV = true;
        else if (sig.type == SignalType::CURRENT) hasI = true;
        else if (sig.type == SignalType::POWER) hasP = true;
    }
    
    // Mapping the mouse position back to the focused pane's series would be ideal.
    // For now, just show the last reported value.y()
    coordStr += QString(" | Y: %1").arg(SiFormatter::format(value.y(), ""));
    
    m_coordLabel->setText(coordStr);
}

void WaveformViewer::toggleCursors() {
    const bool hasPlottedSeries = std::any_of(m_panes.begin(), m_panes.end(), [](const ChartPane* pane) {
        return pane && pane->chart && !pane->chart->series().isEmpty();
    });

    if (!m_cursorsEnabled && !hasPlottedSeries) {
        if (m_measureDialog) m_measureDialog->hide();
        return;
    }

    m_cursorsEnabled = !m_cursorsEnabled;
    for (auto* p : m_panes) p->view->setCursorsEnabled(m_cursorsEnabled);
    if (m_cursorsEnabled) {
        if (!m_measureDialog) m_measureDialog = new MeasurementDialog(this);
        m_measureDialog->setAcMode(m_acMode);
        m_measureDialog->show();

        if (m_cursor1X == 0 && m_cursor2X == 0) {
            m_cursor1X = m_panes.first()->axisX->min() + (m_panes.first()->axisX->max() - m_panes.first()->axisX->min()) * 0.25;
            m_cursor2X = m_panes.first()->axisX->min() + (m_panes.first()->axisX->max() - m_panes.first()->axisX->min()) * 0.75;
        }
        updateCursors();
    } else {
        if (m_measureDialog) m_measureDialog->hide();
    }
}

void WaveformViewer::toggleCrosshair() {
    if (m_panes.isEmpty()) return;
    bool enabled = !m_panes.first()->view->isCrosshairEnabled();
    for (auto* p : m_panes) p->view->setCrosshairEnabled(enabled);
}

void WaveformViewer::updateCursors() {
    if (!m_cursorsEnabled || m_panes.isEmpty()) return;

    const bool hasPlottedSeries = std::any_of(m_panes.begin(), m_panes.end(), [](const ChartPane* pane) {
        return pane && pane->chart && !pane->chart->series().isEmpty();
    });

    if (!hasPlottedSeries) {
        if (m_measureDialog) m_measureDialog->hide();
        return;
    }

    m_cursor1X = m_panes.first()->view->cursor1X();
    m_cursor2X = m_panes.first()->view->cursor2X();

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

    for (auto* p : m_panes) {
        QLineSeries* primarySeries = nullptr;
        // Try to find a series in this pane to measure
        for (auto* s : p->chart->series()) {
            if (s->name() == m_activeSeriesName) {
                primarySeries = qobject_cast<QLineSeries*>(s);
                break;
            }
        }
        if (!primarySeries && !p->chart->series().isEmpty()) {
            primarySeries = qobject_cast<QLineSeries*>(p->chart->series().first());
        }

        if (primarySeries) {
            double v1 = getY(primarySeries, m_cursor1X);
            double v2 = getY(primarySeries, m_cursor2X);
            p->view->setCursorPositions(m_cursor1X, v1, m_cursor2X, v2, primarySeries);
            
            // If this is the active series, update the measure dialog
            if (m_activeSeriesName.isEmpty() || primarySeries->name() == m_activeSeriesName) {
                 if (m_measureDialog && m_measureDialog->isVisible()) {
                    double dt = m_cursor2X - m_cursor1X;
                    double dv = v2 - v1;
                    QString freqStr = (std::abs(dt) > 1e-12) ? SiFormatter::format(1.0/std::abs(dt), "Hz") : "---";
                    QString slopeStr = (std::abs(dt) > 1e-12) ? SiFormatter::format(dv/dt, "V/s") : "---";
                    
                    m_measureDialog->updateValues(primarySeries->name(), 
                        SiFormatter::format(m_cursor1X, "s"), SiFormatter::format(v1, ""),
                        SiFormatter::format(m_cursor2X, "s"), SiFormatter::format(v2, ""),
                        SiFormatter::format(dt, "s"), SiFormatter::format(dv, ""),
                        freqStr, slopeStr
                    );
                }
            }
        } else {
            p->view->setCursorPositions(m_cursor1X, 0, m_cursor2X, 0, nullptr);
        }
    }
}

void WaveformViewer::updatePlot(bool autoScale) {
    if (m_blockUpdates) return;
    
    for (auto* pane : m_panes) {
        pane->chart->removeAllSeries();
        pane->view->setCursorPositions(m_cursor1X, 0, m_cursor2X, 0, nullptr);
    }
    
    if (m_signals.isEmpty()) return;

    struct PaneStats {
        double minY = 1e30, maxY = -1e30;
        bool hasData = false;
    };
    QMap<ChartPane*, PaneStats> paneStats;
    QMap<ChartPane*, QSet<SignalType>> paneSignalTypes;
    double globalMinX = 1e30, globalMaxX = -1e30;
    bool hasAnyData = false;

    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->text();
            if (m_signals.contains(name)) {
                auto& sig = m_signals[name];
                if (sig.time.isEmpty()) continue;
                
                ChartPane* pane = nullptr;
                if (sig.paneIndex >= 0) {
                    ensurePaneCount(sig.paneIndex + 1);
                    if (sig.paneIndex < m_panes.size()) {
                        pane = m_panes[sig.paneIndex];
                    }
                }
                if (!pane) {
                    pane = getPaneForType(sig.type);
                }
                auto& stats = paneStats[pane];
                paneSignalTypes[pane].insert(sig.type);
                
                double sMinX = *std::min_element(sig.time.begin(), sig.time.end());
                double sMaxX = *std::max_element(sig.time.begin(), sig.time.end());
                globalMinX = std::min(globalMinX, sMinX);
                globalMaxX = std::max(globalMaxX, sMaxX);
                
                for (double v : sig.values) {
                    double tv = m_acMode ? toDb(v) : v;
                    stats.minY = std::min(stats.minY, tv);
                    stats.maxY = std::max(stats.maxY, tv);
                }
                stats.hasData = true;
                hasAnyData = true;
            }
        }
    }

    if (!hasAnyData) {
        globalMinX = 0; globalMaxX = 1;
    }

    for (auto* pane : m_panes) {
        if (paneSignalTypes.contains(pane)) {
            const QSet<SignalType>& types = paneSignalTypes[pane];
            if (types.size() == 1) {
                pane->type = *types.constBegin();
            } else if (types.isEmpty()) {
                pane->type = SignalType::OTHER;
            } else {
                pane->type = SignalType::OTHER;
            }
        }

        if (autoScale) {
            pane->axisX->setRange(globalMinX, globalMaxX);
        }
        pane->axisX->setTitleVisible(false); // Using in-line legend instead
        
        if (autoScale && paneStats.contains(pane)) {
            auto& stats = paneStats[pane];
            double pad = (stats.maxY - stats.minY) * 0.1;
            if (pad == 0) pad = 0.5;
            pane->axisY->setRange(stats.minY - pad, stats.maxY + pad);
        }
        pane->axisY->setTitleText(m_acMode ? "Magnitude (dB)" : 
            (pane->type == SignalType::VOLTAGE ? "Voltage (V)" : 
             pane->type == SignalType::CURRENT ? "Current (A)" : "Value"));
    }

    updateLegend();

    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item->checkState() == Qt::Checked) {
            QString name = item->text();
            if (m_signals.contains(name)) {
                const auto& data = m_signals[name];
                ChartPane* pane = nullptr;
                if (data.paneIndex >= 0 && data.paneIndex < m_panes.size()) {
                    pane = m_panes[data.paneIndex];
                } else {
                    pane = getPaneForType(data.type);
                }
                
                auto* series = new QLineSeries();
                series->setUseOpenGL(shouldUseOpenGL());
                series->setName(name);
                series->setPen(QPen(data.customColor.isValid() ? data.customColor : item->foreground().color(), data.lineWidth, data.penStyle));

                const double minX = pane->axisX->min();
                const double maxX = pane->axisX->max();
                const int viewportWidth = pane->view ? std::max(64, pane->view->viewport()->width()) : 1200;
                const QList<QPointF> visiblePoints = buildVisibleMinMaxSeries(
                    data.time, data.values, minX, maxX, visiblePointBudget(viewportWidth), m_acMode);
                series->append(visiblePoints);

                pane->chart->addSeries(series);
                series->attachAxis(pane->axisX);
                series->attachAxis(pane->axisY);
            }
        }
    }
}

void WaveformViewer::zoomIn() { for (auto* p : m_panes) p->chart->zoomIn(); }
void WaveformViewer::zoomOut() { for (auto* p : m_panes) p->chart->zoomOut(); }
void WaveformViewer::exportImage() {
    if (m_panes.isEmpty()) return;
    QPixmap pixmap = m_splitter->grab();
    QString fileName = QFileDialog::getSaveFileName(this, "Export Waveform", "", "Images (*.png *.jpg)");
    if (!fileName.isEmpty()) {
        pixmap.save(fileName);
    }
}

void WaveformViewer::resetZoom() {
    for (auto* p : m_panes) p->chart->zoomReset();
    scheduleVisibleRangeRefresh();
}

void WaveformViewer::pushZoomState() {
    if (m_panes.isEmpty()) return;
    ZoomState s;
    s.xMin = m_panes.first()->axisX->min();
    s.xMax = m_panes.first()->axisX->max();
    for (auto* p : m_panes) {
        s.yRanges.append({p->axisY->min(), p->axisY->max()});
    }

    if (!m_zoomUndo.isEmpty()) {
        const auto &top = m_zoomUndo.top();
        if (qFuzzyCompare(top.xMin, s.xMin) && qFuzzyCompare(top.xMax, s.xMax) && top.yRanges == s.yRanges)
            return;
    }
    m_zoomUndo.push(s);
    m_zoomRedo.clear();
}

void WaveformViewer::applyZoomState(const ZoomState &s) {
    if (m_panes.isEmpty()) return;
    
    m_blockUpdates = true;
    for (int i = 0; i < m_panes.size(); ++i) {
        m_panes[i]->axisX->setRange(s.xMin, s.xMax);
        if (i < s.yRanges.size()) {
            m_panes[i]->axisY->setRange(s.yRanges[i].first, s.yRanges[i].second);
        }
    }
    m_blockUpdates = false;
    scheduleVisibleRangeRefresh();
}

void WaveformViewer::onZoomRectCompleted(const QRectF &valueRect) {
    VioChartView* view = qobject_cast<VioChartView*>(sender());
    ChartPane* sourcePane = nullptr;
    for (auto* p : m_panes) {
        if (p->view == view) {
            sourcePane = p;
            break;
        }
    }

    pushZoomState();
    if (m_panes.isEmpty()) return;
    
    m_blockUpdates = true;
    for (auto* p : m_panes) {
        p->axisX->setRange(valueRect.left(), valueRect.right());
    }
    
    if (sourcePane) {
        sourcePane->axisY->setRange(valueRect.top(), valueRect.bottom());
    }
    m_blockUpdates = false;
    scheduleVisibleRangeRefresh();
}

void WaveformViewer::undoZoom() {
    if (m_zoomUndo.isEmpty()) return;
    pushZoomState();
    m_zoomRedo.push(m_zoomUndo.pop());
    applyZoomState(m_zoomUndo.pop());
}

void WaveformViewer::redoZoom() {
    if (m_zoomRedo.isEmpty()) return;
    pushZoomState();
    applyZoomState(m_zoomRedo.pop());
}

void WaveformViewer::zoomFit() {
    updatePlot(true);
}

void WaveformViewer::zoomFitYOnly() {
    for (auto* p : m_panes) {
        double minY = 1e30, maxY = -1e30;
        bool found = false;
        for (int i = 0; i < m_nodeList->count(); ++i) {
            QListWidgetItem* item = m_nodeList->item(i);
            if (item->checkState() == Qt::Checked) {
                QString name = item->text();
                if (m_signals.contains(name)) {
                    const auto& data = m_signals[name];
                    if (getPaneForType(data.type) != p) continue;
                    for (double v : data.values) {
                        const double y = m_acMode ? toDb(v) : v;
                        minY = std::min(minY, y);
                        maxY = std::max(maxY, y);
                    }
                    found = true;
                }
            }
        }
        if (!found) continue;
        double dy = (std::abs(maxY - minY) < 1e-15) ? 1.0 : (maxY - minY) * 0.1;
        p->axisY->setRange(minY - dy, maxY + dy);
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

WaveformViewer::ChartPane* WaveformViewer::createPane(WaveformViewer::SignalType type) {
    auto* pane = new ChartPane();
    pane->type = type;
    pane->chart = new QChart();
    pane->chart->setTheme(QChart::ChartThemeDark);
    pane->chart->setBackgroundBrush(QBrush(QColor(20, 20, 20)));
    pane->chart->setPlotAreaBackgroundBrush(QBrush(QColor(15, 15, 15)));
    pane->chart->setMargins(QMargins(2, 2, 2, 2));
    pane->chart->layout()->setContentsMargins(0, 0, 0, 0);
    pane->chart->legend()->hide();
    
    pane->view = new VioChartView(pane->chart);
    pane->view->setContentsMargins(0, 0, 0, 0);
    pane->view->setRenderHint(QPainter::Antialiasing, shouldUseAntialiasing());
    pane->view->setRubberBand(QChartView::RectangleRubberBand);
    pane->view->setInteractive(true);
    pane->view->setCursorsEnabled(m_cursorsEnabled);
    
    pane->axisX = new QValueAxis();
    pane->axisX->setLabelsBrush(QBrush(Qt::white));
    pane->axisX->setTitleBrush(QBrush(Qt::white));
    pane->axisX->setGridLinePen(QPen(QColor("#333"), 1, Qt::DotLine));
    
    pane->axisY = new QValueAxis();
    pane->axisY->setLabelsBrush(QBrush(Qt::white));
    pane->axisY->setTitleBrush(QBrush(Qt::white));
    pane->axisY->setGridLinePen(QPen(QColor("#333"), 1, Qt::DotLine));
    
    pane->chart->addAxis(pane->axisX, Qt::AlignBottom);
    pane->chart->addAxis(pane->axisY, Qt::AlignLeft);
    
    m_panes.append(pane);
    m_splitter->addWidget(pane->view);
    
    connect(pane->axisX, &QValueAxis::rangeChanged, this, [this, pane](){
        syncAxesX(pane->axisX);
    });
    
    connect(pane->view, &VioChartView::mouseMoved, this, &WaveformViewer::onMouseMoved);
    connect(pane->view, &VioChartView::cursorsMoved, this, &WaveformViewer::updateCursors);
    connect(pane->view, &VioChartView::contextMenuRequested, this, &WaveformViewer::onContextMenuRequested);
    connect(pane->view, &VioChartView::clicked, this, &WaveformViewer::onPaneClicked);
    connect(pane->view, &VioChartView::zoomRectCompleted, this, &WaveformViewer::onZoomRectCompleted);
    
    return pane;
}

void WaveformViewer::removePane(int index) {
    if (index < 0 || index >= m_panes.size() || m_panes.size() <= 1) return;
    
    auto* p = m_panes.takeAt(index);
    if (p == m_focusedPane) m_focusedPane = nullptr;

    if (p->view) {
        p->view->hide();
        p->view->deleteLater();
    }
    // Chart and Axes are children of the QChartView/QChart and will be deleted.
    delete p;
    updatePlot(true);
}

void WaveformViewer::syncAxesX(QValueAxis* source) {
    if (m_blockUpdates || !source) return;
    m_blockUpdates = true;
    double min = source->min();
    double max = source->max();
    for (auto* p : m_panes) {
        if (p->axisX != source) {
            p->axisX->setRange(min, max);
        }
    }
    m_blockUpdates = false;
    scheduleVisibleRangeRefresh();
}

void WaveformViewer::scheduleVisibleRangeRefresh() {
    if (m_blockUpdates || m_rebuildQueued) return;
    m_rebuildQueued = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_rebuildQueued = false;
        if (!m_blockUpdates) {
            updatePlot(false);
        }
    }, Qt::QueuedConnection);
}

void WaveformViewer::onPaneClicked() {
    auto* view = qobject_cast<VioChartView*>(sender());
    if (!view) return;

    for (auto* pane : m_panes) {
        if (pane->view == view) {
            m_focusedPane = pane;
            // Use border highlight with zero padding/margin to save space
            pane->view->setStyleSheet("VioChartView { border: 2px solid #55aaff; background: transparent; padding: 0px; margin: 0px; }");
        } else {
            pane->view->setStyleSheet("VioChartView { border: none; background: transparent; padding: 0px; margin: 0px; }");
        }
    }
}

WaveformViewer::ChartPane* WaveformViewer::getPaneForType(WaveformViewer::SignalType type) {
    if (m_panes.size() == 1) {
        return m_panes.first();
    }
    for (auto* p : m_panes) {
        if (p->type == type) return p;
    }
    // If not found, create one
    return createPane(type);
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
    crosshairAct->setChecked(!m_panes.isEmpty() && m_panes.first()->view->isCrosshairEnabled());

    menu.addSeparator();
    QAction* addPaneAct = menu.addAction("Add Plan");
    QAction* remPaneAct = menu.addAction("Remove Current Plan");
    if (m_panes.size() <= 1) remPaneAct->setEnabled(false);

    menu.addSeparator();

    QAction* copyAct = menu.addAction("Copy Value at Cursor");
    QString copyText;
    if (!buildValueAtCursor(copyText)) {
        copyAct->setEnabled(false);
    }

    QAction* exportAct = menu.addAction("Export CSV (Current Signals)");

    menu.addSeparator();
    QAction* exportImgAct = menu.addAction("Export Image...");

    ChartPane* targetPane = nullptr;
    for (auto* p : m_panes) {
        if (p->view->rect().contains(p->view->mapFromGlobal(globalPos))) {
            targetPane = p;
            break;
        }
    }

    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == fitAct) zoomFit();
    else if (chosen == fitYAct) { zoomFitYOnly(); }
    else if (chosen == resetAct) resetZoom();
    else if (chosen == cursorsAct) toggleCursors();
    else if (chosen == crosshairAct) toggleCrosshair();
    else if (chosen == addPaneAct) {
        createPane(SignalType::OTHER);
        updatePlot(true);
    } else if (chosen == remPaneAct && targetPane) {
        int idx = m_panes.indexOf(targetPane);
        if (idx >= 0) removePane(idx);
    } else if (chosen == copyAct) {
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
    PCBTheme* theme = ThemeManager::theme();
    bool isDark = theme && theme->type() == PCBTheme::Dark;

    if (item->checkState() == Qt::Checked) {
        // High-contrast palettes for light/dark modes
        static const QStringList darkPalette = { "#60a5fa", "#34d399", "#fbbf24", "#f87171", "#a78bfa", "#f472b6", "#2dd4bf", "#fb923c" };
        static const QStringList lightPalette = { "#1d4ed8", "#047857", "#b45309", "#b91c1c", "#6d28d9", "#be185d", "#0f766e", "#c2410c" };
        
        const QStringList& palette = isDark ? darkPalette : lightPalette;
        int row = m_nodeList->row(item);
        item->setForeground(QColor(palette[row % palette.size()]));
        item->setFont(QFont("Inter", 9, QFont::Bold));
    } else {
        item->setForeground(isDark ? QColor("#71717a") : QColor("#9ca3af"));
        item->setFont(QFont("Inter", 9, QFont::Normal));
    }
}

void WaveformViewer::updateLegend() {
    if (!m_legendLayout || !m_nodeList) return;
    
    m_xAxisTitleLabel->setText(m_acMode ? "FREQUENCY (HZ)" : "TIME (S)");

    // Clear everything except the title and the stretch
    while (m_legendLayout->count() > 2) {
        QLayoutItem* item = m_legendLayout->takeAt(1);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    
    int insertIdx = 1;
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* listItem = m_nodeList->item(i);
        if (listItem->checkState() == Qt::Checked) {
            QString name = listItem->text();
            QColor color = listItem->foreground().color();
            
            auto* tag = new QToolButton(m_legendContainer);
            tag->setText(name);
            tag->setCursor(Qt::PointingHandCursor);
            tag->setAutoRaise(true);
            tag->setStyleSheet(QString(
                "QToolButton {"
                "  color: %1;"
                "  font-weight: bold;"
                "  border: 1px solid %1;"
                "  padding: 1px 8px;"
                "  border-radius: 4px;"
                "  font-size: 10px;"
                "  background: rgba(%2, %3, %4, 15);"
                "}"
                "QToolButton:hover {"
                "  background: rgba(%2, %3, %4, 40);"
                "}"
            ).arg(color.name()).arg(color.red()).arg(color.green()).arg(color.blue()));

            connect(tag, &QToolButton::clicked, this, [this, name]() {
                if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
                    showAnalysisForSeries(name);
                } else {
                    // Normal click focus in the list
                    for (int i = 0; i < m_nodeList->count(); ++i) {
                        if (m_nodeList->item(i)->text() == name) {
                            m_nodeList->setCurrentRow(i);
                            break;
                        }
                    }
                }
            });

            m_legendLayout->insertWidget(insertIdx++, tag);
        }
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
    if (ThemeManager::theme()) {
        dlg->setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }
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

// Duplicate exportImage removed

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
    QString currentName;
    if (m_nodeList && m_nodeList->currentItem()) {
        currentName = m_nodeList->currentItem()->text();
    }
    QMap<int, int> paneIndexMap;
    int compactPaneIndex = 0;
    for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
        const int paneIndex = it->paneIndex;
        if (paneIndex >= 0 && !paneIndexMap.contains(paneIndex)) {
            paneIndexMap.insert(paneIndex, compactPaneIndex++);
        }
    }
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
        exp.paneIndex = paneIndexMap.value(it->paneIndex, -1);
        exp.checked = false;
        exp.selected = currentName.compare(it->name, Qt::CaseInsensitive) == 0;
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
    int requiredPaneCount = m_panes.size();
    QString selectedSignalName;
    for (const auto& sig : signalExports) {
        if (sig.paneIndex >= 0) {
            requiredPaneCount = std::max(requiredPaneCount, sig.paneIndex + 1);
        }
        if (selectedSignalName.isEmpty() && sig.selected) {
            selectedSignalName = sig.name;
        }
    }

    ensurePaneCount(requiredPaneCount);

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
            sd.paneIndex = sig.paneIndex;
        }
        setSignalChecked(sig.name, sig.checked);
    }
    if (!selectedSignalName.isEmpty()) {
        setCurrentSignal(selectedSignalName);
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
    return m_signals.keys();
}

int WaveformViewer::focusedPaneIndex() const {
    if (!m_focusedPane) return -1;
    return m_panes.indexOf(m_focusedPane);
}

QString WaveformViewer::currentSignalName() const {
    if (!m_nodeList || !m_nodeList->currentItem()) return QString();
    return m_nodeList->currentItem()->text();
}

void WaveformViewer::ensurePaneCount(int count) {
    if (count < 0) return;
    while (m_panes.size() < count) {
        createPane(SignalType::OTHER);
    }
}

void WaveformViewer::setFocusedPaneIndex(int index) {
    if (index < 0 || index >= m_panes.size()) return;

    m_focusedPane = m_panes[index];
    for (int i = 0; i < m_panes.size(); ++i) {
        auto* pane = m_panes[i];
        if (!pane || !pane->view) continue;
        if (i == index) {
            pane->view->setStyleSheet("VioChartView { border: 2px solid #55aaff; background: transparent; padding: 0px; margin: 0px; }");
        } else {
            pane->view->setStyleSheet("VioChartView { border: none; background: transparent; padding: 0px; margin: 0px; }");
        }
    }
}

void WaveformViewer::setSignalPaneIndex(const QString& name, int paneIndex) {
    if (!m_signals.contains(name)) return;
    if (paneIndex < 0) {
        m_signals[name].paneIndex = -1;
        return;
    }

    ensurePaneCount(paneIndex + 1);
    m_signals[name].paneIndex = paneIndex;
}

void WaveformViewer::setCurrentSignal(const QString& name) {
    if (!m_nodeList) return;
    for (int i = 0; i < m_nodeList->count(); ++i) {
        QListWidgetItem* item = m_nodeList->item(i);
        if (item && item->text().compare(name, Qt::CaseInsensitive) == 0) {
            m_nodeList->setCurrentRow(i);
            m_activeSeriesName = item->text();
            if (m_signals.contains(item->text())) {
                setFocusedPaneIndex(m_signals[item->text()].paneIndex);
            }
            return;
        }
    }
}

QStringList WaveformViewer::getSignalsInPane(int index) const {
    QStringList result;
    if (index < 0 || index >= m_panes.size()) return result;
    for (auto it = m_signals.constBegin(); it != m_signals.constEnd(); ++it) {
        if (it.value().paneIndex == index) {
            result.append(it.key());
        }
    }
    return result;
}
