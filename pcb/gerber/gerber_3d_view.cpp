#include "gerber_3d_view.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

Gerber3DView::Gerber3DView(QWidget* parent)
    : QOpenGLWidget(parent) {
    resetView();
}

void Gerber3DView::setLayers(const QList<GerberLayer*>& layers) {
    m_layers = layers;
    fitToBoard(); // Auto-scale when layers change
    update();
}

void Gerber3DView::setComponents(const QList<Model3DInstance>& components) {
    m_components = components;
    update();
}

void Gerber3DView::resetView() {
    m_fov = 45.0f;
    m_cameraPos = QVector3D(0, 0, 100.0f);
    m_rotation = QVector2D(-45.0f, 25.0f); // Pitch and Yaw
    m_boardCenter = QPointF(0, 0);
    updateViewMatrix();
    update();
}

void Gerber3DView::setCameraPos(const QVector3D& pos) {
    m_cameraPos = pos;
    updateViewMatrix();
    update();
}

void Gerber3DView::setCameraRotation(const QVector2D& rot) {
    m_rotation = rot;
    updateViewMatrix();
    update();
}

void Gerber3DView::updateViewMatrix() {
    m_viewMatrix.setToIdentity();
    m_viewMatrix.translate(0, 0, -m_cameraPos.z());
    m_viewMatrix.rotate(m_rotation.x(), 1, 0, 0); // Pitch
    m_viewMatrix.rotate(m_rotation.y(), 0, 0, 1); // Row/Yaw
    m_viewMatrix.translate(-m_cameraPos.x(), -m_cameraPos.y(), 0);
    m_viewMatrix.translate(-m_boardCenter.x(), -m_boardCenter.y(), 0);
}

void Gerber3DView::fitToBoard() {
    QRectF bounds;
    if (m_layers.isEmpty()) {
        bounds = QRectF(-50, -50, 100, 100);
    } else {
        bool first = true;
        for (auto* layer : m_layers) {
            for (const auto& prim : layer->primitives()) {
                QRectF r = prim.path.boundingRect();
                if (prim.type == GerberPrimitive::Flash) {
                    r = QRectF(prim.center.x()-1, prim.center.y()-1, 2, 2); 
                }
                
                if (first) { bounds = r; first = false; }
                else bounds = bounds.united(r);
            }
        }
    }
    
    if (bounds.width() < 1 || bounds.height() < 1) bounds = QRectF(-50, -50, 100, 100);

    m_boardCenter = bounds.center();

    float maxDim = qMax(bounds.width(), bounds.height());
    float dist = (maxDim * 1.2f) / (2.0f * qTan(qDegreesToRadians(m_fov / 2.0f)));
    
    m_cameraPos.setZ(qBound(10.0f, dist, 5000.0f));
    updateViewMatrix();
    update();
}

void Gerber3DView::zoomIn() {
    m_cameraPos.setZ(m_cameraPos.z() * 0.8f);
    updateViewMatrix();
    update();
}

void Gerber3DView::zoomOut() {
    m_cameraPos.setZ(m_cameraPos.z() * 1.25f);
    updateViewMatrix();
    update();
}

void Gerber3DView::initializeGL() {
}

void Gerber3DView::resizeGL(int w, int h) {
    m_projection.setToIdentity();
    m_projection.perspective(m_fov, (float)w/h, 0.1f, 10000.0f);
}

void Gerber3DView::paintGL() {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    painter.fillRect(rect(), QColor(20, 20, 20));

    float visibleHeight = 2.0f * m_cameraPos.z() * qTan(qDegreesToRadians(m_fov / 2.0f));
    float pxScale = height() / visibleHeight;
    
    painter.save();
    painter.translate(width() / 2.0, height() / 2.0);
    painter.scale(pxScale, -pxScale); 
    
    painter.rotate(m_rotation.y()); 
    float pitchRad = qDegreesToRadians(m_rotation.x());
    painter.scale(1.0, qCos(pitchRad));

    painter.translate(-m_cameraPos.x(), -m_cameraPos.y());
    painter.translate(-m_boardCenter.x(), -m_boardCenter.y());

    // Find the board outline from Edge Cuts layer
    QPainterPath boardOutline;
    GerberLayer* edgeLayer = nullptr;
    
    for (auto* layer : m_layers) {
        QString name = layer->name().toLower();
        if (name.contains("edge") || name.contains("outline") || name.contains(".gm1") || name.contains(".gko")) {
            edgeLayer = layer;
            break;
        }
    }

    if (edgeLayer && !edgeLayer->primitives().isEmpty()) {
        for (const auto& prim : edgeLayer->primitives()) {
            if (boardOutline.isEmpty()) {
                boardOutline = prim.path;
            } else {
                boardOutline.addPath(prim.path);
            }
        }
    } else {
        QRectF bounds;
        bool hasAny = false;
        for (auto* layer : m_layers) {
            for (const auto& prim : layer->primitives()) {
                if (!hasAny) { bounds = prim.path.boundingRect(); hasAny = true; }
                else bounds = bounds.united(prim.path.boundingRect());
            }
        }
        if (hasAny) boardOutline.addRect(bounds.adjusted(-0.5, -0.5, 0.5, 0.5));
        else boardOutline.addRect(QRectF(-50, -50, 100, 100));
    }

    // Draw Substrate
    painter.setPen(QPen(QColor(0, 40, 0), 0.2)); 
    painter.setBrush(QColor(0, 60, 0, 245)); 
    painter.drawPath(boardOutline);

    // Render Layers
    for (auto* layer : m_layers) {
        if (!layer->isVisible()) continue;
        
        QColor c;
        QString name = layer->name().toLower();
        
        if (name.contains("cu") || name.contains("copper")) {
            c = QColor(184, 115, 51); // Copper
        } else if (name.contains("silk") || name.contains("legend")) {
            c = QColor(255, 255, 255); // White Silk
        } else if (name.contains("mask")) {
            c = QColor(0, 100, 0, 150); // Green Mask
        } else if (name.contains("edge") || name.contains("outline")) {
            c = QColor(255, 255, 0); // Yellow Edge
        } else {
            c = layer->color(); 
        }
        
        if (!name.contains("mask")) c.setAlpha(255);
        
        for (const auto& prim : layer->primitives()) {
            if (prim.type == GerberPrimitive::Line) {
                GerberAperture ap = layer->getAperture(prim.apertureId);
                double width = ap.params.isEmpty() ? 0.2 : ap.params[0];
                painter.setPen(QPen(c, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(prim.path);
            } else if (prim.type == GerberPrimitive::Flash) {
                GerberAperture ap = layer->getAperture(prim.apertureId);
                double d = ap.params.isEmpty() ? 0.5 : ap.params[0];
                painter.setPen(Qt::NoPen);
                painter.setBrush(c);
                
                if (ap.type == GerberAperture::Rectangle) {
                    double h = ap.params.size() > 1 ? ap.params[1] : d;
                    painter.drawRect(QRectF(prim.center.x() - d/2, prim.center.y() - h/2, d, h));
                } else {
                    painter.drawEllipse(prim.center, d/2, d/2);
                }
            }
        }
    }

    // Render Components (Simple 3D Boxes)
    for (const auto& comp : m_components) {
        painter.save();
        painter.translate(comp.pos.x(), comp.pos.y());
        painter.rotate(comp.rotation);
        
        double w = comp.size.x() > 0 ? comp.size.x() : 5.0;
        double h = comp.size.y() > 0 ? comp.size.y() : 5.0;
        
        painter.setPen(QPen(Qt::black, 0.1));
        painter.setBrush(QColor(40, 40, 40, 240)); 
        painter.drawRect(QRectF(-w/2, -h/2, w, h));
        
        painter.setBrush(QColor(200, 200, 200));
        painter.drawEllipse(QPointF(-w/2 + 0.5, -h/2 + 0.5), 0.5, 0.5);
        
        painter.restore();
    }
    
    painter.restore();
}

void Gerber3DView::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->pos();
}

void Gerber3DView::mouseMoveEvent(QMouseEvent* event) {
    QPoint delta = event->pos() - m_lastMousePos;
    
    if (event->buttons() & Qt::LeftButton) {
        if (event->modifiers() & Qt::ShiftModifier) {
            // Pan
            float visibleHeight = 2.0f * m_cameraPos.z() * qTan(qDegreesToRadians(m_fov / 2.0f));
            float sensitivity = visibleHeight / height();
            m_cameraPos.setX(m_cameraPos.x() - delta.x() * sensitivity);
            m_cameraPos.setY(m_cameraPos.y() + delta.y() * sensitivity);
        } else {
            // Rotate
            m_rotation.setY(m_rotation.y() + delta.x() * 0.5f);
            m_rotation.setX(qBound(-90.0f, m_rotation.x() - delta.y() * 0.5f, 0.0f));
        }
        updateViewMatrix();
        update();
    }
    m_lastMousePos = event->pos();
}

void Gerber3DView::keyPressEvent(QKeyEvent* event) {
    float moveStep = 5.0f;
    if (event->key() == Qt::Key_Left) m_cameraPos.setX(m_cameraPos.x() - moveStep);
    else if (event->key() == Qt::Key_Right) m_cameraPos.setX(m_cameraPos.x() + moveStep);
    else if (event->key() == Qt::Key_Up) m_cameraPos.setY(m_cameraPos.y() + moveStep);
    else if (event->key() == Qt::Key_Down) m_cameraPos.setY(m_cameraPos.y() - moveStep);
    else if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) zoomIn();
    else if (event->key() == Qt::Key_Minus) zoomOut();
    else {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    updateViewMatrix();
    update();
}

void Gerber3DView::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() > 0) zoomIn();
    else zoomOut();
}