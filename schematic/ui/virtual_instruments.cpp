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
