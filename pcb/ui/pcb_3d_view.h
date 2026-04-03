#ifndef PCB_3D_VIEW_H
#define PCB_3D_VIEW_H

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGraphicsScene>
#include <QTimer>
#include <QVector>
#include <QUuid>
#include <QHash>
#include <QSet>
#include <QColor>
#include <QLibrary>

class PCB3DView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit PCB3DView(QWidget* parent = nullptr);
    ~PCB3DView() override;

    void setScene(QGraphicsScene* scene);
    void updateScene();

    void setShowSubstrate(bool enabled);
    void setShowCopper(bool enabled);
    void setShowTopCopper(bool enabled);
    void setShowBottomCopper(bool enabled);
    void setShowSilkscreen(bool enabled);
    void setShowComponents(bool enabled);
    void setShowGrid(bool enabled);
    void setSelectedOnly(bool enabled);
    void setNetFilter(const QString& netName);
    void setSubstrateAlpha(float alpha);
    void setRaytracingEnabled(bool enabled);
    void setOrthographic(bool enabled);
    void setExplodeAmount(float mm);
    void setSoldermaskAlpha(float alpha);
    void setSoldermaskColor(const QColor& color);
    void setCopperTopColor(const QColor& color);
    void setCopperBottomColor(const QColor& color);
    void setComponentColor(const QColor& color);
    void setComponentAlpha(float alpha);
    void setMeasureMode(bool enabled);
    void clearMeasurement();
    bool setSpaceMouseEnabled(bool enabled);
    int detectComponentCollisions();

    void resetCamera();
    void setViewPreset(const QString& preset);

signals:
    void componentPicked(const QUuid& id);
    void measurementUpdated(double distanceMm);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class RenderMode { Full, Fast };
    enum class MaterialKind { SolderMask, Dielectric, CopperTop, CopperBottom, CopperInner, Plating, Silkscreen, Plastic, ComponentMetal, ComponentLED, Collision, AxisX, AxisY, AxisZ };

public:
    struct Vertex {
        QVector3D pos;
        QVector3D nrm;
    };

private:
    struct PickProxy {
        QUuid id;
        QVector3D bmin;
        QVector3D bmax;
    };

    struct ComponentDraw {
        QUuid id;
        MaterialKind material = MaterialKind::Plastic;
        float alpha = 1.0f;
        QVector<Vertex> vertices;
    };

    struct ObjMesh {
        QVector<Vertex> vertices;
    };

    void initShaders();
    void rebuildSceneCache();
    void updateProjectionMatrix();
    void drawBatch(const QVector<Vertex>& verts, MaterialKind material, float alpha = 1.0f);
    void drawAxisTriadOverlay();
    void drawGridOverlay();
    void drawMeasurementOverlay();

    bool passesSelectionFilter(QGraphicsItem* item) const;

    ObjMesh loadOBJ(const QString& path);
    ObjMesh loadObjMeshFromText(const QString& text) const;
    ObjMesh loadVrmlMeshFromText(const QString& text) const;
    ObjMesh loadStepMeshFromText(const QString& text) const;
    ObjMesh loadIgesMeshFromText(const QString& text) const;
    QString expandModelEnvVars(const QString& rawPath) const;
    QStringList modelSearchRoots() const;
    QString resolveModelPath(const QString& rawPath) const;
    QVector<Vertex> makeBoxVertices(float hx, float hy, float hz) const;

    bool pickAt(const QPoint& pos, QUuid& outId) const;
    bool intersectBoardPlane(const QPoint& pos, QVector3D& out) const;
    bool gpuPickAt(const QPoint& pos, QUuid& outId);
    void initPickShader();
    void initShadowMap();
    void ensurePickFbo();
    QMatrix4x4 currentViewMatrix() const;
    QMatrix4x4 currentShadowMatrix() const;
    QVector3D unprojectToWorld(const QPoint& p, float ndcZ) const;
    bool rayIntersectsAabb(const QVector3D& ro, const QVector3D& rd,
                           const QVector3D& bmin, const QVector3D& bmax,
                           float& outT) const;

    void beginInteractiveRender();
    void pollSpaceMouse();
    void tickCameraAnimation();
    void tickSpinAnimation();
    void startCameraTransition(const QQuaternion& targetRot, float targetZoom);

    QGraphicsScene* m_scene = nullptr;

    QOpenGLShaderProgram m_shader;
    QOpenGLShaderProgram m_pickShader;
    QOpenGLShaderProgram m_shadowShader;
    QOpenGLBuffer m_streamVbo{QOpenGLBuffer::VertexBuffer};
    mutable QOpenGLFramebufferObject* m_pickFbo = nullptr;
    mutable QOpenGLFramebufferObject* m_shadowFbo = nullptr;
    GLuint m_shadowDepthTex = 0;

    QMap<QString, ObjMesh> m_objCache;

    QVector<Vertex> m_substrateVerts;
    QVector<Vertex> m_dielectricVerts;
    QVector<Vertex> m_copperTopVerts;
    QVector<Vertex> m_copperInnerVerts;
    QVector<Vertex> m_copperBottomVerts;
    QVector<Vertex> m_silkscreenVerts;
    QVector<Vertex> m_platedHoleVerts;
    QVector<Vertex> m_componentVerts;
    QVector<ComponentDraw> m_componentDraws;
    QVector<PickProxy> m_pickProxies;
    QSet<QUuid> m_collidedComponents;
    mutable QHash<int, QUuid> m_pickIdToUuid;
    bool m_sceneDirty = true;

    bool m_showSubstrate = true;
    bool m_showCopperTop = true;
    bool m_showCopperBottom = true;
    bool m_showSilkscreen = true;
    bool m_showComponents = true;
    bool m_showGrid = false;
    bool m_selectedOnly = false;
    bool m_raytracingEnabled = false;
    bool m_orthographic = false;
    float m_explodeAmount = 0.0f;
    QString m_netFilter;
    float m_substrateAlpha = 1.0f;
    float m_soldermaskAlpha = 1.0f;
    float m_componentAlpha = 1.0f;
    QColor m_soldermaskColor = QColor(20, 90, 20);
    QColor m_copperTopColor = QColor(212, 71, 51);
    QColor m_copperBottomColor = QColor(46, 107, 219);
    QColor m_componentColor = QColor(48, 48, 54);

    RenderMode m_renderMode = RenderMode::Full;
    QTimer m_interactionTimer;

    QQuaternion m_rotation;
    QVector3D m_pan = QVector3D(0.0f, 0.0f, 0.0f);
    float m_zoom = -300.0f;
    QPoint m_lastPos;
    QPoint m_pressPos;
    bool m_leftPressed = false;
    bool m_measureMode = false;
    bool m_measureHasFirst = false;
    bool m_measureHasSecond = false;
    QVector3D m_measureP1;
    QVector3D m_measureP2;

    QTimer m_cameraAnimTimer;
    QTimer m_spinTimer;
    float m_spinSpeedDeg = 0.0f;
    QTimer m_spaceMousePollTimer;
    QLibrary m_spaceMouseLib;
    bool m_spaceMouseEnabled = false;
    bool m_spaceMouseConnected = false;
    int (*m_spnavOpen)() = nullptr;
    int (*m_spnavClose)() = nullptr;
    int (*m_spnavPollEvent)(void*) = nullptr;
    float m_cameraAnimT = 1.0f;
    QQuaternion m_rotFrom;
    QQuaternion m_rotTo;
    float m_zoomFrom = -300.0f;
    float m_zoomTo = -300.0f;

    QMatrix4x4 m_projection;
};

#endif // PCB_3D_VIEW_H
