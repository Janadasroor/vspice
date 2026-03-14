#include "schematic_item.h"
#include "schematic_text_item.h"
#include <QPainter>

SchematicItem::SchematicItem(QGraphicsItem *parent)
    : QObject()
    , QGraphicsItem(parent)
    , m_id(QUuid::createUuid())
    , m_isHighlighted(false)
    , m_isLocked(false)
    , m_isMirroredX(false)
    , m_isMirroredY(false)
    , m_isSubItem(false)
{
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
}

void SchematicItem::createLabels(const QPointF& refOffset, const QPointF& valOffset) {
    m_defaultRefOffset = refOffset;
    m_defaultValOffset = valOffset;
    
    if (!m_refLabelItem) {
        QString refText = reference().isEmpty() ? (referencePrefix() + "?") : reference();
        m_refLabelItem = new SchematicTextItem(refText, QPointF(0,0), this);
        m_refLabelItem->setSubItem(true);
        m_refLabelItem->setPos(refOffset);
        m_refLabelItem->setFont(QFont("Inter", 10, QFont::Bold));
        m_refLabelItem->setName("RefLabel");
    }
    if (!m_valueLabelItem) {
        m_valueLabelItem = new SchematicTextItem(value(), QPointF(0,0), this);
        m_valueLabelItem->setSubItem(true);
        m_valueLabelItem->setPos(valOffset);
        m_valueLabelItem->setFont(QFont("Inter", 9));
        m_valueLabelItem->setName("ValueLabel");
    }
}

void SchematicItem::resetLabels() {
    if (m_refLabelItem) {
        m_refLabelItem->setPos(m_defaultRefOffset);
        m_refLabelItem->setRotation(0);
    }
    if (m_valueLabelItem) {
        m_valueLabelItem->setPos(m_defaultValOffset);
        m_valueLabelItem->setRotation(0);
    }
    update();
}

void SchematicItem::updateLabelText() {
    if (m_refLabelItem) {
        QString refText = reference().isEmpty() ? (referencePrefix() + "?") : reference();
        m_refLabelItem->setText(refText);
    }
    if (m_valueLabelItem) {
        m_valueLabelItem->setText(value());
    }
}

QPointF SchematicItem::referenceLabelPos() const {
    return m_refLabelItem ? m_refLabelItem->pos() : QPointF();
}

void SchematicItem::setReferenceLabelPos(const QPointF& p) {
    if (m_refLabelItem) {
        m_refLabelItem->setPos(p);
        update();
    }
}

QPointF SchematicItem::valueLabelPos() const {
    return m_valueLabelItem ? m_valueLabelItem->pos() : QPointF();
}

void SchematicItem::setValueLabelPos(const QPointF& p) {
    if (m_valueLabelItem) {
        m_valueLabelItem->setPos(p);
        update();
    }
}

void SchematicItem::drawConnectionPointHighlights(QPainter* painter) const {
    QList<QPointF> points = connectionPoints();
    if (points.isEmpty()) return;

    // 1. Draw Net Highlight Glow
    QColor glowColor = QColor(255, 215, 0, 150); // Gold glow
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(glowColor));

    if (m_isHighlighted) {
        // Full item highlight (Wires, Buses, etc.)
        for (const QPointF& point : points) {
            painter->drawEllipse(point, 6, 6);
        }
    } else if (!m_highlightedPins.isEmpty()) {
        // Granular pin highlight (ICs, Resistors, etc.)
        for (int index : m_highlightedPins) {
            if (index >= 0 && index < points.size()) {
                painter->drawEllipse(points[index], 6, 6);
            }
        }
    }

    // Only draw interactive highlights when hovering or when item is selected
    if (!isUnderMouse() && !isSelected()) return;

    QColor pinColor = QColor(0, 200, 150); // Cyan-green for visibility
    
    // Draw outer glow ring
    painter->setPen(QPen(pinColor, 2, Qt::SolidLine));
    painter->setBrush(Qt::NoBrush);
    for (const QPointF& point : points) {
        painter->drawEllipse(point, 4, 4);
    }
    
    // Draw filled center dot
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(pinColor));
    for (const QPointF& point : points) {
        painter->drawEllipse(point, 2.5, 2.5);
    }
}

QJsonObject SchematicItem::toJson() const {
    QJsonObject j;
    j["id"] = m_id.toString();
    j["x"] = pos().x();
    j["y"] = pos().y();
    j["rotation"] = rotation();
    j["name"] = m_name;
    j["value"] = m_value;
    j["reference"] = m_reference;
    j["footprint"] = m_footprint;
    j["spiceModel"] = m_spiceModel;
    j["isLocked"] = m_isLocked;
    j["isMirroredX"] = m_isMirroredX;
    j["isMirroredY"] = m_isMirroredY;
    j["excludeFromSim"] = m_excludeFromSimulation;
    j["excludeFromPcb"] = m_excludeFromPcb;
    
    QJsonObject exprs;
    for (auto it = m_paramExpressions.begin(); it != m_paramExpressions.end(); ++it) exprs[it.key()] = it.value();
    j["paramExpressions"] = exprs;

    QJsonObject tols;
    for (auto it = m_tolerances.begin(); it != m_tolerances.end(); ++it) tols[it.key()] = it.value();
    j["tolerances"] = tols;
    j["pinPadMapping"] = pinPadMappingToJson();

    if (m_refLabelItem) {
        j["refLabelX"] = m_refLabelItem->pos().x();
        j["refLabelY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        j["valLabelX"] = m_valueLabelItem->pos().x();
        j["valLabelY"] = m_valueLabelItem->pos().y();
    }
    
    return j;
}

bool SchematicItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid::fromString(json["id"].toString());
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setRotation(json["rotation"].toDouble());
    m_name = json["name"].toString();
    m_value = json["value"].toString();
    m_reference = json["reference"].toString();
    m_footprint = json["footprint"].toString();
    m_spiceModel = json["spiceModel"].toString();
    m_isLocked = json["isLocked"].toBool();
    m_isMirroredX = json["isMirroredX"].toBool();
    m_isMirroredY = json["isMirroredY"].toBool();
    m_excludeFromSimulation = json["excludeFromSim"].toBool(false);
    m_excludeFromPcb = json["excludeFromPcb"].toBool(false);
    
    m_paramExpressions.clear();
    if (json.contains("paramExpressions")) {
        QJsonObject exprs = json["paramExpressions"].toObject();
        for (auto it = exprs.begin(); it != exprs.end(); ++it) m_paramExpressions[it.key()] = it.value().toString();
    }

    m_tolerances.clear();
    if (json.contains("tolerances")) {
        QJsonObject tols = json["tolerances"].toObject();
        for (auto it = tols.begin(); it != tols.end(); ++it) m_tolerances[it.key()] = it.value().toString();
    }
    loadPinPadMappingFromJson(json);

    if (json.contains("refLabelX") && m_refLabelItem) {
        m_refLabelItem->setPos(json["refLabelX"].toDouble(), json["refLabelY"].toDouble());
    }
    if (json.contains("valLabelX") && m_valueLabelItem) {
        m_valueLabelItem->setPos(json["valLabelX"].toDouble(), json["valLabelY"].toDouble());
    }
    
    updateLabelText();
    update();
    return true;
}

QJsonObject SchematicItem::pinPadMappingToJson() const {
    QJsonObject map;
    for (auto it = m_pinPadMapping.begin(); it != m_pinPadMapping.end(); ++it) {
        map[it.key()] = it.value();
    }
    return map;
}

void SchematicItem::loadPinPadMappingFromJson(const QJsonObject& json) {
    m_pinPadMapping.clear();
    if (!json.contains("pinPadMapping")) return;
    const QJsonObject map = json["pinPadMapping"].toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QString pin = it.key().trimmed();
        const QString pad = it.value().toString().trimmed();
        if (!pin.isEmpty() && !pad.isEmpty()) {
            m_pinPadMapping[pin] = pad;
        }
    }
}
