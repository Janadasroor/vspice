#include "via_item.h"
#include "theme_manager.h"
#include "../layers/pcb_layer.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QDebug>
#include <algorithm>

using namespace Flux::Model;

namespace {
int copperLayerOrderIndex(int layerId) {
    if (layerId == PCBLayerManager::TopCopper) return 0;
    if (layerId >= 100) return 1 + (layerId - 100);
    if (layerId == PCBLayerManager::BottomCopper) return 1000;
    return layerId;
}
}

ViaItem::ViaItem(QPointF pos, double diameter, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(new ViaModel())
    , m_ownsModel(true) {
    
    m_model->setPos(pos);
    m_model->setDiameter(diameter);
    m_model->setStartLayer(PCBLayerManager::TopCopper);
    m_model->setEndLayer(PCBLayerManager::BottomCopper);
    
    PCBTheme* theme = ThemeManager::theme();
    m_brush = QBrush(theme->viaFill());
    m_pen = QPen(theme->viaStroke(), 2);
    m_pen.setCosmetic(true);
    setPos(pos);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

ViaItem::ViaItem(ViaModel* model, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false) {
    
    PCBTheme* theme = ThemeManager::theme();
    m_brush = QBrush(theme->viaFill());
    m_pen = QPen(theme->viaStroke(), 2);
    m_pen.setCosmetic(true);
    setPos(model->pos());
    setLayer(model->startLayer());
    setId(model->id());
    setNetName(model->netName());
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

ViaItem::~ViaItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void ViaItem::setDrillSize(double size) {
    if (m_model->drillSize() != size) {
        m_model->setDrillSize(size);
        update();
    }
}

void ViaItem::setDiameter(double diameter) {
    if (m_model->diameter() != diameter) {
        prepareGeometryChange();
        m_model->setDiameter(diameter);
        update();
    }
}

void ViaItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setStartLayer(layer);
        update();
    }
}

void ViaItem::setStartLayer(int l) {
    if (m_model->startLayer() == l) return;
    m_model->setStartLayer(l);
    if (layer() != l) {
        setLayer(l);
    }
    update();
}

void ViaItem::setEndLayer(int layer) {
    if (m_model->endLayer() == layer) return;
    m_model->setEndLayer(layer);
    update();
}

bool ViaItem::spansLayer(int layerId) const {
    const int a = copperLayerOrderIndex(m_model->startLayer());
    const int b = copperLayerOrderIndex(m_model->endLayer());
    const int x = copperLayerOrderIndex(layerId);
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    return x >= lo && x <= hi;
}

QString ViaItem::viaType() const {
    if (m_model->isMicrovia()) return "Microvia";
    const bool touchesTop = spansLayer(PCBLayerManager::TopCopper);
    const bool touchesBottom = spansLayer(PCBLayerManager::BottomCopper);
    if (touchesTop && touchesBottom) return "Through";
    if (touchesTop || touchesBottom) return "Blind";
    return "Buried";
}

void ViaItem::setMicrovia(bool microvia) {
    if (m_model->isMicrovia() == microvia) return;
    m_model->setMicrovia(microvia);
    update();
}

QRectF ViaItem::boundingRect() const {
    double radius = m_model->diameter() / 2.0;
    double penWidth = m_pen.widthF();
    return QRectF(-radius - penWidth/2, -radius - penWidth/2,
                  m_model->diameter() + penWidth, m_model->diameter() + penWidth);
}

QPainterPath ViaItem::shape() const {
    QPainterPath path;
    double radius = m_model->diameter() / 2.0;
    path.addEllipse(-radius, -radius, m_model->diameter(), m_model->diameter());
    return path;
}

void ViaItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    double radius = m_model->diameter() / 2.0;
    QRectF ellipseRect(-radius, -radius, m_model->diameter(), m_model->diameter());

    // Draw outer via
    painter->setPen(m_pen);
    painter->setBrush(m_brush);
    painter->drawEllipse(ellipseRect);

    // Draw center hole
    double holeRadius = m_model->drillSize() / 2.0;
    QRectF holeRect(-holeRadius, -holeRadius, m_model->drillSize(), m_model->drillSize());
    painter->setPen(Qt::NoPen);
    PCBTheme* theme = ThemeManager::theme();
    painter->setBrush(QBrush(theme->drillHoles()));
    painter->drawEllipse(holeRect);

    // Create a modified option to hide the default Qt selection box
    QStyleOptionGraphicsItem opt = *option;
    opt.state &= ~QStyle::State_Selected;

    // Draw professional selection glow
    drawSelectionGlow(painter);
}

QJsonObject ViaItem::toJson() const {
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    json["name"] = name();
    json["layer"] = layer(); // Keep for compatibility
    return json;
}

bool ViaItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }

    m_model->fromJson(json);
    
    setId(m_model->id());
    setName(json["name"].toString());
    setLayer(m_model->startLayer());
    setPos(m_model->pos());

    update();
    return true;
}

PCBItem* ViaItem::clone() const {
    ViaModel* newModel = new ViaModel();
    newModel->fromJson(m_model->toJson());
    
    ViaItem* newItem = new ViaItem(newModel, parentItem());
    newItem->m_ownsModel = true;
    newItem->setName(name());
    newItem->setLayer(layer());
    newItem->m_brush = m_brush;
    newItem->m_pen = m_pen;
    return newItem;
}
