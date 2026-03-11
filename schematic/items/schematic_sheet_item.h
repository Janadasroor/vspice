#ifndef SCHEMATIC_SHEET_ITEM_H
#define SCHEMATIC_SHEET_ITEM_H

#include "schematic_item.h"
#include "hierarchical_port_item.h"
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>

class SheetPinItem : public QGraphicsItem {
public:
    SheetPinItem(const QString& name, HierarchicalPortItem::PortType type, QGraphicsItem* parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QString name() const { return m_name; }
    HierarchicalPortItem::PortType portType() const { return m_portType; }
    
private:
    QString m_name;
    HierarchicalPortItem::PortType m_portType;
};

class SchematicSheetItem : public SchematicItem {
public:
    SchematicSheetItem(QPointF pos = QPointF(), QGraphicsItem* parent = nullptr);
    
    // SchematicItem interface
    QString itemTypeName() const override { return "Sheet"; }
    ItemType itemType() const override { return SheetType; }
    QJsonObject toJson() const override;
    bool fromJson(const QJsonObject& json) override;
    SchematicItem* clone() const override;
    
    QString value() const override { return m_sheetName; }
    void setValue(const QString& val) override { setSheetName(val); }

    // Sheet specific
    QString sheetName() const { return m_sheetName; }
    void setSheetName(const QString& name);
    
    QString fileName() const { return m_fileName; }
    void setFileName(const QString& filename);

    // Sync ports from child file
    void updatePorts(const QString& basePath = QString()); 

    // Connectivity
    QList<QPointF> connectionPoints() const override;

    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    QList<SheetPinItem*> getPins() const { return m_pins; }

private:
    QString m_sheetName;
    QString m_fileName;
    QSizeF m_size;
    
    QGraphicsTextItem* m_nameItem;
    QGraphicsTextItem* m_fileItem;
    
    QList<SheetPinItem*> m_pins;
};

#endif // SCHEMATIC_SHEET_ITEM_H
