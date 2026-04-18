#include "footprint_preview_view.h"
#include "theme_manager.h"
#include "../../pcb/items/pad_item.h"
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QTimer>

using namespace Flux::Model;
using Flux::Model::FootprintPrimitive;

FootprintPreviewView::FootprintPreviewView(QWidget* parent)
    : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(QBrush(QColor(12, 12, 12))); // Professional dark background
    setFrameShape(QFrame::NoFrame);
}

void FootprintPreviewView::clear() {
    m_scene->clear();
}

void FootprintPreviewView::setFootprint(const FootprintDefinition& def) {
    m_scene->clear();
    if (!def.isValid()) return;

    PCBTheme* theme = ThemeManager::theme();
    
    // Consistent with ComponentItem - use safety defaults if theme is missing
    QColor silkColor = theme ? theme->componentOutline() : Qt::yellow;
    QColor padFillColor = theme ? theme->padFill() : QColor(200, 200, 0);
    QColor padStrokeColor = theme ? theme->padStroke() : Qt::white;
    
    QPen silkPen(silkColor, 1.0);
    silkPen.setCosmetic(true);
    silkPen.setCapStyle(Qt::RoundCap);
    silkPen.setJoinStyle(Qt::RoundJoin);
    
    QBrush padBrush(padFillColor);
    QPen padPen(padStrokeColor, 0.1);

    for (const auto& prim : def.primitives()) {
        if (prim.type == FootprintPrimitive::Pad) {
            qreal w = prim.data["width"].toDouble();
            qreal h = prim.data["height"].toDouble();
            qreal x = prim.data["x"].toDouble();
            qreal y = prim.data["y"].toDouble();
            QString shape = prim.data["shape"].toString();
            
            // Safety defaults for zero sizes
            if (w <= 0) w = 1.0;
            if (h <= 0) h = 1.0;
            
            QGraphicsItem* padItem = nullptr;
            if (shape == "Round" || shape == "Circle") {
                padItem = m_scene->addEllipse(-w/2, -h/2, w, h, padPen, padBrush);
            } else if (shape == "Oblong") {
                QPainterPath path;
                qreal r = std::min(w, h) / 2.0;
                path.addRoundedRect(-w/2, -h/2, w, h, r, r);
                padItem = m_scene->addPath(path, padPen, padBrush);
            } else {
                padItem = m_scene->addRect(-w/2, -h/2, w, h, padPen, padBrush);
            }
            
            if (padItem) {
                padItem->setPos(x, y);
                padItem->setRotation(prim.data["rotation"].toDouble());
                padItem->setZValue(1.0); // Pads on top
            }
            
            // Draw drill hole for preview if it's a TH pad
            if (prim.data.contains("drill_size")) {
                double drill = prim.data["drill_size"].toDouble();
                if (drill > 0) {
                    auto* hole = m_scene->addEllipse(x - drill/2, y - drill/2, drill, drill, Qt::NoPen, QBrush(QColor(30, 30, 30)));
                    hole->setZValue(1.1);
                }
            }
        } else {
            QGraphicsItem* item = nullptr;
            if (prim.type == FootprintPrimitive::Line) {
                item = m_scene->addLine(prim.data["x1"].toDouble(), prim.data["y1"].toDouble(),
                                       prim.data["x2"].toDouble(), prim.data["y2"].toDouble(), silkPen);
            } else if (prim.type == FootprintPrimitive::Rect) {
                item = m_scene->addRect(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                                       prim.data["width"].toDouble(), prim.data["height"].toDouble(), silkPen);
            } else if (prim.type == FootprintPrimitive::Circle) {
                qreal r = prim.data["radius"].toDouble();
                item = m_scene->addEllipse(prim.data["cx"].toDouble()-r, prim.data["cy"].toDouble()-r, r*2, r*2, silkPen);
            } else if (prim.type == FootprintPrimitive::Arc) {
                double r = prim.data["radius"].toDouble();
                double startAngle = prim.data["startAngle"].toDouble();
                double spanAngle = prim.data["spanAngle"].toDouble();
                QPainterPath path;
                path.arcMoveTo(prim.data["cx"].toDouble()-r, prim.data["cy"].toDouble()-r, r*2, r*2, startAngle);
                path.arcTo(prim.data["cx"].toDouble()-r, prim.data["cy"].toDouble()-r, r*2, r*2, startAngle, spanAngle);
                item = m_scene->addPath(path, silkPen);
            } else if (prim.type == FootprintPrimitive::Polygon) {
                QJsonArray points = prim.data["points"].toArray();
                if (!points.isEmpty()) {
                    QPolygonF poly;
                    for (const QJsonValue& p : points) {
                        QJsonObject pt = p.toObject();
                        poly << QPointF(pt["x"].toDouble(), pt["y"].toDouble());
                    }
                    item = m_scene->addPolygon(poly, silkPen);
                }
            }
            
            if (item) {
                item->setZValue(0.1);
            }
        }
    }
    
    // Zoom to fit - use safe pointer guard to prevent crash on dialog close
    QTimer::singleShot(20, this, [this]() {
        if (!m_scene) return;
        QRectF br = m_scene->itemsBoundingRect();
        if (!br.isNull() && br.isValid() && br.width() > 0) {
            fitInView(br.adjusted(-1, -1, 1, 1), Qt::KeepAspectRatio);
        }
    });
}

void FootprintPreviewView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    QRectF br = m_scene->itemsBoundingRect();
    if (!br.isNull() && br.isValid()) {
        fitInView(br.adjusted(-2, -2, 2, 2), Qt::KeepAspectRatio);
    }
}
