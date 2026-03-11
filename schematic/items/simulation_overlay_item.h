#ifndef SIMULATIONOVERLAYITEM_H
#define SIMULATIONOVERLAYITEM_H

#include <QGraphicsItem>
#include <QString>

/**
 * @brief Volatile overlay item for displaying simulation results (DC OP)
 */
class SimulationOverlayItem : public QGraphicsItem {
public:
    enum OverlayType { Voltage, Current };

    SimulationOverlayItem(const QString& value, const QPointF& pos, OverlayType type = Voltage, QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), m_value(value), m_type(type) {
        setPos(pos);
        setZValue(1000); // Always on top
        setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    }

    QRectF boundingRect() const override {
        return QRectF(-30, -10, 60, 20);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override {
        Q_UNUSED(option) Q_UNUSED(widget)
        
        painter->setRenderHint(QPainter::Antialiasing);
        
        // LTSpice Style: Light yellow for voltage, light green for current
        QColor bgColor = (m_type == Voltage) ? QColor(255, 255, 200) : QColor(200, 255, 200);
        QColor borderColor = (m_type == Voltage) ? QColor(180, 180, 100) : QColor(100, 180, 100);
        
        // Draw small pointer line
        painter->setPen(QPen(borderColor, 1));
        painter->drawLine(0, 0, 0, (m_type == Voltage) ? 10 : -10);

        painter->setBrush(bgColor);
        painter->setPen(QPen(borderColor, 1));
        
        QRectF rect = boundingRect();
        if (m_type == Voltage) rect.translate(0, -15);
        else rect.translate(0, 15);
        
        painter->drawRect(rect);
        
        painter->setPen(Qt::black);
        QFont font = painter->font();
        font.setPointSize(8);
        font.setFamily("Consolas"); // Monospace for values
        painter->setFont(font);
        
        painter->drawText(rect, Qt::AlignCenter, m_value);
    }

private:
    QString m_value;
    OverlayType m_type;
};

#endif // SIMULATIONOVERLAYITEM_H
