#include "pad_item.h"
#include "theme_manager.h"
#include "../layers/pcb_layer.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QDebug>

using namespace Flux::Model;

PadItem::PadItem(QPointF pos, double diameter, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(new PadModel())
    , m_ownsModel(true) {
    
    m_model->setPos(pos);
    m_model->setSize(QSizeF(diameter, diameter));
    
    PCBTheme* theme = ThemeManager::theme();
    m_brush = QBrush(theme->padFill());
    m_pen = QPen(theme->padStroke(), 0.1);
    m_pen.setCosmetic(true);
    setPos(pos);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

PadItem::PadItem(PadModel* model, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false) {
    
    PCBTheme* theme = ThemeManager::theme();
    m_brush = QBrush(theme->padFill());
    m_pen = QPen(theme->padStroke(), 0.1);
    m_pen.setCosmetic(true);
    setPos(model->pos());
    setRotation(model->rotation());
    setNetName(model->netName());
    setLayer(model->layer());
    setId(model->id());
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

PadItem::~PadItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void PadItem::setDiameter(double diameter) {
    setSize(QSizeF(diameter, diameter));
}

void PadItem::setSize(const QSizeF& size) {
    if (m_model->size() != size) {
        prepareGeometryChange();
        m_model->setSize(size);
        update();
    }
}

void PadItem::setPadShape(const QString& shape) {
    if (m_model->shape() != shape) {
        prepareGeometryChange();
        m_model->setShape(shape);
        update();
    }
}

void PadItem::setDrillSize(double size) {
    if (m_model->drillSize() != size) {
        m_model->setDrillSize(size);
        update();
    }
}

void PadItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setLayer(layer);
        update();
    }
}

QRectF PadItem::boundingRect() const {
    double border = m_pen.widthF() / 2.0;
    QSizeF size = m_model->size();
    return QRectF(-size.width()/2 - border, -size.height()/2 - border,
                  size.width() + 2*border, size.height() + 2*border);
}

QPainterPath PadItem::shape() const {
    QPainterPath path;
    QSizeF size = m_model->size();
    QString shape = m_model->shape().toLower();
    const QString padNumber = m_model->number().trimmed();
    
    if (shape == "rect" || shape == "rectangle" || shape == "square") {
        path.addRect(-size.width()/2, -size.height()/2, size.width(), size.height());
    } else if (shape == "oblong" || shape == "oval") {
        double r = std::min(size.width(), size.height()) / 2.0;
        path.addRoundedRect(-size.width()/2, -size.height()/2, 
                           size.width(), size.height(), r, r);
    } else if (shape == "roundedrect" || shape == "roundrect") {
        double r = std::min(size.width(), size.height()) * 0.25;
        path.addRoundedRect(-size.width()/2, -size.height()/2, 
                           size.width(), size.height(), r, r);
    } else {
        path.addEllipse(-size.width()/2, -size.height()/2, size.width(), size.height());
    }
    return path;
}

void PadItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    PCBTheme* theme = ThemeManager::theme();
    PCBLayerManager& layerMgr = PCBLayerManager::instance();
    int activeLayerId = layerMgr.activeLayerId();
    PCBLayer* activeLayer = layerMgr.layer(activeLayerId);
    
    QColor baseColor;
    bool isTH = (m_model->drillSize() > 0.001);
    bool onActiveLayer = false;

    if (isTH) {
        baseColor = theme->multiLayer();
        // Through-hole pads exist on all copper layers
        if (activeLayer && activeLayer->isCopperLayer()) {
            onActiveLayer = true;
        }
    } else {
        PCBLayer* l = layerMgr.layer(layer());
        baseColor = l ? l->color() : theme->padFill();
        if (layer() == activeLayerId) {
            onActiveLayer = true;
        }
    }
    
    // Draw the main pad body
    painter->setPen(QPen(baseColor.darker(150), 0.05));
    painter->setBrush(QBrush(baseColor));

    QSizeF size = m_model->size();
    QString shape = m_model->shape().toLower();
    const QString padNumber = m_model->number().trimmed();
    QRectF rect(-size.width()/2, -size.height()/2, size.width(), size.height());
    
    if (shape == "rect" || shape == "rectangle" || shape == "square") {
        painter->drawRect(rect);
    } else if (shape == "oblong" || shape == "oval") {
        double r = std::min(size.width(), size.height()) / 2.0;
        painter->drawRoundedRect(rect, r, r);
    } else if (shape == "roundedrect" || shape == "roundrect") {
        double r = std::min(size.width(), size.height()) * 0.25;
        painter->drawRoundedRect(rect, r, r);
    } else {
        painter->drawEllipse(rect);
    }

    // KiCad-style Active Layer Highlight (Outline)
    if (onActiveLayer && activeLayer) {
        QColor highlightColor = activeLayer->color();
        painter->setBrush(Qt::NoBrush);
        // Bright, thick outline to show connectability
        QPen highlightPen(highlightColor, 0.15); 
        painter->setPen(highlightPen);
        
        // Draw slightly outside the pad
        double margin = 0.05;
        QRectF highlightRect = rect.adjusted(-margin, -margin, margin, margin);
        
        if (shape == "rect" || shape == "rectangle" || shape == "square") {
            painter->drawRect(highlightRect);
        } else if (shape == "oblong" || shape == "oval") {
            double r = std::min(highlightRect.width(), highlightRect.height()) / 2.0;
            painter->drawRoundedRect(highlightRect, r, r);
        } else if (shape == "roundedrect" || shape == "roundrect") {
            double r = std::min(highlightRect.width(), highlightRect.height()) * 0.25;
            painter->drawRoundedRect(highlightRect, r, r);
        } else {
            painter->drawEllipse(highlightRect);
        }
    }

    // Draw the drill hole
    if (isTH) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QBrush(QColor(30, 30, 30)));
        painter->drawEllipse(QPointF(0, 0), m_model->drillSize()/2, m_model->drillSize()/2);
    }
    
    painter->setPen(QPen(baseColor.lighter(130), 0.02));
    double crossSize = std::min(size.width(), size.height()) * 0.2;
    painter->drawLine(QLineF(-crossSize, 0, crossSize, 0));
    painter->drawLine(QLineF(0, -crossSize, 0, crossSize));

    if (!padNumber.isEmpty()) {
        painter->setPen(baseColor.lightness() < 140 ? Qt::white : Qt::black);
        painter->setFont(QFont("Monospace", 1));
        painter->drawText(rect, Qt::AlignCenter, padNumber);
    }

    drawSelectionGlow(painter);
}

QJsonObject PadItem::toJson() const {
    m_model->setNetName(netName());
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    json["name"] = name();
    return json;
}

bool PadItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }

    m_model->fromJson(json);
    
    setId(m_model->id());
    setName(json["name"].toString());
    setNetName(m_model->netName());
    setLayer(m_model->layer());
    setPos(m_model->pos());
    setRotation(m_model->rotation());

    update();
    return true;
}

PCBItem* PadItem::clone() const {
    m_model->setNetName(netName());
    PadModel* newModel = new PadModel();
    newModel->fromJson(m_model->toJson());
    
    PadItem* newItem = new PadItem(newModel, parentItem());
    newItem->m_ownsModel = true;
    newItem->setName(name());
    newItem->setNetName(netName());
    newItem->setLayer(layer());
    newItem->setRotation(rotation());
    newItem->m_brush = m_brush;
    newItem->m_pen = m_pen;
    return newItem;
}
