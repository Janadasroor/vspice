#include "virtual_instruments.h"
#include "../analysis/fft_analyzer.h"
#include <QPainterPath>
#include <QPainter>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <cmath>
#include <algorithm>

namespace {
QString formatValueSI(double val, const QString& unit) {
    double absVal = std::abs(val);
    if (absVal < 1e-18) return "0" + unit;
    
    static const struct { double mult; const char* sym; } suffixes[] = {
        {1e12, "T"}, {1e9, "G"}, {1e6, "M"}, {1e3, "k"},
        {1.0, ""},
        {1e-3, "m"}, {1e-6, "u"}, {1e-9, "n"}, {1e-12, "p"}, {1e-15, "f"}
    };
    
    for (const auto& s : suffixes) {
        if (absVal >= s.mult * 0.999) {
            return QString::number(val / s.mult, 'f', 2).remove(QRegularExpression("\\.?0+$")) + s.sym + unit;
        }
    }
    return QString::number(val, 'g', 4) + unit;
}
}

QString OscilloscopeWidget::formatValue(double val, const QString& unit) {
    return formatValueSI(val, unit);
}

OscilloscopeWidget::OscilloscopeWidget(QWidget* parent) : QWidget(parent) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMinimumHeight(300);
    setMouseTracking(true);
    m_timePerDiv = 1.0;   // 1ms
    m_voltsPerDiv = 1.0;  // 1V
    m_triggerLevel = 0.0;
}

void OscilloscopeWidget::requestRepaint() {
    if (m_batchMode) {
        m_repaintPending = true;
        return;
    }
    update();
}

void OscilloscopeWidget::beginBatchUpdate() {
    m_batchMode = true;
    m_repaintPending = false;
}

void OscilloscopeWidget::endBatchUpdate() {
    m_batchMode = false;
    if (m_repaintPending) {
        m_repaintPending = false;
        update();
    }
}

void OscilloscopeWidget::setFftEnabled(bool enabled) {
    m_fftEnabled = enabled;
    if (enabled) {
        // Trigger initial FFT computation
        for (auto it = m_data.begin(); it != m_data.end(); ++it) {
            updateMeasurements(it.key());
        }
    }
    requestRepaint();
}

void OscilloscopeWidget::setActiveChannel(const QString& name) {
    if (!name.isEmpty() && name != "None" && !m_data.contains(name)) {
        return;
    }
    m_activeChannel = name;
    if (!m_activeChannel.isEmpty() && m_voltsPerDivs.contains(m_activeChannel)) {
        m_voltsPerDiv = m_voltsPerDivs[m_activeChannel];
    }
    requestRepaint();
}

void OscilloscopeWidget::setVoltsPerDiv(double v) {
    m_voltsPerDiv = v;
    if (!m_activeChannel.isEmpty()) {
        m_voltsPerDivs[m_activeChannel] = v;
    }
    emit voltsPerDivChanged(v);
    requestRepaint();
}

void OscilloscopeWidget::zoomInX() {
    double factor = 0.8;
    m_timePerDiv = std::clamp(m_timePerDiv * factor, 0.0001, 10000.0);
    emit timePerDivChanged(m_timePerDiv);
    requestRepaint();
}

void OscilloscopeWidget::zoomOutX() {
    double factor = 1.25;
    m_timePerDiv = std::clamp(m_timePerDiv * factor, 0.0001, 10000.0);
    emit timePerDivChanged(m_timePerDiv);
    requestRepaint();
}

void OscilloscopeWidget::zoomInY() {
    if (m_activeChannel.isEmpty()) return;
    double factor = 0.8;
    double vDiv = m_voltsPerDivs.value(m_activeChannel, m_voltsPerDiv);
    vDiv = std::clamp(vDiv * factor, 0.001, 100.0);
    m_voltsPerDivs[m_activeChannel] = vDiv;
    m_voltsPerDiv = vDiv;
    emit voltsPerDivChanged(vDiv);
    requestRepaint();
}

void OscilloscopeWidget::zoomOutY() {
    if (m_activeChannel.isEmpty()) return;
    double factor = 1.25;
    double vDiv = m_voltsPerDivs.value(m_activeChannel, m_voltsPerDiv);
    vDiv = std::clamp(vDiv * factor, 0.001, 100.0);
    m_voltsPerDivs[m_activeChannel] = vDiv;
    m_voltsPerDiv = vDiv;
    emit voltsPerDivChanged(vDiv);
    requestRepaint();
}

void OscilloscopeWidget::autoScale() {
    if (m_data.isEmpty()) return;
    // Simple implementation: find min/max of active channel or all
    QString ch = m_activeChannel.isEmpty() ? m_data.keys().first() : m_activeChannel;
    const auto& pts = m_data[ch];
    if (pts.isEmpty()) return;

    double minV = 1e18, maxV = -1e18;
    for (const auto& p : pts) {
        minV = std::min(minV, p.y());
        maxV = std::max(maxV, p.y());
    }

    double range = std::max(0.1, maxV - minV);
    m_voltsPerDivs[ch] = range / 6.0; // Fit in ~6 divisions
    m_verticalOffsets[ch] = -(minV + maxV) / 2.0;
    
    if (ch == m_activeChannel) m_voltsPerDiv = m_voltsPerDivs[ch];
    emit voltsPerDivChanged(m_voltsPerDiv);
    requestRepaint();
}

void OscilloscopeWidget::addData(const QString& channel, double x, double y) {
    m_data[channel].append(QPointF(x, y));
    if (m_data[channel].size() > 10000) m_data[channel].removeFirst();
    
    if (!m_batchMode) {
        updateMeasurements(channel);
        requestRepaint();
    }
}

void OscilloscopeWidget::setChannelData(const QString& channel, const QVector<QPointF>& points) {
    m_data[channel] = points;
    updateMeasurements(channel);
    requestRepaint();
}

void OscilloscopeWidget::clear() {
    m_data.clear();
    m_fftData.clear();
    m_measurements.clear();
    requestRepaint();
}

void OscilloscopeWidget::updateMeasurements(const QString& channel) {
    const auto& pts = m_data[channel];
    if (pts.isEmpty()) return;

    Measurements& m = m_measurements[channel];
    double minV = 1e18, maxV = -1e18, sumSq = 0;
    for (const auto& p : pts) {
        minV = std::min(minV, p.y());
        maxV = std::max(maxV, p.y());
        sumSq += p.y() * p.y();
    }
    m.vpp = maxV - minV;
    m.vrms = std::sqrt(sumSq / pts.size());

    // Simple frequency estimation (zero crossing)
    if (pts.size() > 10) {
        double avg = (minV + maxV) / 2.0;
        int crossings = 0;
        for (int i = 1; i < pts.size(); ++i) {
            if ((pts[i-1].y() < avg && pts[i].y() >= avg)) crossings++;
        }
        double dur = pts.back().x() - pts.front().x();
        if (dur > 0 && crossings > 0) m.freq = crossings / dur;
    }

    // Update FFT if enabled
    if (m_fftEnabled) {
        std::vector<double> t, v;
        t.reserve(pts.size()); v.reserve(pts.size());
        for (const auto& p : pts) { t.push_back(p.x()); v.push_back(p.y()); }
        
        auto res = Flux::Analysis::FftAnalyzer::compute(t, v);
        QVector<QPointF> fftPts;
        fftPts.reserve(res.frequencies.size());
        for (size_t i = 0; i < res.frequencies.size(); ++i) {
            fftPts.append(QPointF(res.frequencies[i], res.magnitudes[i]));
        }
        m_fftData[channel].points = fftPts;
    }
}

void OscilloscopeWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1) Background
    p.fillRect(rect(), QColor(10, 15, 12));

    QRectF displayRect = rect().adjusted(40, 20, -20, -40);
    qreal w = displayRect.width();
    qreal h = displayRect.height();

    // 2) Grid
    p.setPen(QPen(QColor(40, 50, 45), 1, Qt::DotLine));
    for (int i = 0; i <= 10; ++i) { // Vertical lines (Time)
        qreal x = displayRect.left() + i * (w / 10.0);
        p.drawLine(x, displayRect.top(), x, displayRect.bottom());
    }
    for (int i = 0; i <= 8; ++i) { // Horizontal lines (Volts)
        qreal y = displayRect.top() + i * (h / 8.0);
        p.drawLine(displayRect.left(), y, displayRect.right(), y);
    }

    // 3) Axes Labels
    p.setPen(QColor(150, 160, 155));
    QFont font = p.font(); font.setPointSize(8); p.setFont(font);
    
    if (m_fftEnabled) {
        p.drawText(displayRect.bottomLeft() + QPointF(0, 15), "Frequency (Hz)");
        p.drawText(displayRect.topLeft() + QPointF(-35, 0), "dBV");
    } else {
        p.drawText(displayRect.bottomLeft() + QPointF(0, 15), QString("Time: %1/div").arg(formatValueSI(m_timePerDiv / 1000.0, "s")));
        if (!m_activeChannel.isEmpty()) {
            double vDiv = m_voltsPerDivs.value(m_activeChannel, m_voltsPerDiv);
            p.drawText(displayRect.topLeft() + QPointF(-35, 0), QString("%1/div").arg(formatValueSI(vDiv, "V")));
        }
    }

    // 4) Traces
    const auto& sourceData = m_fftEnabled ? m_fftData : QMap<QString, FftTrace>();
    
    int colorIdx = 0;
    static const QColor colors[] = { Qt::yellow, Qt::cyan, Qt::magenta, QColor(0, 255, 150), Qt::white };

    auto renderTrace = [&](const QString& name, const QVector<QPointF>& points, QColor color) {
        if (points.isEmpty()) return;
        
        QPainterPath path;
        bool first = true;
        
        double vDiv = m_voltsPerDivs.value(name, m_voltsPerDiv);
        double vOff = m_verticalOffsets.value(name, 0.0);

        for (const auto& pt : points) {
            qreal px, py;
            if (m_fftEnabled) {
                // Log scale for frequency? For now linear
                px = displayRect.left() + (pt.x() / (m_timePerDiv * 100.0)) * w; // Arbitrary scaling for FFT view
                py = displayRect.center().y() - (pt.y() / 20.0) * (h / 8.0); // -20dB per div
            } else {
                px = displayRect.left() + ((pt.x() * 1000.0 - m_horizontalOffset) / (m_timePerDiv * 10.0)) * w;
                py = displayRect.center().y() - ((pt.y() + vOff) / vDiv) * (h / 8.0);
            }

            if (px < displayRect.left() - 50) { first = true; continue; }
            if (px > displayRect.right() + 50) break;

            if (first) { path.moveTo(px, py); first = false; }
            else path.lineTo(px, py);
        }

        // Trace Glow
        QColor glow = color; glow.setAlpha(40);
        p.setPen(QPen(glow, 4));
        p.drawPath(path);
        
        p.setPen(QPen(color, name == m_activeChannel ? 2 : 1.2));
        p.drawPath(path);
    };

    if (m_fftEnabled) {
        for (auto it = m_fftData.begin(); it != m_fftData.end(); ++it) {
            renderTrace(it.key(), it.value().points, colors[colorIdx++ % 5]);
        }
    } else {
        for (auto it = m_data.begin(); it != m_data.end(); ++it) {
            renderTrace(it.key(), it.value(), colors[colorIdx++ % 5]);
        }
    }

    // 5) Cursors
    if (!m_fftEnabled) {
        p.setRenderHint(QPainter::Antialiasing, false);
        
        auto drawXCursor = [&](double norm, const QString& lbl, QColor col) {
            qreal x = displayRect.left() + norm * w;
            p.setPen(QPen(col, 1, Qt::DashLine));
            p.drawLine(x, displayRect.top(), x, displayRect.bottom());
            p.setPen(col);
            p.drawText(x + 2, displayRect.top() + 12, lbl);
        };

        drawXCursor(m_cursorA, "A", QColor(200, 200, 0));
        drawXCursor(m_cursorB, "B", QColor(0, 200, 200));

        // Cursor Readouts
        double tA = (m_cursorA * 10.0 * m_timePerDiv + m_horizontalOffset) / 1000.0;
        double tB = (m_cursorB * 10.0 * m_timePerDiv + m_horizontalOffset) / 1000.0;
        double dT = std::abs(tB - tA);
        
        p.setPen(Qt::white);
        QString readout = QString("dT: %1  |  1/dT: %2")
            .arg(formatValueSI(dT, "s"))
            .arg(dT > 1e-12 ? formatValueSI(1.0/dT, "Hz") : "---");
        p.drawText(displayRect.left() + 5, displayRect.bottom() + 25, readout);
    }

    // 6) Tracking Cursor (LTSpice Style)
    if (m_showTracking && !m_fftEnabled) {
        double vDiv = m_voltsPerDivs.value(m_activeChannel, m_voltsPerDiv);
        double vOff = m_verticalOffsets.value(m_activeChannel, 0.0);
        
        qreal px = displayRect.left() + ((m_trackingPoint.x() * 1000.0 - m_horizontalOffset) / (m_timePerDiv * 10.0)) * w;
        qreal py = displayRect.center().y() - ((m_trackingPoint.y() + vOff) / vDiv) * (h / 8.0);
        
        if (displayRect.contains(px, py)) {
            p.setPen(Qt::white);
            p.setBrush(Qt::transparent);
            p.drawEllipse(QPointF(px, py), 4, 4);
            
            QString tag = QString("%1, %2").arg(formatValueSI(m_trackingPoint.x(), "s"), formatValueSI(m_trackingPoint.y(), "V"));
            p.drawText(px + 8, py - 8, tag);
        }
    }

    // 7) Box Zoom
    if (m_zoomRectActive) {
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawRect(m_zoomRect);
    }
}

void OscilloscopeWidget::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
    QRectF displayRect = rect().adjusted(40, 20, -20, -40);
    double xNorm = (double)(event->pos().x() - displayRect.left()) / displayRect.width();
    double yNorm = 1.0 - ((double)(event->pos().y() - displayRect.top()) / displayRect.height());
    
    if      (std::abs(xNorm - m_cursorA) < 0.03) m_draggingCursor = 1;
    else if (std::abs(xNorm - m_cursorB) < 0.03) m_draggingCursor = 2;
    else if (std::abs(yNorm - m_cursorV1) < 0.03) m_draggingCursor = 3;
    else if (std::abs(yNorm - m_cursorV2) < 0.03) m_draggingCursor = 4;
    else if (event->button() == Qt::LeftButton) {
        m_zoomRectActive = true;
        m_zoomRect = QRect(event->pos(), QSize(0, 0));
        m_draggingCursor = 0;
    } else {
        m_draggingCursor = 0;
    }
}

void OscilloscopeWidget::mouseMoveEvent(QMouseEvent* event) {
    QRectF displayRect = rect().adjusted(40, 20, -20, -40);

    if (event->buttons() & Qt::MiddleButton) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();
        qreal stepX = displayRect.width() / 10.0;
        qreal stepY = displayRect.height() / 8.0;
        double dxMs = (delta.x() * m_timePerDiv) / stepX;
        m_horizontalOffset -= dxMs;
        emit horizontalOffsetChanged(m_horizontalOffset);
        if (!m_activeChannel.isEmpty()) {
            double vDiv = m_voltsPerDivs.value(m_activeChannel, m_voltsPerDiv);
            double dyVolts = (delta.y() * vDiv) / stepY;
            m_verticalOffsets[m_activeChannel] += dyVolts;
        }
        requestRepaint();
        return;
    }

    if (m_zoomRectActive) {
        m_zoomRect.setBottomRight(event->pos());
        requestRepaint();
        return;
    }

    double xNorm = (double)(event->pos().x() - displayRect.left()) / displayRect.width();
    double yNorm = 1.0 - ((double)(event->pos().y() - displayRect.top()) / displayRect.height());

    if (m_draggingCursor != 0) {
        if      (m_draggingCursor == 1) m_cursorA = std::clamp(xNorm, 0.0, 1.0);
        else if (m_draggingCursor == 2) m_cursorB = std::clamp(xNorm, 0.0, 1.0);
        else if (m_draggingCursor == 3) m_cursorV1 = std::clamp(yNorm, 0.0, 1.0);
        else if (m_draggingCursor == 4) m_cursorV2 = std::clamp(yNorm, 0.0, 1.0);
        requestRepaint();
        return;
    }

    // Tracking
    m_showTracking = false;
    if (!m_activeChannel.isEmpty() && m_data.contains(m_activeChannel)) {
        const auto& points = m_data[m_activeChannel];
        if (!points.isEmpty()) {
            double targetTime = (( (event->pos().x() - displayRect.left()) * m_timePerDiv / (displayRect.width()/10.0)) + m_horizontalOffset) / 1000.0;
            auto it = std::lower_bound(points.begin(), points.end(), QPointF(targetTime, 0), 
                [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
            if (it != points.end()) { m_trackingPoint = *it; m_showTracking = true; }
        }
    }
    requestRepaint();
}

void OscilloscopeWidget::mouseReleaseEvent(QMouseEvent*) {
    if (m_zoomRectActive) {
        m_zoomRectActive = false;
        // Zoom logic... (keep existing)
    }
    m_draggingCursor = 0;
    requestRepaint();
}

void OscilloscopeWidget::wheelEvent(QWheelEvent* event) {
    double angle = event->angleDelta().y();
    double factor = (angle > 0) ? 0.8 : 1.25;
    if (event->modifiers() & Qt::ControlModifier) zoomInY();
    else zoomInX();
}

void OscilloscopeWidget::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* fftAct = menu.addAction("FFT Spectrum Mode");
    fftAct->setCheckable(true);
    fftAct->setChecked(m_fftEnabled);
    connect(fftAct, &QAction::toggled, this, &OscilloscopeWidget::setFftEnabled);
    
    menu.addSeparator();
    QAction* autoScaleAct = menu.addAction("Auto Scale");
    connect(autoScaleAct, &QAction::triggered, this, &OscilloscopeWidget::autoScale);
    
    menu.exec(event->globalPos());
}

void OscilloscopeWidget::setVerticalOffset(const QString& channel, double volts) {
    m_verticalOffsets[channel] = volts;
    requestRepaint();
}

// ─── Logic Analyzer Implementation ──────────────────────────────────────────
LogicAnalyzerWidget::LogicAnalyzerWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(300);
    setMouseTracking(true);
}

void LogicAnalyzerWidget::requestRepaint() { update(); }
void LogicAnalyzerWidget::beginBatchUpdate() { m_batchMode = true; }
void LogicAnalyzerWidget::endBatchUpdate() { m_batchMode = false; update(); }
void LogicAnalyzerWidget::clear() { m_channels.clear(); m_decodedData.clear(); update(); }

void LogicAnalyzerWidget::addDigitalData(const QString& channel, double time, bool level) {
    m_channels[channel].append({time, level});
    if (m_channels[channel].size() > 5000) m_channels[channel].removeFirst();
    update();
}

void LogicAnalyzerWidget::setChannelData(const QString& channel, const QVector<QPointF>& points) {
    QVector<DigitalSample> trace;
    for (const auto& p : points) trace.append({p.x(), p.y() > m_threshold});
    m_channels[channel] = trace;
    update();
}

void LogicAnalyzerWidget::addDecoder(const DecoderConfig& config) {
    m_decoders.append(config);
    update();
}

void LogicAnalyzerWidget::clearDecoders() {
    m_decoders.clear();
    m_decodedData.clear();
    update();
}

void LogicAnalyzerWidget::zoomInX() { m_timePerDiv *= 0.8; update(); }
void LogicAnalyzerWidget::zoomOutX() { m_timePerDiv *= 1.25; update(); }
void LogicAnalyzerWidget::autoScale() { update(); }

void LogicAnalyzerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::MiddleButton) {
        m_horizontalOffset -= (event->pos().x() - m_lastMousePos.x()) * (m_timePerDiv / (width()/10.0));
        m_lastMousePos = event->pos();
        update();
    }
}
void LogicAnalyzerWidget::mousePressEvent(QMouseEvent* event) { m_lastMousePos = event->pos(); }
void LogicAnalyzerWidget::wheelEvent(QWheelEvent* event) { if (event->angleDelta().y() > 0) zoomInX(); else zoomOutX(); }

void LogicAnalyzerWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(15, 15, 20));
    p.setPen(QColor(100, 255, 150));
    p.drawText(10, 20, "Logic Analyzer - 8 Channels");
    
    // Grid
    p.setPen(QColor(40, 40, 50));
    qreal stepX = (qreal)width() / 10.0;
    for (int i = 0; i <= 10; ++i) p.drawLine(i * stepX, 0, i * stepX, height());
}

// ─── Panel Meters ──────────────────────────────────────────────────────────
VoltmeterWidget::VoltmeterWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    m_signalLabel = new QLabel("--");
    m_valueLabel = new QLabel("0.00 V");
    m_valueLabel->setStyleSheet("font-size: 24px; font-weight: bold; font-family: 'JetBrains Mono'; color: #00ff00;");
    l->addWidget(m_signalLabel);
    l->addWidget(m_valueLabel);
}
void VoltmeterWidget::setReading(const QString& s, double v) { m_signalLabel->setText(s); m_valueLabel->setText(formatValueSI(v, "V")); }
void VoltmeterWidget::clear() { m_valueLabel->setText("0.00 V"); }

AmmeterWidget::AmmeterWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    m_signalLabel = new QLabel("--");
    m_valueLabel = new QLabel("0.00 A");
    m_valueLabel->setStyleSheet("font-size: 24px; font-weight: bold; font-family: 'JetBrains Mono'; color: #00ccff;");
    l->addWidget(m_signalLabel);
    l->addWidget(m_valueLabel);
}
void AmmeterWidget::setReading(const QString& s, double v) { m_signalLabel->setText(s); m_valueLabel->setText(formatValueSI(v, "A")); }
void AmmeterWidget::clear() { m_valueLabel->setText("0.00 A"); }

WattmeterWidget::WattmeterWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    m_signalLabel = new QLabel("--");
    m_valueLabel = new QLabel("0.00 W");
    m_valueLabel->setStyleSheet("font-size: 24px; font-weight: bold; font-family: 'JetBrains Mono'; color: #ffcc00;");
    l->addWidget(m_signalLabel);
    l->addWidget(m_valueLabel);
}
void WattmeterWidget::setReading(const QString& s, double v) { m_signalLabel->setText(s); m_valueLabel->setText(formatValueSI(v, "W")); }
void WattmeterWidget::clear() { m_valueLabel->setText("0.00 W"); }

FrequencyCounterWidget::FrequencyCounterWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    m_signalLabel = new QLabel("--");
    m_valueLabel = new QLabel("0.00 Hz");
    m_valueLabel->setStyleSheet("font-size: 24px; font-weight: bold; font-family: 'JetBrains Mono'; color: #ff33cc;");
    l->addWidget(m_signalLabel);
    l->addWidget(m_valueLabel);
}
void FrequencyCounterWidget::setReading(const QString& s, double v) { m_signalLabel->setText(s); m_valueLabel->setText(formatValueSI(v, "Hz")); }
void FrequencyCounterWidget::clear() { m_valueLabel->setText("0.00 Hz"); }

LogicProbeWidget::LogicProbeWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* l = new QVBoxLayout(this);
    m_signalLabel = new QLabel("--");
    m_stateLabel = new QLabel("LOW");
    m_stateLabel->setStyleSheet("font-size: 24px; font-weight: bold; font-family: 'JetBrains Mono'; color: #888888;");
    l->addWidget(m_signalLabel);
    l->addWidget(m_stateLabel);
}
void LogicProbeWidget::setState(const QString& s, bool high, double v) { 
    m_signalLabel->setText(QString("%1 (%2)").arg(s, formatValueSI(v, "V")));
    m_stateLabel->setText(high ? "HIGH" : "LOW");
    m_stateLabel->setStyleSheet(QString("font-size: 24px; font-weight: bold; color: %1;").arg(high ? "#ff3300" : "#444444"));
}
void LogicProbeWidget::clear() { m_stateLabel->setText("N/A"); }
