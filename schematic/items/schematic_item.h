#ifndef SCHEMATICITEM_H
#define SCHEMATICITEM_H

#include <QObject>
#include <QGraphicsItem>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QMap>

class SchematicItem : public QObject, public QGraphicsItem {
    Q_OBJECT
public:
    enum ItemType {
        ComponentType = QGraphicsItem::UserType + 1,
        WireType,
        LabelType,
        NetLabelType,
        JunctionType,
        NoConnectType,
        BusType,
        SheetType, // Hierarchical Sheet
        HierarchicalPortType,
        // Specific Component Types
        ResistorType,
        CapacitorType,
        InductorType,
        DiodeType,
        TransistorType,
        ICType,
        TransformerType,
        PowerType,
        VoltageSourceType,
        CurrentSourceType,
        SmartSignalType,
        SpiceDirectiveType,
        CustomType = QGraphicsItem::UserType + 100
    };

    enum PinElectricalType {
        PassivePin,
        InputPin,
        OutputPin,
        BidirectionalPin,
        TriStatePin,
        FreePin,
        UnspecifiedPin,
        PowerInputPin,
        PowerOutputPin,
        OpenCollectorPin,
        OpenEmitterPin,
        NotConnectedPin
    };

    SchematicItem(QGraphicsItem *parent = nullptr);
    virtual ~SchematicItem() = default;

    // Pure virtual methods that all schematic items must implement
    virtual QString itemTypeName() const = 0;
    virtual ItemType itemType() const = 0;

    // Common serialization (override in subclasses but call base)
    virtual QJsonObject toJson() const;
    virtual bool fromJson(const QJsonObject& json);
    
    // Reference designator prefix (e.g., "R" for resistor, "C" for capacitor)
    virtual QString referencePrefix() const { return "U"; }
    virtual QString referenceDisplayText() const;
    
    // Rebuild visual primitives (override in subclasses)
    virtual void rebuildPrimitives() {}

    // Common properties
    QUuid id() const { return m_id; }
    void setId(const QUuid& id) { m_id = id; }

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    virtual QString value() const { return m_value; }
    virtual void setValue(const QString& value) { 
        m_value = value; 
        updateLabelText();
        update();
    }
    
    // Reference designator (e.g., R1, C1, D1)
    QString reference() const { return m_reference; }
    void setReference(const QString& ref) { 
        m_reference = ref; 
        rebuildPrimitives();
        updateLabelText();
        update();
    }

    QString footprint() const { return m_footprint; }
    void setFootprint(const QString& footprint) { m_footprint = footprint; }

    QString manufacturer() const { return m_manufacturer; }
    void setManufacturer(const QString& manufacturer) { m_manufacturer = manufacturer; }

    QString mpn() const { return m_mpn; }
    void setMpn(const QString& mpn) { m_mpn = mpn; }

    QString description() const { return m_description; }
    void setDescription(const QString& description) { m_description = description; }

    QString spiceModel() const { return m_spiceModel; }
    void setSpiceModel(const QString& model) { m_spiceModel = model; }

    bool excludeFromSimulation() const { return m_excludeFromSimulation; }
    void setExcludeFromSimulation(bool exclude) { m_excludeFromSimulation = exclude; }

    bool excludeFromPcb() const { return m_excludeFromPcb; }
    void setExcludeFromPcb(bool exclude) { m_excludeFromPcb = exclude; }

    QMap<QString, QString> paramExpressions() const { return m_paramExpressions; }
    void setParamExpression(const QString& name, const QString& expr) { m_paramExpressions[name] = expr; }
    void clearParamExpressions() { m_paramExpressions.clear(); }

    QMap<QString, QString> tolerances() const { return m_tolerances; }
    void setTolerance(const QString& name, const QString& value) { m_tolerances[name] = value; }
    void clearTolerances() { m_tolerances.clear(); }

    QMap<QString, QString> pinPadMapping() const { return m_pinPadMapping; }
    void setPinPadMapping(const QMap<QString, QString>& mapping) { m_pinPadMapping = mapping; }
    void setPinPadMap(const QString& pinName, const QString& padName) { m_pinPadMapping[pinName] = padName; }
    QString mappedPadForPin(const QString& pinName) const { return m_pinPadMapping.value(pinName); }
    void clearPinPadMapping() { m_pinPadMapping.clear(); }

    // Cloning support
    virtual SchematicItem* clone() const = 0;

    // Highlighting
    void setHighlighted(bool highlighted) { 
        m_isHighlighted = highlighted; 
        if (!highlighted) m_highlightedPins.clear();
        update(); 
    }
    bool isHighlighted() const { return m_isHighlighted; }

    void setHighlightedPin(int index, bool highlighted = true) {
        if (highlighted) m_highlightedPins.insert(index);
        else m_highlightedPins.remove(index);
        update();
    }
    void clearHighlightedPins() { m_highlightedPins.clear(); update(); }
    bool isPinHighlighted(int index) const { return m_highlightedPins.contains(index); }
    QSet<int> highlightedPins() const { return m_highlightedPins; }

    // Locking
    bool isLocked() const { return m_isLocked; }
    void setLocked(bool locked) { 
        m_isLocked = locked; 
        setFlag(QGraphicsItem::ItemIsMovable, !locked);
        setFlag(QGraphicsItem::ItemIsSelectable, true); // Still selectable
        update(); 
    }

    bool isSelectedByDrag() const { return m_isSelectedByDrag; }
    void setSelectedByDrag(bool s) { m_isSelectedByDrag = s; }

    // Mirroring
    bool isMirroredX() const { return m_isMirroredX; }
    void setMirroredX(bool mirrored) { 
        m_isMirroredX = mirrored; 
        rebuildPrimitives();
        update(); 
    }

    bool isMirroredY() const { return m_isMirroredY; }
    void setMirroredY(bool mirrored) { 
        m_isMirroredY = mirrored; 
        rebuildPrimitives();
        update(); 
    }

    // Sub-item tracking (for draggable labels)
    bool isSubItem() const { return m_isSubItem; }
    void setSubItem(bool sub) { m_isSubItem = sub; }

    int unit() const { return m_unit; }
    void setUnit(int u) {
        m_unit = qMax(1, u);
        rebuildPrimitives();
        updateLabelText();
        update();
    }

    // Label management
    void createLabels(const QPointF& refOffset, const QPointF& valOffset);
    void updateLabelText();
    void resetLabels();
    QPointF referenceLabelPos() const;
    void setReferenceLabelPos(const QPointF& p);
    QPointF valueLabelPos() const;
    void setValueLabelPos(const QPointF& p);

    // Connectivity
    virtual QList<QPointF> connectionPoints() const { return QList<QPointF>(); }
    virtual QString pinName(int index) const { return QString::number(index + 1); }
    virtual QList<PinElectricalType> pinElectricalTypes() const {
        QList<PinElectricalType> types;
        for (int i = 0; i < connectionPoints().size(); ++i) types << PassivePin;
        return types;
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

public:
    // Bounding rect (required by QGraphicsItem)
    virtual QRectF boundingRect() const override = 0;

    // Painting (required by QGraphicsItem)
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override = 0;

protected:
    // Hover events to update connection point highlights
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override {
        QGraphicsItem::hoverEnterEvent(event);
        update(); // Trigger repaint to show connection highlights
    }
    
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override {
        QGraphicsItem::hoverLeaveEvent(event);
        update(); // Trigger repaint to hide connection highlights
    }

public:
    // Netlist and Simulation state
    void setPinNet(int pinIndex, const QString& netName) { m_pinNets[pinIndex] = netName; }
    QString pinNet(int pinIndex) const { return m_pinNets.value(pinIndex); }
    void clearPinNets() { m_pinNets.clear(); }

    virtual void setSimState(const QMap<QString, double>& nodeVoltages, const QMap<QString, double>& branchCurrents) {
        Q_UNUSED(nodeVoltages) Q_UNUSED(branchCurrents)
        m_powerDissipation = 0;
    }
    double powerDissipation() const { return m_powerDissipation; }
    
    static double parseValue(const QString& val) {
        if (val.isEmpty()) return 0;
        QString s = val.trimmed().toLower();
        double multiplier = 1.0;
        if (s.endsWith("k")) { multiplier = 1e3; s.chop(1); }
        else if (s.endsWith("m") && !s.endsWith("meg")) { multiplier = 1e-3; s.chop(1); }
        else if (s.endsWith("meg")) { multiplier = 1e6; s.chop(3); }
        else if (s.endsWith("u")) { multiplier = 1e-6; s.chop(1); }
        else if (s.endsWith("n")) { multiplier = 1e-9; s.chop(1); }
        else if (s.endsWith("p")) { multiplier = 1e-12; s.chop(1); }
        bool ok;
        double v = s.toDouble(&ok);
        return ok ? v * multiplier : 0;
    }
    virtual bool isInteractive() const { return false; }
    virtual void onInteractiveClick(const QPointF& scenePos) { Q_UNUSED(scenePos) }
    virtual void onInteractiveDoubleClick(const QPointF& scenePos) { Q_UNUSED(scenePos) }
    virtual void onInteractivePress(const QPointF& scenePos) { Q_UNUSED(scenePos) }
    virtual void onInteractiveRelease(const QPointF& scenePos) { Q_UNUSED(scenePos) }

signals:
    void interactiveStateChanged();

protected:
    // Helper to draw connection point highlights
    void drawConnectionPointHighlights(QPainter* painter) const;
    QJsonObject pinPadMappingToJson() const;
    void loadPinPadMappingFromJson(const QJsonObject& json);

private:
    void updateLabelRotation();

protected:
    QUuid m_id;
    int m_unit = 1; // Default to Unit A
    QString m_name;
    QString m_value;
    QString m_reference;  // Reference designator (R1, C1, D1, etc.)
    QString m_footprint;  // PCB Footprint name
    QString m_spiceModel; // SPICE model name
    QString m_manufacturer;
    QString m_mpn;
    QString m_description;
    bool m_isHighlighted; // Net highlighting
    QSet<int> m_highlightedPins; // Granular pin highlights
    bool m_isLocked;
    bool m_isSelectedByDrag = false;
    bool m_isMirroredX;
    bool m_isMirroredY;

    bool m_isSubItem;
    bool m_excludeFromSimulation = false;
    bool m_excludeFromPcb = false;
    class SchematicTextItem* m_refLabelItem = nullptr;
    class SchematicTextItem* m_valueLabelItem = nullptr;
    QPointF m_defaultRefOffset;
    QPointF m_defaultValOffset;
    QMap<int, QString> m_pinNets;
    QMap<QString, QString> m_paramExpressions;
    QMap<QString, QString> m_tolerances;
    QMap<QString, QString> m_pinPadMapping;
    double m_powerDissipation = 0;
};

#endif // SCHEMATICITEM_H
