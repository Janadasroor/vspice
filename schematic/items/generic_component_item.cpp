#include "generic_component_item.h"
#include "schematic_text_item.h"
#include "flux/core/text_resolver.h"
#include "flux/core/theme_manager.h"
#include <QPainter>
#include <QDateTime>
#include <QStyleOptionGraphicsItem>
#include "flux/symbols/symbol_library.h"
#include <cmath>

using Flux::Model::SymbolPrimitive;
using namespace Flux::Item;

QList<SymbolPrimitive> GenericComponentItem::resolvedPrimitives() const {
    QList<SymbolPrimitive> out;
    const QList<SymbolPrimitive> effective = m_symbol.effectivePrimitives();
    out.reserve(effective.size());

    constexpr int kSchematicBodyStyle = 1; // Match symbol editor's default "Standard" style.
    for (const auto& prim : effective) {
        if (prim.unit() != 0 && prim.unit() != unit()) continue;
        if (prim.bodyStyle() != 0 && prim.bodyStyle() != kSchematicBodyStyle) continue;
        out.append(prim);
    }
    return out;
}

GenericComponentItem::GenericComponentItem(const SymbolDefinition& symbol, QGraphicsItem* parent)
    : SchematicItem(parent)
    , m_symbol(symbol) {
    // Set default name and value from symbol
    m_name = symbol.name();
    // Default value is usually the name or empty
    m_value = symbol.name();
    
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    
    // Optimization: Cache complex vector graphics to speed up rendering
    setCacheMode(QGraphicsItem::NoCache);

    // Power symbols should never require PCB footprint assignment.
    if (m_symbol.isPowerSymbol()) {
        setExcludeFromPcb(true);
        setFootprint(QString());
    }

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        update();
    });
    
    rebuildPrimitives();
    QRectF b = boundingRect();
    createLabels(b.topLeft() + QPointF(0, -5), b.bottomLeft() + QPointF(0, 15));
}

GenericComponentItem::~GenericComponentItem() {
    m_primitiveItems.clear();
}

void GenericComponentItem::setSymbol(const SymbolDefinition& symbol) {
    m_symbol = symbol;
    rebuildPrimitives();
    QRectF b = boundingRect();
    createLabels(b.topLeft() + QPointF(0, -5), b.bottomLeft() + QPointF(0, 15));
    updateLabelText();
    update();
}

QJsonObject GenericComponentItem::toJson() const {
    // Start with all base class fields (spiceModel, excludeFromSim/Pcb, paramExpressions,
    // tolerances, isLocked, isMirroredX/Y, refLabel/valLabel positions, etc.)
    QJsonObject json = SchematicItem::toJson();
    json["type"] = m_symbol.name();
    json["id"] = m_id.toString();
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["manufacturer"] = manufacturer();
    json["mpn"] = mpn();
    json["description"] = description();
    json["value"] = m_value;
    json["name"] = m_name;
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["rotation"] = rotation();
    
    // Label offsets (using different keys from base to avoid overwriting)
    if (m_refLabelItem) {
        json["refX"] = m_refLabelItem->pos().x();
        json["refY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }

    QJsonObject overrides;
    for (auto it = m_pinModeOverrides.begin(); it != m_pinModeOverrides.end(); ++it) {
        overrides[QString::number(it.key())] = it.value();
    }
    json["pinModeOverrides"] = overrides;
    json["pinPadMapping"] = pinPadMappingToJson();
    
    return json;
}

bool GenericComponentItem::fromJson(const QJsonObject& json) {
    if (json.contains("id")) m_id = QUuid(json["id"].toString());
    if (json.contains("reference")) setReference(json["reference"].toString());
    if (json.contains("footprint")) setFootprint(json["footprint"].toString());
    if (json.contains("manufacturer")) setManufacturer(json["manufacturer"].toString());
    if (json.contains("mpn")) setMpn(json["mpn"].toString());
    if (json.contains("description")) setDescription(json["description"].toString());
    if (json.contains("value")) m_value = json["value"].toString();
    if (json.contains("name")) m_name = json["name"].toString();
    if (json.contains("spiceModel")) m_spiceModel = json["spiceModel"].toString();
    m_excludeFromSimulation = json["excludeFromSim"].toBool(false);
    m_excludeFromPcb = json["excludeFromPcb"].toBool(false);
    m_isLocked = json["isLocked"].toBool(false);
    m_isMirroredX = json["isMirroredX"].toBool(false);
    m_isMirroredY = json["isMirroredY"].toBool(false);

    // Restore param expressions and tolerances
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

    if (m_symbol.isPowerSymbol()) {
        setExcludeFromPcb(true);
        setFootprint(QString());
    }
    
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setRotation(json["rotation"].toDouble());
    
    rebuildPrimitives();
    QRectF b = boundingRect();
    createLabels(b.topLeft() + QPointF(0, -5), b.bottomLeft() + QPointF(0, 15));
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }

    if (json.contains("pinModeOverrides")) {
        QJsonObject overrides = json["pinModeOverrides"].toObject();
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            m_pinModeOverrides[it.key().toInt()] = it.value().toInt();
        }
    }
    loadPinPadMappingFromJson(json);
    
    updateLabelText();
    return true;
}

void GenericComponentItem::rebuildPrimitives() {
    for (auto* item : m_primitiveItems) {
        if (item) {
            item->setParentItem(nullptr);
            delete item;
        }
    }
    m_primitiveItems.clear();

    const QList<SymbolPrimitive> primitives = resolvedPrimitives();
    for (const auto& prim : primitives) {
        SymbolPrimitiveItem* visual = nullptr;
        switch (prim.type) {
            case SymbolPrimitive::Line:    visual = new SymbolLineItem(prim, this); break;
            case SymbolPrimitive::Rect:    visual = new SymbolRectItem(prim, this); break;
            case SymbolPrimitive::Circle:  visual = new SymbolCircleItem(prim, this); break;
            case SymbolPrimitive::Arc:     visual = new SymbolArcItem(prim, this); break;
            case SymbolPrimitive::Polygon: visual = new SymbolPolygonItem(prim, this); break;
            case SymbolPrimitive::Text: {
                auto* t = new SymbolTextItem(prim, this);
                t->setSymbolContext(name(), reference(), value());
                visual = t;
                break;
            }
            case SymbolPrimitive::Pin:     visual = new SymbolPinItem(prim, this); break;
            case SymbolPrimitive::Bezier:  visual = new SymbolBezierItem(prim, this); break;
            case SymbolPrimitive::Image:   visual = new SymbolImageItem(prim, this); break;
            default: break;
        }

        if (visual) {
            visual->setFlag(ItemIsSelectable, false);
            visual->setFlag(ItemIsMovable, false);
            m_primitiveItems.append(visual);
        }
    }
    update();
}

SchematicItem* GenericComponentItem::clone() const {
    GenericComponentItem* item = new GenericComponentItem(m_symbol);
    item->setPos(pos());
    item->setRotation(rotation());
    item->setId(QUuid::createUuid());
    item->setReference(m_reference); 
    item->setValue(m_value);
    item->setName(m_name);
    item->m_pinModeOverrides = m_pinModeOverrides;
    return item;
}

QList<QPointF> GenericComponentItem::connectionPoints() const {
    QList<QPointF> points;
    const QList<SymbolPrimitive> primitives = resolvedPrimitives();
    for (const auto& prim : primitives) {
        if (prim.type == SymbolPrimitive::Pin) {
            points.append(QPointF(prim.data["x"].toDouble(), prim.data["y"].toDouble()));
        }
    }
    return points;
}

QRectF GenericComponentItem::boundingRect() const {
    SymbolDefinition filtered = m_symbol.clone();
    filtered.clearPrimitives();
    const QList<SymbolPrimitive> primitives = resolvedPrimitives();
    for (const auto& prim : primitives) filtered.addPrimitive(prim);
    return filtered.boundingRect().adjusted(-10, -10, 10, 10);
}

void GenericComponentItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);
    
    // Draw selection highlight for the whole component
    if (option->state & QStyle::State_Selected) {
        painter->save();
        painter->setPen(QPen(QColor(0, 122, 204), 1.0, Qt::DashLine));
        painter->setBrush(QColor(0, 122, 204, 20));
        painter->drawRect(boundingRect().adjusted(-2, -2, 2, 2));
        painter->restore();
    }

    // Highlight pins if requested (e.g. for connectivity)
    drawConnectionPointHighlights(painter);
}

void GenericComponentItem::setPinMode(int pinNumber, int modeIndex) {
    if (modeIndex < 0) m_pinModeOverrides.remove(pinNumber);
    else m_pinModeOverrides[pinNumber] = modeIndex;
    rebuildPrimitives(); // Rebuild to update pin names if needed
}

int GenericComponentItem::getPinMode(int pinNumber) const {
    return m_pinModeOverrides.value(pinNumber, -1);
}

QList<SchematicItem::PinElectricalType> GenericComponentItem::pinElectricalTypes() const {
    QList<PinElectricalType> types;
    const QList<SymbolPrimitive> primitives = resolvedPrimitives();
    for (const auto& prim : primitives) {
        if (prim.type == SymbolPrimitive::Pin) {
            QString typeStr = prim.data.value("electricalType").toString("Passive");
            
            int pinNum = prim.data["number"].toInt();
            if (m_pinModeOverrides.contains(pinNum)) {
                int modeIdx = m_pinModeOverrides[pinNum];
                QJsonArray modes = prim.data.value("pinModes").toArray();
                if (modeIdx >= 0 && modeIdx < modes.size()) {
                    typeStr = modes[modeIdx].toObject()["type"].toString("Passive");
                }
            }

            PinElectricalType type = PassivePin;
            if      (typeStr == "Input") type = InputPin;
            else if (typeStr == "Output") type = OutputPin;
            else if (typeStr == "Bidirectional") type = BidirectionalPin;
            else if (typeStr == "Tri-state") type = TriStatePin;
            else if (typeStr == "Free") type = FreePin;
            else if (typeStr == "Unspecified") type = UnspecifiedPin;
            else if (typeStr == "Power Input") type = PowerInputPin;
            else if (typeStr == "Power Output") type = PowerOutputPin;
            else if (typeStr == "Open Collector") type = OpenCollectorPin;
            else if (typeStr == "Open Emitter") type = OpenEmitterPin;
            else if (typeStr == "Not Connected") type = NotConnectedPin;
            
            types.append(type);
        }
    }
    return types;
}
