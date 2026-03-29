#ifndef TUNING_SLIDER_SYMBOL_ITEM_H
#define TUNING_SLIDER_SYMBOL_ITEM_H

#include "schematic_item.h"

/**
 * @brief An independent slider component that can be placed on the schematic.
 * Acts as a .param in SPICE and can be tuned in real-time.
 */
class TuningSliderSymbolItem : public SchematicItem {
    Q_OBJECT
public:
    explicit TuningSliderSymbolItem(QPointF pos, QGraphicsItem* parent = nullptr);
    virtual ~TuningSliderSymbolItem() override;

    ItemType itemType() const override { return ItemType::CustomType; }
    QString itemTypeName() const override { return "TuningSlider"; }

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override { return {}; } // No electrical pins

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    double minValue() const { return m_min; }
    void setMinValue(double v) { m_min = v; update(); }

    double maxValue() const { return m_max; }
    void setMaxValue(double v) { m_max = v; update(); }

    double currentValue() const { return m_current; }
    void setCurrentValue(double v);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    double posToValue(double x) const;
    double valueToPos(double val) const;
    void triggerRealTimeUpdate();

    double m_min = 0.0;
    double m_max = 100.0;
    double m_current = 50.0;
    bool m_dragging = false;

    const double m_width = 120.0;
    const double m_height = 35.0;
};

#endif // TUNING_SLIDER_SYMBOL_ITEM_H
