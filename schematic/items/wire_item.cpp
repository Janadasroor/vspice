#include "wire_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

WireItem::WireItem(QPointF start, QPointF end, QGraphicsItem *parent)
    : SchematicItem(parent)
    , m_wireType(SignalWire) {
    m_points.append(start);
    m_points.append(end);
    setPos(0, 0); // Force origin to zero
    setFlag(QGraphicsItem::ItemIsMovable, false); // Points move, not origin
    updatePen();
}

WireItem::WireItem(WireType type, QPointF start, QPointF end, QGraphicsItem *parent)
    : SchematicItem(parent)
    , m_wireType(type) {
    m_points.append(start);
    m_points.append(end);
    setPos(0, 0);
    setFlag(QGraphicsItem::ItemIsMovable, false);
    updatePen();
}

void WireItem::updatePen() {
    PCBTheme* theme = ThemeManager::theme();
    QColor wireColor;
    qreal wireWidth;

    switch (m_wireType) {
    case PowerWire:
        wireColor = theme->powerWire();
        wireWidth = 4.5;
        break;
    case SignalWire:
    default:
        wireColor = theme->signalWire();
        wireWidth = 2.25;
        break;
    }
    m_pen = QPen(wireColor, wireWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void WireItem::addJunction(const QPointF& point) {
    if (!m_junctions.contains(point)) {
        m_junctions.append(point);
        update();
    }
}

void WireItem::setStartPoint(const QPointF& point) {
    if (!m_points.isEmpty()) {
        prepareGeometryChange();
        m_points[0] = point;
        update();
    }
}

void WireItem::setEndPoint(const QPointF& point) {
    if (m_points.size() >= 2) {
        prepareGeometryChange();
        m_points.last() = point;
        update();
    }
}

void WireItem::addSegment(const QPointF& point) {
    prepareGeometryChange();
    m_points.append(point);
    update();
}

void WireItem::removeLastSegment() {
    if (m_points.size() > 2) {
        prepareGeometryChange();
        m_points.removeLast();
        update();
    }
}

void WireItem::setPoints(const QList<QPointF>& points) {
    if (m_points != points) {
        prepareGeometryChange();
        m_points = points;
        update();
    }
}

QRectF WireItem::boundingRect() const {
    if (m_points.isEmpty()) return QRectF();
    QRectF rect(m_points.first(), QSizeF(1, 1));
    for (const QPointF& point : m_points) rect = rect.united(QRectF(point, QSizeF(1, 1)));
    qreal p = qMax(m_pen.widthF() / 2.0 + 1.0, 6.0);
    return rect.adjusted(-p, -p, p, p);
}

QPainterPath WireItem::shape() const {
    if (m_points.size() < 2) return QPainterPath();
    QPainterPath path;
    path.moveTo(m_points.first());
    for (int i = 1; i < m_points.size(); ++i) path.lineTo(m_points[i]);
    QPainterPathStroker stroker;
    stroker.setWidth(6.0);
    return stroker.createStroke(path);
}

void WireItem::setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>&) {
    QString net = pinNet(0);
    if (!net.isEmpty() && nodeVoltages.contains(net)) {
        m_voltage = nodeVoltages[net];
        m_hasVoltage = true;
    } else {
        m_hasVoltage = false;
    }
    update();
}

void WireItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget)
    if (m_points.size() < 2) return;

    PCBTheme* theme = ThemeManager::theme();
    const bool selected = isSelected();
    
    if (selected) {
        painter->setPen(QPen(theme->selectionBox(), m_pen.widthF() + 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (int i = 0; i < m_points.size() - 1; ++i) painter->drawLine(m_points[i], m_points[i+1]);
    }

    QPen mainPen = m_pen;
    if (m_hasVoltage) {
        // Live voltage coloration:
        // 0V or less -> Blue/GND
        // 5V or more -> Bright Red
        // In between -> Gradient
        QColor vColor;
        if (m_voltage <= 0.05) vColor = QColor(0, 120, 255); // Dark Blue GND
        else if (m_voltage >= 4.95) vColor = QColor(255, 0, 50); // Bright Red High
        else {
            // Adaptive gradient for analog signals
            float t = qBound(0.0, (m_voltage) / 5.0, 1.0);
            int r = 0 + (255 - 0) * t;
            int g = 180 * (1.0 - std::abs(t - 0.5) * 2.0); // Green peak at 2.5V
            int b = 255 + (0 - 255) * t;
            vColor = QColor(r, g, b);
        }
        mainPen.setColor(vColor);
        
        // Add a subtle glow for powered wires
        if (m_voltage > 0.1) {
            QPen glowPen = mainPen;
            glowPen.setWidthF(mainPen.widthF() + 2.0);
            QColor gc = vColor;
            gc.setAlpha(60);
            glowPen.setColor(gc);
            painter->setPen(glowPen);
            for (int i = 0; i < m_points.size() - 1; ++i) painter->drawLine(m_points[i], m_points[i+1]);
        }
    }

    painter->setPen(mainPen);
    for (int i = 0; i < m_points.size() - 1; ++i) painter->drawLine(m_points[i], m_points[i+1]);

    painter->setBrush(mainPen.color());
    for (const QPointF& j : m_junctions) painter->drawEllipse(j, 4, 4);
}

QJsonObject WireItem::toJson() const {
    QJsonObject json;
    json["type"] = "Wire";
    json["wireType"] = static_cast<int>(m_wireType);
    QJsonArray pts;
    for (const auto& p : m_points) { QJsonObject o; o["x"] = p.x(); o["y"] = p.y(); pts.append(o); }
    json["points"] = pts;
    return json;
}

bool WireItem::fromJson(const QJsonObject& json) {
    m_points.clear();
    QJsonArray pts = json["points"].toArray();
    for (const auto& v : pts) m_points.append(QPointF(v.toObject()["x"].toDouble(), v.toObject()["y"].toDouble()));
    setPos(0, 0);
    updatePen();
    return true;
}

SchematicItem* WireItem::clone() const {
    WireItem* nw = new WireItem(m_wireType, QPointF(), QPointF());
    nw->setPoints(m_points);
    return nw;
}

QList<QPointF> WireItem::connectionPoints() const {
    if (m_points.isEmpty()) return {};
    return {m_points.first(), m_points.last()};
}
