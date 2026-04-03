#include "pcb_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QDebug>
#include <QStyleOptionGraphicsItem>
#include <QPainterPath>

void PCBItem::drawSelectionGlow(QPainter* painter) const {
    if (!isSelected()) return;

    painter->save();
    
    QColor selColor = QColor(0, 200, 255); // Default cyan-blue tech glow
    if (ThemeManager::theme()) {
        selColor = ThemeManager::theme()->selectionBox();
    }

    // Draw a thick semi-transparent halo following the exact shape of the item
    QPainterPath p = shape();
    if (p.isEmpty()) p.addRect(boundingRect());

    // Outer Glow (faint)
    QColor outerGlow = selColor;
    outerGlow.setAlpha(40);
    painter->setPen(QPen(outerGlow, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(p);

    // Inner Glow (stronger)
    QColor innerGlow = selColor;
    innerGlow.setAlpha(120);
    painter->setPen(QPen(innerGlow, 0.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(p);

    painter->restore();
}

PCBItem::PCBItem(QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_id(QUuid::createUuid())
    , m_layer(0)
    , m_isLocked(false)
    , m_height(0.0)
    , m_modelPath("")
    , m_modelScale(1.0)
    , m_modelOffset(0.0f, 0.0f, 0.0f)
    , m_modelRotation(0.0f, 0.0f, 0.0f)
    , m_modelScale3D(1.0f, 1.0f, 1.0f)
{
    updateFlags();
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
}

QVariant PCBItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemParentHasChanged || change == ItemParentChange) {
        updateFlags();
    }
    return QGraphicsObject::itemChange(change, value);
}

QJsonObject PCBItem::toJson() const {
    QJsonObject json;
    json["id"] = m_id.toString();
    json["name"] = m_name;
    json["netName"] = m_netName;
    json["layer"] = m_layer;
    json["height"] = m_height;
    json["modelPath"] = m_modelPath;
    json["modelScale"] = m_modelScale;
    QJsonObject modelOffset;
    modelOffset["x"] = m_modelOffset.x();
    modelOffset["y"] = m_modelOffset.y();
    modelOffset["z"] = m_modelOffset.z();
    json["modelOffset"] = modelOffset;
    QJsonObject modelRotation;
    modelRotation["x"] = m_modelRotation.x();
    modelRotation["y"] = m_modelRotation.y();
    modelRotation["z"] = m_modelRotation.z();
    json["modelRotation"] = modelRotation;
    QJsonObject modelScale3D;
    modelScale3D["x"] = m_modelScale3D.x();
    modelScale3D["y"] = m_modelScale3D.y();
    modelScale3D["z"] = m_modelScale3D.z();
    json["modelScale3D"] = modelScale3D;
    json["x"] = pos().x();
    json["y"] = pos().y();
    return json;
}

bool PCBItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) {
        m_id = QUuid::fromString(json["id"].toString());
    }
    if (json.contains("height")) {
        m_height = json["height"].toDouble();
    }
    if (json.contains("modelPath")) {
        m_modelPath = json["modelPath"].toString();
    }
    if (json.contains("modelScale")) {
        m_modelScale = json["modelScale"].toDouble(1.0);
    }
    if (json.contains("modelOffset")) {
        const QJsonObject off = json["modelOffset"].toObject();
        m_modelOffset = QVector3D(off["x"].toDouble(), off["y"].toDouble(), off["z"].toDouble());
    }
    if (json.contains("modelRotation")) {
        const QJsonObject rot = json["modelRotation"].toObject();
        m_modelRotation = QVector3D(rot["x"].toDouble(), rot["y"].toDouble(), rot["z"].toDouble());
    }
    if (json.contains("modelScale3D")) {
        const QJsonObject scl = json["modelScale3D"].toObject();
        m_modelScale3D = QVector3D(scl["x"].toDouble(1.0), scl["y"].toDouble(1.0), scl["z"].toDouble(1.0));
    } else {
        m_modelScale3D = QVector3D(float(m_modelScale), float(m_modelScale), float(m_modelScale));
    }
    if (json.contains("name")) {
        m_name = json["name"].toString();
    }
    if (json.contains("netName")) {
        m_netName = json["netName"].toString();
    }
    if (json.contains("layer")) {
        m_layer = json["layer"].toInt();
    }
    if (json.contains("x") && json.contains("y")) {
        setPos(json["x"].toDouble(), json["y"].toDouble());
    }
    return true;
}
