#include "logic_analyzer_item.h"
#include <QPainter>
#include <QJsonObject>

LogicAnalyzerItem::LogicAnalyzerItem(QPointF pos, QGraphicsItem *parent)
    : SchematicItem(parent), m_channelCount(8) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setReference("LA1");
    setValue("8-Channel");
}

QRectF LogicAnalyzerItem::boundingRect() const {
    double height = (m_channelCount + 2) * 15.0;
    return QRectF(-30, -15, 60, height);
}

void LogicAnalyzerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);
    
    QRectF rect = boundingRect().adjusted(5, 5, -5, -5);
    
    // Body
    QPen pen(Qt::white, 2);
    if (isSelected()) pen.setColor(QColor(0, 120, 255));
    painter->setPen(pen);
    painter->setBrush(QColor(45, 45, 50));
    painter->drawRect(rect);

    // Header Accent
    painter->fillRect(rect.left(), rect.top(), rect.width(), 15, QColor(0, 80, 200));
    painter->setPen(Qt::white);
    painter->setFont(QFont("Inter", 7, QFont::Bold));
    painter->drawText(QRectF(rect.left(), rect.top(), rect.width(), 15), Qt::AlignCenter, "LOGIC");

    // Channels
    painter->setFont(QFont("Inter", 6));
    for (int i = 0; i < m_channelCount; ++i) {
        double y = (i + 1) * 15.0;
        painter->setPen(QPen(QColor(100, 100, 105), 1));
        painter->drawLine(-30, y, -20, y);
        
        painter->setPen(Qt::white);
        painter->drawText(QRectF(-18, y - 7.5, 20, 15), Qt::AlignVCenter, QString("D%1").arg(i));
    }

    drawConnectionPointHighlights(painter);
}

QList<QPointF> LogicAnalyzerItem::connectionPoints() const {
    QList<QPointF> pts;
    for (int i = 0; i < m_channelCount; ++i) {
        pts << QPointF(-30, (i + 1) * 15.0);
    }
    return pts;
}

void LogicAnalyzerItem::setChannelCount(int count) {
    if (m_channelCount != count) {
        prepareGeometryChange();
        m_channelCount = qBound(1, count, 16);
        setValue(QString("%1-Channel").arg(m_channelCount));
        update();
    }
}

QJsonObject LogicAnalyzerItem::toJson() const {
    QJsonObject j = SchematicItem::toJson();
    j["type"] = "Logic Analyzer";
    j["channels"] = m_channelCount;
    return j;
}

bool LogicAnalyzerItem::fromJson(const QJsonObject& json) {
    SchematicItem::fromJson(json);
    if (json.contains("channels")) m_channelCount = json["channels"].toInt();
    return true;
}

SchematicItem* LogicAnalyzerItem::clone() const {
    auto* item = new LogicAnalyzerItem(pos());
    item->setChannelCount(m_channelCount);
    return item;
}
