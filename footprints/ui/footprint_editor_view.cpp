#include "footprint_editor_view.h"
#include "../footprint_editor.h"
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QAbstractGraphicsShapeItem>
#include <QScrollBar>

namespace {
QGraphicsItem* findResizeHandleItem(QGraphicsItem* item) {
    while (item) {
        if (item->data(0).toString() == "resize_handle") return item;
        item = item->parentItem();
    }
    return nullptr;
}
}

FootprintEditorView::FootprintEditorView(QWidget* parent)
    : QGraphicsView(parent), m_currentTool(0), m_gridSize(1.27), // 1.27mm (50mil) default
      m_isPanning(false), m_isDrawing(false), m_isMeasuring(false), m_snapToGrid(true)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(QColor(18, 18, 18))); // Deep dark background
    scale(10.0, 10.0); // improved zoom level for mm units
}

void FootprintEditorView::setGridSize(qreal size) {
    m_gridSize = size;
    update(); // Redraw
}

QPointF FootprintEditorView::snapToGrid(QPointF pos) const {
    // Priority 1: Snap to existing pad centers (Always enabled for precision)
    qreal snapRadius = 1.0; // mm
    QList<QGraphicsItem*> items = scene()->items(QRectF(pos.x() - snapRadius, pos.y() - snapRadius, 
                                                       snapRadius * 2, snapRadius * 2));
    for (auto* item : items) {
        if (qgraphicsitem_cast<QAbstractGraphicsShapeItem*>(item)) {
            // Check if it's likely a pad (pads have children like numbers)
            if (!item->childItems().isEmpty()) {
                return item->scenePos();
            }
        }
    }

    // Priority 2: Standard Grid Snapping (if enabled)
    if (!m_snapToGrid) return pos;

    qreal x = std::round(pos.x() / m_gridSize) * m_gridSize;
    qreal y = std::round(pos.y() / m_gridSize) * m_gridSize;
    return QPointF(x, y);
}

void FootprintEditorView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::RightButton) {
        // Cancel current tool
        if (m_currentTool != 0) { // Assuming 0 is Select
             m_isDrawing = false;
             m_isMeasuring = false;
             viewport()->update();
             emit toolCancelled();
             return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        if (m_currentTool == 0) {
            if (QGraphicsItem* handle = findResizeHandleItem(itemAt(event->pos()))) {
                m_rectResizeActive = true;
                emit rectResizeStarted(handle->data(1).toString(), snapToGrid(mapToScene(event->pos())));
                event->accept();
                return;
            }
        }

        QPointF scenePos = snapToGrid(mapToScene(event->pos()));
        
        if (m_currentTool == 0) { // Select
            QGraphicsView::mousePressEvent(event);
        } else {
            m_isDrawing = true;
            m_drawStart = scenePos;
            if (m_currentTool == FootprintEditor::Measure) {
                m_measureCurrent = scenePos;
            }
            emit pointClicked(scenePos);
        }
    }
}

void FootprintEditorView::mouseMoveEvent(QMouseEvent* event) {
    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPoint;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        m_lastPanPoint = event->pos();
        return;
    }

    QPointF scenePos = snapToGrid(mapToScene(event->pos()));
    emit mouseMoved(scenePos);

    if (m_isDrawing) {
         if (m_currentTool == FootprintEditor::Measure) {
             m_measureCurrent = scenePos;
             viewport()->update(); // Trigger redraw
         }
         emit lineDragged(m_drawStart, scenePos);
    }
    if (m_rectResizeActive) {
        emit rectResizeUpdated(scenePos);
        event->accept();
        return;
    }
    
    QGraphicsView::mouseMoveEvent(event);
}

void FootprintEditorView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }

    if (event->button() == Qt::LeftButton && m_isDrawing) {
        m_isDrawing = false;
        QPointF scenePos = snapToGrid(mapToScene(event->pos()));
        emit drawingFinished(m_drawStart, scenePos);
        viewport()->update();
    }
    if (event->button() == Qt::LeftButton && m_rectResizeActive) {
        m_rectResizeActive = false;
        emit rectResizeFinished(snapToGrid(mapToScene(event->pos())));
        event->accept();
        return;
    }
    
    QGraphicsView::mouseReleaseEvent(event);
}

void FootprintEditorView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom
        double scaleFactor = 1.15;
        if (event->angleDelta().y() < 0) scaleFactor = 1.0 / scaleFactor;
        scale(scaleFactor, scaleFactor);
    } else {
        QGraphicsView::wheelEvent(event);
    }
}

void FootprintEditorView::drawBackground(QPainter* painter, const QRectF& rect) {
    Q_UNUSED(rect);
    painter->fillRect(rect, QColor(14, 15, 17)); // Modern Dark Horizon color
    
    // Draw major and minor grid lines
    qreal fineGrid = m_gridSize;
    qreal majorGrid = m_gridSize * 10;
    
    qreal startX = std::floor(rect.left() / fineGrid) * fineGrid;
    qreal startY = std::floor(rect.top() / fineGrid) * fineGrid;
    
    QPen minorPen(QColor(40, 42, 48), 0.0);
    QPen majorPen(QColor(60, 64, 72), 0.0);
    minorPen.setCosmetic(true);
    majorPen.setCosmetic(true);

    // Draw horizontal grid lines
    for (qreal y = startY; y < rect.bottom(); y += fineGrid) {
        if (qFuzzyIsNull(std::fmod(std::abs(y), majorGrid))) {
            painter->setPen(majorPen);
        } else {
            painter->setPen(minorPen);
        }
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }
    
    // Draw vertical grid lines
    for (qreal x = startX; x < rect.right(); x += fineGrid) {
        if (qFuzzyIsNull(std::fmod(std::abs(x), majorGrid))) {
            painter->setPen(majorPen);
        } else {
            painter->setPen(minorPen);
        }
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }

    // Draw Origin Crosshair
    QPen originPen(QColor(0, 200, 255, 160)); // Vibrant cyan
    originPen.setWidth(0);
    originPen.setCosmetic(true);
    painter->setPen(originPen);
    
    qreal crossSize = majorGrid / 2.0;
    painter->drawLine(QLineF(-crossSize, 0, crossSize, 0));
    painter->drawLine(QLineF(0, -crossSize, 0, crossSize));
    
    // Draw a small circle at origin
    painter->setBrush(QColor(0, 200, 255, 80));
    painter->drawEllipse(QPointF(0, 0), fineGrid/2.0, fineGrid/2.0);
}

void FootprintEditorView::drawForeground(QPainter* painter, const QRectF& rect) {
    Q_UNUSED(rect);
    if (m_isDrawing && m_currentTool == FootprintEditor::Measure) {
        painter->save();
        QPointF start = m_drawStart;
        QPointF end = m_measureCurrent;
        
        QPen measurePen(QColor(0, 255, 255, 200)); 
        measurePen.setWidth(0); 
        measurePen.setStyle(Qt::DashLine);
        painter->setPen(measurePen);
        painter->drawLine(start, end);
        
        qreal dist = QLineF(start, end).length();
        qreal dx = end.x() - start.x();
        qreal dy = end.y() - start.y();
        
        QString text = QString("L: %1mm | ΔX: %2 | ΔY: %3")
                       .arg(dist, 0, 'f', 2).arg(dx, 0, 'f', 2).arg(dy, 0, 'f', 2);
        
        double s = transform().m11();
        painter->translate(end + QPointF(5/s, 5/s));
        painter->scale(1.0/s, 1.0/s);
        
        QFont font("Inter", 10, QFont::Bold);
        QFontMetrics fm(font);
        QRect textRect = fm.boundingRect(text).adjusted(-8, -4, 8, 4);
        
        painter->setBrush(QColor(0, 0, 0, 220));
        painter->setPen(QPen(QColor(0, 255, 255, 100), 1));
        painter->drawRoundedRect(textRect, 4, 4);
        
        painter->setPen(Qt::white);
        painter->setFont(font);
        painter->drawText(textRect, Qt::AlignCenter, text);
        painter->restore();
    }
}

void FootprintEditorView::contextMenuEvent(QContextMenuEvent* event) {
    emit contextMenuRequested(event->pos());
}
