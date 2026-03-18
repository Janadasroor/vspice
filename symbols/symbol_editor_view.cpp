#include "symbol_editor_view.h"
#include "theme_manager.h"
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QKeyEvent>
#include <QGraphicsItem>
#include <QPen>
#include <cmath>

SymbolEditorView::SymbolEditorView(QWidget* parent)
    : QGraphicsView(parent) {
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    setMouseTracking(true);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setFocusPolicy(Qt::StrongFocus);
    setInteractive(true);
    
    // Require items to be fully contained in the rubber band to be selected.
    // This allows selecting tiny items inside larger ones.
    setRubberBandSelectionMode(Qt::ContainsItemShape);

    PCBTheme* theme = ThemeManager::theme();
    setBackgroundBrush(QBrush(theme ? theme->canvasBackground() : QColor(30, 30, 30)));
}

void SymbolEditorView::setCurrentTool(int tool) {
    m_currentTool = tool;
    
    // Tools: Select=0, Line=1, Rect=2, Circle=3, Arc=4, Text=5, Pin=6, Polygon=7, Erase=8, ZoomArea=9, Anchor=10, Bezier=11, Image=12
    if (tool == 6) {
        // Pin tool: specialized crosshair for precision
        setCursor(Qt::CrossCursor);
    } else if ((tool >= 1 && tool <= 5) || tool == 7 || tool == 11 || tool == 12) {
        // General drawing tools: use pencil cursor
        QPixmap pixmap(":/icons/cursor_pencil.svg");
        setCursor(QCursor(pixmap.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation), 0, 23));
    } else if (tool == 8) {
        // Erase tool: use eraser cursor
        QPixmap pixmap(":/icons/cursor_eraser.svg");
        setCursor(QCursor(pixmap.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation), 7, 19));
    } else {
        // Default / Select / Zoom / Anchor
        setCursor(Qt::ArrowCursor);
    }
}

void SymbolEditorView::drawBackground(QPainter* painter, const QRectF& rect) {
    PCBTheme* theme = ThemeManager::theme();
    QColor bgColor = theme ? theme->canvasBackground() : QColor(30, 30, 30);
    painter->fillRect(rect, bgColor);
    
    // Safety check: if grid is too small or viewport is too large, skip fine grid
    qreal fineGrid = m_gridSize;
    if (fineGrid < 0.5) fineGrid = 10.0; // Default fallback if corrupted
    
    // Determine how many lines would be drawn
    qreal horizontalLines = rect.height() / fineGrid;
    qreal verticalLines = rect.width() / fineGrid;
    
    // If we'd draw more than 500 lines, only draw major grid to prevent hang
    bool drawFine = (horizontalLines < 500 && verticalLines < 500);
    qreal majorGrid = fineGrid * 10;
    
    qreal startX = std::floor(rect.left() / (drawFine ? fineGrid : majorGrid)) * (drawFine ? fineGrid : majorGrid);
    qreal startY = std::floor(rect.top() / (drawFine ? fineGrid : majorGrid)) * (drawFine ? fineGrid : majorGrid);
    
    QColor subColor = theme ? theme->gridSecondary() : QColor(120, 120, 130);
    subColor.setAlpha(110);
    QColor mainColor = theme ? theme->gridPrimary() : QColor(180, 180, 190);
    mainColor.setAlpha(200);

    QPen minorPen(subColor, 0.0);
    QPen majorPen(mainColor, 0.0);
    minorPen.setCosmetic(true);
    majorPen.setCosmetic(true);

    // Draw horizontal grid lines
    for (qreal y = startY; y < rect.bottom(); y += (drawFine ? fineGrid : majorGrid)) {
        if (qFuzzyIsNull(std::fmod(std::abs(y), majorGrid))) {
            painter->setPen(majorPen);
        } else {
            painter->setPen(minorPen);
        }
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }
    
    // Draw vertical grid lines
    for (qreal x = startX; x < rect.right(); x += (drawFine ? fineGrid : majorGrid)) {
        if (qFuzzyIsNull(std::fmod(std::abs(x), majorGrid))) {
            painter->setPen(majorPen);
        } else {
            painter->setPen(minorPen);
        }
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }

    // Draw Origin Crosshair
    QColor originColor = theme ? theme->accentColor() : QColor(0, 200, 255);
    originColor.setAlpha(160);
    QPen originPen(originColor);
    originPen.setWidth(0);
    originPen.setCosmetic(true);
    painter->setPen(originPen);
    
    qreal crossSize = majorGrid / 2.0;
    painter->drawLine(QLineF(-crossSize, 0, crossSize, 0));
    painter->drawLine(QLineF(0, -crossSize, 0, crossSize));
    
    // Draw a small circle at origin
    QColor originFill = originColor;
    originFill.setAlpha(80);
    painter->setBrush(originFill);
    painter->drawEllipse(QPointF(0, 0), fineGrid/2.0, fineGrid/2.0);
}

QPointF SymbolEditorView::snapToGrid(QPointF pos) const {
    if (!m_snapToGrid) return pos;
    return QPointF(std::round(pos.x() / m_gridSize) * m_gridSize,
                   std::round(pos.y() / m_gridSize) * m_gridSize);
}

void SymbolEditorView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_isPanning    = true;
        m_lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton) {
        emit rightClicked();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_currentTool > 0) {
            if (m_currentTool == 8) { // Erase tool
                QGraphicsItem* hit = itemAt(event->pos());
                if (hit) {
                    emit itemErased(hit);
                }
                event->accept();
                return;
            }
            m_isDrawing = true;
            m_drawStart = snapToGrid(mapToScene(event->pos()));
            emit pointClicked(m_drawStart);
            event->accept();
            return;
        } else {
            m_snapPressPos  = snapToGrid(mapToScene(event->pos()));
            QGraphicsItem* hit = itemAt(event->pos());
            bool isReal = false;
            QGraphicsItem* p = hit;
            while (p) {
                if (p->data(1).isValid()) { isReal = true; break; }
                p = p->parentItem();
            }
            setDragMode(isReal ? QGraphicsView::NoDrag
                               : QGraphicsView::RubberBandDrag);
        }
    }

    QGraphicsView::mousePressEvent(event);
}

void SymbolEditorView::mouseMoveEvent(QMouseEvent* event) {
    QPointF scenePos = mapToScene(event->pos());
    QPointF snapped = snapToGrid(scenePos);
    emit coordinatesChanged(scenePos);
    emit mouseMoved(snapped);

    if (m_isPanning) {
        QPoint delta = event->pos() - m_lastPanPoint;
        m_lastPanPoint = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }

    if (m_isDrawing) {
        clearPinAlignmentGuides();
        emit lineDragged(m_drawStart, snapToGrid(mapToScene(event->pos())));
        event->accept();
        return;
    }

    if (m_currentTool == 0 && (event->buttons() & Qt::LeftButton)) {
        updatePinAlignmentGuides();
    } else if (m_showVGuide || m_showHGuide) {
        clearPinAlignmentGuides();
    }

    QGraphicsView::mouseMoveEvent(event);
}

void SymbolEditorView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        m_isPanning = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_isDrawing) {
            m_isDrawing = false;
            clearPinAlignmentGuides();
            emit drawingFinished(m_drawStart, snapToGrid(mapToScene(event->pos())));
            event->accept();
            return;
        }

        if (m_currentTool == 0 && dragMode() != QGraphicsView::RubberBandDrag) {
            QPointF snapEnd = snapToGrid(mapToScene(event->pos()));
            QPointF delta   = snapEnd - m_snapPressPos;
            if (delta != QPointF(0, 0) && scene() && !scene()->selectedItems().isEmpty())
                emit itemsMoved(delta);
        }
        setDragMode(QGraphicsView::NoDrag);
        clearPinAlignmentGuides();
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void SymbolEditorView::wheelEvent(QWheelEvent* event) {
    const double factor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    scale(factor, factor);
    event->accept();
}

void SymbolEditorView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape)
        emit rightClicked();
    QGraphicsView::keyPressEvent(event);
}

void SymbolEditorView::contextMenuEvent(QContextMenuEvent* event) {
    emit contextMenuRequested(event->pos());
}

void SymbolEditorView::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);
    if (!m_showVGuide && !m_showHGuide) return;

    QPen guidePen(QColor(59, 130, 246, 110), 0.0, Qt::DashLine);
    guidePen.setCosmetic(true);
    painter->setPen(guidePen);
    if (m_showVGuide) {
        painter->drawLine(QLineF(m_vGuideX, rect.top(), m_vGuideX, rect.bottom()));
    }
    if (m_showHGuide) {
        painter->drawLine(QLineF(rect.left(), m_hGuideY, rect.right(), m_hGuideY));
    }
}

void SymbolEditorView::updatePinAlignmentGuides() {
    if (!scene()) return;

    QList<QGraphicsItem*> selected = scene()->selectedItems();
    QList<QPointF> selectedPins;
    QList<QPointF> otherPins;

    auto pinPoint = [](QGraphicsItem* item) -> QPointF {
        const qreal px = item->data(3).toDouble();
        const qreal py = item->data(4).toDouble();
        return item->mapToScene(QPointF(px, py));
    };

    for (QGraphicsItem* it : scene()->items()) {
        if (!it) continue;
        if (it->data(2).toString() != "pin") continue;
        if (it->isSelected()) selectedPins.append(pinPoint(it));
        else otherPins.append(pinPoint(it));
    }

    if (selectedPins.isEmpty() || otherPins.isEmpty()) {
        clearPinAlignmentGuides();
        return;
    }

    const qreal threshold = qMax<qreal>(2.0, m_gridSize * 0.4);
    qreal bestDx = threshold + 1.0;
    qreal bestDy = threshold + 1.0;
    bool foundX = false;
    bool foundY = false;
    qreal targetX = 0.0;
    qreal targetY = 0.0;

    for (const QPointF& sp : selectedPins) {
        for (const QPointF& op : otherPins) {
            const qreal dx = qAbs(sp.x() - op.x());
            if (dx < bestDx && dx <= threshold) {
                bestDx = dx;
                targetX = op.x();
                foundX = true;
            }
            const qreal dy = qAbs(sp.y() - op.y());
            if (dy < bestDy && dy <= threshold) {
                bestDy = dy;
                targetY = op.y();
                foundY = true;
            }
        }
    }

    m_showVGuide = foundX;
    m_showHGuide = foundY;
    m_vGuideX = targetX;
    m_hGuideY = targetY;
    viewport()->update();
}

void SymbolEditorView::clearPinAlignmentGuides() {
    if (!m_showVGuide && !m_showHGuide) return;
    m_showVGuide = false;
    m_showHGuide = false;
    viewport()->update();
}
