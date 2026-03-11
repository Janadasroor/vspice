#include "color_button.h"
#include <QColorDialog>
#include <QPainter>
#include <QMouseEvent>

ColorButton::ColorButton(const QColor& color, QWidget* parent)
    : QPushButton(parent), m_color(color)
{
    setFlat(true);
    setMinimumHeight(24);
    setCursor(Qt::PointingHandCursor);
}

void ColorButton::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        update();
        emit colorChanged(m_color);
    }
}

void ColorButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QColor newColor = QColorDialog::getColor(m_color, this, "Select Color", QColorDialog::ShowAlphaChannel);
        if (newColor.isValid()) {
            setColor(newColor);
        }
    }
    QPushButton::mousePressEvent(event);
}

void ColorButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background/border
    QRect colorRect = rect().adjusted(4, 4, -4, -4);
    
    // Draw checkerboard for alpha if needed? (Skip for now to keep it clean)
    
    // Draw the color
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.setBrush(m_color);
    painter.drawRoundedRect(colorRect, 3, 3);
    
    // Draw text (hex code)
    painter.setPen(QColor(220, 220, 220));
    painter.setFont(font());
    painter.drawText(colorRect.adjusted(30, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, m_color.name().toUpper());
    
    // Draw small preview square on the left if there's enough space
    // Wait, the main rect is the color. Let's make it a small swatch on the left and text on the right
    QRect swatch(colorRect.left() + 2, colorRect.top() + 2, colorRect.height() - 4, colorRect.height() - 4);
    painter.setBrush(m_color);
    painter.drawRoundedRect(swatch, 2, 2);
}
