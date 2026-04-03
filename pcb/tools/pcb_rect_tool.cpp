#include "pcb_rect_tool.h"
#include "pcb_view.h"
#include "copper_pour_item.h"
#include "pcb_layer.h"
#include "pcb_commands.h"
#include <QGraphicsScene>
#include <QPen>
#include <QBrush>
#include <QUndoStack>

PCBRectTool::PCBRectTool(QObject* parent)
    : PCBTool("Rectangle", parent)
    , m_isDrawing(false)
    , m_previewRect(nullptr)
    , m_layerId(PCBLayerManager::instance().activeLayerId())
{
}

void PCBRectTool::activate(PCBView* view) {
    PCBTool::activate(view);
    m_isDrawing = false;
    m_layerId = PCBLayerManager::instance().activeLayerId();
}

void PCBRectTool::deactivate() {
    if (m_previewRect && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewRect);
        delete m_previewRect;
        m_previewRect = nullptr;
    }
    m_isDrawing = false;
    PCBTool::deactivate();
}

QMap<QString, QVariant> PCBRectTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Active Layer"] = m_layerId;
    return props;
}

void PCBRectTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Active Layer") {
        m_layerId = value.toInt();
    }
}

void PCBRectTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;

    QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));

    if (!m_isDrawing) {
        m_isDrawing = true;
        m_origin = pos;

        if (!m_previewRect) {
            m_previewRect = new QGraphicsRectItem();
            QColor color = QColor(200, 50, 50, 100);
            m_previewRect->setPen(QPen(color.darker(), 1, Qt::DashLine));
            m_previewRect->setBrush(QBrush(color));
            m_previewRect->setZValue(1000);
            view()->scene()->addItem(m_previewRect);
        }
        updatePreview(pos);
    } else {
        finishRect(pos);
    }
    event->accept();
}

void PCBRectTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDrawing) {
        QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));
        updatePreview(pos);
        event->accept();
    }
}

void PCBRectTool::mouseReleaseEvent(QMouseEvent* event) {
    // We use click-to-click, but support drag-to-release too
    if (m_isDrawing && event->button() == Qt::LeftButton) {
        QPointF pos = view()->snapToGrid(view()->mapToScene(event->pos()));
        if (QLineF(m_origin, pos).length() > 5.0) { // Dragged enough
            finishRect(pos);
            event->accept();
        }
    }
}

void PCBRectTool::updatePreview(const QPointF& pos) {
    if (m_previewRect) {
        m_previewRect->setRect(QRectF(m_origin, pos).normalized());
    }
}

void PCBRectTool::finishRect(const QPointF& pos) {
    if (!view() || !view()->scene()) return;

    QRectF rect = QRectF(m_origin, pos).normalized();
    if (rect.width() > 0.1 && rect.height() > 0.1) {
        QPolygonF poly;
        poly << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft() << rect.topLeft();

        CopperPourItem* pour = new CopperPourItem();
        pour->setPolygon(poly);
        pour->setFilled(true);
        pour->setLayer(m_layerId);
        pour->setNetName("GND"); // Default

        if (view()->undoStack()) {
            view()->undoStack()->push(new PCBAddItemCommand(view()->scene(), pour));
        } else {
            view()->scene()->addItem(pour);
        }
    }

    deactivate();
    activate(view()); // Reset for next rect
}
