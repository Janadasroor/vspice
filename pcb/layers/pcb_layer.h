#ifndef PCBLAYER_H
#define PCBLAYER_H

#include <QString>
#include <QColor>
#include <QList>
#include <QMap>
#include <QObject>

// Represents a single PCB layer
class PCBLayer {
public:
    enum LayerType {
        Copper,
        Silkscreen,
        Soldermask,
        Paste,
        Courtyard,
        EdgeCuts,
        Drill,
        UserDefined
    };

    enum Side {
        Top,
        Bottom,
        Internal,
        Both  // For vias, edge cuts, etc.
    };

    PCBLayer(int id = 0, const QString& name = "", LayerType type = Copper, Side side = Top);

    // Accessors
    int id() const { return m_id; }
    QString name() const { return m_name; }
    LayerType type() const { return m_type; }
    Side side() const { return m_side; }
    QColor color() const { return m_color; }
    bool isVisible() const { return m_visible; }
    bool isLocked() const { return m_locked; }
    double opacity() const { return m_opacity; }

    // Mutators
    void setName(const QString& name) { m_name = name; }
    void setColor(const QColor& color) { m_color = color; }
    void setVisible(bool visible) { m_visible = visible; }
    void setLocked(bool locked) { m_locked = locked; }
    void setOpacity(double opacity) { m_opacity = qBound(0.0, opacity, 1.0); }

    // Utility
    QString typeString() const;
    QString sideString() const;
    bool isCopperLayer() const { return m_type == Copper; }
    bool isTopSide() const { return m_side == Top; }
    bool isBottomSide() const { return m_side == Bottom; }

private:
    int m_id;
    QString m_name;
    LayerType m_type;
    Side m_side;
    QColor m_color;
    bool m_visible;
    bool m_locked;
    double m_opacity;
};

// Manages all PCB layers in a design
class PCBLayerManager : public QObject {
    Q_OBJECT

public:
    static PCBLayerManager& instance();

    // Layer access
    const QList<PCBLayer>& layers() const { return m_layers; }
    PCBLayer* layer(int id);
    PCBLayer* layer(const QString& name);
    PCBLayer* activeLayer();
    int activeLayerId() const { return m_activeLayerId; }

    // Layer management
    void setActiveLayer(int id);
    void setActiveLayer(const QString& name);
    void setLayerVisible(int id, bool visible);
    void setLayerLocked(int id, bool locked);
    void toggleLayerVisibility(int id);

    // Copper layer specific
    QList<PCBLayer*> copperLayers();
    int copperLayerCount() const;
    void setCopperLayerCount(int count);

    // Standard layer IDs
    static const int TopCopper = 0;
    static const int BottomCopper = 1;
    static const int TopSilkscreen = 10;
    static const int BottomSilkscreen = 11;
    static const int TopSoldermask = 20;
    static const int BottomSoldermask = 21;
    static const int TopPaste = 30;
    static const int BottomPaste = 31;
    static const int EdgeCuts = 50;
    static const int Drills = 60;

    // Layer colors (default scheme)
    static QColor copperTopColor() { return QColor(220, 40, 40, 240); }      // Professional Red
    static QColor copperBottomColor() { return QColor(59, 130, 246, 230); }  // Tech Blue
    static QColor silkscreenColor() { return QColor(0, 255, 0, 240); }       // Neon Green
    static QColor soldermaskColor() { return QColor(20, 150, 50, 80); }      // Translucent Green
    static QColor edgeCutsColor() { return QColor(255, 255, 0); }            // Yellow
    static QColor drillColor() { return QColor(100, 100, 100); }             // Grey

    // Stackup structures
    struct StackupLayer {
        int layerId;
        QString name;
        QString type; // "Copper", "Dielectric", "Core", "Prepreg"
        double thickness; // mm
        double dielectricConstant; // Er
        QString material;
        double copperWeightOz; // Valid for copper layers
    };

    struct BoardStackup {
        QList<StackupLayer> stack;
        double finishThickness; // Total mm
        QString surfaceFinish;  // ENIG, HASL, OSP...
        double solderMaskExpansion; // Board-wide default (mm)
        double pasteExpansion;      // Board-wide default (mm)
    };

    BoardStackup stackup() const { return m_stackup; }
    void setStackup(const BoardStackup& stackup);
    void updateStackupFromLayerCount(int count);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

signals:
    void activeLayerChanged(int layerId);
    void layerVisibilityChanged(int layerId, bool visible);
    void layerListChanged();

private:
    PCBLayerManager();
    void initializeStandardLayers();

    QList<PCBLayer> m_layers;
    int m_activeLayerId;
    int m_copperLayerCount;  // 2 for 2-layer, 4 for 4-layer, etc.
    BoardStackup m_stackup;
};

#endif // PCBLAYER_H
