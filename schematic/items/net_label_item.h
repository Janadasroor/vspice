#ifndef NET_LABEL_ITEM_H
#define NET_LABEL_ITEM_H

#include "schematic_item.h"
#include <QFont>
#include <QPen>

class NetLabelItem : public SchematicItem {
public:
    enum LabelScope {
        Local,
        Global
    };

    NetLabelItem(QPointF pos = QPointF(), const QString& label = "NET", QGraphicsItem* parent = nullptr, LabelScope scope = Local);
    
    // SchematicItem interface
    QString itemTypeName() const override { return "Net Label"; }
    ItemType itemType() const override { return NetLabelType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    
    void setLabel(const QString& label) { setValue(label); }
    QString label() const { return value(); }

    void setNetClassName(const QString& className) { m_netClassName = className; }
    QString netClassName() const { return m_netClassName; }
    
    void setLabelScope(LabelScope scope);
    LabelScope labelScope() const { return m_scope; }
    
    void setPen(const QPen& pen) { m_pen = pen; update(); }
    QPen pen() const { return m_pen; }

    QPainterPath shape() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QList<QPointF> connectionPoints() const override;

private:
    LabelScope m_scope;
    QFont m_font;
    QPen m_pen;
    QString m_netClassName;
};

#endif // NET_LABEL_ITEM_H
