#include "transistor_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>

namespace {
constexpr qreal kGridAlignedRightPinX = 15.0;
}

TransistorItem::TransistorItem(QPointF pos, QString value, TransistorType type, QGraphicsItem *parent)
    : SchematicItem(parent), m_value(value), m_transistorType(type) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    createLabels(QPointF(22.5, -15), QPointF(22.5, 7.5));
}

void TransistorItem::buildPrimitives() {
    m_primitives.clear();
    
    if (m_transistorType == NPN || m_transistorType == PNP) {
        // BJT Transistor Symbol
        // Circle body
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(0, 0), 30, false));
        
        // Base line (vertical)
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-15, -18), QPointF(-15, 18)));
        
        // Base lead
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-45, 0), QPointF(-15, 0)));
        
        // Emitter line
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-15, -12), QPointF(kGridAlignedRightPinX, -27)));
        
        // Collector line
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-15, 12), QPointF(kGridAlignedRightPinX, 27)));
        
        // Emitter lead
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(kGridAlignedRightPinX, -27), QPointF(kGridAlignedRightPinX, -45)));
        
        // Collector lead
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(kGridAlignedRightPinX, 27), QPointF(kGridAlignedRightPinX, 45)));
        
        // Arrow on emitter (direction depends on NPN/PNP)
        QList<QPointF> arrow;
        if (m_transistorType == NPN) {
            // Arrow pointing away from base
            arrow << QPointF(9, -21) << QPointF(kGridAlignedRightPinX, -27) << QPointF(6, -27);
        } else {
            // Arrow pointing toward base (PNP)
            arrow << QPointF(-9, -18) << QPointF(-15, -12) << QPointF(-6, -9);
        }
        m_primitives.push_back(std::make_unique<PolygonPrimitive>(arrow, true));
        
    } else {
        // MOSFET Symbol
        // Body line (vertical)
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-12, -22.5), QPointF(-12, 22.5)));
        
        // Gate line
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-18, -15), QPointF(-18, 15)));
        
        // Gate lead
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-45, 0), QPointF(-18, 0)));
        
        // Source segment
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-12, -15), QPointF(kGridAlignedRightPinX, -15)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(kGridAlignedRightPinX, -15), QPointF(kGridAlignedRightPinX, -45)));
        
        // Drain segment
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-12, 15), QPointF(kGridAlignedRightPinX, 15)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(kGridAlignedRightPinX, 15), QPointF(kGridAlignedRightPinX, 45)));
        
        // Body connection
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-12, 0), QPointF(kGridAlignedRightPinX, 0)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(kGridAlignedRightPinX, 0), QPointF(kGridAlignedRightPinX, 15)));
        
        // Arrow (direction depends on N/P type)
        QList<QPointF> arrow;
        if (m_transistorType == NMOS) {
            arrow << QPointF(-6, 0) << QPointF(0, -6) << QPointF(0, 6);
        } else {
            arrow << QPointF(0, 0) << QPointF(-6, -6) << QPointF(-6, 6);
        }
        m_primitives.push_back(std::make_unique<PolygonPrimitive>(arrow, true));
    }
    
    // Pin dots
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(-45, 0), 3.75, true));
    
    if (m_transistorType == NPN || m_transistorType == PNP) {
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(kGridAlignedRightPinX, -45), 3.75, true));
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(kGridAlignedRightPinX, 45), 3.75, true));
    } else {
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(kGridAlignedRightPinX, -45), 3.75, true));
        m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(kGridAlignedRightPinX, 45), 3.75, true));
    }
}

void TransistorItem::setValue(const QString& value) {
    if (m_value != value) {
        m_value = value;
        buildPrimitives();
        update();
    }
}

void TransistorItem::setTransistorType(TransistorType type) {
    if (m_transistorType != type) {
        m_transistorType = type;
        buildPrimitives();
        update();
    }
}

QRectF TransistorItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void TransistorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
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

QJsonObject TransistorItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["name"] = name();
    json["value"] = m_value;
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["transistorType"] = static_cast<int>(m_transistorType);
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

bool TransistorItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;

    setId(QUuid(json["id"].toString()));
    setName(json["name"].toString());
    m_value = json["value"].toString();
    setReference(json["reference"].toString());
    setFootprint(json["footprint"].toString());
    loadPinPadMappingFromJson(json);
    m_transistorType = static_cast<TransistorType>(json["transistorType"].toInt());
    setPos(QPointF(json["x"].toDouble(), json["y"].toDouble()));
    buildPrimitives();
    createLabels(QPointF(22.5, -15), QPointF(22.5, 7.5));
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    update();
    return true;
}

SchematicItem* TransistorItem::clone() const {
    TransistorItem* newItem = new TransistorItem(pos(), m_value, m_transistorType, parentItem());
    newItem->setName(name());
    return newItem;
}

QString TransistorItem::pinName(int index) const {
    if (m_transistorType == NPN || m_transistorType == PNP) {
        switch (index) {
            case 0: return "B";
            case 1: return "C";
            case 2: return "E";
        }
    } else {
        switch (index) {
            case 0: return "G";
            case 1: return "D";
            case 2: return "S";
        }
    }
    return QString::number(index + 1);
}

QList<QPointF> TransistorItem::connectionPoints() const {
    QList<QPointF> points;
    points.append(QPointF(-45, 0)); // Base/Gate
    
    if (m_transistorType == NPN || m_transistorType == PNP) {
        points.append(QPointF(kGridAlignedRightPinX, -45));
        points.append(QPointF(kGridAlignedRightPinX, 45));
    } else {
        points.append(QPointF(kGridAlignedRightPinX, -45));
        points.append(QPointF(kGridAlignedRightPinX, 45));
    }
    return points;
}
