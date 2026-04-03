#ifndef PCBITEM_H
#define PCBITEM_H

#include <QGraphicsObject>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector3D>

class PCBItem : public QGraphicsObject {
    Q_OBJECT

public:
    enum ItemType {
        PadType = QGraphicsItem::UserType + 1,
        ViaType,
        TraceType,
        ComponentType,
        CopperPourType,
        RatsnestType,
        // Reserve space for custom types
        CustomType = QGraphicsItem::UserType + 100
    };

    PCBItem(QGraphicsItem *parent = nullptr);
    virtual ~PCBItem() = default;

    // Virtual methods - base implementation handles common properties
    virtual QString itemTypeName() const = 0;
    virtual ItemType itemType() const = 0;
    int type() const override { return itemType(); }
    virtual QJsonObject toJson() const;
    virtual bool fromJson(const QJsonObject& json);

    // Common properties
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }
    QString idString() const { return m_id.toString(); }

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QString netName() const { return m_netName; }
    void setNetName(const QString& net) { m_netName = net; }

    double height() const { return m_height; }
    void setHeight(double h) { m_height = h; update(); }

    QString modelPath() const { return m_modelPath; }
    void setModelPath(const QString& path) { m_modelPath = path; update(); }

    double modelScale() const { return m_modelScale; }
    void setModelScale(double s) { m_modelScale = s; update(); }
    QVector3D modelOffset() const { return m_modelOffset; }
    void setModelOffset(const QVector3D& offset) { m_modelOffset = offset; update(); }
    QVector3D modelRotation() const { return m_modelRotation; }
    void setModelRotation(const QVector3D& rotation) { m_modelRotation = rotation; update(); }
    QVector3D modelScale3D() const { return m_modelScale3D; }
    void setModelScale3D(const QVector3D& scale) { m_modelScale3D = scale; update(); }

    bool isLocked() const { return m_isLocked; }
    void setLocked(bool locked) { 
        m_isLocked = locked; 
        updateFlags();
        update();
    }

    // Update flags based on parent and lock state
    virtual void updateFlags() {
        bool hasPcBParent = dynamic_cast<PCBItem*>(parentItem()) != nullptr;
        bool selectable = !hasPcBParent;
        
        setFlag(QGraphicsItem::ItemIsSelectable, selectable);
        // We handle movement manually in the tool, so disable built-in movable flag
        // to avoid conflicts and independent movement of child items.
        setFlag(QGraphicsItem::ItemIsMovable, false);
    }

    // Layer management
    int layer() const { return m_layer; }
    virtual void setLayer(int layer) { m_layer = layer; }

    // Selection and editing
    virtual bool isEditable() const { return true; }
    virtual void startEditing() {}
    virtual void finishEditing() {}
    virtual void updateConnectivity() {}

    // Cloning support
    virtual PCBItem* clone() const = 0;

    // Bounding rect (required by QGraphicsItem)
    virtual QRectF boundingRect() const override = 0;

    // Painting (required by QGraphicsItem)
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override = 0;

protected:
    // Helper to draw a consistent professional selection glow around items
    void drawSelectionGlow(QPainter* painter) const;

    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    QUuid m_id;
    QString m_name;
    QString m_netName;
    int m_layer;
    bool m_isLocked;
    double m_height; // mm
    QString m_modelPath;
    double m_modelScale;
    QVector3D m_modelOffset;
    QVector3D m_modelRotation;
    QVector3D m_modelScale3D;
};

#endif // PCBITEM_H
