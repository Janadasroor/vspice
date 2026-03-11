#ifndef SCHEMATIC_TEXT_ITEM_H
#define SCHEMATIC_TEXT_ITEM_H

#include "schematic_item.h"
#include <QFont>
#include <QPen>

class SchematicTextItem : public SchematicItem {
public:
    SchematicTextItem(QString text = "", QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);
    virtual ~SchematicTextItem() = default;

    QString itemTypeName() const override;
    ItemType itemType() const override;
    
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    
    SchematicItem* clone() const override;
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setText(const QString& text) { m_text = text; recalcBounds(); update(); }
    QString text() const { return m_text; }
    
    void setFont(const QFont& font) { m_font = font; recalcBounds(); update(); }
    QFont font() const { return m_font; }
    
    void setColor(const QColor& color) { m_color = color; update(); }
    QColor color() const { return m_color; }
    
    void setAlignment(Qt::Alignment align) { m_alignment = align; update(); }
    Qt::Alignment alignment() const { return m_alignment; }
    
private:
    void recalcBounds();

    QString m_text;
    QFont m_font;
    QColor m_color;
    Qt::Alignment m_alignment;
    QRectF m_cachedBounds;
};

#endif // SCHEMATIC_TEXT_ITEM_H
