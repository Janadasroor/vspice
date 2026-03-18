#ifndef SWITCHITEM_H
#define SWITCHITEM_H

#include "schematic_item.h"
#include <QPainter>

/**
 * @brief Interactive SPICE-compatible switch component.
 */
class SwitchItem : public SchematicItem {
public:
    SwitchItem(QPointF pos = QPointF(), QGraphicsItem *parent = nullptr);

    QString itemTypeName() const override { return "Switch"; }
    ItemType itemType() const override { return SchematicItem::ComponentType; }
    QString referencePrefix() const override { return "SW"; }

    bool isInteractive() const override { return true; }
    void onInteractiveClick(const QPointF& pos) override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<QPointF> connectionPoints() const override;

    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;

    bool isOpen() const { return m_isOpen; }
    void setOpen(bool open);

    bool useModel() const { return m_useModel; }
    void setUseModel(bool useModel);

    QString modelName() const { return m_modelName; }
    void setModelName(const QString& name);

    QString ron() const { return m_ron; }
    void setRon(const QString& value);

    QString roff() const { return m_roff; }
    void setRoff(const QString& value);

    QString vt() const { return m_vt; }
    void setVt(const QString& value);

    QString vh() const { return m_vh; }
    void setVh(const QString& value);

private:
    void syncParamExpressions();
    void applyParamExpressions();

    bool m_isOpen;
    bool m_useModel;
    QString m_modelName;
    QString m_ron;
    QString m_roff;
    QString m_vt;
    QString m_vh;
};

#endif // SWITCHITEM_H
