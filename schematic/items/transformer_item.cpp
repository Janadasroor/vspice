#include "transformer_item.h"
#include "schematic_text_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QJsonObject>
#include <QJsonArray>

TransformerItem::TransformerItem(QPointF pos, QString value, QGraphicsItem *parent)
    : SchematicItem(parent), m_value(value) {
    setPos(pos);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);

    PCBTheme* theme = ThemeManager::theme();
    if (theme) {
        m_pen = QPen(theme->schematicLine(), 2);
        m_brush = QBrush(theme->schematicComponent());
    } else {
        m_pen = QPen(Qt::white, 2);
        m_brush = QBrush(Qt::NoBrush);
    }
    
    buildPrimitives();
    createLabels(QPointF(-15, -50), QPointF(-15, 50));
}

void TransformerItem::buildPrimitives() {
    // KiCad-style Transformer Symbol
    m_primitives.clear();
    
    // Core parameters
    const qreal coilRadius = 6.0;
    const int numWindings = 4;
    const qreal coilSpacing = 12.0; // Vertical spacing between coil centers
    const qreal coreSpacing = 6.0;  // Spacing between primary and secondary
    const qreal leadLength = 10.0;
    
    // Calculate total height to center align
    qreal totalHeight = (numWindings - 1) * coilSpacing + (coilRadius * 2);
    qreal startY = -totalHeight / 2.0;

    // --- Primary Winding (Left) ---
    qreal xPri = -coreSpacing - coilRadius;
    qreal y = startY;
    
    // Top Lead
    m_primitives.push_back(std::make_unique<LinePrimitive>(
        QPointF(xPri, y), QPointF(xPri, y - leadLength)));
        
    // Coils (Semicircles)
    for (int i = 0; i < numWindings; ++i) {
        m_primitives.push_back(std::make_unique<ArcPrimitive>(
            QRectF(xPri - coilRadius, y, coilRadius * 2, coilRadius * 2), 
            90 * 16, 180 * 16)); // Left facing semicircles
        y += coilSpacing;
    }
    
    // Bottom Lead (adjust y back to end of last coil)
    y = startY + (numWindings * coilSpacing); 
    // Correction: The loops stack. Last loop ends at startY + (numWindings-1)*spacing + 2*radius?
    // Let's stick to a simpler "bump" logic.
    // KiCad Inductor style: Series of 180 degree arcs.
    
    // Retrying logic for visually pleasing coils:
    m_primitives.clear();
    
    // Dimensions
    qreal r = 5.0; // Radius of each loop
    qreal d = 10.0; // Diameter / spacing
    qreal xLeft = -10.0;
    qreal xRight = 10.0;
    qreal topY = -20.0;
    
    // --- Primary Side (Left) ---
    // Top Lead
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(xLeft, -40), QPointF(xLeft, topY)));
    
    // 4 Loops
    for (int i = 0; i < 4; ++i) {
        // Arc rect (x, y, w, h)
        // Draw 180 degree arcs facing OUTWARD (left)
        // Rect needs to be centered on xLeft line
        m_primitives.push_back(std::make_unique<ArcPrimitive>(
            QRectF(xLeft - r, topY + (i * d), d, d), 
            90 * 16, 180 * 16)); 
    }
    
    // Bottom Lead
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(xLeft, topY + (4 * d)), QPointF(xLeft, 40)));

    // --- Secondary Side (Right) ---
    // Top Lead
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(xRight, -40), QPointF(xRight, topY)));
    
    // 4 Loops
    for (int i = 0; i < 4; ++i) {
        // Draw 180 degree arcs facing OUTWARD (right)
        m_primitives.push_back(std::make_unique<ArcPrimitive>(
            QRectF(xRight - r, topY + (i * d), d, d), 
            -90 * 16, 180 * 16)); 
    }
    
    // Bottom Lead
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(xRight, topY + (4 * d)), QPointF(xRight, 40)));
    
    // --- Core (Double Lines) ---
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(-3, -35), QPointF(-3, 35)));
    m_primitives.push_back(std::make_unique<LinePrimitive>(QPointF(3, -35), QPointF(3, 35)));

    // Pins
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(xLeft, -40), 2.5, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(xLeft, 40), 2.5, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(xRight, -40), 2.5, true));
    m_primitives.push_back(std::make_unique<CirclePrimitive>(QPointF(xRight, 40), 2.5, true));
}

void TransformerItem::setValue(const QString& value) {
    if (m_value != value) {
        m_value = value;
        buildPrimitives();
        update();
    }
}

QRectF TransformerItem::boundingRect() const {
    QRectF rect;
    for (const auto& prim : m_primitives) {
        rect = rect.united(prim->boundingRect());
    }
    return rect.adjusted(-5, -5, 5, 5);
}

void TransformerItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    for (const auto& prim : m_primitives) {
        prim->paint(painter, m_pen, m_brush);
    }
    
    // Draw highlighted connection points
    drawConnectionPointHighlights(painter);
    
    if (isSelected()) {
        PCBTheme* theme = ThemeManager::theme();
        if (theme) {
            painter->setPen(QPen(theme->selectionBox(), 1, Qt::DashLine));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(boundingRect().adjusted(2, 2, -2, -2));
        }
    }
}

QJsonObject TransformerItem::toJson() const {
    QJsonObject json;
    json["type"] = itemTypeName();
    json["id"] = id().toString();
    json["name"] = name();
    json["value"] = m_value;
    json["reference"] = reference();
    json["footprint"] = footprint();
    json["x"] = pos().x();
    json["y"] = pos().y();

    if (m_refLabelItem) {
        json["refX"] = m_refLabelItem->pos().x();
        json["refY"] = m_refLabelItem->pos().y();
    }
    if (m_valueLabelItem) {
        json["valX"] = m_valueLabelItem->pos().x();
        json["valY"] = m_valueLabelItem->pos().y();
    }
    json["pinPadMapping"] = pinPadMappingToJson();
    return json;
}

bool TransformerItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) return false;
    setId(QUuid(json["id"].toString()));
    setName(json["name"].toString());
    m_value = json["value"].toString();
    setReference(json["reference"].toString());
    setFootprint(json["footprint"].toString());
    loadPinPadMappingFromJson(json);
    setPos(QPointF(json["x"].toDouble(), json["y"].toDouble()));
    buildPrimitives();
    createLabels(QPointF(-15, -50), QPointF(-15, 50));
    if (json.contains("refX")) {
        setReferenceLabelPos(QPointF(json["refX"].toDouble(), json["refY"].toDouble()));
    }
    if (json.contains("valX")) {
        setValueLabelPos(QPointF(json["valX"].toDouble(), json["valY"].toDouble()));
    }
    update();
    return true;
}

SchematicItem* TransformerItem::clone() const {
    TransformerItem* newItem = new TransformerItem(pos(), m_value, parentItem());
    newItem->setName(name());
    return newItem;
}

QList<QPointF> TransformerItem::connectionPoints() const {
    qreal xLeft = -10.0;
    qreal xRight = 10.0;
    return { 
        QPointF(xLeft, -40), QPointF(xLeft, 40),
        QPointF(xRight, -40), QPointF(xRight, 40)
    };
}
