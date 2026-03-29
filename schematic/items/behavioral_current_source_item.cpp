#include "behavioral_current_source_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"

BehavioralCurrentSourceItem::BehavioralCurrentSourceItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("B1");
    setValue("I=0");
    setParamExpression("bi.arrow_direction", "up");
    createLabels(QPointF(30, -15), QPointF(30, 15));
}

QString BehavioralCurrentSourceItem::itemTypeName() const {
    return (m_arrowDirection == ArrowUp) ? "bi2" : "Current_Source_Behavioral";
}

void BehavioralCurrentSourceItem::setValue(const QString& value) {
    QString v = value.trimmed();
    if (v.isEmpty()) v = "I=0";
    if (!v.startsWith("I=", Qt::CaseInsensitive)) v = "I=" + v;
    SchematicItem::setValue(v);
}

void BehavioralCurrentSourceItem::setArrowDirection(ArrowDirection direction) {
    if (m_arrowDirection == direction) return;
    m_arrowDirection = direction;
    setParamExpression("bi.arrow_direction", m_arrowDirection == ArrowUp ? "up" : "down");
    update();
}

QRectF BehavioralCurrentSourceItem::boundingRect() const { return QRectF(-30, -50, 60, 100); }

void BehavioralCurrentSourceItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    PCBTheme* theme = ThemeManager::theme();
    painter->setPen(QPen(theme ? theme->schematicLine() : Qt::white, 2));
    painter->setBrush(Qt::NoBrush);

    // Leads
    painter->drawLine(0, -50, 0, -25);
    painter->drawLine(0, 25, 0, 50);

    // Circle body
    painter->drawEllipse(QPointF(0, 0), 20, 20);

    // Arrow (current source)
    if (m_arrowDirection == ArrowUp) {
        painter->drawLine(0, 10, 0, -6);
        painter->drawLine(0, -6, -5, 0);
        painter->drawLine(0, -6, 5, 0);
    } else {
        painter->drawLine(0, -10, 0, 6);
        painter->drawLine(0, 6, -5, 0);
        painter->drawLine(0, 6, 5, 0);
    }

    // Pin markers
    painter->drawEllipse(QPointF(0, -45), 4, 4);
    painter->drawEllipse(QPointF(0, 45), 4, 4);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> BehavioralCurrentSourceItem::connectionPoints() const {
    return { QPointF(0, -45), QPointF(0, 45) };
}

QJsonObject BehavioralCurrentSourceItem::toJson() const {
    QJsonObject json = SchematicItem::toJson();
    json["type"] = itemTypeName();
    json["arrowDirection"] = (m_arrowDirection == ArrowUp) ? "up" : "down";
    return json;
}

bool BehavioralCurrentSourceItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    const QString arrow = json.value("arrowDirection").toString().trimmed();
    if (arrow.compare("down", Qt::CaseInsensitive) == 0) {
        setArrowDirection(ArrowDown);
    } else if (arrow.compare("up", Qt::CaseInsensitive) == 0) {
        setArrowDirection(ArrowUp);
    } else {
        const QString t = json.value("type").toString().trimmed();
        if (t.compare("bi2", Qt::CaseInsensitive) == 0) {
            setArrowDirection(ArrowUp);
        } else {
            setArrowDirection(ArrowDown);
        }
    }
    setValue(value());
    return true;
}

SchematicItem* BehavioralCurrentSourceItem::clone() const {
    auto* item = new BehavioralCurrentSourceItem(pos());
    item->setValue(value());
    item->setArrowDirection(m_arrowDirection);
    return item;
}
