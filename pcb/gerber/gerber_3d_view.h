#ifndef GERBER_3D_VIEW_H
#define GERBER_3D_VIEW_H

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector2D>
#include "gerber_layer.h"

struct Model3DInstance {
    QPointF pos;
    double rotation;
    // We would store the model data here (filename, offset, etc)
    // For now, we'll just render a box
    QVector3D size; // Approximate size
    QColor color;
};

/**
 * @brief 3D Visualization of Gerber layers
 */
class Gerber3DView : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit Gerber3DView(QWidget* parent = nullptr);
    
    void setLayers(const QList<GerberLayer*>& layers);
    void setComponents(const QList<Model3DInstance>& components);
    void resetView();
    void fitToBoard();
    
    void setCameraPos(const QVector3D& pos);
    void setCameraRotation(const QVector2D& rot);

public slots:
    void zoomIn();
    void zoomOut();

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateViewMatrix();
    void drawBoard();
    void drawLayer(GerberLayer* layer, float zOffset);

    QList<GerberLayer*> m_layers;
    QList<Model3DInstance> m_components;
    
    // View state
    float m_zoom; // Not used with m_fov, kept for compatibility if needed or remove
    float m_fov; 
    QVector3D m_cameraPos;
    QVector2D m_rotation; // Pitch (x) and Yaw (y) in degrees 
    QPoint m_lastMousePos;
    QPointF m_boardCenter;
    
    QMatrix4x4 m_viewMatrix;
    QMatrix4x4 m_projection;
};

#endif // GERBER_3D_VIEW_H
