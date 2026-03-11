#ifndef INSTRUMENT_PROBE_ITEM_H
#define INSTRUMENT_PROBE_ITEM_H

#include "schematic_item.h"

class InstrumentProbeItem : public SchematicItem {
public:
    enum class Kind {
        Oscilloscope,
        Voltmeter,
        Ammeter,
        Wattmeter,
        FrequencyCounter,
        LogicProbe
    };

    explicit InstrumentProbeItem(Kind kind, QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override;
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override;

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

private:
    QString displayName() const;
    QString schemaTypeName() const;
    Kind m_kind;
};

#endif // INSTRUMENT_PROBE_ITEM_H
