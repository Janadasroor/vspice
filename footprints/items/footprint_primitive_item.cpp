#include "footprint_primitive_item.h"
#include "../../core/theme_manager.h"
#include <QPainterPathStroker>
#include <QFontMetricsF>

namespace Flux {
namespace Item {

FootprintPrimitiveItem::FootprintPrimitiveItem(const Model::FootprintPrimitive& model, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_model(model) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
}

void FootprintPrimitiveItem::paintSelectionBorder(QPainter* painter, const QStyleOptionGraphicsItem* option) const {
    if (option->state & QStyle::State_Selected) {
        painter->save();
        QColor selectionColor(56, 189, 248, 180);
        QPen selectionPen(selectionColor, 0.0, Qt::DashLine);
        selectionPen.setDashPattern({2.0, 2.0});
        painter->setPen(selectionPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
        painter->restore();
    }
}

// Helper to get color for a footprint layer
static QColor getLayerColor(Model::FootprintPrimitive::Layer layer) {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return Qt::yellow;

    switch (layer) {
        case Model::FootprintPrimitive::Top_Copper: return theme->topCopper();
        case Model::FootprintPrimitive::Bottom_Copper: return theme->bottomCopper();
        case Model::FootprintPrimitive::Top_Silkscreen: return theme->topSilkscreen();
        case Model::FootprintPrimitive::Bottom_Silkscreen: return theme->bottomSilkscreen();
        case Model::FootprintPrimitive::Top_SolderMask: return theme->topSoldermask();
        case Model::FootprintPrimitive::Bottom_SolderMask: return theme->bottomSoldermask();
        case Model::FootprintPrimitive::Top_Courtyard: return QColor(100, 100, 100);
        case Model::FootprintPrimitive::Bottom_Courtyard: return QColor(100, 100, 100);
        case Model::FootprintPrimitive::Top_Fabrication: return QColor(150, 150, 150);
        case Model::FootprintPrimitive::Bottom_Fabrication: return QColor(150, 150, 150);
        default: return Qt::yellow;
    }
}

// --- Line ---
QRectF FootprintLineItem::boundingRect() const {
    qreal x1 = m_model.data.value("x1").toDouble();
    qreal y1 = m_model.data.value("y1").toDouble();
    qreal x2 = m_model.data.value("x2").toDouble();
    qreal y2 = m_model.data.value("y2").toDouble();
    qreal w = m_model.data.value("width").toDouble(0.1);
    return QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized().adjusted(-w, -w, w, w);
}

QPainterPath FootprintLineItem::shape() const {
    QPainterPath path;
    path.moveTo(m_model.data.value("x1").toDouble(), m_model.data.value("y1").toDouble());
    path.lineTo(m_model.data.value("x2").toDouble(), m_model.data.value("y2").toDouble());
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(0.2, m_model.data.value("width").toDouble(0.1)));
    return stroker.createStroke(path);
}

void FootprintLineItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x1 = m_model.data.value("x1").toDouble();
    qreal y1 = m_model.data.value("y1").toDouble();
    qreal x2 = m_model.data.value("x2").toDouble();
    qreal y2 = m_model.data.value("y2").toDouble();
    qreal w = m_model.data.value("width").toDouble(0.1);

    painter->setPen(QPen(getLayerColor(m_model.layer), w, Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));

    paintSelectionBorder(painter, option);
}

// --- Rect ---
QRectF FootprintRectItem::boundingRect() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    return QRectF(x, y, w, h).adjusted(-lw, -lw, lw, lw);
}

void FootprintRectItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    bool filled = m_model.data.value("filled").toBool();

    QColor color = getLayerColor(m_model.layer);
    painter->setPen(QPen(color, lw));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawRect(QRectF(x, y, w, h));
    paintSelectionBorder(painter, option);
}

// --- Circle ---
QRectF FootprintCircleItem::boundingRect() const {
    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    return QRectF(cx - r, cy - r, r * 2, r * 2).adjusted(-lw, -lw, lw, lw);
}

void FootprintCircleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal lw = m_model.data.value("lineWidth").toDouble(0.1);
    bool filled = m_model.data.value("filled").toBool();

    QColor color = getLayerColor(m_model.layer);
    painter->setPen(QPen(color, lw));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawEllipse(QPointF(cx, cy), r, r);
    paintSelectionBorder(painter, option);
}

// --- Arc ---
QRectF FootprintArcItem::boundingRect() const {
    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal lw = m_model.data.value("width").toDouble(0.1);
    return QRectF(cx - r, cy - r, r * 2, r * 2).adjusted(-lw, -lw, lw, lw);
}

QPainterPath FootprintArcItem::shape() const {
    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal start = m_model.data.value("startAngle").toDouble();
    qreal span = m_model.data.value("spanAngle").toDouble();
    
    QPainterPath path;
    path.arcMoveTo(cx - r, cy - r, r * 2, r * 2, start);
    path.arcTo(cx - r, cy - r, r * 2, r * 2, start, span);
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(0.2, m_model.data.value("width").toDouble(0.1)));
    return stroker.createStroke(path);
}

void FootprintArcItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal cx = m_model.data.value("cx").toDouble();
    qreal cy = m_model.data.value("cy").toDouble();
    qreal r = m_model.data.value("radius").toDouble();
    qreal start = m_model.data.value("startAngle").toDouble();
    qreal span = m_model.data.value("spanAngle").toDouble();
    qreal lw = m_model.data.value("width").toDouble(0.1);

    painter->setPen(QPen(getLayerColor(m_model.layer), lw, Qt::SolidLine, Qt::RoundCap));
    painter->drawArc(QRectF(cx - r, cy - r, r * 2, r * 2), start * 16, span * 16);
    paintSelectionBorder(painter, option);
}

// --- Pad ---
QRectF FootprintPadItem::boundingRect() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    return QRectF(x - w/2, y - h/2, w, h).adjusted(-0.1, -0.1, 0.1, 0.1);
}

QPainterPath FootprintPadItem::shape() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    QString shape = m_model.data.value("shape").toString();
    
    QPainterPath path;
    if (shape == "Circle" || shape == "Round") {
        path.addEllipse(QPointF(x, y), w/2, h/2);
    } else {
        path.addRect(x - w/2, y - h/2, w, h);
    }
    return path;
}

void FootprintPadItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal w = m_model.data.value("width").toDouble();
    qreal h = m_model.data.value("height").toDouble();
    QString shape = m_model.data.value("shape").toString();
    QString num = m_model.data.value("number").toString();

    QColor color = getLayerColor(m_model.layer);
    painter->setPen(QPen(Qt::white, 0.05));
    painter->setBrush(color);

    if (shape == "Circle" || shape == "Round") {
        painter->drawEllipse(QPointF(x, y), w/2, h/2);
    } else {
        painter->drawRect(QRectF(x - w/2, y - h/2, w, h));
    }

    // Draw number
    painter->setPen(Qt::black);
    painter->setFont(QFont("Monospace", 1));
    painter->drawText(boundingRect(), Qt::AlignCenter, num);

    paintSelectionBorder(painter, option);
}

// --- Text ---
QRectF FootprintTextItem::boundingRect() const {
    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal h = m_model.data.value("height").toDouble(1.0);
    return QRectF(x, y, h * 5, h); // Approx
}

void FootprintTextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x = m_model.data.value("x").toDouble();
    qreal y = m_model.data.value("y").toDouble();
    qreal h = m_model.data.value("height").toDouble(1.0);
    QString text = m_model.data.value("text").toString();

    painter->setPen(getLayerColor(m_model.layer));
    painter->setFont(QFont("SansSerif", h));
    painter->drawText(QPointF(x, y + h), text);

    paintSelectionBorder(painter, option);
}

} // namespace Item
} // namespace Flux
