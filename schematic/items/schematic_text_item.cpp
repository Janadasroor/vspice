#include "schematic_text_item.h"
#include "flux/core/theme_manager.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>

SchematicTextItem::SchematicTextItem(QString text, QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent), m_text(text)
{
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsMovable);
    setZValue(100);
    m_font = QFont("Arial", 40);
    
    m_color = Qt::white;
    if (ThemeManager::theme()) {
        m_color = ThemeManager::theme()->textColor();
    }
    m_alignment = Qt::AlignLeft;
    recalcBounds();
}

SchematicItem::ItemType SchematicTextItem::itemType() const { return LabelType; }

QString SchematicTextItem::itemTypeName() const {
    return "Text";
}

QJsonObject SchematicTextItem::toJson() const {
    QJsonObject json; 
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["text"] = m_text;
    json["size"] = m_font.pointSize();
    json["color"] = m_color.name();
    json["bold"] = m_font.bold();
    json["italic"] = m_font.italic();
    json["family"] = m_font.family();
    json["align"] = (int)m_alignment;
    json["rotation"] = rotation();
    return json;
}

bool SchematicTextItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) setId(QUuid::fromString(json["id"].toString()));
    if (json.contains("x")) setPos(json["x"].toDouble(), json["y"].toDouble());
    if (json.contains("text")) m_text = json["text"].toString();
    if (json.contains("size")) m_font.setPointSize(json["size"].toInt());
    if (json.contains("color")) m_color = QColor(json["color"].toString());
    if (json.contains("bold")) m_font.setBold(json["bold"].toBool());
    if (json.contains("italic")) m_font.setItalic(json["italic"].toBool());
    if (json.contains("family")) m_font.setFamily(json["family"].toString());
    if (json.contains("align")) m_alignment = (Qt::Alignment)json["align"].toInt();
    if (json.contains("rotation")) setRotation(json["rotation"].toDouble());
    recalcBounds();
    return true;
}

SchematicItem* SchematicTextItem::clone() const {
    SchematicTextItem* newItem = new SchematicTextItem(m_text, pos(), parentItem());
    newItem->setFont(m_font);
    newItem->setColor(m_color);
    newItem->setAlignment(m_alignment);
    return newItem;
}

void SchematicTextItem::recalcBounds() {
    prepareGeometryChange();
    QFontMetricsF fm(m_font);
    QRectF rect = fm.boundingRect(m_text);
    m_cachedBounds = rect.adjusted(-2, -2, 2, 2);
}

QRectF SchematicTextItem::boundingRect() const {
    return m_cachedBounds;
}

void SchematicTextItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget)
    
    QStyleOptionGraphicsItem opt = *option;
    opt.state &= ~QStyle::State_Selected;

    painter->setPen(m_color);
    painter->setFont(m_font);
    painter->drawText(m_cachedBounds, m_alignment, m_text);
}
