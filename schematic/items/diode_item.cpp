#include "diode_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>

DiodeItem::DiodeItem(QPointF pos, QString value, QGraphicsItem *parent)
    : SchematicItem(parent), m_value(value) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    createLabels(QPointF(-15, -37.5), QPointF(-22.5, 37.5));
}

void DiodeItem::buildPrimitives() {
    m_primitives.clear();
    
    // Leads
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-45, 0), QPointF(-15, 0)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(15, 0), QPointF(45, 0)));
    
    // Triangle (Anode -> Cathode)
    QList<QPointF> triangle;
    triangle << QPointF(-15, -22.5) << QPointF(-15, 22.5) << QPointF(15, 0);
    m_primitives.push_back(std::make_unique<PolygonPrimitive>(triangle, false));
    
    // Cathode bar
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(15, -22.5), QPointF(15, 22.5)));
    
    // Pins
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(-45, 0), 3.75, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(45, 0), 3.75, true));
}

void DiodeItem::setValue(const QString& value) {
    if (m_value != value) {
        m_value = value;
        updateLabelText();
        buildPrimitives();
        update();
    }
}

QRectF DiodeItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void DiodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    for (const auto& prim : m_primitives) {
        prim->paint(painter, m_pen, m_brush);
    }
    
    // Draw highlighted connection points
    drawConnectionPointHighlights(painter);
    
    if (isSelected()) {
        PCBTheme* theme = ThemeManager::theme();
        painter->setPen(QPen(theme->selectionBox(), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
    }
}

QJsonObject DiodeItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["name"] = name();
    json["value"] = m_value;
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["x"] = pos().x();
    json["y"] = pos().y();

    if (m_refLabelItem) {
        json["refX"] = m_refLabelItem->pos().x();
        json["refY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }
    json["pinPadMapping"] = pinPadMappingToJson();
    return json;
}

bool DiodeItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;
    setId(QUuid(json["id"].toString()));
    setName(json["name"].toString());
    m_value = json["value"].toString();
    setReference(json["reference"].toString());
    setFootprint(json["footprint"].toString());
    loadPinPadMappingFromJson(json);
    setPos(QPointF(json["x"].toDouble(), json["y"].toDouble()));
    buildPrimitives();
    createLabels(QPointF(-15, -37.5), QPointF(-22.5, 37.5));
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    updateLabelText();
    update();
    return true;
}

SchematicItem* DiodeItem::clone() const {
    DiodeItem* newItem = new DiodeItem(pos(), m_value, parentItem());
    newItem->setName(name());
    return newItem;
}

QList<QPointF> DiodeItem::connectionPoints() const {
    return { QPointF(-45, 0), QPointF(45, 0) };
}
