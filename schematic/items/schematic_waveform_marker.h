#ifndef SCHEMATIC_WAVEFORM_MARKER_H
#define SCHEMATIC_WAVEFORM_MARKER_H

#include "schematic_item.h"
#include <QString>
#include <QVector>
#include <QPointF>

class SchematicWaveformMarker : public SchematicItem {
public:
    explicit SchematicWaveformMarker(const QString& netName, const QString& kind = "V", QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    ItemType itemType() const override { return CustomType; }
    QString itemTypeName() const override { return "WaveformMarker"; }

    void updateData(const QVector<double>& x, const QVector<double>& y);
    QString netName() const { return m_netName; }
    QString kind() const { return m_kind; }

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

private:
    QString m_netName;
    QString m_kind;
    QVector<double> m_xData;
    QVector<double> m_yData;
    
    double m_minY = 0;
    double m_maxY = 1;

    QColor markerColor() const;
};

#endif // SCHEMATIC_WAVEFORM_MARKER_H
