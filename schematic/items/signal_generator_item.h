#ifndef SIGNALGENERATORITEM_H
#define SIGNALGENERATORITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Interactive Signal Generator component for real-time frequency/amplitude control.
 */
class SignalGeneratorItem : public SchematicItem {
public:
    enum WaveformType {
        Sine,
        Square,
        Triangle,
        Pulse,
        DC
    };

    SignalGeneratorItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "SignalGenerator"; }
    ItemType itemType() const override { return SchematicItem::VoltageSourceType; }
    QString referencePrefix() const override { return "V"; }

    bool isInteractive() const override { return true; }
    void onInteractiveClick(const QPointF& scenePos) override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    // Advanced properties
    WaveformType waveform() const { return m_waveform; }
    void setWaveform(WaveformType type);

    QString frequency() const { return m_freq; }
    void setFrequency(const QString& f);

    QString amplitude() const { return m_amplitude; }
    void setAmplitude(const QString& a);

    QString offset() const { return m_offset; }
    void setOffset(const QString& o);

    QString acMagnitude() const { return m_acMagnitude; }
    void setAcMagnitude(const QString& m);

    QString acPhase() const { return m_acPhase; }
    void setAcPhase(const QString& p);

private:
    void updateSpiceValue();

    WaveformType m_waveform = Sine;
    QString m_freq = "1000";      // Hz
    QString m_amplitude = "5.0";    // V
    QString m_offset = "0.0";       // V
    QString m_acMagnitude = "1.0";  // V (for AC Analysis)
    QString m_acPhase = "0.0";      // Deg (for AC Analysis)
};

#endif // SIGNALGENERATORITEM_H
