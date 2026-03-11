#ifndef SCHEMATICWIRETOOL_H
#define SCHEMATICWIRETOOL_H

#include "schematic_tool.h"
#include <memory>
#include <QElapsedTimer>
#include "../analysis/wire_router.h"

class WireItem;
class QGraphicsEllipseItem;
class QLabel;

class SchematicWireTool : public SchematicTool {
    Q_OBJECT

public:
    SchematicWireTool(QObject* parent = nullptr);

    // SchematicTool interface
    QString tooltip() const override { return "Connect components with smart orthogonal wiring"; }
    QString iconName() const override { return "wire"; }
    QCursor cursor() const override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    
    void activate(SchematicView* view) override;
    void deactivate() override;

    QPointF snapPoint(QPointF scenePos) override {
        return snapToConnection(scenePos);
    }

    QMap<QString, QVariant> toolProperties() const override;
    void setToolProperty(const QString& name, const QVariant& value) override;

protected:
    QPointF snapToConnection(QPointF pos);

private:
    enum RoutingMode {
        ManhattanMode,
        FortyFiveMode,
        FreeMode
    };

    enum CaptureType {
        GridCapture,
        PinCapture,
        WireVertexCapture,
        WireSegmentCapture
    };

    // --- Core wire operations ---
    void finishWire();
    void cancelWire();
    void undoLastSegment();
    void updatePreview();
    void reset();

    // --- Routing helpers ---
    QList<WireItem*> handleComponentIntersections(WireItem* wire);
    QList<QPointF> buildRoutePoints(const QPointF& start, const QPointF& target, Qt::KeyboardModifiers modifiers) const;
    QList<QPointF> buildSmartRoute(const QPointF& start, const QPointF& target) const;
    QList<QPointF> buildManhattanRoute(const QPointF& start, const QPointF& target) const;
    QList<QPointF> buildFortyFiveRoute(const QPointF& start, const QPointF& target, Qt::KeyboardModifiers modifiers) const;

    // --- Visual feedback ---
    void updateSnapIndicator(const QPointF& pos, CaptureType captureType);
    void clearSnapIndicator();
    void updateCaptureCursor(bool captured);
    void ensureModeBadge();
    void updateModeBadge(Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    QString routingModeName() const;
    void updateCommittedPointMarkers();
    void clearCommittedPointMarkers();

    // --- State ---
    WireItem* m_currentWire;
    bool m_isDrawing;
    bool m_hFirst; // Toggle for H-V vs V-H routing
    QList<QPointF> m_committedPoints;
    QPointF m_lastSnappedPos;
    bool m_snappedToConnection = false;
    int m_clickCount = 0; // Track clicks for double-click detection
    
    // --- Properties ---
    double m_width;
    QString m_color;
    QString m_style;
    
    // --- Router & visual helpers ---
    std::unique_ptr<WireRouter> m_router;
    QGraphicsEllipseItem* m_snapIndicator = nullptr;
    QList<QGraphicsEllipseItem*> m_committedMarkers; // Visual dots at committed points
    QPointF m_lastPreviewTarget;
    QElapsedTimer m_previewThrottle;
    bool m_captureActive = false;
    CaptureType m_captureType = GridCapture;
    RoutingMode m_routingMode = ManhattanMode;
    Qt::KeyboardModifiers m_lastModifiers = Qt::NoModifier;
    QLabel* m_modeBadge = nullptr;
};

#endif // SCHEMATICWIRETOOL_H
