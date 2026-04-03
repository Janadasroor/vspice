#include "pcb_pour_tool.h"
#include "pcb_view.h"
#include "copper_pour_item.h"
#include "pcb_layer.h"
#include "theme_manager.h"
#include "pcb_commands.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QDebug>
#include <QUndoStack>

PCBPourTool::PCBPourTool(QObject* parent)
    : PCBTool("Polygon Pour", parent)
    , m_isDrawing(false)
    , m_previewPath(nullptr)
    , m_netName("GND")
    , m_clearance(0.3)
    , m_layerId(PCBLayerManager::instance().activeLayerId())
{
}

PCBPourTool::~PCBPourTool() {
    cancelPour();
}

QCursor PCBPourTool::cursor() const {
    return QCursor(Qt::CrossCursor);
}

QMap<QString, QVariant> PCBPourTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Net Name"] = m_netName;
    props["Clearance (mm)"] = m_clearance;
    props["Active Layer"] = m_layerId;
    return props;
}

void PCBPourTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Net Name") {
        m_netName = value.toString();
    } else if (name == "Clearance (mm)") {
        m_clearance = std::max(0.05, value.toDouble());
    } else if (name == "Active Layer") {
        m_layerId = value.toInt();
    }
}

void PCBPourTool::activate(PCBView* view) {
    PCBTool::activate(view);
    m_isDrawing = false;
    m_currentPolygon.clear();
}

void PCBPourTool::deactivate() {
    cancelPour();
    PCBTool::deactivate();
}

void PCBPourTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    if (event->button() == Qt::LeftButton) {
        if (!m_isDrawing) {
            startPour(snappedPos);
        } else {
            addPoint(snappedPos);
        }
        event->accept();
    } else if (event->button() == Qt::RightButton) {
        if (m_isDrawing) {
            if (m_currentPolygon.size() >= 3) {
                finishPour();
            } else {
                cancelPour();
            }
            event->accept();
        }
    }
}

void PCBPourTool::mouseMoveEvent(QMouseEvent* event) {
    if (!view()) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    if (m_isDrawing) {
        updatePreview(snappedPos);
    }

    event->accept();
}

void PCBPourTool::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;

    if (m_isDrawing && m_currentPolygon.size() >= 3) {
        finishPour();
    }

    event->accept();
}

void PCBPourTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (m_isDrawing) {
            if (m_currentPolygon.size() >= 3) {
                finishPour();
            } else {
                cancelPour();
            }
            event->accept();
            return;
        }
    } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_isDrawing && m_currentPolygon.size() >= 3) {
            finishPour();
            event->accept();
            return;
        }
    } else if (event->key() == Qt::Key_Backspace) {
        // Remove last point
        if (m_isDrawing && m_currentPolygon.size() > 1) {
            m_currentPolygon.removeLast();
            updatePreview(m_currentPolygon.last());
            event->accept();
            return;
        }
    }

    PCBTool::keyPressEvent(event);
}

void PCBPourTool::startPour(QPointF pos) {
    m_isDrawing = true;
    m_currentPolygon.clear();
    m_currentPolygon << pos;

    // Create preview path
    if (!m_previewPath && view()->scene()) {
        m_previewPath = new QGraphicsPathItem();
        
        // Get layer color for preview
        PCBLayer* layer = PCBLayerManager::instance().activeLayer();
        QColor previewColor = layer ? layer->color() : QColor(200, 50, 50);
        previewColor.setAlpha(100);

        QPen pen(previewColor.darker(120), 1, Qt::DashLine);
        m_previewPath->setPen(pen);
        m_previewPath->setBrush(Qt::NoBrush);
        m_previewPath->setZValue(1000);

        view()->scene()->addItem(m_previewPath);
    }

    updatePreview(pos);
    qDebug() << "Started copper pour at" << pos;
}

void PCBPourTool::addPoint(QPointF pos) {
    // Don't add duplicate points
    if (!m_currentPolygon.isEmpty() && m_currentPolygon.last() == pos) {
        return;
    }

    // Check if clicking near the first point to close
    if (m_currentPolygon.size() >= 3) {
        QPointF first = m_currentPolygon.first();
        double dist = QLineF(first, pos).length();
        if (dist < 5.0) {  // Close threshold
            finishPour();
            return;
        }
    }

    m_currentPolygon << pos;
    updatePreview(pos);
    qDebug() << "Added pour point" << pos << ", total points:" << m_currentPolygon.size();
}

void PCBPourTool::updatePreview(QPointF pos) {
    if (!m_previewPath) return;

    QPainterPath path;
    if (m_currentPolygon.size() >= 1) {
        // Draw the polygon so far
        path.moveTo(m_currentPolygon.first());
        for (int i = 1; i < m_currentPolygon.size(); ++i) {
            path.lineTo(m_currentPolygon[i]);
        }
        // Add line to current cursor position
        path.lineTo(pos);
        // Close back to first point (preview)
        path.lineTo(m_currentPolygon.first());
    }

    m_previewPath->setPath(path);
}

void PCBPourTool::finishPour() {
    if (m_currentPolygon.size() < 3) {
        cancelPour();
        return;
    }

    // Remove preview
    if (m_previewPath && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewPath);
        delete m_previewPath;
        m_previewPath = nullptr;
    }

    // Create the actual copper pour item
    if (view() && view()->scene()) {
        CopperPourItem* pour = new CopperPourItem();
        pour->setPolygon(m_currentPolygon);
        pour->closePolygon(); // Ensure the shape is closed
        pour->setFilled(true);
        pour->setPourType(Flux::Model::CopperPourModel::SolidPour);
        pour->setNetName(m_netName);
        pour->setClearance(m_clearance);
        pour->setLayer(m_layerId);

        if (view()->undoStack()) {
            view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), pour));
        } else {
            view()->scene()->addItem(pour);
        }
        qDebug() << "Created copper pour with" << m_currentPolygon.size() << "points on net" << m_netName;
    }

    m_isDrawing = false;
    m_currentPolygon.clear();
}

void PCBPourTool::cancelPour() {
    // Remove preview
    if (m_previewPath && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewPath);
        delete m_previewPath;
        m_previewPath = nullptr;
    }

    m_isDrawing = false;
    m_currentPolygon.clear();
    qDebug() << "Copper pour canceled";
}
