#include "schematic_sheet_item.h"
#include <QPainter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>

SheetPinItem::SheetPinItem(const QString& name, HierarchicalPortItem::PortType type, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_name(name), m_portType(type) {
}

QRectF SheetPinItem::boundingRect() const {
    return QRectF(-8, -8, 16, 16);
}

void SheetPinItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QColor portColor = Qt::cyan;
    painter->setPen(QPen(portColor, 1.2));
    painter->setBrush(QColor(0, 255, 255, 40));
    
    // Draw OrCAD-style arrow/block for the port
    QPainterPath path;
    bool isLeft = (pos().x() < 5);
    
    if (m_portType == HierarchicalPortItem::Input) {
        // Arrow pointing INTO the block
        if (isLeft) {
            path.moveTo(-6, -4);
            path.lineTo(0, 0);
            path.lineTo(-6, 4);
            path.closeSubpath();
        } else {
            path.moveTo(6, -4);
            path.lineTo(0, 0);
            path.lineTo(6, 4);
            path.closeSubpath();
        }
    } else if (m_portType == HierarchicalPortItem::Output) {
        // Arrow pointing OUT of the block
        if (isLeft) {
            path.moveTo(0, 0);
            path.lineTo(-6, -4);
            path.lineTo(-6, 4);
            path.closeSubpath();
        } else {
            path.moveTo(0, 0);
            path.lineTo(6, -4);
            path.lineTo(6, 4);
            path.closeSubpath();
        }
    } else {
        // Diamond for bidirectional/passive
        path.moveTo(-4, 0);
        path.lineTo(0, -4);
        path.lineTo(4, 0);
        path.lineTo(0, 4);
        path.closeSubpath();
    }
    
    painter->drawPath(path);
    
    // Draw label
    painter->setPen(Qt::white);
    QFont font("Inter", 7, QFont::Medium);
    painter->setFont(font);
    
    if (isLeft) // Left side
        painter->drawText(QRectF(8, -10, 120, 20), Qt::AlignLeft | Qt::AlignVCenter, m_name);
    else // Right side
        painter->drawText(QRectF(-128, -10, 120, 20), Qt::AlignRight | Qt::AlignVCenter, m_name);
}

SchematicSheetItem::SchematicSheetItem(QPointF pos, QGraphicsItem* parent)
    : SchematicItem(parent), m_sheetName("NewSheet"), m_fileName("untitled.sch"), m_size(150, 100) {
    setPos(pos);
    
    m_nameItem = new QGraphicsTextItem(m_sheetName, this);
    m_nameItem->setPos(5, 5);
    m_nameItem->setDefaultTextColor(Qt::white);
    QFont nameFont("Inter", 9, QFont::Bold);
    m_nameItem->setFont(nameFont);
    
    m_fileItem = new QGraphicsTextItem(m_fileName, this);
    m_fileItem->setPos(5, 22);
    m_fileItem->setDefaultTextColor(QColor(180, 180, 180));
    QFont fileFont("Inter", 7);
    m_fileItem->setFont(fileFont);
}

void SchematicSheetItem::setSheetName(const QString& name) {
    m_sheetName = name;
    m_nameItem->setPlainText(name);
    update();
}

void SchematicSheetItem::setFileName(const QString& filename) {
    m_fileName = filename;
    m_fileItem->setPlainText(filename);
    update();
}

void SchematicSheetItem::updatePorts(const QString& basePath) {
    // Attempt to load the file
    QString filePath = m_fileName;
    if (QFileInfo(filePath).isRelative() && !basePath.isEmpty()) {
        filePath = basePath + "/" + m_fileName;
    }
    
    QFile file(filePath);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) return;
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return;
    
    QJsonArray items = doc.object()["items"].toArray();
    struct PortInfo { QString name; HierarchicalPortItem::PortType type; };
    QList<PortInfo> ports;
    
    for (const QJsonValue& val : items) {
        QJsonObject obj = val.toObject();
        QString type = obj["type"].toString();
        if (type == "HierarchicalPort" || type == "Hierarchical Port") {
            QString name = obj["label"].toString();
            if (name.isEmpty()) name = obj["value"].toString();
            if (!name.isEmpty()) {
                bool found = false;
                for (const auto& p : ports) if (p.name == name) { found = true; break; }
                if (!found) {
                    ports.append({name, static_cast<HierarchicalPortItem::PortType>(obj["portType"].toInt(0))});
                }
            }
        }
    }
    
    // 1. Identify pins to remove or update
    QList<SheetPinItem*> toRemove;
    for (auto* pin : m_pins) {
        bool stillExists = false;
        for (const auto& p : ports) {
            if (p.name == pin->name()) {
                stillExists = true;
                // If type changed, we'll recreate or update? For now just recreate if mismatch
                if (p.type != pin->portType()) stillExists = false; 
                break;
            }
        }
        if (!stillExists) toRemove << pin;
    }
    
    for (auto* pin : toRemove) {
        m_pins.removeAll(pin);
        delete pin;
    }
    
    // 2. Add new pins
    for (const auto& p : ports) {
        bool exists = false;
        for (auto* pin : m_pins) {
            if (pin->name() == p.name && pin->portType() == p.type) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_pins << new SheetPinItem(p.name, p.type, this);
        }
    }
    
    // 3. Re-layout (Input on left, Output on right, rest on left)
    QList<SheetPinItem*> leftPins, rightPins;
    for (auto* pin : m_pins) {
        if (pin->portType() == HierarchicalPortItem::Output) rightPins << pin;
        else leftPins << pin;
    }
    
    std::sort(leftPins.begin(), leftPins.end(), [](SheetPinItem* a, SheetPinItem* b) { return a->name() < b->name(); });
    std::sort(rightPins.begin(), rightPins.end(), [](SheetPinItem* a, SheetPinItem* b) { return a->name() < b->name(); });

    // Place left pins
    if (!leftPins.isEmpty()) {
        qreal spacing = m_size.height() / (leftPins.size() + 1);
        for (int i = 0; i < leftPins.size(); ++i) leftPins[i]->setPos(0, (i + 1) * spacing);
    }
    // Place right pins
    if (!rightPins.isEmpty()) {
        qreal spacing = m_size.height() / (rightPins.size() + 1);
        for (int i = 0; i < rightPins.size(); ++i) rightPins[i]->setPos(m_size.width(), (i + 1) * spacing);
    }
    
    update();
}

QList<QPointF> SchematicSheetItem::connectionPoints() const {
    QList<QPointF> points;
    for (auto* pin : m_pins) {
        points << pin->pos();
    }
    return points;
}

QRectF SchematicSheetItem::boundingRect() const {
    return QRectF(0, 0, m_size.width(), m_size.height());
}

void SchematicSheetItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option)
    Q_UNUSED(widget)

    painter->setRenderHint(QPainter::Antialiasing);
    QRectF rect = boundingRect();
    
    // Draw Main Body (Solid slate background like OrCAD blocks in dark mode)
    painter->setPen(QPen(Qt::cyan, 1.5));
    painter->setBrush(QColor(30, 41, 59, 250));
    painter->drawRect(rect);

    // Draw Interior Border (OrCAD double-line look)
    painter->setPen(QPen(QColor(0, 255, 255, 60), 0.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect.adjusted(3, 3, -3, -3));

    if (isSelected()) {
        painter->setPen(QPen(Qt::white, 1.5, Qt::DashLine));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect.adjusted(-3, -3, 3, 3));
    }
}

QJsonObject SchematicSheetItem::toJson() const {
    QJsonObject json;
    json["type"] = "Sheet";
    json["id"] = id().toString();
    json["x"] = pos().x();
    json["y"] = pos().y();
    json["sheetName"] = m_sheetName;
    json["fileName"] = m_fileName;
    json["width"] = m_size.width();
    json["height"] = m_size.height();
    return json;
}

bool SchematicSheetItem::fromJson(const QJsonObject& json) {
    if (json["type"].toString() != itemTypeName()) {
        return false;
    }
    setId(QUuid(json["id"].toString()));
    setPos(json["x"].toDouble(), json["y"].toDouble());
    setSheetName(json["sheetName"].toString());
    setFileName(json["fileName"].toString());
    m_size = QSizeF(json["width"].toDouble(100), json["height"].toDouble(60));
    return true;
}

SchematicItem* SchematicSheetItem::clone() const {
    SchematicSheetItem* copy = new SchematicSheetItem(pos(), parentItem());
    copy->setSheetName(m_sheetName);
    copy->setFileName(m_fileName);
    copy->m_size = m_size;
    return copy;
}
