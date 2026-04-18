// symbols/ui/symbol_preview_widget.cpp
#include "symbol_preview_widget.h"
#include <QPainter>
#include <QPainterPath>
#include <QJsonArray>
#include <QJsonObject>
#include <QPixmap>
#include <QBuffer>
#include "theme_manager.h"

using namespace Flux::Model;

SymbolPreviewWidget::SymbolPreviewWidget(QWidget* parent, Qt::WindowFlags f)
    : QWidget(parent, f) {
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
}

void SymbolPreviewWidget::setSymbol(const SymbolDefinition& sym) {
    m_symbol = sym;
    update();
}

void SymbolPreviewWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    PCBTheme* theme = ThemeManager::theme();
    QColor bg = (theme && theme->type() == PCBTheme::Light) ? QColor(255, 255, 255) : QColor(30, 30, 35);
    QColor border = theme ? theme->panelBorder() : QColor(100, 100, 100);
    QColor accent = theme ? theme->accentColor() : QColor(59, 130, 246);
    QColor fg = theme ? theme->textColor() : Qt::white;

    if (m_staticMode) {
        // Draw centered box with rounded corners for tooltip mode
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 12, 12);
        painter.setPen(QPen(border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 12, 12);
    } else {
        painter.fillRect(rect(), bg);
    }

    if (m_symbol.name().isEmpty() && m_symbol.effectivePrimitives().isEmpty()) return;

    // Draw Symbol Name/Ref if available
    if (m_staticMode) {
        painter.setPen(accent);
        painter.setFont(QFont("Inter", 10, QFont::Bold));
        painter.drawText(rect().adjusted(15, 12, -15, -10), Qt::AlignTop | Qt::AlignLeft, m_symbol.name());
    }

    // Filter primitives for Unit 1 and BodyStyle 1 (consistent with browser preview)
    QList<SymbolPrimitive> filtered;
    for (const auto& prim : m_symbol.effectivePrimitives()) {
        if (prim.unit() != 0 && prim.unit() != 1) continue;
        if (prim.bodyStyle() != 0 && prim.bodyStyle() != 1) continue;
        filtered.append(prim);
    }

    if (filtered.isEmpty()) return;

    // Calculate bounding box
    qreal minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    auto updateBounds = [&](qreal x, qreal y) {
        minX = qMin(minX, x); minY = qMin(minY, y);
        maxX = qMax(maxX, x); maxY = qMax(maxY, y);
    };

    for (const auto& prim : filtered) {
        switch (prim.type) {
            case SymbolPrimitive::Line:
                updateBounds(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
                updateBounds(prim.data["x2"].toDouble(), prim.data["y2"].toDouble());
                break;
            case SymbolPrimitive::Rect:
            case SymbolPrimitive::Arc:
            case SymbolPrimitive::Image: {
                qreal x = prim.data["x"].toDouble();
                qreal y = prim.data["y"].toDouble();
                qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
                qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
                updateBounds(x, y); updateBounds(x + w, y + h);
                updateBounds(x, y + h); updateBounds(x + w, y);
                break;
            }
            case SymbolPrimitive::Circle: {
                qreal cx = prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble();
                qreal cy = prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble();
                qreal r = prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble();
                updateBounds(cx - r, cy - r); updateBounds(cx + r, cy + r);
                break;
            }
            case SymbolPrimitive::Polygon: {
                for (const auto& v : prim.data["points"].toArray()) {
                    updateBounds(v.toObject()["x"].toDouble(), v.toObject()["y"].toDouble());
                }
                break;
            }
            case SymbolPrimitive::Pin:
            case SymbolPrimitive::Text:
                updateBounds(prim.data["x"].toDouble(), prim.data["y"].toDouble());
                if (prim.type == SymbolPrimitive::Pin) {
                    // Include pin lead end
                    qreal x = prim.data["x"].toDouble();
                    qreal y = prim.data["y"].toDouble();
                    qreal len = prim.data["length"].toDouble();
                    QString dir = prim.data["orientation"].toString();
                    if (dir == "Right") updateBounds(x + len, y);
                    else if (dir == "Left") updateBounds(x - len, y);
                    else if (dir == "Up") updateBounds(x, y - len);
                    else if (dir == "Down") updateBounds(x, y + len);
                }
                break;
            case SymbolPrimitive::Bezier:
                for (int i = 1; i <= 4; ++i) updateBounds(prim.data[QString("x%1").arg(i)].toDouble(), prim.data[QString("y%1").arg(i)].toDouble());
                break;
            default: break;
        }
    }

    QRectF symbolRect(minX, minY, maxX - minX, maxY - minY);
    if (symbolRect.isEmpty() || symbolRect.width() < 1) symbolRect.setWidth(10);
    if (symbolRect.height() < 1) symbolRect.setHeight(10);

    QRectF drawArea = rect().adjusted(20, m_staticMode ? 40 : 20, -20, -20);
    if (drawArea.width() <= 0 || drawArea.height() <= 0) return;

    qreal scaleX = drawArea.width() / symbolRect.width();
    qreal scaleY = drawArea.height() / symbolRect.height();
    qreal scale = qMin(scaleX, scaleY);
    if (scale > 5.0) scale = 5.0;

    painter.save();
    painter.translate(drawArea.center());
    painter.scale(scale, scale); 
    painter.translate(-symbolRect.center());

    painter.setPen(QPen(fg, 1.2 / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const auto& prim : filtered) {
        drawPrimitive(&painter, prim, fg, scale);
    }
    
    // Also draw Anchor if in editor mode (non-static)
    if (!m_staticMode) {
        painter.setPen(QPen(accent, 1.0 / scale, Qt::DotLine));
        painter.drawLine(minX - 5, 0, maxX + 5, 0);
        painter.drawLine(0, minY - 5, 0, maxY + 5);
    }

    painter.restore();
}

void SymbolPreviewWidget::drawPrimitive(QPainter* p, const SymbolPrimitive& prim, const QColor& fg, qreal scale) {
    switch (prim.type) {
        case SymbolPrimitive::Line: {
            p->drawLine(QPointF(prim.data["x1"].toDouble(), prim.data["y1"].toDouble()),
                        QPointF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble()));
            break;
        }
        case SymbolPrimitive::Rect: {
            qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
            qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
            QRectF r(prim.data["x"].toDouble(), prim.data["y"].toDouble(), w, h);
            if (prim.data["filled"].toBool()) {
                p->setBrush(fg); p->drawRect(r); p->setBrush(Qt::NoBrush);
            } else {
                p->drawRect(r);
            }
            break;
        }
        case SymbolPrimitive::Circle: {
            qreal cx = prim.data.contains("centerX") ? prim.data["centerX"].toDouble() : prim.data["cx"].toDouble();
            qreal cy = prim.data.contains("centerY") ? prim.data["centerY"].toDouble() : prim.data["cy"].toDouble();
            qreal r = prim.data.contains("radius") ? prim.data["radius"].toDouble() : prim.data["r"].toDouble();
            if (prim.data["filled"].toBool()) {
                p->setBrush(fg); p->drawEllipse(QPointF(cx, cy), r, r); p->setBrush(Qt::NoBrush);
            } else {
                p->drawEllipse(QPointF(cx, cy), r, r);
            }
            break;
        }
        case SymbolPrimitive::Arc: {
            qreal x = prim.data["x"].toDouble();
            qreal y = prim.data["y"].toDouble();
            qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
            qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
            int startIdx = prim.data["startAngle"].toInt();
            int spanIdx = prim.data["spanAngle"].toInt();
            p->drawArc(QRectF(x, y, w, h), startIdx, spanIdx);
            break;
        }
        case SymbolPrimitive::Polygon: {
            QPolygonF poly;
            for (const auto& v : prim.data["points"].toArray()) {
                poly << QPointF(v.toObject()["x"].toDouble(), v.toObject()["y"].toDouble());
            }
            if (prim.data["filled"].toBool()) {
                p->setBrush(fg); p->drawPolygon(poly); p->setBrush(Qt::NoBrush);
            } else {
                p->drawPolygon(poly);
            }
            break;
        }
        case SymbolPrimitive::Pin: {
            qreal x = prim.data["x"].toDouble();
            qreal y = prim.data["y"].toDouble();
            qreal len = prim.data["length"].toDouble();
            QString dir = prim.data["orientation"].toString();
            QPointF p1(x, y);
            QPointF p2 = p1;
            if (dir == "Right") p2.rx() += len;
            else if (dir == "Left") p2.rx() -= len;
            else if (dir == "Up") p2.ry() -= len;
            else if (dir == "Down") p2.ry() += len;
            p->drawLine(p1, p2);
            // Draw pin name/number very simply
            p->save();
            p->setFont(QFont("Inter", 6));
            p->drawText(p1 + QPointF(2, -2), prim.data["number"].toString());
            p->restore();
            break;
        }
        case SymbolPrimitive::Text: {
            p->save();
            p->setFont(QFont("Inter", 8));
            p->drawText(QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble()), prim.data["text"].toString());
            p->restore();
            break;
        }
        case SymbolPrimitive::Bezier: {
            QPainterPath path;
            path.moveTo(prim.data["x1"].toDouble(), prim.data["y1"].toDouble());
            path.cubicTo(QPointF(prim.data["x2"].toDouble(), prim.data["y2"].toDouble()),
                         QPointF(prim.data["x3"].toDouble(), prim.data["y3"].toDouble()),
                         QPointF(prim.data["x4"].toDouble(), prim.data["y4"].toDouble()));
            p->drawPath(path);
            break;
        }
        case SymbolPrimitive::Image: {
            QString base64 = prim.data["base64"].toString();
            if (!base64.isEmpty()) {
                QImage img;
                img.loadFromData(QByteArray::fromBase64(base64.toLatin1()));
                if (!img.isNull()) {
                    qreal x = prim.data["x"].toDouble();
                    qreal y = prim.data["y"].toDouble();
                    qreal w = prim.data.contains("width") ? prim.data["width"].toDouble() : prim.data["w"].toDouble();
                    qreal h = prim.data.contains("height") ? prim.data["height"].toDouble() : prim.data["h"].toDouble();
                    p->drawImage(QRectF(x, y, w, h), img);
                }
            }
            break;
        }
        default: break;
    }
}
