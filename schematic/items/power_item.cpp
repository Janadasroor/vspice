#include "power_item.h"
#include "schematic_text_item.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>

PowerItem::PowerItem(QPointF pos, PowerType type, QGraphicsItem *parent)
    : SchematicItem(parent), m_type(type) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setAcceptHoverEvents(true);
    setExcludeFromPcb(true);
    setFootprint(QString());
    
    m_pen = QPen(Qt::white, 1.5);
    m_brush = Qt::NoBrush;
    
    const QString defaultNet = netName();
    setName(defaultNet);
    SchematicItem::setValue(defaultNet);
    buildPrimitives();
    
    QPointF labelOffset = (m_type == GND || m_type == VSS) ? QPointF(-22.5, 7.5) : QPointF(-22.5, -30);
    createLabels(QPointF(0,0), labelOffset); // Reference label hidden, value label used for Net Name
    if (m_refLabelItem) m_refLabelItem->setVisible(false);
}

QString PowerItem::netName() const {
    switch (m_type) {
        case GND: return "GND";
        case VCC: return "VCC";
        case VDD: return "VDD";
        case VSS: return "VSS";
        case VBAT: return "VBAT";
        case THREE_V_THREE: return "+3.3V";
        case FIVE_V: return "+5V";
        case TWELVE_V: return "+12V";
        default: return "PWR";
    }
}

void PowerItem::setPowerType(PowerType type) {
    m_type = type;
    const QString defaultNet = netName();
    setName(defaultNet);
    SchematicItem::setValue(defaultNet);
    buildPrimitives();
    
    QPointF labelOffset = (m_type == GND || m_type == VSS) ? QPointF(-22.5, 7.5) : QPointF(-22.5, -30);
    setValueLabelPos(labelOffset);
    updateLabelText();
    update();
}

void PowerItem::setValue(const QString& value) {
    const QString net = value.trimmed().isEmpty() ? netName() : value.trimmed();
    setName(net);
    SchematicItem::setValue(net);
}

QRectF PowerItem::boundingRect() const {
    return QRectF(-30, -30, 60, 60);
}

void PowerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);
    
    // Create a modified option to hide the default Qt selection box
    QStyleOptionGraphicsItem opt = *option;
    opt.state &= ~QStyle::State_Selected;
    
    for (const auto& primitive : m_primitives) {
        primitive->paint(painter, m_pen, m_brush);
    }
    
    drawConnectionPointHighlights(painter);
}

void PowerItem::buildPrimitives() {
    m_primitives.clear();
    
    switch (m_type) {
        case GND:
        case VSS:
            // Pin connection line (Top to center)
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -15), QPointF(0, 0)));
            // GND Symbol (Triangle or lines)
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-15, 0), QPointF(15, 0)));
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-9, 6), QPointF(9, 6)));
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-3, 12), QPointF(3, 12)));
            break;
            
        case VCC:
        case VDD:
        case THREE_V_THREE:
        case FIVE_V:
        case TWELVE_V:
            // Pin connection line (Bottom to center)
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 15), QPointF(0, 0)));
            // Power Symbol (Arrow Up)
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 0), QPointF(-7.5, 7.5)));
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 0), QPointF(7.5, 7.5)));
            break;
            
        case VBAT:
            // Battery symbol
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, -15), QPointF(0, -7.5)));
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(0, 7.5), QPointF(15, 15)));
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-15, -7.5), QPointF(15, -7.5))); // Long line
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-7.5, 0), QPointF(7.5, 0))); // Short line
            m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-15, 7.5), QPointF(15, 7.5))); // Long line
            break;
    }
}

QList<QPointF> PowerItem::connectionPoints() const {
    QList<QPointF> points;
    if (m_type == GND || m_type == VSS) {
        points.append(QPointF(0, -15));
    } else if (m_type == VBAT) {
        points.append(QPointF(0, -15));
        points.append(QPointF(0, 15));
    } else {
        points.append(QPointF(0, 15));
    }
    return points;
}

QJsonObject PowerItem::toJson() const {
    QJsonObject json;
    json["type"] = "Power";
    json["id"] = id().toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["power_type"] = (int)m_type;
    json["name"] = name();
    json["value"] = value();
    
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }
    return json;
}

bool PowerItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != "Power") return false;
    if (json.contains("id")) setId(QUuid(json["id"].toString()));
    setPos(json["x"].toDouble(), json["y"].toDouble());
    m_type = (PowerType)json["power_type"].toInt();
    const QString defaultNet = netName();
    const QString loadedValue = json["value"].toString();
    const QString loadedName = json["name"].toString();
    const QString net = !loadedValue.trimmed().isEmpty()
                            ? loadedValue.trimmed()
                            : (!loadedName.trimmed().isEmpty() ? loadedName.trimmed() : defaultNet);
    setName(net);
    SchematicItem::setValue(net);
    setExcludeFromPcb(true);
    setFootprint(QString());
    buildPrimitives();
    
    QPointF labelOffset = (m_type == GND || m_type == VSS) ? QPointF(-22.5, 7.5) : QPointF(-22.5, -30);
    createLabels(QPointF(0,0), labelOffset);
    if (m_refLabelItem) m_refLabelItem->setVisible(false);
    
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    update();
    return true;
}

SchematicItem* PowerItem::clone() const {
    PowerItem* item = new PowerItem(pos(), m_type);
    item->fromJson(toJson());
    return item;
}
