#ifndef LOGICANALYZERITEM_H
#define LOGICANALYZERITEM_H

#include "schematic_item.h"
#include <QVector>

/**
 * @brief Multi-channel digital logic analyzer instrument.
 */
class LogicAnalyzerItem : public SchematicItem {
public:
    LogicAnalyzerItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "LogicAnalyzer"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "LA"; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;
    
    int channelCount() const { return m_channelCount; }
    void setChannelCount(int count);

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

private:
    int m_channelCount;
};

#endif // LOGICANALYZERITEM_H
