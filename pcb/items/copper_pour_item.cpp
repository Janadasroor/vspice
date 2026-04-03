#include "copper_pour_item.h"
#include "theme_manager.h"
#include "../layers/pcb_layer.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QDebug>

using namespace Flux::Model;

CopperPourItem::CopperPourItem(QGraphicsItem* parent)
    : PCBItem(parent)
    , m_model(new CopperPourModel())
    , m_ownsModel(true) {
    setZValue(-20); // Copper pours are behind everything
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

CopperPourItem::CopperPourItem(CopperPourModel* model, QGraphicsItem* parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false) {
    setZValue(-20);
    setPos(0, 0); // Polygons are usually in scene coordinates
    setLayer(model->layer());
    setId(model->id());
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
    updatePath();
}

CopperPourItem::~CopperPourItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void CopperPourItem::setPolygon(const QPolygonF& polygon) {
    prepareGeometryChange();
    m_model->setPolygon(polygon);
    updatePath();
}

void CopperPourItem::addPoint(const QPointF& point) {
    prepareGeometryChange();
    QPolygonF poly = m_model->polygon();
    poly << point;
    m_model->setPolygon(poly);
    updatePath();
}

void CopperPourItem::closePolygon() {
    QPolygonF poly = m_model->polygon();
    if (poly.size() > 2 && poly.first() != poly.last()) {
        prepareGeometryChange();
        poly << poly.first();
        m_model->setPolygon(poly);
        updatePath();
    }
}

void CopperPourItem::rebuild() {
    updatePath();
    update();
}

void CopperPourItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setLayer(layer);
        update();
    }
}

void CopperPourItem::setPriority(int priority) {
    if (m_model->priority() != priority) {
        m_model->setPriority(priority);
        update();
    }
}

void CopperPourItem::setRemoveIslands(bool remove) {
    if (m_model->removeIslands() != remove) {
        m_model->setRemoveIslands(remove);
        update();
    }
}

void CopperPourItem::setThermalSpokeWidth(double width) {
    if (m_model->thermalSpokeWidth() != width) {
        m_model->setThermalSpokeWidth(width);
        rebuild();
    }
}

void CopperPourItem::setThermalSpokeCount(int count) {
    if (m_model->thermalSpokeCount() != count) {
        m_model->setThermalSpokeCount(count);
        rebuild();
    }
}

void CopperPourItem::setThermalSpokeAngleDeg(double deg) {
    if (m_model->thermalSpokeAngleDeg() != deg) {
        m_model->setThermalSpokeAngleDeg(deg);
        rebuild();
    }
}

QRectF CopperPourItem::boundingRect() const {
    return m_path.boundingRect().adjusted(-1, -1, 1, 1);
}

QPainterPath CopperPourItem::shape() const {
    return m_path;
}

void CopperPourItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget *widget) {
    Q_UNUSED(widget);
    
    PCBTheme* theme = ThemeManager::theme();
    PCBLayer* l = PCBLayerManager::instance().layer(layer());
    QColor baseColor = l ? l->color() : theme->trace();
    
    // Fill with semi-transparent copper color
    QColor fillColor = baseColor;
    fillColor.setAlpha(120);
    
    painter->setPen(QPen(baseColor, 0.1, Qt::SolidLine));

    if (m_model->pourType() == CopperPourModel::SolidPour) {
        painter->setBrush(m_model->filled() ? QBrush(fillColor) : Qt::NoBrush);
        painter->drawPath(m_path);
    } else {
        painter->setBrush(m_model->filled() ? QBrush(fillColor) : Qt::NoBrush);
        painter->drawPath(m_path);
        painter->setPen(QPen(baseColor, m_model->hatchWidth(), Qt::SolidLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(m_hatchPath);
    }

    if (option->state & QStyle::State_Selected) {
        drawSelectionGlow(painter);
    }
}

void CopperPourItem::updatePath() {
    m_path = QPainterPath();
    if (m_model->polygon().isEmpty()) return;
    
    m_path.addPolygon(m_model->polygon());
    
    if (m_model->pourType() == CopperPourModel::HatchPour) {
        generateHatchPattern();
    }
}

void CopperPourItem::generateHatchPattern() {
    m_hatchPath = QPainterPath();
    QRectF rect = m_path.boundingRect();
    double step = m_model->hatchWidth() * 3.0;
    
    // Simple 45-degree hatching
    for (double x = rect.left() - rect.height(); x < rect.right(); x += step) {
        QLineF line(x, rect.top(), x + rect.height(), rect.bottom());
        m_hatchPath.moveTo(line.p1());
        m_hatchPath.lineTo(line.p2());
    }
    for (double x = rect.left(); x < rect.right() + rect.height(); x += step) {
        QLineF line(x, rect.top(), x - rect.height(), rect.bottom());
        m_hatchPath.moveTo(line.p1());
        m_hatchPath.lineTo(line.p2());
    }
    
    m_hatchPath = m_hatchPath.intersected(m_path);
}

QJsonObject CopperPourItem::toJson() const {
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    json["name"] = name();
    return json;
}

bool CopperPourItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;
    
    m_model->fromJson(json);
    setId(m_model->id());
    setName(json["name"].toString());
    setLayer(m_model->layer());
    
    updatePath();
    update();
    return true;
}

PCBItem* CopperPourItem::clone() const {
    CopperPourModel* newModel = new CopperPourModel();
    newModel->fromJson(m_model->toJson());
    
    CopperPourItem* newItem = new CopperPourItem(newModel, parentItem());
    newItem->m_ownsModel = true;
    newItem->setName(name());
    newItem->setLayer(layer());
    return newItem;
}
