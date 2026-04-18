#include "schematic_probe_tool.h"
#include "schematic_view.h"
#include "../editor/schematic_editor.h"
#include "net_manager.h"
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsSimpleTextItem>
#include "schematic_waveform_marker.h"
#include <QToolTip>
#include <QPainter>

SchematicProbeTool::SchematicProbeTool(const QString& toolName, ProbeKind kind)
    : SchematicTool(toolName), m_kind(kind) {
}

namespace {
SchematicItem* owningSchematicItem(QGraphicsItem* item) {
    SchematicItem* lastSchematic = nullptr;
    QGraphicsItem* current = item;
    while (current) {
        if (auto* schematic = dynamic_cast<SchematicItem*>(current)) {
            lastSchematic = schematic;
            if (!schematic->isSubItem()) {
                return schematic;
            }
        }
        current = current->parentItem();
    }
    return lastSchematic;
}

bool isProbeableSchematicComponent(SchematicItem* candidate) {
    if (!candidate || candidate->isSubItem()) return false;
    const auto type = candidate->itemType();
    return type != SchematicItem::WireType &&
           type != SchematicItem::LabelType &&
           type != SchematicItem::NetLabelType &&
           type != SchematicItem::JunctionType &&
           type != SchematicItem::BusType &&
           type != SchematicItem::NoConnectType &&
           type != SchematicItem::SpiceDirectiveType &&
           type != SchematicItem::SheetType;
}

SchematicItem* findProbeableComponentAt(SchematicView* view, const QPoint& viewPos, const QPointF& scenePos) {
    if (!view || !view->scene()) return nullptr;

    auto findFromItems = [](const QList<QGraphicsItem*>& items) -> SchematicItem* {
        for (QGraphicsItem* it : items) {
            SchematicItem* candidate = owningSchematicItem(it);
            if (isProbeableSchematicComponent(candidate)) {
                return candidate;
            }
        }
        return nullptr;
    };

    const QRect exactRect(viewPos.x() - 2, viewPos.y() - 2, 5, 5);
    if (SchematicItem* candidate = findFromItems(view->items(exactRect))) {
        return candidate;
    }

    constexpr qreal kBodyHitRadius = 8.0;
    const QRectF sceneRect(scenePos.x() - kBodyHitRadius,
                           scenePos.y() - kBodyHitRadius,
                           kBodyHitRadius * 2.0,
                           kBodyHitRadius * 2.0);
    return findFromItems(view->scene()->items(sceneRect,
                                              Qt::IntersectsItemBoundingRect,
                                              Qt::DescendingOrder,
                                              view->transform()));
}

QString markerKeyFor(const QString& netName, const QString& kindTag) {
    return (kindTag + ":" + netName).toUpper();
}

QString signalNameFor(const QString& netName, const QString& kindTag) {
    if (kindTag == "I") return "I(" + netName + ")";
    if (kindTag == "P") return "P(" + netName + ")";
    if (netName == "0" || netName.toUpper() == "GND") return "0 (GND)";
    return "V(" + netName + ")";
}

SchematicWaveformMarker* findProbeMarker(QGraphicsScene* scene, const QString& markerKey) {
    if (!scene) return nullptr;
    for (QGraphicsItem* item : scene->items()) {
        if (auto* marker = dynamic_cast<SchematicWaveformMarker*>(item)) {
            if ((marker->kind() + ":" + marker->netName()).toUpper() == markerKey) {
                return marker;
            }
        }
    }
    return nullptr;
}

QGraphicsItem* findProbeDot(QGraphicsScene* scene, const QString& markerKey) {
    if (!scene) return nullptr;
    for (QGraphicsItem* item : scene->items()) {
        if (item->data(0).toString() == "probe_dot" && item->data(1).toString() == markerKey) {
            return item;
        }
    }
    return nullptr;
}

void removeProbeMarker(QGraphicsScene* scene, const QString& markerKey) {
    if (!scene) return;
    if (auto* marker = findProbeMarker(scene, markerKey)) {
        scene->removeItem(marker);
        delete marker;
    }
    if (auto* dot = findProbeDot(scene, markerKey)) {
        scene->removeItem(dot);
        delete dot;
    }
}

void removeAllProbeMarkers(QGraphicsScene* scene) {
    if (!scene) return;
    QList<QGraphicsItem*> toDelete;
    for (QGraphicsItem* item : scene->items()) {
        if (dynamic_cast<SchematicWaveformMarker*>(item)) {
            toDelete.append(item);
            continue;
        }
        if (item->data(0).toString() == "probe_dot") {
            toDelete.append(item);
        }
    }
    for (QGraphicsItem* item : toDelete) {
        scene->removeItem(item);
        delete item;
    }
}

void placeProbeMarker(QGraphicsScene* scene, const QPointF& pos, const QString& netName, const QString& kindTag) {
    if (!scene || netName.isEmpty()) return;
    const QString markerKey = markerKeyFor(netName, kindTag);
    const QPointF markerPos = pos + QPointF(12.0, -34.0);

    // Check for existing marker
    if (auto* marker = findProbeMarker(scene, markerKey)) {
        marker->setPos(markerPos);
        if (auto* dotItem = findProbeDot(scene, markerKey)) {
            dotItem->setPos(pos);
        }
        return;
    }

    auto* marker = new SchematicWaveformMarker(netName, kindTag);
    marker->setPos(markerPos);
    scene->addItem(marker);

    // Professional locator dot (test point style)
    QColor dotColor(34, 197, 94); // V
    if (kindTag == "I") dotColor = QColor(245, 158, 11);
    else if (kindTag == "P") dotColor = QColor(239, 68, 68);
    
    QGraphicsEllipseItem* dot = new QGraphicsEllipseItem(-5, -5, 10, 10);
    dot->setBrush(dotColor);
    dot->setPen(QPen(Qt::white, 1));
    dot->setZValue(1199);
    dot->setPos(pos);
    
    // Add a concentric inner circle for a 'technical' look
    QGraphicsEllipseItem* inner = new QGraphicsEllipseItem(-2, -2, 4, 4, dot);
    inner->setBrush(Qt::white);
    inner->setPen(Qt::NoPen);

    dot->setData(0, "probe_dot");
    dot->setData(1, markerKey);
    dot->setAcceptedMouseButtons(Qt::NoButton);
    scene->addItem(dot);
}
} // namespace

QString SchematicProbeTool::tooltip() const {
    switch (m_kind) {
    case ProbeKind::DifferentialVoltage:
        return "Interactive Differential Voltage Probe (Click two nets)";
    case ProbeKind::Current:
        return "Interactive Current Probe (Click net/pin)";
    case ProbeKind::Power:
        return "Interactive Power Probe (Click net/pin)";
    case ProbeKind::Voltage:
    default:
        return "Interactive Voltage Probe (Click net/pin)";
    }
}

#include <QSvgRenderer>
#include <cmath>

SchematicProbeTool::ProbeCursorArt SchematicProbeTool::createProbeCursorArt(ProbeKind kind) {
    constexpr int kBaseSize = 32;
    constexpr int kDrawSize = 48;   // Visual size of probe
    constexpr int kPadding = 4;     // Extra margin to avoid clipping at edges
    constexpr int kCursorSize = kDrawSize + (kPadding * 2);
    const qreal scale = static_cast<qreal>(kDrawSize) / static_cast<qreal>(kBaseSize);
    const QPointF hotspotF(kPadding + (0.125 * kDrawSize),
                           kPadding + (0.875 * kDrawSize)); // (4,28) scaled from 32 + padding
    const QPoint hotspot(static_cast<int>(std::lround(hotspotF.x())),
                         static_cast<int>(std::lround(hotspotF.y())));

    if (kind == ProbeKind::Voltage || kind == ProbeKind::DifferentialVoltage) {
        QPixmap pixmap(kCursorSize, kCursorSize);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QString iconPath = (kind == ProbeKind::DifferentialVoltage)
            ? ":/icons/n-v-probe.svg"
            : ":/icons/p-v-probe.svg";
        QSvgRenderer renderer(iconPath);
        if (renderer.isValid()) {
            renderer.render(&painter, QRectF(kPadding, kPadding, kDrawSize, kDrawSize));
            return {pixmap, hotspot};
        }
    }

    // Fallback or Current/Power probe kinds
    QPixmap pix(kCursorSize, kCursorSize);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    // Color scheme
    QColor tipColor = QColor(220, 38, 38);
    if (kind == ProbeKind::DifferentialVoltage) tipColor = QColor(24, 24, 27);
    else if (kind == ProbeKind::Current) tipColor = QColor(245, 158, 11);
    else if (kind == ProbeKind::Power) tipColor = QColor(239, 68, 68);
    QColor bodyColor = QColor(209, 213, 219); // Light Gray
    QColor shadowColor = QColor(0, 0, 0, 100);

    // Tip at (4, 28) scaled - hotspot
    // The probe points from bottom-left towards top-right
    
    // Draw shadow for depth
    p.setPen(Qt::NoPen);
    p.setBrush(shadowColor);
    QPolygonF shadow;
    shadow << QPointF(5, 29) << QPointF(8, 20) << QPointF(29, 5) << QPointF(31, 7) << QPointF(10, 31);
    for (QPointF& pt : shadow) pt = pt * scale + QPointF(kPadding, kPadding);
    p.drawPolygon(shadow);

    // Draw Probe Body (the stick)
    p.setPen(QPen(Qt::black, 0.5 * scale));
    QLinearGradient bodyGrad(8 * scale, 24 * scale, 28 * scale, 4 * scale);
    bodyGrad.setColorAt(0, bodyColor);
    bodyGrad.setColorAt(1, bodyColor.darker(110));
    p.setBrush(bodyGrad);
    
    QPolygonF body;
    body << QPointF(8, 22) << QPointF(26, 4) << QPointF(29, 7) << QPointF(11, 25);
    for (QPointF& pt : body) pt = pt * scale + QPointF(kPadding, kPadding);
    p.drawPolygon(body);

    // Draw the active Tip (colored part)
    p.setBrush(tipColor);
    p.setPen(QPen(Qt::black, 0.5 * scale));
    QPolygonF tip;
    tip << QPointF(4, 28) << QPointF(8, 20) << QPointF(12, 24);
    for (QPointF& pt : tip) pt = pt * scale + QPointF(kPadding, kPadding);
    p.drawPolygon(tip);
    
    // Glossy overlay on tip
    p.setBrush(QColor(255, 255, 255, 60));
    QPolygonF gloss;
    gloss << QPointF(5, 26) << QPointF(7, 21) << QPointF(9, 23);
    for (QPointF& pt : gloss) pt = pt * scale + QPointF(kPadding, kPadding);
    p.drawPolygon(gloss);

    // Grip lines on handle
    p.setPen(QPen(QColor(0, 0, 0, 80), 1 * scale));
    p.drawLine(QPointF(15, 15) * scale + QPointF(kPadding, kPadding),
               QPointF(18, 18) * scale + QPointF(kPadding, kPadding));
    p.drawLine(QPointF(18, 12) * scale + QPointF(kPadding, kPadding),
               QPointF(21, 15) * scale + QPointF(kPadding, kPadding));
    p.drawLine(QPointF(21, 9) * scale + QPointF(kPadding, kPadding),
               QPointF(24, 12) * scale + QPointF(kPadding, kPadding));

    return {pix, hotspot};
}

QCursor SchematicProbeTool::createProbeCursor(ProbeKind kind) {
    const ProbeCursorArt art = createProbeCursorArt(kind);
    return QCursor(art.pixmap, art.hotspot.x(), art.hotspot.y());
}

QCursor SchematicProbeTool::cursor() const {
    return QCursor(Qt::BlankCursor);
}


void SchematicProbeTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    
    // Get the editor to access net manager
    SchematicEditor* editor = qobject_cast<SchematicEditor*>(view()->window());
    if (!editor) return;

    NetManager* netMgr = editor->netManager();
    if (!netMgr) return;

    QString netName = netMgr->findNetAtPoint(scenePos);
    
    if (event->button() == Qt::LeftButton) {
        if (!netName.isEmpty()) {
            if (m_kind == ProbeKind::Voltage) {
                // Potential start of differential probe
                m_isDragging = true;
                m_startNetName = netName;
                // We don't place a marker yet, wait for release to decide if single or diff
            } else {
                // Current/Power probe - single point
                QString kindTag = (m_kind == ProbeKind::Current) ? "I" : "P";
                const QString signalName = signalNameFor(netName, kindTag);
                const QString markerKey = markerKeyFor(netName, kindTag);

                if (findProbeMarker(view()->scene(), markerKey)) {
                    Q_EMIT signalClearFocusedPaneProbes();
                    // Don't remove all markers here anymore, let SimulationPanel handle selective removal
                    Q_EMIT signalProbed(signalName);
                    placeProbeMarker(view()->scene(), scenePos, netName, kindTag);
                } else {
                    Q_EMIT signalProbed(signalName);
                    placeProbeMarker(view()->scene(), scenePos, netName, kindTag);
                }
            }
            event->accept();
        } else if (m_kind == ProbeKind::Current || m_kind == ProbeKind::Power) {
            if (SchematicItem* compItem = findProbeableComponentAt(view(), event->pos(), scenePos)) {
                const QString ref = compItem->reference().trimmed();
                if (!ref.isEmpty()) {
                    const QString kindTag = (m_kind == ProbeKind::Current) ? "I" : "P";
                    Q_EMIT signalProbed(signalNameFor(ref, kindTag));
                    event->accept();
                }
            }
        }
    }
}

void SchematicProbeTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging && m_kind == ProbeKind::Voltage && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        SchematicEditor* editor = qobject_cast<SchematicEditor*>(view()->window());
        if (editor && editor->netManager()) {
            QString netName = editor->netManager()->findNetAtPoint(scenePos);
            
            // If over a different net, show the differential-voltage probe cursor.
            if (!netName.isEmpty() && netName != m_startNetName) {
                view()->setProbeCursorOverlay(ProbeKind::DifferentialVoltage, scenePos);
            } else {
                view()->setProbeCursorOverlay(ProbeKind::Voltage, scenePos);
            }
        }
        event->accept();
        return;
    }

    if (view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        view()->setProbeCursorOverlay(m_kind, scenePos);
        event->accept();
        return;
    }
}

void SchematicProbeTool::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isDragging && m_kind == ProbeKind::Voltage && view()) {
        m_isDragging = false;
        QPointF scenePos = view()->mapToScene(event->pos());
        SchematicEditor* editor = qobject_cast<SchematicEditor*>(view()->window());
        
        if (editor && editor->netManager()) {
            QString endNetName = editor->netManager()->findNetAtPoint(scenePos);
            
            if (!endNetName.isEmpty() && endNetName != m_startNetName) {
                // Differential Probe
                Q_EMIT signalDifferentialProbed(m_startNetName, endNetName);
                QToolTip::showText(event->globalPosition().toPoint(), 
                                 QString("Differential Probe: V(%1, %2)").arg(m_startNetName, endNetName), view());
            } else {
                // Single point Voltage Probe (treat as if it was a single click)
                const QString signalName = signalNameFor(m_startNetName, "V");
                const QString markerKey = markerKeyFor(m_startNetName, "V");

                if (findProbeMarker(view()->scene(), markerKey)) {
                    Q_EMIT signalClearFocusedPaneProbes();
                    // Don't remove all markers here anymore
                    Q_EMIT signalProbed(signalName);
                    placeProbeMarker(view()->scene(), scenePos, m_startNetName, "V");
                } else {
                    Q_EMIT signalProbed(signalName);
                    // Place marker at the START point for single probe
                    placeProbeMarker(view()->scene(), scenePos, m_startNetName, "V");
                }
            }
        }
        
        // Keep probe cursor visible after release
        if (view()) {
            view()->setProbeCursorOverlay(m_kind, scenePos);
        }
        event->accept();
    }
}
