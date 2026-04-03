#include "component_item.h"
#include "pad_item.h"
#include "theme_manager.h"
#include "../../footprints/footprint_library.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonObject>
#include <QJsonArray>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QRegularExpression>
#endif
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QDebug>

using namespace Flux::Model;

ComponentItem::ComponentItem(QPointF pos, QString type, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(new ComponentModel())
    , m_ownsModel(true)
    , m_body(nullptr)
    , m_label(nullptr) {
    
    m_model->setPos(pos);
    m_model->setComponentType(type);
    m_model->setSize(QSizeF(5.0, 5.0)); // Default fallback size
    
    setPos(pos);
    setFiltersChildEvents(true);
    createBody();
    createLabel();
    createPads(); // This will populate model from library if available
}

ComponentItem::ComponentItem(ComponentModel* model, QGraphicsItem *parent)
    : PCBItem(parent)
    , m_model(model)
    , m_ownsModel(false)
    , m_body(nullptr)
    , m_label(nullptr) {
    
    setPos(model->pos());
    setRotation(model->rotation());
    setName(model->name());
    setLayer(model->layer());
    setId(model->id());
    setFiltersChildEvents(true);
    createBody();
    createLabel();
    createPads(); // This will create UI items for existing PadModels
}

ComponentItem::~ComponentItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

QVariant ComponentItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (m_model) {
        if (change == ItemPositionHasChanged) {
            m_model->setPos(value.toPointF());
        } else if (change == ItemRotationHasChanged) {
            m_model->setRotation(value.toDouble());
        }
    }
    return PCBItem::itemChange(change, value);
}

void ComponentItem::setComponentType(const QString& type) {
    if (m_model->componentType() != type) {
        m_model->setComponentType(type);
        if (m_label) {
            m_label->setText(type);
            qreal targetHeight = 1.5;
            QRectF br = m_label->boundingRect();
            if (br.height() > 0) {
                m_label->setScale(targetHeight / br.height());
            }
            qreal s = m_label->scale();
            m_label->setPos(-br.width() * s / 2.0, -br.height() * s / 2.0);
        }
        
        m_model->clearPads(); // Reset model pads so they are regenerated
        createPads(); 
        updateBody();
        update();
    }
}

void ComponentItem::setName(const QString& name) {
    PCBItem::setName(name);
    if (m_model) {
        m_model->setName(name);
    }
}

void ComponentItem::setValue(const QString& value) {
    if (m_model) {
        m_model->setValue(value);
    }
}

void ComponentItem::setSize(const QSizeF& size) {
    if (m_model->size() != size) {
        prepareGeometryChange();
        m_model->setSize(size);
        updateBody();
        update();
    }
}

void ComponentItem::setLayer(int layer) {
    if (m_layer != layer) {
        m_layer = layer;
        m_model->setLayer(layer);
        // Cascade to pads
        for (auto* child : childItems()) {
            if (PadItem* pad = dynamic_cast<PadItem*>(child)) {
                pad->setLayer(layer);
            }
        }
        update();
    }
}

QRectF ComponentItem::boundingRect() const {
    if (FootprintLibraryManager::instance().hasFootprint(m_model->componentType())) {
        FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(m_model->componentType());
        return def.boundingRect().adjusted(-1, -1, 1, 1);
    }

    QSizeF size = m_model->size();
    return QRectF(-size.width()/2 - 2, -size.height()/2 - 2,
                  size.width() + 4, size.height() + 4);
}

QPainterPath ComponentItem::shape() const {
    QPainterPath path;
    for (QGraphicsItem* child : childItems()) {
        if (child->isVisible() && (dynamic_cast<PadItem*>(child))) {
            path.addPath(child->mapToParent(child->shape()));
        }
    }
    
    if (path.isEmpty()) {
        QSizeF size = m_model->size();
        path.addRect(-size.width()/2, -size.height()/2, size.width(), size.height());
    }
    
    return path.simplified();
}

void ComponentItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget)
    QStyleOptionGraphicsItem opt = *option;
    opt.state &= ~QStyle::State_Selected;
    drawSelectionGlow(painter);
}

void ComponentItem::createBody() {
    if (m_body) delete m_body;
    QSizeF size = m_model->size();
    m_body = new QGraphicsRectItem(-size.width()/2, -size.height()/2,
                                   size.width(), size.height(), this);
    PCBTheme* theme = ThemeManager::theme();
    m_body->setBrush(QBrush(theme->componentFill()));
    m_body->setPen(QPen(theme->componentOutline(), 2));
}

void ComponentItem::createLabel() {
    if (m_label) delete m_label;
    m_label = new QGraphicsSimpleTextItem(m_model->componentType(), this);
    
    PCBTheme* theme = ThemeManager::theme();
    m_label->setBrush(QBrush(theme->componentOutline()));
    m_label->setPen(Qt::NoPen);
    
    qreal targetHeight = 1.5;
    QRectF br = m_label->boundingRect();
    if (br.height() > 0) {
        m_label->setScale(targetHeight / br.height());
    }
    
    qreal s = m_label->scale();
    m_label->setPos(-br.width() * s / 2.0, -br.height() * s / 2.0);
}

void ComponentItem::createPads() {
    // Clear existing UI items
    QList<QGraphicsItem*> children = childItems();
    for (QGraphicsItem* child : children) {
        if (dynamic_cast<PadItem*>(child)) delete child;
    }
    for (auto* item : m_footprintItems) delete item;
    m_footprintItems.clear();

    QString type = m_model->componentType();
    
    // Check if we need to populate model from library
    bool needsLibraryPopulate = m_model->pads().isEmpty() && FootprintLibraryManager::instance().hasFootprint(type);

    if (needsLibraryPopulate) {
        FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(type);
        for (const auto& prim : def.primitives()) {
            if (prim.type == FootprintPrimitive::Pad) {
                PadModel* pm = new PadModel();
                pm->setPos(QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble()));
                pm->setSize(QSizeF(prim.data["width"].toDouble(), prim.data["height"].toDouble()));
                pm->setShape(prim.data["shape"].toString());
                pm->setDrillSize(prim.data["drill_size"].toDouble());
                pm->setRotation(prim.data["rotation"].toDouble());
                pm->setLayer(m_model->layer());
                pm->setNetName(""); // Default
                m_model->addPad(pm);
            }
        }
        m_model->setSize(def.boundingRect().size());
    }

    // Now create UI wrappers for all PadModels in the model
    for (auto* pm : m_model->pads()) {
        PadItem* pad = new PadItem(pm, this);
        pad->setLayer(m_model->layer());
        pad->setZValue(1.0);
    }

    // Handle silk primitives (View only for now)
    if (FootprintLibraryManager::instance().hasFootprint(type)) {
        FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(type);
        if (m_body) m_body->hide();
        if (m_label) m_label->hide();
        
        PCBTheme* theme = ThemeManager::theme();
        QPen silkPen(theme->componentOutline(), 0.15);
        silkPen.setCapStyle(Qt::RoundCap);
        silkPen.setJoinStyle(Qt::RoundJoin);

        for (const auto& prim : def.primitives()) {
            if (prim.type == FootprintPrimitive::Pad) continue; // Already handled
            
            QGraphicsItem* item = nullptr;
            if (prim.type == FootprintPrimitive::Line) {
                item = new QGraphicsLineItem(prim.data["x1"].toDouble(), prim.data["y1"].toDouble(),
                                             prim.data["x2"].toDouble(), prim.data["y2"].toDouble(), this);
                static_cast<QGraphicsLineItem*>(item)->setPen(silkPen);
            } else if (prim.type == FootprintPrimitive::Rect) {
                item = new QGraphicsRectItem(prim.data["x"].toDouble(), prim.data["y"].toDouble(),
                                             prim.data["width"].toDouble(), prim.data["height"].toDouble(), this);
                static_cast<QGraphicsRectItem*>(item)->setPen(silkPen);
            } else if (prim.type == FootprintPrimitive::Circle) {
                double r = prim.data["radius"].toDouble();
                item = new QGraphicsEllipseItem(prim.data["cx"].toDouble()-r, prim.data["cy"].toDouble()-r, r*2, r*2, this);
                static_cast<QGraphicsEllipseItem*>(item)->setPen(silkPen);
            } else if (prim.type == FootprintPrimitive::Arc) {
                double r = prim.data["radius"].toDouble();
                double startAngle = prim.data["startAngle"].toDouble();
                double spanAngle = prim.data["spanAngle"].toDouble();
                QPainterPath path;
                path.arcMoveTo(prim.data["cx"].toDouble()-r, prim.data["cy"].toDouble()-r, r*2, r*2, startAngle);
                path.arcTo(prim.data["cx"].toDouble()-r, prim.data["cy"].toDouble()-r, r*2, r*2, startAngle, spanAngle);
                item = new QGraphicsPathItem(path, this);
                static_cast<QGraphicsPathItem*>(item)->setPen(silkPen);
            } else if (prim.type == FootprintPrimitive::Polygon) {
                QJsonArray points = prim.data["points"].toArray();
                QPolygonF poly;
                for (const QJsonValue& p : points) {
                    QJsonObject pt = p.toObject();
                    poly << QPointF(pt["x"].toDouble(), pt["y"].toDouble());
                }
                item = new QGraphicsPolygonItem(poly, this);
                static_cast<QGraphicsPolygonItem*>(item)->setPen(silkPen);
            }
            
            if (item) {
                item->setZValue(0.1);
                m_footprintItems.append(item);
            }
        }
    } else {
        // Fallback fallback
        if (m_body) {
            m_body->show();
            updateBody();
        }
        if (m_label) m_label->show();
    }
}

void ComponentItem::updateBody() {
    if (m_body) {
        QSizeF size = m_model->size();
        m_body->setRect(-size.width()/2, -size.height()/2,
                        size.width(), size.height());
    }
}

void ComponentItem::updatePads() {
    createPads();
}

QJsonObject ComponentItem::toJson() const {
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    return json;
}

bool ComponentItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;
    
    m_model->fromJson(json);
    setId(m_model->id());
    setName(m_model->name());
    setLayer(m_model->layer());
    setPos(m_model->pos());
    setRotation(m_model->rotation());
    
    createPads(); // Regenerate UI wrappers from model
    update();
    return true;
}

PCBItem* ComponentItem::clone() const {
    ComponentModel* newModel = new ComponentModel();
    newModel->fromJson(m_model->toJson());
    
    ComponentItem* newItem = new ComponentItem(newModel, parentItem());
    newItem->m_ownsModel = true;
    newItem->setName(name());
    newItem->setLayer(layer());
    return newItem;
}
