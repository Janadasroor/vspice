#include "trace_item.h"
#include "theme_manager.h"
#include "../layers/pcb_layer.h"
#include <QGraphicsScene>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QJsonObject>
#include <QDebug>
#include <cmath>

using namespace Flux::Model;

TraceItem::TraceItem(QPointF start, QPointF end, double width, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(new TraceModel())
    , m_ownsModel(true) {
    
    m_model->setStart(start);
    m_model->setEnd(end);
    m_model->setWidth(width);
    
    m_pen.setCapStyle(Qt::RoundCap);
    m_pen.setJoinStyle(Qt::RoundJoin);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

TraceItem::TraceItem(TraceModel* model, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false) {
    
    m_pen.setCapStyle(Qt::RoundCap);
    m_pen.setJoinStyle(Qt::RoundJoin);
    setLayer(model->layer());
    setId(model->id());
    setNetName(model->netName());
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

TraceItem::~TraceItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void TraceItem::setStartPoint(QPointF start) {
    if (m_model->start() != start) {
        prepareGeometryChange();
        m_model->setStart(start);
        update();
    }
}

void TraceItem::setEndPoint(QPointF end) {
    if (m_model->end() != end) {
        prepareGeometryChange();
        m_model->setEnd(end);
        update();
    }
}

void TraceItem::setWidth(double width) {
    if (m_model->width() != width) {
        prepareGeometryChange();
        m_model->setWidth(width);
        m_pen.setWidthF(width);
        update();
    }
}

void TraceItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setLayer(layer);
        update();
    }
}

QRectF TraceItem::boundingRect() const {
    QLineF line(m_model->start(), m_model->end());
    double halfWidth = m_model->width() / 2.0;
    QRectF rect = QRectF(line.p1(), line.p2()).normalized();
    rect.adjust(-halfWidth, -halfWidth, halfWidth, halfWidth);
    return rect;
}

QPainterPath TraceItem::shape() const {
    QPainterPath path;
    path.moveTo(m_model->start());
    path.lineTo(m_model->end());
    
    QPainterPathStroker stroker;
    stroker.setWidth(m_model->width());
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    
    return stroker.createStroke(path);
}

void TraceItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    PCBTheme* theme = ThemeManager::theme();
    PCBLayer* l = PCBLayerManager::instance().layer(layer());
    QColor color = l ? l->color() : theme->trace();
    
    m_pen.setColor(color);
    m_pen.setWidthF(m_model->width());

    painter->setPen(m_pen);
    painter->drawLine(QLineF(m_model->start(), m_model->end()));

    // Create a modified option to hide the default Qt selection box
    QStyleOptionGraphicsItem opt = *option;
    opt.state &= ~QStyle::State_Selected;

    // Draw professional selection glow
    drawSelectionGlow(painter);
}

void TraceItem::updateConnectivity() {
    if (!scene()) return;

    auto checkPoint = [&](QPointF p) {
        QList<QGraphicsItem*> items = scene()->items(p);
        for (auto* item : items) {
            QGraphicsItem* current = item;
            while (current) {
                if (current->type() == PadType || current->type() == ViaType) {
                    if (PCBItem* pcbItem = dynamic_cast<PCBItem*>(current)) {
                        if (!pcbItem->netName().isEmpty()) {
                            this->setNetName(pcbItem->netName());
                            m_model->setNetName(pcbItem->netName());
                            return true;
                        }
                    }
                }
                current = current->parentItem();
            }
        }
        return false;
    };

    if (!checkPoint(m_model->start())) {
        checkPoint(m_model->end());
    }
}

QJsonObject TraceItem::toJson() const {
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName(); // Metadata for the UI layer
    json["name"] = name();
    return json;
}

bool TraceItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }

    m_model->fromJson(json);
    
    setId(m_model->id());
    setName(json["name"].toString());
    setLayer(m_model->layer());

    update();
    return true;
}

PCBItem* TraceItem::clone() const {
    TraceModel* newModel = new TraceModel();
    newModel->fromJson(m_model->toJson());
    
    TraceItem* newItem = new TraceItem(newModel, parentItem());
    newItem->m_ownsModel = true; // New item owns the cloned model
    newItem->setName(name());
    newItem->setLayer(layer());
    newItem->m_pen = m_pen;
    return newItem;
}
