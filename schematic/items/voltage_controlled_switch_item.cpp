#include "voltage_controlled_switch_item.h"
#include <QPainter>

VoltageControlledSwitchItem::VoltageControlledSwitchItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent),
      m_modelName("MySwitchName"),
      m_ron("0.1"),
      m_roff("1Meg"),
      m_vt("0.5"),
      m_vh("0.1") {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("S1");
    syncParamExpressions();
}

QRectF VoltageControlledSwitchItem::boundingRect() const { return QRectF(-50, -35, 100, 70); }

void VoltageControlledSwitchItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 2));

    // Main terminals
    painter->drawLine(-45, 0, -30, 0);
    painter->drawLine(30, 0, 45, 0);
    painter->drawEllipse(-32, -2, 4, 4);
    painter->drawEllipse(28, -2, 4, 4);

    // Control terminals (aligned to grid)
    painter->drawLine(-15, -30, -15, -10);
    painter->drawLine(15, 30, 15, 10);
    painter->drawEllipse(-17, -32, 4, 4);
    painter->drawEllipse(13, 28, 4, 4);

    // Switch body
    painter->drawLine(-30, 0, 30, 0);
    painter->drawLine(-15, -10, 15, 10);

    drawConnectionPointHighlights(painter);
}

QList<QPointF> VoltageControlledSwitchItem::connectionPoints() const {
    return { QPointF(-45, 0), QPointF(45, 0), QPointF(-15, -30), QPointF(15, 30) };
}

QJsonObject VoltageControlledSwitchItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Voltage Controlled Switch";
    return j;
}

bool VoltageControlledSwitchItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    applyParamExpressions();
    syncParamExpressions();
    return true;
}

SchematicItem* VoltageControlledSwitchItem::clone() const {
    auto* item = new VoltageControlledSwitchItem(pos());
    item->m_modelName = m_modelName;
    item->m_ron = m_ron;
    item->m_roff = m_roff;
    item->m_vt = m_vt;
    item->m_vh = m_vh;
    item->syncParamExpressions();
    return item;
}

void VoltageControlledSwitchItem::setModelName(const QString& name) {
    m_modelName = name.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setRon(const QString& value) {
    m_ron = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setRoff(const QString& value) {
    m_roff = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setVt(const QString& value) {
    m_vt = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::setVh(const QString& value) {
    m_vh = value.trimmed();
    syncParamExpressions();
}

void VoltageControlledSwitchItem::syncParamExpressions() {
    setParamExpression("switch.model_name", m_modelName);
    setParamExpression("switch.ron", m_ron);
    setParamExpression("switch.roff", m_roff);
    setParamExpression("switch.vt", m_vt);
    setParamExpression("switch.vh", m_vh);
}

void VoltageControlledSwitchItem::applyParamExpressions() {
    const QString modelNameExpr = paramExpressions().value("switch.model_name").trimmed();
    if (!modelNameExpr.isEmpty()) m_modelName = modelNameExpr;

    const QString ronExpr = paramExpressions().value("switch.ron").trimmed();
    if (!ronExpr.isEmpty()) m_ron = ronExpr;

    const QString roffExpr = paramExpressions().value("switch.roff").trimmed();
    if (!roffExpr.isEmpty()) m_roff = roffExpr;

    const QString vtExpr = paramExpressions().value("switch.vt").trimmed();
    if (!vtExpr.isEmpty()) m_vt = vtExpr;

    const QString vhExpr = paramExpressions().value("switch.vh").trimmed();
    if (!vhExpr.isEmpty()) m_vh = vhExpr;
}
