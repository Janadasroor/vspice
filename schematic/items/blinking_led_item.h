#ifndef BLINKING_LEDITEM_H
#define BLINKING_LEDITEM_H

#include "schematic_item.h"
#include <QTimer>

// Simple interactive blinking LED indicator
class BlinkingLEDItem : public SchematicItem {
    Q_OBJECT
public:
    BlinkingLEDItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Blinking LED"; }
    ItemType itemType() const override { return CustomType; }
    QString referencePrefix() const override { return "D"; }

    void setSimState(const QMap<QString, double>& nodeVoltages,
                     const QMap<QString, double>& currents) override;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& j) override;
    SchematicItem* clone() const override;

    void setValue(const QString& value) override;
    void setBlinkEnabled(bool on);
    bool blinkEnabled() const { return m_blinkEnabled; }
    void setBlinkHz(double hz);
    double blinkHz() const { return m_blinkHz; }
    void setThreshold(double v) { m_threshold = v; }
    double threshold() const { return m_threshold; }
    void setColorName(const QString& name);
    QString colorName() const { return m_colorName; }

private:
    void updateBlinkTimer();
    double parseBlinkHz(const QString& value) const;
    QString parseColorName(const QString& value) const;
    void updateValueString();

    QTimer* m_blinkTimer = nullptr;
    bool m_blinkOn = true;
    bool m_powered = false;
    double m_blinkHz = 1.0;
    double m_voltage = 0.0;
    bool m_blinkEnabled = true;
    double m_threshold = 1.5;
    QString m_colorName = "RED";
};

#endif // BLINKING_LEDITEM_H
