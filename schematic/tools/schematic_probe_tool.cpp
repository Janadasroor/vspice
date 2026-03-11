#include "schematic_probe_tool.h"
#include "schematic_view.h"
#include "../editor/schematic_editor.h"
#include "flux/core/net_manager.h"
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
    case ProbeKind::Current:
        return "Interactive Current Probe (Click net/pin)";
    case ProbeKind::Power:
        return "Interactive Power Probe (Click net/pin)";
    case ProbeKind::Voltage:
    default:
        return "Interactive Voltage Probe (Click net/pin)";
    }
}

QCursor SchematicProbeTool::createProbeCursor(ProbeKind kind) {
    if (kind == ProbeKind::Voltage) {
        QPixmap pixmap(":/icons/p-v-probe.png");
        if (!pixmap.isNull()) return QCursor(pixmap, 4, 44);
    } else if (kind == ProbeKind::Current) {
        // Current used as Black/Negative probe for differential
        QPixmap pixmap(":/icons/n-v-probe.png");
        if (!pixmap.isNull()) return QCursor(pixmap, 4, 44);
    }
    
    // Fallback for Power/Logic
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QColor color = (kind == ProbeKind::Power) ? QColor(255, 105, 180) : QColor(0, 120, 215);
    QString letter = (kind == ProbeKind::Power) ? "W" : "L";

    p.setPen(QPen(Qt::black, 1));
    p.setBrush(color);
    QPolygon tip;
    tip << QPoint(4, 28) << QPoint(10, 18) << QPoint(14, 22);
    p.drawPolygon(tip);

    p.setBrush(Qt::white);
    p.drawRect(10, 4, 18, 18);
    p.setPen(color);
    p.setFont(QFont("Arial", 10, QFont::Bold));
    p.drawText(QRect(10, 4, 18, 18), Qt::AlignCenter, letter);
    
    return QCursor(pix, 4, 28);
}

QCursor SchematicProbeTool::cursor() const {
    return createProbeCursor(m_kind);
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
                    removeProbeMarker(view()->scene(), markerKey);
                    emit signalUnprobed(signalName);
                } else {
                    emit signalProbed(signalName);
                    placeProbeMarker(view()->scene(), scenePos, netName, kindTag);
                }
            }
            event->accept();
        }
    }
}

void SchematicProbeTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging && m_kind == ProbeKind::Voltage && view()) {
        QPointF scenePos = view()->mapToScene(event->pos());
        SchematicEditor* editor = qobject_cast<SchematicEditor*>(view()->window());
        if (editor && editor->netManager()) {
            QString netName = editor->netManager()->findNetAtPoint(scenePos);
            
            // If over a different net, show BLACK probe cursor
            if (!netName.isEmpty() && netName != m_startNetName) {
                view()->viewport()->setCursor(createProbeCursor(ProbeKind::Current)); // Current is Black
            } else {
                view()->viewport()->setCursor(createProbeCursor(ProbeKind::Voltage)); // Voltage is Red
            }
        }
        event->accept();
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
                emit signalDifferentialProbed(m_startNetName, endNetName);
                QToolTip::showText(event->globalPosition().toPoint(), 
                                 QString("Differential Probe: V(%1, %2)").arg(m_startNetName, endNetName), view());
            } else {
                // Single point Voltage Probe (treat as if it was a single click)
                const QString signalName = signalNameFor(m_startNetName, "V");
                const QString markerKey = markerKeyFor(m_startNetName, "V");

                if (findProbeMarker(view()->scene(), markerKey)) {
                    removeProbeMarker(view()->scene(), markerKey);
                    emit signalUnprobed(signalName);
                } else {
                    emit signalProbed(signalName);
                    // Place marker at the START point for single probe
                    placeProbeMarker(view()->scene(), scenePos, m_startNetName, "V");
                }
            }
        }
        
        // Restore standard cursor
        view()->viewport()->setCursor(cursor());
        event->accept();
    }
}
