#ifndef OSCILLOSCOPEITEM_H
#define OSCILLOSCOPEITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Schematic symbol for a 4-channel oscilloscope instrument.
 */
class OscilloscopeItem : public SchematicItem {
public:
    OscilloscopeItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "OscilloscopeInstrument"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "OSC"; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    struct ChannelConfig {
        bool enabled = true;
        double scale = 1.0;
        double offset = 0.0;
        QColor color;
    };

    struct Config {
        ChannelConfig channels[4];
        double timebase = 1e-3;
        QString triggerSource = "CH1";
        double triggerLevel = 0.0;
    };

    Config config() const { return m_config; }
    void setConfig(const Config& cfg) { m_config = cfg; emit configChanged(); update(); }

    QString channelNet(int chIdx) const; // Logic to find net on a specific channel pin

signals:
    void configChanged();

private:
    Config m_config;
};

#endif // OSCILLOSCOPEITEM_H
