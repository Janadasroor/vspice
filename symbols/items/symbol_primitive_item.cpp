#include "symbol_primitive_item.h"
#include "../../core/theme_manager.h"
#include "../../core/text_resolver.h"
#include <QPainterPathStroker>
#include <QJsonArray>
#include <QDate>
#include <QFontMetricsF>

namespace Flux {
namespace Item {

namespace {

int normalizeArcAngle16(int angle) {
    // Accept legacy degrees or modern Qt 1/16 degree units.
    return (qAbs(angle) <= 360) ? angle * 16 : angle;
}

int readArcAngle16(const QJsonObject& data, const char* primaryKey, const char* aliasKey, int defaultValue16) {
    if (data.contains(primaryKey)) {
        return normalizeArcAngle16(data.value(primaryKey).toInt(defaultValue16));
    }
    if (data.contains(aliasKey)) {
        return normalizeArcAngle16(data.value(aliasKey).toInt(defaultValue16));
    }
    return defaultValue16;
}

} // namespace

SymbolPrimitiveItem::SymbolPrimitiveItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_model(model) {
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
}

void SymbolPrimitiveItem::paintSelectionBorder(QPainter* painter, const QStyleOptionGraphicsItem* option) const {
    if (option->state & QStyle::State_Selected) {
        painter->save();
        painter->setPen(QPen(QColor(0, 122, 204), 1.0, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect());
        painter->restore();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolLineItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolLineItem::SymbolLineItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolLineItem::boundingRect() const {
    qreal x1 = m_model.data.value("x1").toDouble();
    qreal y1 = m_model.data.value("y1").toDouble();
    qreal x2 = m_model.data.value("x2").toDouble();
    qreal y2 = m_model.data.value("y2").toDouble();
    return QRectF(QPointF(x1, y1), QPointF(x2, y2)).normalized().adjusted(-2, -2, 2, 2);
}

QPainterPath SymbolLineItem::shape() const {
    QPainterPath path;
    path.moveTo(m_model.data.value("x1").toDouble(), m_model.data.value("y1").toDouble());
    path.lineTo(m_model.data.value("x2").toDouble(), m_model.data.value("y2").toDouble());
    QPainterPathStroker stroker;
    stroker.setWidth(10); // 10px hit area
    return stroker.createStroke(path);
}

void SymbolLineItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    qreal x1 = m_model.data.value("x1").toDouble();
    qreal y1 = m_model.data.value("y1").toDouble();
    qreal x2 = m_model.data.value("x2").toDouble();
    qreal y2 = m_model.data.value("y2").toDouble();

    qreal w = m_model.data.value("lineWidth").toDouble();
    if (w <= 0) w = 1.5;
    
    Qt::PenStyle ps = Qt::SolidLine;
    const QString ls = m_model.data.value("lineStyle").toString();
    if (ls == "Dash")    ps = Qt::DashLine;
    else if (ls == "Dot")     ps = Qt::DotLine;
    else if (ls == "DashDot") ps = Qt::DashDotLine;

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();
    
    painter->setPen(QPen(color, w, ps));
    painter->drawLine(QPointF(x1, y1), QPointF(x2, y2));

    paintSelectionBorder(painter, option);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolRectItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolRectItem::SymbolRectItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolRectItem::boundingRect() const {
    const qreal w = m_model.data.contains("width") ? m_model.data.value("width").toDouble() : m_model.data.value("w").toDouble();
    const qreal h = m_model.data.contains("height") ? m_model.data.value("height").toDouble() : m_model.data.value("h").toDouble();
    return QRectF(m_model.data.value("x").toDouble(), m_model.data.value("y").toDouble(), w, h).adjusted(-2, -2, 2, 2);
}

void SymbolRectItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    const qreal x = m_model.data.value("x").toDouble();
    const qreal y = m_model.data.value("y").toDouble();
    const qreal w = m_model.data.contains("width") ? m_model.data.value("width").toDouble() : m_model.data.value("w").toDouble();
    const qreal h = m_model.data.contains("height") ? m_model.data.value("height").toDouble() : m_model.data.value("h").toDouble();
    bool filled = m_model.data.value("filled").toBool(false);

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();

    painter->setPen(QPen(color, 1.5));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawRect(QRectF(x, y, w, h));

    paintSelectionBorder(painter, option);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolCircleItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolCircleItem::SymbolCircleItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolCircleItem::boundingRect() const {
    const qreal cx = m_model.data.contains("centerX") ? m_model.data.value("centerX").toDouble() : m_model.data.value("cx").toDouble();
    const qreal cy = m_model.data.contains("centerY") ? m_model.data.value("centerY").toDouble() : m_model.data.value("cy").toDouble();
    const qreal r = m_model.data.contains("radius") ? m_model.data.value("radius").toDouble() : m_model.data.value("r").toDouble();
    return QRectF(cx - r, cy - r, r * 2, r * 2).adjusted(-2, -2, 2, 2);
}

void SymbolCircleItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    const qreal cx = m_model.data.contains("centerX") ? m_model.data.value("centerX").toDouble() : m_model.data.value("cx").toDouble();
    const qreal cy = m_model.data.contains("centerY") ? m_model.data.value("centerY").toDouble() : m_model.data.value("cy").toDouble();
    const qreal r = m_model.data.contains("radius") ? m_model.data.value("radius").toDouble() : m_model.data.value("r").toDouble();
    bool filled = m_model.data.value("filled").toBool(false);

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();

    painter->setPen(QPen(color, 1.5));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawEllipse(QPointF(cx, cy), r, r);

    paintSelectionBorder(painter, option);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolArcItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolArcItem::SymbolArcItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolArcItem::boundingRect() const {
    const qreal x = m_model.data.value("x").toDouble();
    const qreal y = m_model.data.value("y").toDouble();
    const qreal w = m_model.data.contains("width") ? m_model.data.value("width").toDouble() : m_model.data.value("w").toDouble();
    const qreal h = m_model.data.contains("height") ? m_model.data.value("height").toDouble() : m_model.data.value("h").toDouble();
    return QRectF(x, y, w, h).adjusted(-2, -2, 2, 2);
}

QPainterPath SymbolArcItem::shape() const {
    const qreal x = m_model.data.value("x").toDouble();
    const qreal y = m_model.data.value("y").toDouble();
    const qreal w = m_model.data.contains("width") ? m_model.data.value("width").toDouble() : m_model.data.value("w").toDouble();
    const qreal h = m_model.data.contains("height") ? m_model.data.value("height").toDouble() : m_model.data.value("h").toDouble();
    const int sa = readArcAngle16(m_model.data, "startAngle", "start", 0);
    const int span = readArcAngle16(m_model.data, "spanAngle", "span", 180 * 16);

    QPainterPath path;
    QRectF rect(x, y, w, h);
    path.arcMoveTo(rect, sa / 16.0);
    path.arcTo(rect, sa / 16.0, span / 16.0);
    
    QPainterPathStroker stroker;
    stroker.setWidth(10);
    return stroker.createStroke(path);
}

void SymbolArcItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    const qreal x = m_model.data.value("x").toDouble();
    const qreal y = m_model.data.value("y").toDouble();
    const qreal w = m_model.data.contains("width") ? m_model.data.value("width").toDouble() : m_model.data.value("w").toDouble();
    const qreal h = m_model.data.contains("height") ? m_model.data.value("height").toDouble() : m_model.data.value("h").toDouble();
    const int sa = readArcAngle16(m_model.data, "startAngle", "start", 0);
    const int span = readArcAngle16(m_model.data, "spanAngle", "span", 180 * 16);

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();

    painter->setPen(QPen(color, 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawArc(QRectF(x, y, w, h), sa, span);

    paintSelectionBorder(painter, option);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolPolygonItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolPolygonItem::SymbolPolygonItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolPolygonItem::boundingRect() const {
    QJsonArray pts = m_model.data["points"].toArray();
    if (pts.isEmpty()) return QRectF();
    qreal x1 = 1e9, y1 = 1e9, x2 = -1e9, y2 = -1e9;
    for (auto v : pts) {
        QJsonObject o = v.toObject();
        qreal x = o["x"].toDouble();
        qreal y = o["y"].toDouble();
        x1 = qMin(x1, x); y1 = qMin(y1, y);
        x2 = qMax(x2, x); y2 = qMax(y2, y);
    }
    return QRectF(x1, y1, x2 - x1, y2 - y1).adjusted(-2, -2, 2, 2);
}

void SymbolPolygonItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    QJsonArray ptsArr = m_model.data["points"].toArray();
    QPolygonF poly;
    for (auto v : ptsArr) {
        QJsonObject o = v.toObject();
        poly << QPointF(o["x"].toDouble(), o["y"].toDouble());
    }
    bool filled = m_model.data.value("filled").toBool(false);

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();

    painter->setPen(QPen(color, 1.5));
    if (filled) painter->setBrush(color);
    else painter->setBrush(Qt::NoBrush);

    painter->drawPolygon(poly);

    paintSelectionBorder(painter, option);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolTextItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolTextItem::SymbolTextItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolTextItem::boundingRect() const {
    QString rawText = m_model.data.value("text").toString();
    QMap<QString, QString> vars;
    vars["REFERENCE"] = m_symbolRef.isEmpty() ? "U?" : m_symbolRef;
    vars["VALUE"]     = m_symbolVal.isEmpty() ? "Value" : m_symbolVal;
    vars["NAME"]      = m_symbolName;
    vars["DATE"]      = QDate::currentDate().toString(Qt::ISODate);
    
    QString resolved = TextResolver::resolve(rawText, vars);
    int fs = m_model.data.value("fontSize").toInt(10);
    if (fs <= 0) fs = 10;
    QFont font("SansSerif", fs);
    QFontMetricsF fm(font);
    QRectF r = fm.boundingRect(resolved);
    
    const qreal anchorX = m_model.data.value("x").toDouble();
    const qreal anchorY = m_model.data.value("y").toDouble();
    
    return r.translated(anchorX, anchorY);
}

void SymbolTextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    QString rawText = m_model.data.value("text").toString();
    QMap<QString, QString> vars;
    vars["REFERENCE"] = m_symbolRef.isEmpty() ? "U?" : m_symbolRef;
    vars["VALUE"]     = m_symbolVal.isEmpty() ? "Value" : m_symbolVal;
    vars["NAME"]      = m_symbolName;
    vars["DATE"]      = QDate::currentDate().toString(Qt::ISODate);
    
    QString resolved = TextResolver::resolve(rawText, vars);
    const qreal anchorX = m_model.data.value("x").toDouble();
    const qreal anchorY = m_model.data.value("y").toDouble();
    int fs = m_model.data.value("fontSize").toInt(10);
    if (fs <= 0) fs = 10;
    
    QFont font("SansSerif", fs);
    painter->setFont(font);
    
    QColor c = Qt::white;
    if (ThemeManager::theme()) c = ThemeManager::theme()->textColor();
    if (m_model.data.contains("color")) {
        c = QColor(m_model.data["color"].toString());
    }
    painter->setPen(c);

    QFontMetricsF fm(font);
    QRectF tb = fm.boundingRect(resolved);
    
    qreal dx = 0.0;
    const QString hAlign = m_model.data.value("hAlign").toString("left").toLower();
    const QString vAlign = m_model.data.value("vAlign").toString("baseline").toLower();
    
    if (hAlign == "center") dx = -tb.width() * 0.5;
    else if (hAlign == "right") dx = -tb.width();
    
    qreal py = anchorY;
    if (vAlign == "center") py += tb.height() * 0.5;
    else if (vAlign == "top") py += fm.ascent();
    
    painter->drawText(QPointF(anchorX + dx, py), resolved);

    paintSelectionBorder(painter, option);
}

void SymbolTextItem::setSymbolContext(const QString& name, const QString& ref, const QString& val) {
    m_symbolName = name;
    m_symbolRef = ref;
    m_symbolVal = val;
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolPinItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolPinItem::SymbolPinItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolPinItem::boundingRect() const {
    const qreal x = m_model.data.value("x").toDouble();
    const qreal y = m_model.data.value("y").toDouble();
    qreal len = m_model.data.value("length").toDouble(15.0);
    const QString orient = m_model.data.value("orientation").toString("Right");
    
    QRectF r(x, y, 0, 0);
    if      (orient == "Left")  r = QRectF(x - len, y - 10, len, 20);
    else if (orient == "Right") r = QRectF(x, y - 10, len, 20);
    else if (orient == "Up")    r = QRectF(x - 10, y - len, 20, len);
    else if (orient == "Down")  r = QRectF(x - 10, y, 20, len);
    
    return r.adjusted(-10, -10, 10, 10);
}

QPainterPath SymbolPinItem::shape() const {
    const qreal x = m_model.data.value("x").toDouble();
    const qreal y = m_model.data.value("y").toDouble();
    qreal len = m_model.data.value("length").toDouble(15.0);
    const QString orient = m_model.data.value("orientation").toString("Right");
    
    QPainterPath path;
    if      (orient == "Left")  path.addRect(x - len, y - 2, len, 4);
    else if (orient == "Right") path.addRect(x, y - 2, len, 4);
    else if (orient == "Up")    path.addRect(x - 2, y - len, 4, len);
    else if (orient == "Down")  path.addRect(x - 2, y, 4, len);
    
    return path;
}

void SymbolPinItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    const qreal px = m_model.data.value("x").toDouble();
    const qreal py = m_model.data.value("y").toDouble();
    qreal len = m_model.data.value("length").toDouble(15.0);
    const QString orient = m_model.data.value("orientation").toString("Right");
    bool isVisible = m_model.data.value("visible").toBool(true);

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();
    
    if (!isVisible) painter->setOpacity(0.3);

    QPointF endPt;
    if      (orient == "Left")  endPt = QPointF(px - len, py);
    else if (orient == "Up")    endPt = QPointF(px, py - len);
    else if (orient == "Down")  endPt = QPointF(px, py + len);
    else                        endPt = QPointF(px + len, py);

    painter->setPen(QPen(color, 2.0, isVisible ? Qt::SolidLine : Qt::DashLine));
    painter->drawLine(QPointF(px, py), endPt);

    QString shapeStr = m_model.data.value("pinShape").toString("Line");
    drawPinShape(painter, endPt, orient, shapeStr, color);

    QColor dotBrush = ThemeManager::theme() ? ThemeManager::theme()->schematicComponent() : QColor(12, 12, 12);
    painter->setBrush(dotBrush);
    painter->setPen(QPen(color, 2.0));
    painter->drawEllipse(QPointF(px, py), 2.5, 2.5);

    QColor textColor = color;
    if (ThemeManager::theme()) textColor = ThemeManager::theme()->accentColor().lighter(120);

    QJsonValue numVal = m_model.data["number"];
    if (numVal.isUndefined()) numVal = m_model.data["num"];
    QString numStr = numVal.isString() ? numVal.toString() : QString::number(numVal.toInt());
    QString stacked = m_model.data.value("stackedNumbers").toString();
    if (!stacked.isEmpty()) numStr += QString(" [+%1]").arg(stacked.split(",", Qt::SkipEmptyParts).size());

    if (!m_model.data.value("hideNum").toBool()) {
        int nsz = m_model.data.value("numSize").toInt(7);
        painter->setFont(QFont("Monospace", nsz > 0 ? nsz : 7));
        painter->setPen(textColor);
        QPointF numPos = (QPointF(px, py) + endPt) / 2.0 + QPointF(0, -2);
        painter->drawText(numPos, numStr);
    }

    if (!m_model.data.value("hideName").toBool()) {
        QString nameStr = m_model.data.value("name").toString();
        int asz = m_model.data.value("nameSize").toInt(7);
        painter->setFont(QFont("SansSerif", asz > 0 ? asz : 7));
        painter->setPen(textColor);
        QPointF namePos = endPt + QPointF(5, 5);
        painter->drawText(namePos, nameStr);
    }

    paintSelectionBorder(painter, option);
}

void SymbolPinItem::drawPinShape(QPainter* painter, const QPointF& endPt, const QString& orientation, const QString& shape, const QColor& color) {
    if (shape == "Inverted" || shape == "Inverted Clock" || shape == "Falling Edge Clock") {
        qreal cr = 3.0;
        painter->setBrush(ThemeManager::theme() ? ThemeManager::theme()->panelBackground() : Qt::black);
        painter->setPen(QPen(color, 1.5));
        painter->drawEllipse(endPt, cr, cr);
    }
    
    if (shape == "Clock" || shape == "Inverted Clock" || shape == "Falling Edge Clock") {
        QPointF p1, p2, p3;
        qreal w = 4.0;
        QPointF wedgeBase = endPt;

        if (shape != "Clock") {
            qreal offset = 6.0;
            if      (orientation == "Left")  wedgeBase.setX(endPt.x() + offset);
            else if (orientation == "Right") wedgeBase.setX(endPt.x() - offset);
            else if (orientation == "Up")    wedgeBase.setY(endPt.y() + offset);
            else                             wedgeBase.setY(endPt.y() - offset);
        }

        if (orientation == "Left" || orientation == "Right") {
            qreal dir = (orientation == "Left") ? 1 : -1;
            p1 = wedgeBase + QPointF(0, -w);
            p2 = wedgeBase + QPointF(dir * w, 0);
            p3 = wedgeBase + QPointF(0, w);
        } else {
            qreal dir = (orientation == "Up") ? 1 : -1;
            p1 = wedgeBase + QPointF(-w, 0);
            p2 = wedgeBase + QPointF(0, dir * w);
            p3 = wedgeBase + QPointF(w, 0);
        }
        
        QPainterPath wp; wp.moveTo(p1); wp.lineTo(p2); wp.lineTo(p3);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(color, 1.5));
        painter->drawPath(wp);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolBezierItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolBezierItem::SymbolBezierItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolBezierItem::boundingRect() const {
    QPainterPath p;
    p.moveTo(m_model.data["x1"].toDouble(), m_model.data["y1"].toDouble());
    p.cubicTo(m_model.data["x2"].toDouble(), m_model.data["y2"].toDouble(),
              m_model.data["x3"].toDouble(), m_model.data["y3"].toDouble(),
              m_model.data["x4"].toDouble(), m_model.data["y4"].toDouble());
    return p.boundingRect().adjusted(-2, -2, 2, 2);
}

QPainterPath SymbolBezierItem::shape() const {
    QPainterPath p;
    p.moveTo(m_model.data["x1"].toDouble(), m_model.data["y1"].toDouble());
    p.cubicTo(m_model.data["x2"].toDouble(), m_model.data["y2"].toDouble(),
              m_model.data["x3"].toDouble(), m_model.data["y3"].toDouble(),
              m_model.data["x4"].toDouble(), m_model.data["y4"].toDouble());
    QPainterPathStroker stroker;
    stroker.setWidth(10);
    return stroker.createStroke(p);
}

void SymbolBezierItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    QPainterPath p;
    p.moveTo(m_model.data["x1"].toDouble(), m_model.data["y1"].toDouble());
    p.cubicTo(m_model.data["x2"].toDouble(), m_model.data["y2"].toDouble(),
              m_model.data["x3"].toDouble(), m_model.data["y3"].toDouble(),
              m_model.data["x4"].toDouble(), m_model.data["y4"].toDouble());

    QColor color = Qt::white;
    if (ThemeManager::theme()) color = ThemeManager::theme()->schematicLine();

    painter->setPen(QPen(color, 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(p);

    paintSelectionBorder(painter, option);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SymbolImageItem
// ─────────────────────────────────────────────────────────────────────────────

SymbolImageItem::SymbolImageItem(const Model::SymbolPrimitive& model, QGraphicsItem* parent)
    : SymbolPrimitiveItem(model, parent) {}

QRectF SymbolImageItem::boundingRect() const {
    qreal x = m_model.data["x"].toDouble();
    qreal y = m_model.data["y"].toDouble();
    qreal w = m_model.data.value("width").toDouble(m_model.data.value("w").toDouble());
    qreal h = m_model.data.value("height").toDouble(m_model.data.value("h").toDouble());
    return QRectF(x, y, w, h);
}

void SymbolImageItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(widget);
    QStyleOptionGraphicsItem opt = *option;
    prepareOption(&opt);

    if (!m_pixmapLoaded) {
        QString base64 = m_model.data["image"].toString();
        m_pixmap.loadFromData(QByteArray::fromBase64(base64.toLatin1()));
        m_pixmapLoaded = true;
    }

    painter->drawPixmap(boundingRect(), m_pixmap, m_pixmap.rect());

    paintSelectionBorder(painter, option);
}

} // namespace Item
} // namespace Flux
