#ifndef VISUAL_PIN_MAPPER_H
#define VISUAL_PIN_MAPPER_H

#include <QWidget>
#include <QStringList>
#include <QRectF>
#include <QPointF>

class VisualPinMapper : public QWidget {
    Q_OBJECT
public:
    explicit VisualPinMapper(QWidget* parent = nullptr);

    void setPins(const QStringList& inputs, const QStringList& outputs);
    QStringList inputPins() const { return m_inputs; }
    QStringList outputPins() const { return m_outputs; }

signals:
    void pinsChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct PinRect {
        QRectF rect;
        int index;
        bool isInput;
        QString name;
    };

    QList<PinRect> m_pinRects;
    QStringList m_inputs;
    QStringList m_outputs;

    int m_dragIdx = -1;
    bool m_dragIsInput = false;
    QPointF m_dragPos;
    
    QRectF m_blockRect;
    void updateRects();
};

#endif // VISUAL_PIN_MAPPER_H
