#include "smart_probe_overlay.h"
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QFontDatabase>
#include "theme_manager.h"

SmartProbeOverlay::SmartProbeOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(320, 180);
    setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip);
    setAttribute(Qt::WA_TranslucentBackground);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 12, 15, 12);
    m_netLabel = new QLabel(this);
    layout->addWidget(m_netLabel);

    m_instantLabel = new QLabel(this);
    layout->addWidget(m_instantLabel);

    m_contextLabel = new QLabel(this);
    m_contextLabel->setWordWrap(true);
    layout->addWidget(m_contextLabel);

    m_separator = new QFrame(this);
    m_separator->setFrameShape(QFrame::HLine);
    m_separator->setFrameShadow(QFrame::Plain);
    layout->addWidget(m_separator);

    m_aiStatusLabel = new QLabel(this);
    layout->addWidget(m_aiStatusLabel);

    m_aiLabel = new QLabel(this);
    m_aiLabel->setWordWrap(true);
    m_aiLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_aiLabel, 1);

    refreshTheme();

    m_fadeAnimation = new QPropertyAnimation(this, "opacity");
    m_fadeAnimation->setDuration(150);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    
    // Single persistent connection to avoid leaks and race conditions
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_fadeAnimation->endValue().toDouble() == 0.0) {
            hide();
        }
    });

    hide();
}

void SmartProbeOverlay::refreshTheme() {
    PCBTheme* theme = ThemeManager::theme();
    bool isLight = (theme->type() == PCBTheme::Light);

    QString primaryColor = isLight ? "#1e293b" : "#ffffff";
    QString secondaryColor = isLight ? "#64748b" : "#aaaaaa";
    QString accentColor = isLight ? "#7c3aed" : "#a78bfa";
    QString sepColor = isLight ? "rgba(0, 0, 0, 20)" : "rgba(255, 255, 255, 40)";

    m_netLabel->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 14px;").arg(primaryColor));
    m_instantLabel->setStyleSheet(QString("color: %1; font-family: 'Monospace'; font-weight: bold; font-size: 12px;").arg(accentColor));
    m_contextLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(secondaryColor));
    m_separator->setStyleSheet(QString("background-color: %1; max-height: 1px;").arg(sepColor));
    m_aiStatusLabel->setStyleSheet(QString("color: %1; font-style: italic; font-size: 10px;").arg(accentColor));
    m_aiLabel->setStyleSheet(QString("color: %1; font-size: 11px; line-height: 1.4;").arg(primaryColor));
}

void SmartProbeOverlay::clearAIAnnotation() {
    if (m_aiLabel) m_aiLabel->clear();
    if (m_aiStatusLabel) m_aiStatusLabel->clear();
    if (m_separator) m_separator->hide();
}

void SmartProbeOverlay::showAt(const QPoint& pos, const QString& netName, const QString& instantVal, const QString& context) {
    refreshTheme(); // Sync with current theme before showing
    
    m_netLabel->setText("Net: " + netName);
    m_instantLabel->setText(instantVal);
    m_contextLabel->setText("Connected: " + context);
    m_aiLabel->clear();
    m_aiStatusLabel->setText("Viora is thinking...");
    
    // Stop any running fade-out animation
    m_fadeAnimation->stop();
    
    move(pos);
    show(); // show() before animation starts so it's visible while fading in
    
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
    if (m_fadeAnimation->state() == QAbstractAnimation::Running && 
        m_fadeAnimation->endValue().toDouble() == 0.0) {
        return; // Already fading out
    }

    m_fadeAnimation->stop();
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(0.0);
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

    PCBTheme* theme = ThemeManager::theme();
    bool isLight = (theme->type() == PCBTheme::Light);

    QColor bgColor = isLight ? QColor(255, 255, 255, 245) : QColor(20, 20, 30, 235);
    QColor borderColor = isLight ? QColor(124, 58, 237, 80) : QColor(124, 58, 237, 100);

    painter.setBrush(bgColor);
    painter.setPen(QPen(borderColor, 1.5));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
}
