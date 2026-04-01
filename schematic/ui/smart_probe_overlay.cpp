#include "smart_probe_overlay.h"
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QFontDatabase>

SmartProbeOverlay::SmartProbeOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(320, 180);
    setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip);
    setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 12, 15, 12);
    layout->setSpacing(8);

    m_netLabel = new QLabel(this);
    m_netLabel->setStyleSheet("color: white; font-weight: bold; font-size: 14px;");
    layout->addWidget(m_netLabel);

    m_instantLabel = new QLabel(this);
    m_instantLabel->setStyleSheet("color: #7c3aed; font-family: 'Monospace'; font-weight: bold; font-size: 12px;");
    layout->addWidget(m_instantLabel);

    m_contextLabel = new QLabel(this);
    m_contextLabel->setStyleSheet("color: #aaaaaa; font-size: 11px;");
    m_contextLabel->setWordWrap(true);
    layout->addWidget(m_contextLabel);

    QFrame* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet("background-color: rgba(255, 255, 255, 40); max-height: 1px;");
    layout->addWidget(separator);

    m_aiStatusLabel = new QLabel(this);
    m_aiStatusLabel->setStyleSheet("color: #7c3aed; font-style: italic; font-size: 10px;");
    layout->addWidget(m_aiStatusLabel);

    m_aiLabel = new QLabel(this);
    m_aiLabel->setStyleSheet("color: #ffffff; font-size: 11px; line-height: 1.4;");
    m_aiLabel->setWordWrap(true);
    m_aiLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_aiLabel, 1);

    m_fadeAnimation = new QPropertyAnimation(this, "opacity");
    m_fadeAnimation->setDuration(150);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);

    hide();
}

void SmartProbeOverlay::showAt(const QPoint& pos, const QString& netName, const QString& instantVal, const QString& context) {
    m_netLabel->setText("⚡ Net: " + netName);
    m_instantLabel->setText(instantVal);
    m_contextLabel->setText("Connected: " + context);
    m_aiLabel->clear();
    m_aiStatusLabel->setText("Viora is thinking...");
    
    move(pos);
    show();
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(1.0);
    m_fadeAnimation->start();
}

void SmartProbeOverlay::updateAIAnnotation(const QString& text) {
    m_aiLabel->setText(text);
    m_aiStatusLabel->clear();
}

void SmartProbeOverlay::setAIStatus(const QString& status) {
    m_aiStatusLabel->setText(status);
}

void SmartProbeOverlay::hideOverlay() {
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(0.0);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, &QWidget::hide);
    m_fadeAnimation->start();
}

void SmartProbeOverlay::setOpacity(double opacity) {
    m_opacity = opacity;
    update();
}

void SmartProbeOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_opacity);

    QColor bgColor(20, 20, 30, 235);
    painter.setBrush(bgColor);
    painter.setPen(QPen(QColor(124, 58, 237, 100), 1.5));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
}
