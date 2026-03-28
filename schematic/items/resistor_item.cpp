#include "resistor_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

using namespace Flux::Model;

ResistorItem::ResistorItem(QPointF pos, QString value, ResistorStyle style, QGraphicsItem *parent)
    : SchematicItem(parent)
    , m_model(new SchematicComponentModel())
    , m_ownsModel(true)
    , m_style(style) {
    setPos(pos);
    m_model->setPos(pos);
    m_model->setValue(value);
    m_model->setName("Resistor");
    
    // Add default pins to model
    PinModel* p1 = new PinModel();
    p1->name = "1"; p1->number = "1"; p1->pos = QPointF(-60, 0);
    m_model->addPin(p1);
    
    PinModel* p2 = new PinModel();
    p2->name = "2"; p2->number = "2"; p2->pos = QPointF(60, 0);
    m_model->addPin(p2);

    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    createLabels(QPointF(-22.5, -37.5), QPointF(-22.5, 37.5));
}

ResistorItem::ResistorItem(SchematicComponentModel* model, QGraphicsItem *parent)
    : SchematicItem(parent)
    , m_model(model)
    , m_ownsModel(false)
    , m_style(US) {
    setPos(model->pos());
    setRotation(model->rotation());
    
    PCBTheme* theme = ThemeManager::theme();
    m_pen = QPen(theme->schematicLine(), 2);
    m_brush = QBrush(theme->schematicComponent());
    
    buildPrimitives();
    createLabels(QPointF(-22.5, -37.5), QPointF(-22.5, 37.5));
}

ResistorItem::~ResistorItem() {
    if (m_ownsModel) {
        delete m_model;
    }
}

void ResistorItem::buildPrimitives() {
    m_primitives.clear();
    
    if (m_style == US) {
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-60, 0), QPointF(-45, 0)));
        
        QList<QPointF> zigzag;
        zigzag << QPointF(-45, 0)
               << QPointF(-37.5, -22.5)
               << QPointF(-22.5, 22.5)
               << QPointF(-7.5, -22.5)
               << QPointF(7.5, 22.5)
               << QPointF(22.5, -22.5)
               << QPointF(37.5, 22.5)
               << QPointF(45, 0);
        m_primitives.push_back(std::make_unique<PolylinePrimitive>(zigzag));
        
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(45, 0), QPointF(60, 0)));
    } else {
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-60, 0), QPointF(-22.5, 0)));
        m_primitives.push_back(std::make_unique<RectPrimitive>(QRectF(-22.5, -22.5, 45, 45)));
        m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(22.5, 0), QPointF(60, 0)));
    }
    
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(-60, 0), 3.75, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(60, 0), 3.75, true));
}

void ResistorItem::setValue(const QString& value) {
    if (m_model->value() != value) {
        m_model->setValue(value);
        SchematicItem::setValue(value); // Updates labels
        buildPrimitives();
        update();
    }
}

QRectF ResistorItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void ResistorItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    for (const auto& prim : m_primitives) {
        prim->paint(painter, m_pen, m_brush);
    }

    drawConnectionPointHighlights(painter);

    if (isSelected()) {
        PCBTheme* theme = ThemeManager::theme();
        painter->setPen(QPen(theme->selectionBox(), 1, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
    }
}

QJsonObject ResistorItem::toJson() const {
    // Start with model serialization (pos, rotation, value, reference, pins, etc.)
    QJsonObject json = m_model->toJson();
    json["type"] = itemTypeName();
    json["style"] = static_cast<int>(m_style);
    json["pinPadMapping"] = pinPadMappingToJson();

    // Merge base-class fields that the model doesn't know about
    // (spiceModel, excludeFromSim/Pcb, paramExpressions, tolerances, isLocked, mirrored)
    QJsonObject baseJson = SchematicItem::toJson();
    const QStringList baseOnlyKeys = {
        "spiceModel", "excludeFromSim", "excludeFromPcb",
        "paramExpressions", "tolerances", "pinPadMapping",
        "isLocked", "isMirroredX", "isMirroredY",
        "manufacturer", "mpn", "description"
    };
    for (const QString& key : baseOnlyKeys) {
        if (baseJson.contains(key)) json[key] = baseJson[key];
    }

    if (m_refLabelItem) {
        json["refX"] = m_refLabelItem->pos().x();
        json["refY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }
    
    return json;
}

bool ResistorItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }

    m_model->fromJson(json);
    setId(m_model->id());
    setName(m_model->name());
    setPos(m_model->pos().x(), m_model->pos().y());
    setReference(m_model->reference());
    setFootprint(m_model->footprint());
    loadPinPadMappingFromJson(json);
    setRotation(json["rotation"].toDouble(m_model->rotation()));
    
    // Restore base-class fields
    setSpiceModel(json["spiceModel"].toString());
    setExcludeFromSimulation(json["excludeFromSim"].toBool(false));
    setExcludeFromPcb(json["excludeFromPcb"].toBool(false));
    setLocked(json["isLocked"].toBool(false));
    setMirroredX(json["isMirroredX"].toBool(false));
    setMirroredY(json["isMirroredY"].toBool(false));

    m_paramExpressions.clear();
    if (json.contains("paramExpressions")) {
        QJsonObject exprs = json["paramExpressions"].toObject();
        for (auto it = exprs.begin(); it != exprs.end(); ++it) m_paramExpressions[it.key()] = it.value().toString();
    }

    m_tolerances.clear();
    if (json.contains("tolerances")) {
        QJsonObject tols = json["tolerances"].toObject();
        for (auto it = tols.begin(); it != tols.end(); ++it) m_tolerances[it.key()] = it.value().toString();
    }

    if (json.contains("style")) {
        m_style = static_cast<ResistorStyle>(json["style"].toInt());
    }

    buildPrimitives();
    createLabels(QPointF(-22.5, -27), QPointF(-22.5, 30));
    
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    
    updateLabelText();
    update();
    return true;
}

SchematicItem* ResistorItem::clone() const {
    SchematicComponentModel* newModel = new SchematicComponentModel();
    newModel->fromJson(m_model->toJson());
    ResistorItem* newItem = new ResistorItem(newModel);
    newItem->setOwned(true);
    newItem->m_style = m_style;
    newItem->setName(name());
    return newItem;
}

QList<QPointF> ResistorItem::connectionPoints() const {
    QList<QPointF> points;
    for (auto* pin : m_model->pins()) {
        points.append(pin->pos);
    }
    return points;
}
void ResistorItem::setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents) {
    Q_UNUSED(branchCurrents)
    QString n1 = pinNet(0);
    QString n2 = pinNet(1);
    if (n1.isEmpty() || n2.isEmpty()) {
        m_powerDissipation = 0;
        return;
    }
    double v1 = nodeVoltages.value(n1, 0.0);
    double v2 = nodeVoltages.value(n2, 0.0);
    double dv = v1 - v2;
    double r = parseValue(value());
    if (r > 1e-12) {
        m_powerDissipation = (dv * dv) / r;
    } else {
        m_powerDissipation = 0;
    }
    update();
}
