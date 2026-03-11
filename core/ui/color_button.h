#ifndef COLOR_BUTTON_H
#define COLOR_BUTTON_H

#include <QPushButton>
#include <QColor>

class ColorButton : public QPushButton {
    Q_OBJECT

public:
    explicit ColorButton(const QColor& color, QWidget* parent = nullptr);
    
    QColor color() const { return m_color; }
    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    QColor m_color;
};

#endif // COLOR_BUTTON_H
