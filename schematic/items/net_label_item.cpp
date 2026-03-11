#include "net_label_item.h"
#include "flux/core/theme_manager.h"
#include <QPainter>
#include <QFontMetrics>

NetLabelItem::NetLabelItem(QPointF pos, const QString& label, QGraphicsItem* parent, LabelScope scope)
    : SchematicItem(parent), m_scope(scope) {
    setPos(pos);
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    setAcceptHoverEvents(true);
    
    m_font = QFont("Inter", 8, QFont::Medium);
    m_pen = QPen(Qt::white, 1.5);
    
    setValue(label);
    setName("Net Label");
}

void NetLabelItem::setLabelScope(LabelScope scope) {
    m_scope = scope;
    prepareGeometryChange();
    update();
}

QRectF NetLabelItem::boundingRect() const {
    QFontMetrics fm(m_font);
    int w = fm.horizontalAdvance(value());
    int h = fm.height();
    if (m_scope == Global) {
        return QRectF(-2, -h/2 - 2, w + 14, h + 4);
    }
    return QRectF(-2, -h - 2, w + 4, h + 4);
}

QPainterPath NetLabelItem::shape() const {
    QPainterPath path;
    if (m_scope == Global) {
        QString txt = value();
        QFontMetrics fm(m_font);
        int tw = fm.horizontalAdvance(txt);
        int th = fm.height();
        qreal w = tw + 10;
        qreal h = th + 2;
        qreal y = -h/2;
        
        path.moveTo(0, 0);
        path.lineTo(5, y);
        path.lineTo(w, y);
        path.lineTo(w, -y);
        path.lineTo(5, -y);
        path.closeSubpath();
    } else {
        path.addRect(boundingRect());
    }
    return path;
}

void NetLabelItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    PCBTheme* theme = ThemeManager::theme();
    QColor color = theme ? theme->schematicLine() : Qt::white;
    if (isSelected()) color = theme ? theme->selectionBox() : Qt::yellow;
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Draw highlight glow if enabled
    if (m_isHighlighted) {
        painter->save();
        painter->setPen(QPen(QColor(255, 215, 0, 100), 4.0));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(m_scope == Global ? shape() : QPainterPath()); // shape() or boundingRect()
        if (m_scope != Global) painter->drawEllipse(QPointF(0,0), 3, 3);
        painter->restore();
    }

    painter->setPen(QPen(color, 1.2));
    painter->setFont(m_font);
    
    QString txt = value();
    QFontMetrics fm(m_font);
    int tw = fm.horizontalAdvance(txt);
    int th = fm.height();

    if (m_scope == Global) {
        // Draw Global Label Banner
        qreal w = tw + 10;
        qreal h = th + 2;
        qreal y = -h/2;
        
        QPainterPath path;
        path.moveTo(0, 0);
        path.lineTo(5, y);
        path.lineTo(w, y);
        path.lineTo(w, -y);
        path.lineTo(5, -y);
        path.closeSubpath();
        
        painter->drawPath(path);
        painter->drawText(QRectF(5, y, tw, h), Qt::AlignCenter, txt);
    } else {
        // Draw connection point (small dot)
        painter->setBrush(color);
        painter->drawEllipse(QPointF(0, 0), 1.5, 1.5);
        
        // Draw label text
        painter->drawText(QPointF(4, -4), txt);
    }
    
    if (isSelected()) {
        painter->setPen(QPen(color, 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
    }
    
    drawConnectionPointHighlights(painter);
}

QList<QPointF> NetLabelItem::connectionPoints() const {
    return { QPointF(0, 0) };
}

QJsonObject NetLabelItem::toJson() const {
    QJsonObject json;
    json["type"] = "Net Label";
    json["id"] = id().toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["label"] = value();
    json["scope"] = (int)m_scope;
    json["netClass"] = m_netClassName;
    return json;
}

bool NetLabelItem::fromJson(const QJsonObject& json) {
    setId(QUuid::fromString(json["id"].toString()));
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setValue(json["label"].toString());
    m_scope = (LabelScope)json["scope"].toInt();
    m_netClassName = json["netClass"].toString();
    return true;
}

SchematicItem* NetLabelItem::clone() const {
    NetLabelItem* item = new NetLabelItem(pos(), value(), nullptr, m_scope);
    item->setNetClassName(m_netClassName);
    return item;
}
