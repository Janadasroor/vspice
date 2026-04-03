#include "pcb_3d_view.h"

#include "../items/component_item.h"
#include "../items/pad_item.h"
#include "../items/pcb_item.h"
#include "../items/trace_item.h"
#include "../items/via_item.h"
#include "../layers/pcb_layer.h"
#include "../../core/config_manager.h"
#include "../../footprints/footprint_library.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QLineF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextStream>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QtMath>
#include <algorithm>

using Flux::Model::Footprint3DModel;

namespace {
constexpr float kBoardThickness = 1.6f;
constexpr float kCopperZTop = 0.82f;
constexpr float kCopperZBottom = -0.82f;
constexpr float kCopperThickness = 0.05f;

constexpr int kSpnavEventMotion = 1;
constexpr int kSpnavEventButton = 2;

struct SpnavMotionEvent {
    int type;
    int x;
    int y;
    int z;
    int rx;
    int ry;
    int rz;
    unsigned int period;
};

struct SpnavButtonEvent {
    int type;
    int press;
    int bnum;
};

union SpnavEvent {
    int type;
    SpnavMotionEvent motion;
    SpnavButtonEvent button;
};

QVector3D colorToVec3(const QColor& c) {
    return QVector3D(c.redF(), c.greenF(), c.blueF());
}

QColor colorForComponentName(const QString& name) {
    const QString upper = name.toUpper();
    if (upper.startsWith("U") || upper.startsWith("IC")) return QColor(40, 40, 44);
    if (upper.startsWith("C")) return QColor(180, 130, 70);
    if (upper.startsWith("R")) return QColor(70, 110, 180);
    return QColor(120, 120, 128);
}

QVector3D transformPoint(const QMatrix4x4& m, const QVector3D& p) {
    return (m * QVector4D(p, 1.0f)).toVector3D();
}

void appendTri(QVector<PCB3DView::Vertex>& out, const QVector3D& a, const QVector3D& b, const QVector3D& c) {
    const QVector3D n = QVector3D::normal(a, b, c);
    out.push_back({a, n});
    out.push_back({b, n});
    out.push_back({c, n});
}

void appendQuad(QVector<PCB3DView::Vertex>& out,
                const QVector3D& a,
                const QVector3D& b,
                const QVector3D& c,
                const QVector3D& d,
                const QVector3D& normal) {
    out.push_back({a, normal});
    out.push_back({b, normal});
    out.push_back({c, normal});
    out.push_back({a, normal});
    out.push_back({c, normal});
    out.push_back({d, normal});
}

void appendRing(QVector<PCB3DView::Vertex>& out,
                float cx, float cy,
                float rIn, float rOut,
                float z, bool upNormal,
                int segments = 24) {
    if (rOut <= rIn + 1e-6f) return;
    const QVector3D n = upNormal ? QVector3D(0, 0, 1) : QVector3D(0, 0, -1);
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(i) * float(2.0 * M_PI / segments);
        const float a1 = float(i + 1) * float(2.0 * M_PI / segments);
        const QVector3D o0(cx + rOut * std::cos(a0), cy + rOut * std::sin(a0), z);
        const QVector3D o1(cx + rOut * std::cos(a1), cy + rOut * std::sin(a1), z);
        const QVector3D i1(cx + rIn * std::cos(a1), cy + rIn * std::sin(a1), z);
        const QVector3D i0(cx + rIn * std::cos(a0), cy + rIn * std::sin(a0), z);
        if (upNormal) appendQuad(out, o0, o1, i1, i0, n);
        else appendQuad(out, o0, i0, i1, o1, n);
    }
}

void appendCylinder(QVector<PCB3DView::Vertex>& out,
                    float cx, float cy,
                    float radius,
                    float zTop, float zBot,
                    int segments = 24) {
    if (radius <= 1e-6f || zTop <= zBot) return;
    for (int i = 0; i < segments; ++i) {
        const float a0 = float(i) * float(2.0 * M_PI / segments);
        const float a1 = float(i + 1) * float(2.0 * M_PI / segments);
        const QVector3D p0(cx + radius * std::cos(a0), cy + radius * std::sin(a0), zTop);
        const QVector3D p1(cx + radius * std::cos(a1), cy + radius * std::sin(a1), zTop);
        const QVector3D p2(cx + radius * std::cos(a1), cy + radius * std::sin(a1), zBot);
        const QVector3D p3(cx + radius * std::cos(a0), cy + radius * std::sin(a0), zBot);
        const QVector3D n(std::cos((a0 + a1) * 0.5f), std::sin((a0 + a1) * 0.5f), 0.0f);
        appendQuad(out, p0, p1, p2, p3, n);
    }
}

void appendThickSegment(QVector<PCB3DView::Vertex>& out,
                        const QPointF& a, const QPointF& b,
                        float width, float z, const QVector3D& normal) {
    QVector2D d(float(b.x() - a.x()), float(b.y() - a.y()));
    if (d.lengthSquared() < 1e-9f) return;
    d.normalize();
    QVector2D n(-d.y(), d.x());
    const float hw = std::max(0.01f, width * 0.5f);
    const QVector3D p1(float(a.x()), float(-a.y()), z);
    const QVector3D p2(float(b.x()), float(-b.y()), z);
    const QVector3D o(n.x() * hw, -n.y() * hw, 0.0f);
    
    // Use the provided normal for correct lighting and culling
    if (normal.z() > 0) {
        appendQuad(out, p1 - o, p2 - o, p2 + o, p1 + o, normal);
    } else {
        appendQuad(out, p1 - o, p1 + o, p2 + o, p2 - o, normal);
    }
}
}

PCB3DView::PCB3DView(QWidget* parent)
    : QOpenGLWidget(parent) {
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -45.0f) *
                 QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 45.0f);

    m_interactionTimer.setSingleShot(true);
    m_interactionTimer.setInterval(200);
    connect(&m_interactionTimer, &QTimer::timeout, this, [this]() {
        if (m_renderMode != RenderMode::Full) {
            m_renderMode = RenderMode::Full;
            update();
        }
    });

    m_cameraAnimTimer.setInterval(16);
    connect(&m_cameraAnimTimer, &QTimer::timeout, this, &PCB3DView::tickCameraAnimation);
    m_spinTimer.setInterval(16);
    connect(&m_spinTimer, &QTimer::timeout, this, &PCB3DView::tickSpinAnimation);
    m_spaceMousePollTimer.setInterval(8);
    connect(&m_spaceMousePollTimer, &QTimer::timeout, this, &PCB3DView::pollSpaceMouse);
}

PCB3DView::~PCB3DView() {
    if (m_spnavClose && m_spaceMouseConnected) {
        m_spnavClose();
        m_spaceMouseConnected = false;
    }
    makeCurrent();
    if (m_streamVbo.isCreated()) m_streamVbo.destroy();
    delete m_pickFbo;
    m_pickFbo = nullptr;
    delete m_shadowFbo;
    m_shadowFbo = nullptr;
    doneCurrent();
}

void PCB3DView::setScene(QGraphicsScene* scene) {
    m_scene = scene;
    m_sceneDirty = true;
    update();
}

void PCB3DView::updateScene() {
    m_sceneDirty = true;
    update();
}

void PCB3DView::setShowSubstrate(bool enabled) { m_showSubstrate = enabled; update(); }
void PCB3DView::setShowCopper(bool enabled) {
    m_showCopperTop = enabled;
    m_showCopperBottom = enabled;
    update();
}
void PCB3DView::setShowTopCopper(bool enabled) { m_showCopperTop = enabled; update(); }
void PCB3DView::setShowBottomCopper(bool enabled) { m_showCopperBottom = enabled; update(); }
void PCB3DView::setShowSilkscreen(bool enabled) { m_showSilkscreen = enabled; update(); }
void PCB3DView::setShowComponents(bool enabled) { m_showComponents = enabled; update(); }
void PCB3DView::setShowGrid(bool enabled) { m_showGrid = enabled; update(); }
void PCB3DView::setSelectedOnly(bool enabled) { m_selectedOnly = enabled; m_sceneDirty = true; update(); }
void PCB3DView::setNetFilter(const QString& netName) { m_netFilter = netName; m_sceneDirty = true; update(); }
void PCB3DView::setRaytracingEnabled(bool enabled) { m_raytracingEnabled = enabled; update(); }
void PCB3DView::setOrthographic(bool enabled) {
    if (m_orthographic == enabled) return;
    m_orthographic = enabled;
    updateProjectionMatrix();
    update();
}

void PCB3DView::setExplodeAmount(float mm) {
    const float clamped = std::clamp(mm, 0.0f, 50.0f);
    if (std::abs(m_explodeAmount - clamped) < 1e-6f) return;
    m_explodeAmount = clamped;
    m_sceneDirty = true;
    update();
}

void PCB3DView::setSoldermaskColor(const QColor& color) {
    if (!color.isValid()) return;
    m_soldermaskColor = color;
    update();
}

void PCB3DView::setCopperTopColor(const QColor& color) {
    if (!color.isValid()) return;
    m_copperTopColor = color;
    update();
}

void PCB3DView::setCopperBottomColor(const QColor& color) {
    if (!color.isValid()) return;
    m_copperBottomColor = color;
    update();
}

void PCB3DView::setComponentColor(const QColor& color) {
    if (!color.isValid()) return;
    m_componentColor = color;
    update();
}

void PCB3DView::setComponentAlpha(float alpha) {
    m_componentAlpha = std::clamp(alpha, 0.0f, 1.0f);
    update();
}

void PCB3DView::setMeasureMode(bool enabled) {
    m_measureMode = enabled;
    if (!enabled) clearMeasurement();
    update();
}

void PCB3DView::clearMeasurement() {
    m_measureHasFirst = false;
    m_measureHasSecond = false;
    emit measurementUpdated(-1.0);
    update();
}

bool PCB3DView::setSpaceMouseEnabled(bool enabled) {
    m_spaceMouseEnabled = enabled;
    if (!enabled) {
        m_spaceMousePollTimer.stop();
        return true;
    }

    if (!m_spaceMouseConnected) {
        if (!m_spaceMouseLib.isLoaded()) {
            m_spaceMouseLib.setFileName("libspnav.so.0");
            if (!m_spaceMouseLib.load()) {
                m_spaceMouseLib.setFileName("libspnav.so");
                if (!m_spaceMouseLib.load()) return false;
            }
        }

        m_spnavOpen = reinterpret_cast<int (*)()>(m_spaceMouseLib.resolve("spnav_open"));
        m_spnavClose = reinterpret_cast<int (*)()>(m_spaceMouseLib.resolve("spnav_close"));
        m_spnavPollEvent = reinterpret_cast<int (*)(void*)>(m_spaceMouseLib.resolve("spnav_poll_event"));
        if (!m_spnavOpen || !m_spnavClose || !m_spnavPollEvent) return false;
        if (m_spnavOpen() == -1) return false;
        m_spaceMouseConnected = true;
    }

    m_spaceMousePollTimer.start();
    return true;
}

void PCB3DView::setSubstrateAlpha(float alpha) {
    m_substrateAlpha = std::clamp(alpha, 0.05f, 1.0f);
    update();
}

void PCB3DView::setSoldermaskAlpha(float alpha) {
    m_soldermaskAlpha = std::clamp(alpha, 0.05f, 1.0f);
    update();
}

void PCB3DView::resetCamera() {
    m_pan = QVector3D(0.0f, 0.0f, 0.0f);
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -45.0f) *
                 QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 45.0f);
    m_zoom = -300.0f;
    m_cameraAnimTimer.stop();
    m_cameraAnimT = 1.0f;
    update();
}

void PCB3DView::setViewPreset(const QString& preset) {
    m_pan = QVector3D(0.0f, 0.0f, 0.0f);
    const QString p = preset.toLower();
    if (p != "spin cw" && p != "spin ccw") {
        m_spinTimer.stop();
        m_spinSpeedDeg = 0.0f;
    }

    if (p == "top") {
        startCameraTransition(QQuaternion(), -260.0f);
    } else if (p == "bottom") {
        startCameraTransition(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 180.0f), -260.0f);
    } else if (p == "front") {
        startCameraTransition(QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -90.0f), -300.0f);
    } else if (p == "back") {
        startCameraTransition(
            QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 90.0f) *
            QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 180.0f),
            -300.0f);
    } else if (p == "flip board") {
        m_cameraAnimTimer.stop();
        const QQuaternion target =
            QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 180.0f) * m_rotation;
        startCameraTransition(target, m_zoom);
    } else if (p == "spin cw") {
        m_cameraAnimTimer.stop();
        m_spinSpeedDeg = 0.7f;
        if (!m_spinTimer.isActive()) m_spinTimer.start();
    } else if (p == "spin ccw") {
        m_cameraAnimTimer.stop();
        m_spinSpeedDeg = -0.7f;
        if (!m_spinTimer.isActive()) m_spinTimer.start();
    } else if (p == "spin stop") {
        m_spinTimer.stop();
        m_spinSpeedDeg = 0.0f;
    } else {
        startCameraTransition(
            QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), -45.0f) *
            QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 45.0f),
            -300.0f);
    }
}

void PCB3DView::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.07f, 0.09f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    initShaders();
    initPickShader();
    initShadowMap();

    m_streamVbo.create();
    m_streamVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
}

void PCB3DView::initShadowMap() {
    m_shadowShader.removeAllShaders();
    m_shadowShader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 uModel;
        uniform mat4 uShadowViewProj;
        void main() {
            gl_Position = uShadowViewProj * uModel * vec4(aPos, 1.0);
        }
    )");
    m_shadowShader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        void main() {
            // Depth is written automatically
        }
    )");
    m_shadowShader.link();

    // Create 2048x2048 high-res shadow map.
    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::Depth);
    format.setInternalTextureFormat(GL_DEPTH_COMPONENT);
    m_shadowFbo = new QOpenGLFramebufferObject(2048, 2048, format);
}

QMatrix4x4 PCB3DView::currentShadowMatrix() const {
    const QVector3D lightPos(350.0f, -280.0f, 450.0f);
    const QVector3D target(0, 0, 0);
    QMatrix4x4 view;
    view.lookAt(lightPos, target, QVector3D(0, 0, 1));
    
    QMatrix4x4 proj;
    // Tight orthographic projection for the board area.
    proj.ortho(-250, 250, -200, 200, 100, 1200);
    return proj * view;
}

void PCB3DView::resizeGL(int w, int h) {
    Q_UNUSED(w)
    Q_UNUSED(h)
    updateProjectionMatrix();
}

void PCB3DView::paintGL() {
    if (m_sceneDirty) rebuildSceneCache();
    updateProjectionMatrix();

    const QMatrix4x4 shadowVP = currentShadowMatrix();

    // Pass 1: Shadow Map
    if (m_shadowFbo) {
        m_shadowFbo->bind();
        glViewport(0, 0, 2048, 2048);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glCullFace(GL_FRONT); // Avoid Peter Panning by culling front faces in shadow pass

        m_shadowShader.bind();
        m_shadowShader.setUniformValue("uShadowViewProj", shadowVP);

        auto drawShadowBatch = [&](const QVector<Vertex>& verts) {
            if (verts.isEmpty()) return;
            m_shadowShader.setUniformValue("uModel", QMatrix4x4());
            m_streamVbo.bind();
            m_streamVbo.allocate(verts.constData(), verts.size() * sizeof(Vertex));
            m_shadowShader.enableAttributeArray(0);
            m_shadowShader.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, pos), 3, sizeof(Vertex));
            glDrawArrays(GL_TRIANGLES, 0, verts.size());
            m_shadowShader.disableAttributeArray(0);
        };

        if (m_showSubstrate) drawShadowBatch(m_substrateVerts);
        if (m_showCopperTop) drawShadowBatch(m_copperTopVerts);
        if (m_showCopperBottom) drawShadowBatch(m_copperBottomVerts);
        if (m_showComponents) {
            for (const ComponentDraw& draw : m_componentDraws) {
                drawShadowBatch(draw.vertices);
            }
        }
        m_shadowShader.release();
        m_shadowFbo->release();
    }

    // Pass 2: Main Render
    glViewport(0, 0, std::max(1, int(width() * devicePixelRatioF())), std::max(1, int(height() * devicePixelRatioF())));
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glCullFace(GL_BACK);

    const QMatrix4x4 view = currentViewMatrix();

    m_shader.bind();
    m_shader.setUniformValue("uView", view);
    m_shader.setUniformValue("uProj", m_projection);
    m_shader.setUniformValue("uShadowVP", shadowVP);
    m_shader.setUniformValue("uLightPos", QVector3D(350.0f, -280.0f, 450.0f));
    m_shader.setUniformValue("uCamPos", QVector3D(0.0f, 0.0f, -m_zoom));
    m_shader.setUniformValue("uRaytraceMode", m_raytracingEnabled ? 1 : 0);

    // Bind shadow map texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowFbo ? m_shadowFbo->texture() : 0);
    m_shader.setUniformValue("uShadowMap", 0);

    if (m_showSubstrate) {
        drawBatch(m_substrateVerts, MaterialKind::SolderMask, m_soldermaskAlpha);
        drawBatch(m_dielectricVerts, MaterialKind::Dielectric, std::clamp(m_substrateAlpha * 0.85f, 0.2f, 0.95f));
    }
    if (m_showCopperTop) drawBatch(m_copperTopVerts, MaterialKind::CopperTop, 1.0f);
    if (m_showCopperTop || m_showCopperBottom) drawBatch(m_copperInnerVerts, MaterialKind::CopperInner, 0.95f);
    if (m_showCopperBottom) drawBatch(m_copperBottomVerts, MaterialKind::CopperBottom, 1.0f);
    if (m_showCopperTop || m_showCopperBottom) drawBatch(m_platedHoleVerts, MaterialKind::Plating, 1.0f);
    if (m_showSilkscreen) drawBatch(m_silkscreenVerts, MaterialKind::Silkscreen, 1.0f);
    if (m_showComponents) {
        for (const ComponentDraw& draw : m_componentDraws) {
            const bool collided = m_collidedComponents.contains(draw.id);
            const float alpha = collided ? 1.0f : std::clamp(draw.alpha * m_componentAlpha, 0.0f, 1.0f);
            drawBatch(draw.vertices, collided ? MaterialKind::Collision : draw.material, alpha);
        }
    }

    m_shader.release();

    drawGridOverlay();
    drawAxisTriadOverlay();
    drawMeasurementOverlay();
}

void PCB3DView::initShaders() {
    m_shader.removeAllShaders();
    m_shader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aNrm;
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProj;
        uniform mat4 uShadowVP;
        out vec3 vWorldPos;
        out vec3 vNormal;
        out vec4 vShadowPos;
        void main() {
            vec4 wp = uModel * vec4(aPos, 1.0);
            vWorldPos = wp.xyz;
            vNormal = mat3(transpose(inverse(uModel))) * aNrm;
            vShadowPos = uShadowVP * wp;
            gl_Position = uProj * uView * wp;
        }
    )");

    m_shader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec3 vWorldPos;
        in vec3 vNormal;
        in vec4 vShadowPos;
        uniform vec3 uCamPos;
        uniform vec3 uLightPos;
        uniform vec3 uAlbedo;
        uniform float uMetallic;
        uniform float uRoughness;
        uniform float uSpecularStrength;
        uniform vec3 uEmissive;
        uniform float uAlpha;
        uniform int uRaytraceMode;
        uniform sampler2D uShadowMap;
        out vec4 FragColor;

        float calculateShadow() {
            vec3 projCoords = vShadowPos.xyz / vShadowPos.w;
            projCoords = projCoords * 0.5 + 0.5;
            if (projCoords.z > 1.0) return 0.0;
            
            float currentDepth = projCoords.z;
            float bias = max(0.005 * (1.0 - dot(normalize(vNormal), normalize(uLightPos - vWorldPos))), 0.0005);
            
            float shadow = 0.0;
            vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);
            for(int x = -1; x <= 1; ++x) {
                for(int y = -1; y <= 1; ++y) {
                    float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
                    shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
                }
            }
            return shadow / 9.0;
        }

        vec3 environmentColor(vec3 dir) {
            vec3 d = normalize(dir);
            float up = clamp(d.z * 0.5 + 0.5, 0.0, 1.0);
            vec3 skyHorizon = vec3(0.42, 0.52, 0.62);
            vec3 skyZenith = vec3(0.13, 0.20, 0.32);
            vec3 ground = vec3(0.16, 0.13, 0.10);
            vec3 sky = mix(skyHorizon, skyZenith, smoothstep(0.45, 1.0, up));
            return mix(ground, sky, smoothstep(0.25, 0.75, up));
        }

        // Lightweight SSAO-style approximation using local normal/depth variation.
        float approxSSAO(vec3 N) {
            vec3 n = normalize(N);
            float normalVar = clamp(length(fwidth(n)), 0.0, 1.0);
            float depthVar = clamp(length(vec2(dFdx(gl_FragCoord.z), dFdy(gl_FragCoord.z))) * 220.0, 0.0, 1.0);
            float cavity = clamp(normalVar * 0.85 + depthVar * 0.55, 0.0, 1.0);
            return 1.0 - cavity * 0.38;
        }

        void main() {
            vec3 N = normalize(vNormal);
            vec3 L = normalize(uLightPos - vWorldPos);
            vec3 V = normalize(uCamPos - vWorldPos);
            vec3 H = normalize(L + V);

            float ndl = max(dot(N, L), 0.0);
            float shininess = mix(128.0, 8.0, clamp(uRoughness, 0.0, 1.0));
            float specTerm = pow(max(dot(N, H), 0.0), shininess) * uSpecularStrength;
            vec3 F0 = mix(vec3(0.04), uAlbedo, uMetallic);
            vec3 specular = mix(vec3(specTerm), F0 * specTerm, uMetallic);

            float shadow = calculateShadow();
            float lit = 1.0 - shadow;

            vec3 ambient = 0.14 * uAlbedo;
            vec3 diffuse = ndl * uAlbedo * (1.0 - 0.2 * uMetallic) * lit;
            vec3 color = ambient + diffuse + specular * lit + uEmissive;

            // Lightweight environment mapping for metallic/reflection cues.
            vec3 R = reflect(-V, N);
            vec3 env = environmentColor(R);
            float fresnelEnv = pow(1.0 - max(dot(N, V), 0.0), 4.0);
            color += env * mix(0.04, 0.36, uMetallic) * (0.35 + 0.65 * fresnelEnv) * lit;

            // Screen-space AO approximation for connector/pin depth perception.
            color *= approxSSAO(N);

            if (uRaytraceMode == 1) {
                // Cinematic "raytraced-like" post-lighting model:
                // second key light, Fresnel boost, reflections/refractions and GI-like indirect bounce.
                vec3 L2 = normalize(vec3(-260.0, 180.0, 320.0) - vWorldPos);
                vec3 H2 = normalize(L2 + V);
                float ndl2 = max(dot(N, L2), 0.0);
                float spec2 = pow(max(dot(N, H2), 0.0), max(12.0, shininess * 0.65)) * (uSpecularStrength * 0.65);
                float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
                float horizon = smoothstep(-0.2, 0.8, N.z);
                vec3 bounce = 0.08 * uAlbedo * vec3(0.88, 0.92, 1.0) * (1.0 - horizon);

                // Approximate global illumination from upper/lower hemisphere and a bent-normal probe.
                vec3 skyBounce = environmentColor(N) * (0.10 + 0.12 * (1.0 - uRoughness));
                vec3 groundBounce = environmentColor(-N) * (0.06 + 0.06 * horizon);
                vec3 bentNormal = normalize(mix(N, R, 0.35));
                vec3 indirectProbe = environmentColor(bentNormal) * (0.07 + 0.10 * (1.0 - uRoughness));
                vec3 gi = (skyBounce + groundBounce + indirectProbe) * uAlbedo;

                // Reflection and simple refraction tint against procedural environment.
                vec3 reflDir = reflect(-V, N);
                vec3 reflCol = environmentColor(reflDir);
                float eta = 1.0 / 1.32;
                vec3 refrDir = refract(-V, N, eta);
                vec3 refrCol = environmentColor(refrDir);
                vec3 transmissionTint = mix(vec3(1.0), uAlbedo, 0.35);
                vec3 glassCol = refrCol * transmissionTint;
                vec3 rrMix = mix(glassCol, reflCol, clamp(fresnel * 1.15, 0.0, 1.0));

                color = color
                      + 0.35 * ndl2 * uAlbedo * lit
                      + spec2 * mix(vec3(0.55), F0, 0.7) * lit
                      + fresnel * (0.25 + 0.35 * uMetallic)
                      + bounce
                      + gi
                      + rrMix * (0.10 + 0.22 * (1.0 - uRoughness))
                      + uEmissive * 0.55;
                // Gentle tone mapping for highlight rolloff.
                color = color / (color + vec3(1.0));
            }

            FragColor = vec4(color, uAlpha);
        }
    )");
    m_shader.link();
}

void PCB3DView::initPickShader() {
    m_pickShader.removeAllShaders();
    m_pickShader.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 uModel;
        uniform mat4 uView;
        uniform mat4 uProj;
        void main() {
            gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
        }
    )");
    m_pickShader.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        uniform vec3 uIdColor;
        out vec4 FragColor;
        void main() {
            FragColor = vec4(uIdColor, 1.0);
        }
    )");
    m_pickShader.link();
}

void PCB3DView::updateProjectionMatrix() {
    const float w = std::max(1, width());
    const float h = std::max(1, height());
    const float aspect = w / h;

    m_projection.setToIdentity();
    if (m_orthographic) {
        // Keep orthographic zoom behavior tied to camera zoom magnitude.
        const float halfHeight = std::max(10.0f, std::abs(m_zoom) * 0.5f);
        const float halfWidth = halfHeight * aspect;
        m_projection.ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, -4000.0f, 4000.0f);
    } else {
        m_projection.perspective(45.0f, aspect, 0.1f, 4000.0f);
    }
}

void PCB3DView::drawBatch(const QVector<Vertex>& verts, MaterialKind material, float alpha) {
    if (verts.isEmpty() || !m_streamVbo.isCreated()) return;

    m_shader.setUniformValue("uEmissive", QVector3D(0.0f, 0.0f, 0.0f));
    switch (material) {
    case MaterialKind::SolderMask:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_soldermaskColor));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.80f);
        m_shader.setUniformValue("uSpecularStrength", 0.18f);
        break;
    case MaterialKind::Dielectric:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.86f, 0.80f, 0.62f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.92f);
        m_shader.setUniformValue("uSpecularStrength", 0.06f);
        break;
    case MaterialKind::CopperTop:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_copperTopColor));
        m_shader.setUniformValue("uMetallic", 0.95f);
        m_shader.setUniformValue("uRoughness", 0.18f);
        m_shader.setUniformValue("uSpecularStrength", 1.15f);
        break;
    case MaterialKind::CopperBottom:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_copperBottomColor));
        m_shader.setUniformValue("uMetallic", 0.95f);
        m_shader.setUniformValue("uRoughness", 0.18f);
        m_shader.setUniformValue("uSpecularStrength", 1.15f);
        break;
    case MaterialKind::CopperInner:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.92f, 0.58f, 0.22f));
        m_shader.setUniformValue("uMetallic", 0.95f);
        m_shader.setUniformValue("uRoughness", 0.22f);
        m_shader.setUniformValue("uSpecularStrength", 1.05f);
        break;
    case MaterialKind::Plating:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.95f, 0.68f, 0.28f));
        m_shader.setUniformValue("uMetallic", 0.98f);
        m_shader.setUniformValue("uRoughness", 0.15f);
        m_shader.setUniformValue("uSpecularStrength", 1.2f);
        break;
    case MaterialKind::Silkscreen:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.96f, 0.96f, 0.96f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.86f);
        m_shader.setUniformValue("uSpecularStrength", 0.05f);
        break;
    case MaterialKind::Plastic:
        m_shader.setUniformValue("uAlbedo", colorToVec3(m_componentColor));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.84f);
        m_shader.setUniformValue("uSpecularStrength", 0.16f);
        break;
    case MaterialKind::ComponentMetal:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.70f, 0.72f, 0.76f));
        m_shader.setUniformValue("uMetallic", 0.92f);
        m_shader.setUniformValue("uRoughness", 0.24f);
        m_shader.setUniformValue("uSpecularStrength", 1.10f);
        break;
    case MaterialKind::ComponentLED:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.92f, 0.94f, 0.96f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.22f);
        m_shader.setUniformValue("uSpecularStrength", 0.75f);
        m_shader.setUniformValue("uEmissive", QVector3D(0.22f, 0.26f, 0.10f));
        break;
    case MaterialKind::Collision:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.92f, 0.22f, 0.22f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.55f);
        m_shader.setUniformValue("uSpecularStrength", 0.20f);
        m_shader.setUniformValue("uEmissive", QVector3D(0.08f, 0.01f, 0.01f));
        break;
    case MaterialKind::AxisX:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.95f, 0.25f, 0.25f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.5f);
        m_shader.setUniformValue("uSpecularStrength", 0.2f);
        break;
    case MaterialKind::AxisY:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.25f, 0.9f, 0.35f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.5f);
        m_shader.setUniformValue("uSpecularStrength", 0.2f);
        break;
    case MaterialKind::AxisZ:
        m_shader.setUniformValue("uAlbedo", QVector3D(0.3f, 0.55f, 0.95f));
        m_shader.setUniformValue("uMetallic", 0.0f);
        m_shader.setUniformValue("uRoughness", 0.5f);
        m_shader.setUniformValue("uSpecularStrength", 0.2f);
        break;
    }

    m_shader.setUniformValue("uAlpha", alpha);
    QMatrix4x4 model;
    m_shader.setUniformValue("uModel", model);

    m_streamVbo.bind();
    m_streamVbo.allocate(verts.constData(), int(verts.size() * sizeof(Vertex)));

    m_shader.enableAttributeArray(0);
    m_shader.enableAttributeArray(1);
    m_shader.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, pos), 3, sizeof(Vertex));
    m_shader.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, nrm), 3, sizeof(Vertex));

    glDrawArrays(GL_TRIANGLES, 0, verts.size());

    m_shader.disableAttributeArray(0);
    m_shader.disableAttributeArray(1);
    m_streamVbo.release();
}

void PCB3DView::rebuildSceneCache() {
    m_substrateVerts.clear();
    m_dielectricVerts.clear();
    m_copperTopVerts.clear();
    m_copperInnerVerts.clear();
    m_copperBottomVerts.clear();
    m_silkscreenVerts.clear();
    m_platedHoleVerts.clear();
    m_componentVerts.clear();
    m_componentDraws.clear();
    m_pickProxies.clear();

    if (!m_scene) {
        m_sceneDirty = false;
        return;
    }

    bool hasContentBounds = false;
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    auto includePoint = [&](float x, float y) {
        if (!hasContentBounds) {
            minX = maxX = x;
            minY = maxY = y;
            hasContentBounds = true;
            return;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    };

    bool hasEdgeCuts = false;
    float ecMinX = 0.0f, ecMinY = 0.0f, ecMaxX = 0.0f, ecMaxY = 0.0f;
    auto includeEdgePoint = [&](float x, float y) {
        if (!hasEdgeCuts) {
            ecMinX = ecMaxX = x;
            ecMinY = ecMaxY = y;
            hasEdgeCuts = true;
            return;
        }
        ecMinX = std::min(ecMinX, x);
        ecMinY = std::min(ecMinY, y);
        ecMaxX = std::max(ecMaxX, x);
        ecMaxY = std::max(ecMaxY, y);
    };

    // Copper: traces as thin quads, pads as quads.
    for (QGraphicsItem* item : m_scene->items()) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(item);
        if (!pcb) continue;
        if (!passesSelectionFilter(pcb)) continue;

        if (pcb->layer() == PCBLayerManager::EdgeCuts) {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(pcb)) {
                includeEdgePoint(float(trace->startPoint().x()), float(-trace->startPoint().y()));
                includeEdgePoint(float(trace->endPoint().x()), float(-trace->endPoint().y()));
            }
            continue;
        }

        if (!m_netFilter.isEmpty() && pcb->netName() != m_netFilter) continue;

        float cz = kCopperZTop;
        if (pcb->layer() == PCBLayerManager::TopCopper) {
            cz = kCopperZTop;
        } else if (pcb->layer() == PCBLayerManager::BottomCopper) {
            cz = kCopperZBottom - kCopperThickness;
        } else if (pcb->layer() >= 100) {
            // Internal layers: ID 101, 102...
            const int internalIdx = pcb->layer() - 100;
            const float factor = (internalIdx == 1) ? 0.34f : -0.34f;
            cz = (kBoardThickness * 0.5f) * factor;
        } else {
            cz = kCopperZTop; // Fallback
        }

        const float faceZ = (pcb->layer() == PCBLayerManager::BottomCopper) ? cz : (cz + kCopperThickness);

        if (TraceItem* trace = dynamic_cast<TraceItem*>(pcb)) {
            // Map start and end points to scene coordinates to handle any parent transforms.
            QPointF s2 = trace->mapToScene(trace->startPoint());
            QPointF e2 = trace->mapToScene(trace->endPoint());
            QVector2D d(float(e2.x() - s2.x()), float(-(e2.y() - s2.y())));
            if (d.lengthSquared() < 1e-9f) continue;
            d.normalize();
            QVector2D n(-d.y(), d.x());
            float hw = float(trace->width() * 0.5);
            QVector3D p1(float(s2.x()), float(-s2.y()), faceZ);
            QVector3D p2(float(e2.x()), float(-e2.y()), faceZ);
            QVector3D o(n.x() * hw, n.y() * hw, 0.0f);

            includePoint((p1 - o).x(), (p1 - o).y());
            includePoint((p2 - o).x(), (p2 - o).y());
            includePoint((p2 + o).x(), (p2 + o).y());
            includePoint((p1 + o).x(), (p1 + o).y());

            if (faceZ > 0) appendQuad(m_copperTopVerts, p1 - o, p2 - o, p2 + o, p1 + o, {0, 0, 1.0f});
            else appendQuad(m_copperBottomVerts, p1 - o, p1 + o, p2 + o, p2 - o, {0, 0, -1.0f});
            continue;
        }

        if (PadItem* pad = dynamic_cast<PadItem*>(pcb)) {
            // Pads are often children of ComponentItem, so map corners to scene space.
            QRectF r = pad->boundingRect();
            QPointF c1 = pad->mapToScene(r.topLeft());
            QPointF c2 = pad->mapToScene(r.topRight());
            QPointF c3 = pad->mapToScene(r.bottomRight());
            QPointF c4 = pad->mapToScene(r.bottomLeft());
            
            QVector3D a(float(c1.x()), float(-c1.y()), faceZ);
            QVector3D b(float(c2.x()), float(-c2.y()), faceZ);
            QVector3D c(float(c3.x()), float(-c3.y()), faceZ);
            QVector3D d(float(c4.x()), float(-c4.y()), faceZ);
            
            includePoint(a.x(), a.y());
            includePoint(b.x(), b.y());
            includePoint(c.x(), c.y());
            includePoint(d.x(), d.y());

            if (faceZ > 0) appendQuad(m_copperTopVerts, a, d, c, b, {0, 0, 1.0f});
            else appendQuad(m_copperBottomVerts, a, b, c, d, {0, 0, -1.0f});

            const float drill = float(pad->drillSize());
            if (drill > 0.01f) {
                const QPointF sc = pad->mapToScene(0, 0);
                const float px = float(sc.x());
                const float py = float(-sc.y());
                const float rIn = std::max(0.01f, drill * 0.5f);
                const float rOut = std::max(rIn + 0.015f, std::min(float(r.width()), float(r.height())) * 0.5f);
                appendRing(m_copperTopVerts, px, py, rIn, rOut, kCopperZTop + kCopperThickness, true);
                appendRing(m_copperBottomVerts, px, py, rIn, rOut, kCopperZBottom - kCopperThickness, false);
                appendCylinder(m_platedHoleVerts, px, py, rIn + 0.008f, kCopperZTop + kCopperThickness, kCopperZBottom - kCopperThickness);
            }
            continue;
        }

        if (ViaItem* via = dynamic_cast<ViaItem*>(pcb)) {
            const QPointF sp = via->scenePos();
            const float px = float(sp.x());
            const float py = float(-sp.y());
            const float outer = std::max(0.02f, float(via->diameter()) * 0.5f);
            const float drill = std::max(0.01f, float(via->drillSize()) * 0.5f);
            appendRing(m_copperTopVerts, px, py, drill, outer, kCopperZTop + kCopperThickness, true);
            appendRing(m_copperBottomVerts, px, py, drill, outer, kCopperZBottom - kCopperThickness, false);
            appendCylinder(m_platedHoleVerts, px, py, drill + 0.008f, kCopperZTop + kCopperThickness, kCopperZBottom - kCopperThickness);
            includePoint(px - outer, py - outer);
            includePoint(px + outer, py + outer);
            continue;
        }
    }

    // Components and pick proxies.
    for (QGraphicsItem* item : m_scene->items()) {
        ComponentItem* comp = dynamic_cast<ComponentItem*>(item);
        if (!comp) continue;
        if (!passesSelectionFilter(comp)) continue;

        QRectF r = comp->boundingRect();
        float hx = float(r.width() * 0.5);
        float hy = float(r.height() * 0.5);
        float hz = float((comp->height() > 0.0) ? comp->height() : 2.0);
        const QPointF cp = comp->scenePos();
        QVector<ComponentDraw> localDraws;

        const bool isBottom = (comp->layer() == PCBLayerManager::BottomCopper);
        const float compZ = isBottom ? (kCopperZBottom - kCopperThickness - 0.02f - m_explodeAmount)
                                     : (kCopperZTop + kCopperThickness + 0.02f + m_explodeAmount);

        auto appendModelGeometry = [&](const QVector<Vertex>& modelLocalVerts,
                                       const QVector3D& modelOffset,
                                       const QVector3D& modelRotation,
                                       float alpha) {
            if (modelLocalVerts.isEmpty()) return;
            QMatrix4x4 model;
            model.translate(float(cp.x()), float(-cp.y()), compZ);
            model.rotate(float(comp->rotation()), 0, 0, 1);
            if (isBottom) model.rotate(180.0f, 0, 1, 0); // Flip to bottom
            
            model.translate(modelOffset.x(), -modelOffset.y(), modelOffset.z());
            model.rotate(modelRotation.z(), 0, 0, 1);
            model.rotate(modelRotation.y(), 0, 1, 0);
            model.rotate(modelRotation.x(), 1, 0, 0);
            
            ComponentDraw draw;
            draw.id = comp->id();
            draw.alpha = std::clamp(alpha, 0.05f, 1.0f);
            const QString typeUpper = comp->componentType().toUpper();
            const QString nameUpper = comp->name().toUpper();
            const bool isLed = typeUpper.contains("LED") || nameUpper.startsWith("D");
            const bool isMetal = typeUpper.contains("CONN") || typeUpper.contains("USB") ||
                                 typeUpper.contains("SHIELD") || typeUpper.contains("SMA") ||
                                 typeUpper.contains("ANT") || typeUpper.contains("COAX") ||
                                 typeUpper.contains("BAT");
            draw.material = isLed ? MaterialKind::ComponentLED
                                  : (isMetal ? MaterialKind::ComponentMetal : MaterialKind::Plastic);
            draw.vertices.reserve(modelLocalVerts.size());
            for (const Vertex& lv : modelLocalVerts) {
                QVector3D wp = transformPoint(model, lv.pos);
                QVector3D wn = model.mapVector(lv.nrm).normalized();
                draw.vertices.push_back({wp, wn});
            }
            localDraws.push_back(draw);
        };

        if (m_renderMode == RenderMode::Full) {
            if (!comp->modelPath().isEmpty()) {
                const QString resolvedPath = resolveModelPath(comp->modelPath());
                const ObjMesh mesh = loadOBJ(resolvedPath.isEmpty() ? comp->modelPath() : resolvedPath);
                if (!mesh.vertices.isEmpty()) {
                    QVector<Vertex> modelVerts = mesh.vertices;
                    const QVector3D vecScale = comp->modelScale3D();
                    const float sx = float(comp->modelScale()) * vecScale.x();
                    const float sy = float(comp->modelScale()) * vecScale.y();
                    const float sz = float(comp->modelScale()) * vecScale.z();
                    for (Vertex& v : modelVerts) {
                        v.pos = QVector3D(v.pos.x() * sx, v.pos.y() * sy, v.pos.z() * sz);
                    }
                    appendModelGeometry(modelVerts, comp->modelOffset(), comp->modelRotation(), 1.0f);
                }
            } else if (FootprintLibraryManager::instance().hasFootprint(comp->componentType())) {
                const FootprintDefinition def = FootprintLibraryManager::instance().findFootprint(comp->componentType());
                QList<Footprint3DModel> models = def.models3D();
                if (models.isEmpty() && !def.model3D().filename.trimmed().isEmpty()) {
                    models.append(def.model3D());
                }
                for (const Footprint3DModel& model3D : models) {
                    if (!model3D.visible) continue;
                    if (model3D.filename.trimmed().isEmpty()) continue;
                    const QString resolvedPath = resolveModelPath(model3D.filename);
                    const ObjMesh mesh = loadOBJ(resolvedPath.isEmpty() ? model3D.filename : resolvedPath);
                    if (mesh.vertices.isEmpty()) continue;
                    QVector<Vertex> modelVerts = mesh.vertices;
                    const float sx = model3D.scale.x();
                    const float sy = model3D.scale.y();
                    const float sz = model3D.scale.z();
                    for (Vertex& v : modelVerts) {
                        v.pos = QVector3D(v.pos.x() * sx, v.pos.y() * sy, v.pos.z() * sz);
                    }
                    appendModelGeometry(modelVerts, model3D.offset, model3D.rotation, model3D.opacity);
                }
            }
        }
        if (localDraws.isEmpty()) {
            QVector<Vertex> local = makeBoxVertices(hx, hy, hz);
            QMatrix4x4 model;
            model.translate(float(cp.x()), float(-cp.y()), compZ);
            model.rotate(float(comp->rotation()), 0, 0, 1);
            if (isBottom) model.rotate(180.0f, 0, 1, 0);
            
            ComponentDraw draw;
            draw.id = comp->id();
            draw.alpha = 1.0f;
            const QString typeUpper = comp->componentType().toUpper();
            const QString nameUpper = comp->name().toUpper();
            const bool isLed = typeUpper.contains("LED") || nameUpper.startsWith("D");
            const bool isMetal = typeUpper.contains("CONN") || typeUpper.contains("USB") ||
                                 typeUpper.contains("SHIELD") || typeUpper.contains("SMA") ||
                                 typeUpper.contains("ANT") || typeUpper.contains("COAX") ||
                                 typeUpper.contains("BAT");
            draw.material = isLed ? MaterialKind::ComponentLED
                                  : (isMetal ? MaterialKind::ComponentMetal : MaterialKind::Plastic);
            draw.vertices.reserve(local.size());
            for (const Vertex& lv : local) {
                QVector3D wp = transformPoint(model, lv.pos);
                QVector3D wn = model.mapVector(lv.nrm).normalized();
                draw.vertices.push_back({wp, wn});
            }
            localDraws.push_back(draw);
        }

        QVector3D bmin(1e9f, 1e9f, 1e9f), bmax(-1e9f, -1e9f, -1e9f);
        for (const ComponentDraw& draw : localDraws) {
            for (const Vertex& v : draw.vertices) {
                m_componentVerts.push_back(v);
                includePoint(v.pos.x(), v.pos.y());
                bmin.setX(std::min(bmin.x(), v.pos.x()));
                bmin.setY(std::min(bmin.y(), v.pos.y()));
                bmin.setZ(std::min(bmin.z(), v.pos.z()));
                bmax.setX(std::max(bmax.x(), v.pos.x()));
                bmax.setY(std::max(bmax.y(), v.pos.y()));
                bmax.setZ(std::max(bmax.z(), v.pos.z()));
            }
            m_componentDraws.push_back(draw);
        }
        m_pickProxies.push_back({comp->id(), bmin, bmax});

        // High-resolution silkscreen rendering from footprint graphics child items.
        const float silkZ = isBottom ? (kCopperZBottom - kCopperThickness - 0.01f)
                                     : (kCopperZTop + kCopperThickness + 0.01f);
        const QVector3D silkNormal = isBottom ? QVector3D(0, 0, -1) : QVector3D(0, 0, 1);
        for (QGraphicsItem* child : comp->childItems()) {
            if (!child || !child->isVisible()) continue;

            auto addLine = [&](const QPointF& la, const QPointF& lb, float lw) {
                const QPointF a2 = comp->mapToScene(child->mapToParent(la));
                const QPointF b2 = comp->mapToScene(child->mapToParent(lb));
                appendThickSegment(m_silkscreenVerts, a2, b2, lw, silkZ, silkNormal);
                includePoint(float(a2.x()), float(-a2.y()));
                includePoint(float(b2.x()), float(-b2.y()));
            };

            if (auto* li = dynamic_cast<QGraphicsLineItem*>(child)) {
                const QLineF ln = li->line();
                const float lw = std::max(0.08f, float(li->pen().widthF()));
                addLine(ln.p1(), ln.p2(), lw);
                continue;
            }

            if (auto* ri = dynamic_cast<QGraphicsRectItem*>(child)) {
                const QRectF r = ri->rect();
                const float lw = std::max(0.08f, float(ri->pen().widthF()));
                addLine(r.topLeft(), r.topRight(), lw);
                addLine(r.topRight(), r.bottomRight(), lw);
                addLine(r.bottomRight(), r.bottomLeft(), lw);
                addLine(r.bottomLeft(), r.topLeft(), lw);
                continue;
            }

            if (auto* ei = dynamic_cast<QGraphicsEllipseItem*>(child)) {
                const QRectF r = ei->rect();
                const float lw = std::max(0.08f, float(ei->pen().widthF()));
                const int seg = 72;
                QPointF prev(r.center().x() + r.width() * 0.5, r.center().y());
                for (int s = 1; s <= seg; ++s) {
                    const float a = float(2.0 * M_PI * s / seg);
                    const QPointF cur(r.center().x() + std::cos(a) * r.width() * 0.5,
                                      r.center().y() + std::sin(a) * r.height() * 0.5);
                    addLine(prev, cur, lw);
                    prev = cur;
                }
                continue;
            }

            if (auto* pi = dynamic_cast<QGraphicsPathItem*>(child)) {
                const QPainterPath p = pi->path();
                const float lw = std::max(0.08f, float(pi->pen().widthF()));
                const QList<QPolygonF> polys = p.toSubpathPolygons();
                for (const QPolygonF& poly : polys) {
                    if (poly.size() < 2) continue;
                    for (int i = 1; i < poly.size(); ++i) {
                        addLine(poly[i - 1], poly[i], lw);
                    }
                }
                continue;
            }

            if (auto* gi = dynamic_cast<QGraphicsPolygonItem*>(child)) {
                const QPolygonF poly = gi->polygon();
                if (poly.size() < 2) continue;
                const float lw = std::max(0.08f, float(gi->pen().widthF()));
                for (int i = 1; i < poly.size(); ++i) addLine(poly[i - 1], poly[i], lw);
                addLine(poly.back(), poly.front(), lw);
                continue;
            }
        }
    }

    // Build substrate from EdgeCuts if available, else from content bounds.
    if (hasEdgeCuts) {
        minX = ecMinX; maxX = ecMaxX;
        minY = ecMinY; maxY = ecMaxY;
    } else if (!hasContentBounds) {
        QRectF br = m_scene->itemsBoundingRect();
        if (br.isEmpty()) br = QRectF(-50, -50, 100, 100);
        minX = float(br.left());
        maxX = float(br.right());
        minY = float(-br.bottom());
        maxY = float(-br.top());
    }

    const float margin = 1.0f;
    const float x1 = minX - margin;
    const float y1 = minY - margin;
    const float x2 = maxX + margin;
    const float y2 = maxY + margin;
    const float z = kBoardThickness * 0.5f;

    appendQuad(m_substrateVerts,
               {x1, y1, z}, {x2, y1, z}, {x2, y2, z}, {x1, y2, z},
               {0, 0, 1});
    appendQuad(m_substrateVerts,
               {x1, y2, -z}, {x2, y2, -z}, {x2, y1, -z}, {x1, y1, -z},
               {0, 0, -1});
    appendQuad(m_substrateVerts,
               {x1, y1, -z}, {x2, y1, -z}, {x2, y1, z}, {x1, y1, z},
               {0, 1, 0});
    appendQuad(m_substrateVerts,
               {x2, y1, -z}, {x2, y2, -z}, {x2, y2, z}, {x2, y1, z},
               {1, 0, 0});
    appendQuad(m_substrateVerts,
               {x2, y2, -z}, {x1, y2, -z}, {x1, y2, z}, {x2, y2, z},
               {0, -1, 0});
    appendQuad(m_substrateVerts,
               {x1, y2, -z}, {x1, y1, -z}, {x1, y1, z}, {x1, y2, z},
               {-1, 0, 0});

    // Full stackup approximation: dielectric cores/prepreg and two internal copper planes.
    const float innerZ1 = z * 0.34f;
    const float innerZ2 = -z * 0.34f;
    const float dielTopA = z * 0.78f;
    const float dielTopB = innerZ1 + kCopperThickness * 0.4f;
    const float dielMidA = innerZ1 - kCopperThickness * 0.4f;
    const float dielMidB = innerZ2 + kCopperThickness * 0.4f;
    const float dielBotA = innerZ2 - kCopperThickness * 0.4f;
    const float dielBotB = -z * 0.78f;

    auto appendSlab = [&](float za, float zb) {
        if (za <= zb + 1e-6f) return;
        appendQuad(m_dielectricVerts, {x1, y1, za}, {x2, y1, za}, {x2, y2, za}, {x1, y2, za}, {0, 0, 1});
        appendQuad(m_dielectricVerts, {x1, y2, zb}, {x2, y2, zb}, {x2, y1, zb}, {x1, y1, zb}, {0, 0, -1});
        appendQuad(m_dielectricVerts, {x1, y1, zb}, {x2, y1, zb}, {x2, y1, za}, {x1, y1, za}, {0, 1, 0});
        appendQuad(m_dielectricVerts, {x2, y1, zb}, {x2, y2, zb}, {x2, y2, za}, {x2, y1, za}, {1, 0, 0});
        appendQuad(m_dielectricVerts, {x2, y2, zb}, {x1, y2, zb}, {x1, y2, za}, {x2, y2, za}, {0, -1, 0});
        appendQuad(m_dielectricVerts, {x1, y2, zb}, {x1, y1, zb}, {x1, y1, za}, {x1, y2, za}, {-1, 0, 0});
    };
    appendSlab(dielTopA, dielTopB);
    appendSlab(dielMidA, dielMidB);
    appendSlab(dielBotA, dielBotB);

    appendQuad(m_copperInnerVerts,
               {x1, y1, innerZ1}, {x2, y1, innerZ1}, {x2, y2, innerZ1}, {x1, y2, innerZ1},
               {0, 0, 1});
    appendQuad(m_copperInnerVerts,
               {x1, y2, innerZ2}, {x2, y2, innerZ2}, {x2, y1, innerZ2}, {x1, y1, innerZ2},
               {0, 0, -1});

    // Drop stale collision marks for components no longer present.
    if (!m_collidedComponents.isEmpty()) {
        QSet<QUuid> live;
        for (const PickProxy& p : m_pickProxies) live.insert(p.id);
        QSet<QUuid> pruned;
        for (const QUuid& id : m_collidedComponents) {
            if (live.contains(id)) pruned.insert(id);
        }
        m_collidedComponents = pruned;
    }

    m_sceneDirty = false;
}

int PCB3DView::detectComponentCollisions() {
    if (m_sceneDirty) rebuildSceneCache();
    m_collidedComponents.clear();
    if (m_pickProxies.size() < 2) {
        update();
        return 0;
    }

    int pairs = 0;
    constexpr float eps = 0.02f;
    auto overlaps = [&](const PickProxy& a, const PickProxy& b) {
        if (a.bmax.x() < b.bmin.x() + eps || b.bmax.x() < a.bmin.x() + eps) return false;
        if (a.bmax.y() < b.bmin.y() + eps || b.bmax.y() < a.bmin.y() + eps) return false;
        if (a.bmax.z() < b.bmin.z() + eps || b.bmax.z() < a.bmin.z() + eps) return false;
        return true;
    };

    for (int i = 0; i < m_pickProxies.size(); ++i) {
        for (int j = i + 1; j < m_pickProxies.size(); ++j) {
            if (overlaps(m_pickProxies[i], m_pickProxies[j])) {
                m_collidedComponents.insert(m_pickProxies[i].id);
                m_collidedComponents.insert(m_pickProxies[j].id);
                ++pairs;
            }
        }
    }

    update();
    return pairs;
}

PCB3DView::ObjMesh PCB3DView::loadOBJ(const QString& path) {
    if (path.isEmpty()) return {};
    const QString finalPath = resolveModelPath(path);
    if (finalPath.isEmpty()) return {};
    if (m_objCache.contains(finalPath)) return m_objCache[finalPath];
    if (m_objCache.contains(path)) return m_objCache[path];

    QFile file(finalPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ObjMesh out;
        m_objCache[finalPath] = out;
        m_objCache[path] = out;
        return out;
    }
    const QString text = QString::fromUtf8(file.readAll());
    const QString ext = QFileInfo(finalPath).suffix().toLower();
    ObjMesh out;
    if (ext == "obj") out = loadObjMeshFromText(text);
    else if (ext == "wrl" || ext == "vrml") out = loadVrmlMeshFromText(text);
    else if (ext == "step" || ext == "stp") out = loadStepMeshFromText(text);
    else if (ext == "igs" || ext == "iges") out = loadIgesMeshFromText(text);
    else out = loadObjMeshFromText(text);

    m_objCache[finalPath] = out;
    m_objCache[path] = out;
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadObjMeshFromText(const QString& text) const {
    ObjMesh out;
    QVector<QVector3D> temp;
    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        if (parts[0] == "v" && parts.size() >= 4) {
            temp.push_back(QVector3D(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat()));
            continue;
        }
        if (parts[0] != "f" || parts.size() < 4) continue;

        auto idx = [&](int p) { return parts[p].split('/')[0].toInt() - 1; };
        for (int i = 1; i <= parts.size() - 3; ++i) {
            QVector3D a = temp.value(idx(1));
            QVector3D b = temp.value(idx(i + 1));
            QVector3D c = temp.value(idx(i + 2));
            QVector3D n = QVector3D::normal(a, b, c);
            out.vertices.push_back({a, n});
            out.vertices.push_back({b, n});
            out.vertices.push_back({c, n});
        }
    }
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadVrmlMeshFromText(const QString& text) const {
    ObjMesh out;

    QRegularExpression pointBlockRe(
        "point\\s*\\[([^\\]]+)\\]",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression coordIndexRe(
        "coordIndex\\s*\\[([^\\]]+)\\]",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    const QRegularExpressionMatch pm = pointBlockRe.match(text);
    if (!pm.hasMatch()) return out;

    QVector<QVector3D> points;
    const QString pointBlock = pm.captured(1);
    QRegularExpression numRe("([\\-+]?\\d+(?:\\.\\d+)?(?:[EeDd][\\-+]?\\d+)?)");
    QRegularExpressionMatchIterator it = numRe.globalMatch(pointBlock);
    QVector<double> nums;
    while (it.hasNext()) {
        const QString tok = it.next().captured(1);
        nums.push_back(tok.toDouble());
    }
    for (int i = 0; i + 2 < nums.size(); i += 3) {
        points.push_back(QVector3D(float(nums[i]), float(nums[i + 1]), float(nums[i + 2])));
    }
    if (points.isEmpty()) return out;

    const QRegularExpressionMatch cm = coordIndexRe.match(text);
    if (!cm.hasMatch()) {
        for (int i = 0; i + 2 < points.size(); i += 3) {
            const QVector3D a = points[i];
            const QVector3D b = points[i + 1];
            const QVector3D c = points[i + 2];
            const QVector3D n = QVector3D::normal(a, b, c);
            out.vertices.push_back({a, n});
            out.vertices.push_back({b, n});
            out.vertices.push_back({c, n});
        }
        return out;
    }

    QVector<int> indices;
    it = QRegularExpression("-?\\d+").globalMatch(cm.captured(1));
    while (it.hasNext()) {
        indices.push_back(it.next().captured(0).toInt());
    }

    QVector<int> face;
    auto emitFace = [&]() {
        if (face.size() < 3) return;
        const int i0 = face[0];
        for (int k = 1; k + 1 < face.size(); ++k) {
            const int i1 = face[k];
            const int i2 = face[k + 1];
            if (i0 < 0 || i1 < 0 || i2 < 0) continue;
            if (i0 >= points.size() || i1 >= points.size() || i2 >= points.size()) continue;
            const QVector3D a = points[i0];
            const QVector3D b = points[i1];
            const QVector3D c = points[i2];
            const QVector3D n = QVector3D::normal(a, b, c);
            out.vertices.push_back({a, n});
            out.vertices.push_back({b, n});
            out.vertices.push_back({c, n});
        }
    };

    for (int idx : indices) {
        if (idx == -1) {
            emitFace();
            face.clear();
            continue;
        }
        face.push_back(idx);
    }
    emitFace();
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadStepMeshFromText(const QString& text) const {
    ObjMesh out;
    QVector<QVector3D> points;
    QRegularExpression ptRe(
        "CARTESIAN_POINT\\s*\\([^\\(]*\\(\\s*([\\-+0-9.EeDd]+)\\s*,\\s*([\\-+0-9.EeDd]+)\\s*,\\s*([\\-+0-9.EeDd]+)\\s*\\)\\s*\\)",
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = ptRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const auto cvt = [](QString s) {
            s.replace('D', 'E');
            s.replace('d', 'e');
            return s.toDouble();
        };
        points.push_back(QVector3D(float(cvt(m.captured(1))), float(cvt(m.captured(2))), float(cvt(m.captured(3)))));
        if (points.size() >= 3000) break;
    }
    if (points.isEmpty()) return out;

    QVector3D pmin(1e9f, 1e9f, 1e9f), pmax(-1e9f, -1e9f, -1e9f);
    for (const QVector3D& p : points) {
        pmin.setX(std::min(pmin.x(), p.x())); pmin.setY(std::min(pmin.y(), p.y())); pmin.setZ(std::min(pmin.z(), p.z()));
        pmax.setX(std::max(pmax.x(), p.x())); pmax.setY(std::max(pmax.y(), p.y())); pmax.setZ(std::max(pmax.z(), p.z()));
    }
    const float span = (pmax - pmin).length();
    const float h = std::max(0.02f, span * 0.003f);

    for (const QVector3D& p : points) {
        const QVector3D a = p + QVector3D(-h, -h, -h);
        const QVector3D b = p + QVector3D( h, -h, -h);
        const QVector3D c = p + QVector3D( h,  h, -h);
        const QVector3D d = p + QVector3D(-h,  h, -h);
        const QVector3D e = p + QVector3D(-h, -h,  h);
        const QVector3D f = p + QVector3D( h, -h,  h);
        const QVector3D g = p + QVector3D( h,  h,  h);
        const QVector3D hq= p + QVector3D(-h,  h,  h);
        appendQuad(out.vertices, e, f, g, hq, {0, 0, 1});
        appendQuad(out.vertices, a, b, f, e, {0, -1, 0});
        appendQuad(out.vertices, b, c, g, f, {1, 0, 0});
        appendQuad(out.vertices, c, d, hq, g, {0, 1, 0});
        appendQuad(out.vertices, d, a, e, hq, {-1, 0, 0});
        appendQuad(out.vertices, a, d, c, b, {0, 0, -1});
    }
    return out;
}

PCB3DView::ObjMesh PCB3DView::loadIgesMeshFromText(const QString& text) const {
    ObjMesh out;
    QVector<QVector3D> points;
    const QStringList lines = text.split('\n');
    QRegularExpression numRe("([\\-+]?\\d+(?:\\.\\d+)?(?:[EeDd][\\-+]?\\d+)?)");
    for (const QString& line : lines) {
        if (!line.contains(',')) continue;
        QRegularExpressionMatchIterator it = numRe.globalMatch(line);
        QVector<double> nums;
        while (it.hasNext()) {
            const QString tok = it.next().captured(1);
            QString t = tok;
            t.replace('D', 'E');
            t.replace('d', 'e');
            nums.push_back(t.toDouble());
        }
        for (int i = 0; i + 2 < nums.size(); i += 3) {
            points.push_back(QVector3D(float(nums[i]), float(nums[i + 1]), float(nums[i + 2])));
            if (points.size() >= 2500) break;
        }
        if (points.size() >= 2500) break;
    }
    if (points.isEmpty()) return out;

    QVector3D pmin(1e9f, 1e9f, 1e9f), pmax(-1e9f, -1e9f, -1e9f);
    for (const QVector3D& p : points) {
        pmin.setX(std::min(pmin.x(), p.x())); pmin.setY(std::min(pmin.y(), p.y())); pmin.setZ(std::min(pmin.z(), p.z()));
        pmax.setX(std::max(pmax.x(), p.x())); pmax.setY(std::max(pmax.y(), p.y())); pmax.setZ(std::max(pmax.z(), p.z()));
    }
    const float span = (pmax - pmin).length();
    const float h = std::max(0.02f, span * 0.0025f);

    for (const QVector3D& p : points) {
        const QVector3D a = p + QVector3D(-h, -h, -h);
        const QVector3D b = p + QVector3D( h, -h, -h);
        const QVector3D c = p + QVector3D( h,  h, -h);
        const QVector3D d = p + QVector3D(-h,  h, -h);
        const QVector3D e = p + QVector3D(-h, -h,  h);
        const QVector3D f = p + QVector3D( h, -h,  h);
        const QVector3D g = p + QVector3D( h,  h,  h);
        const QVector3D hq= p + QVector3D(-h,  h,  h);
        appendQuad(out.vertices, e, f, g, hq, {0, 0, 1});
        appendQuad(out.vertices, a, b, f, e, {0, -1, 0});
        appendQuad(out.vertices, b, c, g, f, {1, 0, 0});
        appendQuad(out.vertices, c, d, hq, g, {0, 1, 0});
        appendQuad(out.vertices, d, a, e, hq, {-1, 0, 0});
        appendQuad(out.vertices, a, d, c, b, {0, 0, -1});
    }
    return out;
}

QString PCB3DView::expandModelEnvVars(const QString& rawPath) const {
    QString out = rawPath.trimmed();
    if (out.isEmpty()) return out;

    QRegularExpression braceVar("\\$\\{([^}]+)\\}");
    QRegularExpressionMatch m = braceVar.match(out);
    while (m.hasMatch()) {
        const QString var = m.captured(1).trimmed();
        const QString val = qEnvironmentVariable(var.toUtf8().constData());
        out.replace(m.capturedStart(0), m.capturedLength(0), val);
        m = braceVar.match(out);
    }

    QRegularExpression plainVar("\\$([A-Za-z_][A-Za-z0-9_]*)");
    m = plainVar.match(out);
    while (m.hasMatch()) {
        const QString var = m.captured(1).trimmed();
        const QString val = qEnvironmentVariable(var.toUtf8().constData());
        out.replace(m.capturedStart(0), m.capturedLength(0), val);
        m = plainVar.match(out);
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(out));
}

QStringList PCB3DView::modelSearchRoots() const {
    QStringList roots;
    auto appendRoot = [&roots](const QString& p) {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(p.trimmed()));
        if (clean.isEmpty()) return;
        if (!QFileInfo(clean).isDir()) return;
        if (!roots.contains(clean, Qt::CaseInsensitive)) roots.push_back(clean);
    };

    for (const QString& p : ConfigManager::instance().modelPaths()) appendRoot(p);

    appendRoot(qEnvironmentVariable("KISYS3DMOD"));
    for (int v = 5; v <= 9; ++v) {
        appendRoot(qEnvironmentVariable(QString("KICAD%1_3DMODEL_DIR").arg(v).toUtf8().constData()));
    }

    return roots;
}

QString PCB3DView::resolveModelPath(const QString& rawPath) const {
    const QString expanded = expandModelEnvVars(rawPath);
    if (expanded.isEmpty()) return QString();

    auto pickExisting = [](const QString& p) -> QString {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(p));
        QFileInfo fi(clean);
        if (fi.exists() && fi.isFile()) return fi.absoluteFilePath();

        const QString suffix = fi.suffix().toLower();
        if (suffix == "wrl" || suffix == "step" || suffix == "stp") {
            const QString objPath = fi.path() + "/" + fi.completeBaseName() + ".obj";
            QFileInfo objFi(objPath);
            if (objFi.exists() && objFi.isFile()) return objFi.absoluteFilePath();
        }
        return QString();
    };

    if (QFileInfo(expanded).isAbsolute()) return pickExisting(expanded);
    {
        const QString local = pickExisting(expanded);
        if (!local.isEmpty()) return local;
    }

    for (const QString& root : modelSearchRoots()) {
        const QString candidate = QDir(root).filePath(expanded);
        const QString found = pickExisting(candidate);
        if (!found.isEmpty()) return found;
    }

    return QString();
}

QVector<PCB3DView::Vertex> PCB3DView::makeBoxVertices(float hx, float hy, float hz) const {
    QVector<Vertex> v;
    QVector3D p000(-hx, -hy, 0.0f), p100(hx, -hy, 0.0f), p110(hx, hy, 0.0f), p010(-hx, hy, 0.0f);
    QVector3D p001(-hx, -hy, hz),   p101(hx, -hy, hz),   p111(hx, hy, hz),   p011(-hx, hy, hz);

    appendQuad(v, p001, p101, p111, p011, {0, 0, 1});
    appendQuad(v, p000, p100, p101, p001, {0, -1, 0});
    appendQuad(v, p100, p110, p111, p101, {1, 0, 0});
    appendQuad(v, p110, p010, p011, p111, {0, 1, 0});
    appendQuad(v, p010, p000, p001, p011, {-1, 0, 0});
    appendQuad(v, p000, p010, p110, p100, {0, 0, -1});
    return v;
}

bool PCB3DView::passesSelectionFilter(QGraphicsItem* item) const {
    if (!m_selectedOnly) return true;
    if (!item) return false;
    if (item->isSelected()) return true;
    QGraphicsItem* p = item->parentItem();
    while (p) {
        if (p->isSelected()) return true;
        p = p->parentItem();
    }
    return false;
}

void PCB3DView::mousePressEvent(QMouseEvent* event) {
    m_lastPos = event->pos();
    if (event->button() == Qt::LeftButton) {
        m_leftPressed = true;
        m_pressPos = event->pos();
    }
}

void PCB3DView::mouseMoveEvent(QMouseEvent* event) {
    const int dx = int(event->position().x()) - m_lastPos.x();
    const int dy = int(event->position().y()) - m_lastPos.y();

    if (event->buttons() & Qt::LeftButton) {
        beginInteractiveRender();
        m_cameraAnimTimer.stop();
        m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), 0.45f * dx) * m_rotation;
        m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), 0.45f * dy) * m_rotation;
        update();
    }

    m_lastPos = event->pos();
}

void PCB3DView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_leftPressed) {
        m_leftPressed = false;
        if ((event->pos() - m_pressPos).manhattanLength() <= 3) {
            if (m_measureMode) {
                QVector3D hit;
                if (intersectBoardPlane(event->pos(), hit)) {
                    if (!m_measureHasFirst || m_measureHasSecond) {
                        m_measureP1 = hit;
                        m_measureHasFirst = true;
                        m_measureHasSecond = false;
                        emit measurementUpdated(-1.0);
                    } else {
                        m_measureP2 = hit;
                        m_measureHasSecond = true;
                        const double dist = QLineF(
                            QPointF(m_measureP1.x(), m_measureP1.y()),
                            QPointF(m_measureP2.x(), m_measureP2.y())).length();
                        emit measurementUpdated(dist);
                    }
                    update();
                }
                return;
            }
            QUuid id;
            if (pickAt(event->pos(), id)) {
                emit componentPicked(id);
            }
        }
    }
}

void PCB3DView::wheelEvent(QWheelEvent* event) {
    beginInteractiveRender();
    m_cameraAnimTimer.stop();
    m_zoom += event->angleDelta().y() * 0.2f;
    m_zoom = std::clamp(m_zoom, -2000.0f, -20.0f);
    update();
}

void PCB3DView::beginInteractiveRender() {
    if (m_renderMode != RenderMode::Fast) {
        m_renderMode = RenderMode::Fast;
        m_sceneDirty = true;
    }
    m_interactionTimer.start();
}

void PCB3DView::pollSpaceMouse() {
    if (!m_spaceMouseEnabled || !m_spaceMouseConnected || !m_spnavPollEvent) return;

    bool changed = false;
    SpnavEvent ev;
    int guard = 0;
    while (m_spnavPollEvent(&ev) > 0 && guard < 64) {
        ++guard;
        if (ev.type == kSpnavEventMotion) {
            const float panScale = std::max(0.02f, std::abs(m_zoom) * 0.0012f);
            const float rotScale = 0.02f;
            m_pan += QVector3D(ev.motion.x * panScale, -ev.motion.y * panScale, 0.0f);
            m_zoom += ev.motion.z * 0.12f;
            m_zoom = std::clamp(m_zoom, -2000.0f, -20.0f);
            m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), -ev.motion.rx * rotScale) * m_rotation;
            m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(1, 0, 0), ev.motion.ry * rotScale) * m_rotation;
            m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 1, 0), ev.motion.rz * rotScale) * m_rotation;
            changed = true;
        } else if (ev.type == kSpnavEventButton) {
            if (ev.button.press && ev.button.bnum == 0) {
                resetCamera();
                changed = true;
            }
        }
    }

    if (changed) {
        beginInteractiveRender();
        m_cameraAnimTimer.stop();
        update();
    }
}

void PCB3DView::startCameraTransition(const QQuaternion& targetRot, float targetZoom) {
    m_rotFrom = m_rotation;
    m_rotTo = targetRot;
    m_zoomFrom = m_zoom;
    m_zoomTo = targetZoom;
    m_cameraAnimT = 0.0f;
    m_cameraAnimTimer.start();
}

void PCB3DView::tickCameraAnimation() {
    m_cameraAnimT = std::min(1.0f, m_cameraAnimT + 0.06f);
    const float s = m_cameraAnimT * m_cameraAnimT * (3.0f - 2.0f * m_cameraAnimT);
    m_rotation = QQuaternion::slerp(m_rotFrom, m_rotTo, s);
    m_zoom = m_zoomFrom + (m_zoomTo - m_zoomFrom) * s;
    update();
    if (m_cameraAnimT >= 1.0f) {
        m_cameraAnimTimer.stop();
    }
}

void PCB3DView::tickSpinAnimation() {
    if (std::abs(m_spinSpeedDeg) < 1e-6f) {
        m_spinTimer.stop();
        return;
    }
    beginInteractiveRender();
    m_rotation = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), m_spinSpeedDeg) * m_rotation;
    update();
}

QVector3D PCB3DView::unprojectToWorld(const QPoint& p, float ndcZ) const {
    const float x = (2.0f * p.x()) / float(width()) - 1.0f;
    const float y = 1.0f - (2.0f * p.y()) / float(height());
    QVector4D clip(x, y, ndcZ, 1.0f);

    QMatrix4x4 view;
    view.translate(m_pan.x(), m_pan.y(), m_pan.z());
    view.translate(0.0f, 0.0f, m_zoom);
    view.rotate(m_rotation);

    QMatrix4x4 inv = (m_projection * view).inverted();
    QVector4D w = inv * clip;
    if (std::abs(w.w()) < 1e-8f) return QVector3D();
    return (w / w.w()).toVector3D();
}

bool PCB3DView::rayIntersectsAabb(const QVector3D& ro, const QVector3D& rd,
                                  const QVector3D& bmin, const QVector3D& bmax,
                                  float& outT) const {
    float tmin = 0.0f;
    float tmax = 1e30f;

    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ro[axis];
        const float dir = rd[axis];
        if (std::abs(dir) < 1e-9f) {
            if (origin < bmin[axis] || origin > bmax[axis]) return false;
            continue;
        }

        float t1 = (bmin[axis] - origin) / dir;
        float t2 = (bmax[axis] - origin) / dir;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }

    outT = tmin;
    return true;
}

bool PCB3DView::pickAt(const QPoint& pos, QUuid& outId) const {
    if (!m_showComponents) return false;
    if (const_cast<PCB3DView*>(this)->gpuPickAt(pos, outId)) return true;

    if (m_pickProxies.isEmpty()) return false;

    const QVector3D ro = unprojectToWorld(pos, -1.0f);
    const QVector3D rf = unprojectToWorld(pos, 1.0f);
    const QVector3D rd = (rf - ro).normalized();

    float bestT = 1e30f;
    bool hit = false;
    for (const PickProxy& proxy : m_pickProxies) {
        float t = 0.0f;
        if (rayIntersectsAabb(ro, rd, proxy.bmin, proxy.bmax, t) && t < bestT) {
            bestT = t;
            outId = proxy.id;
            hit = true;
        }
    }

    return hit;
}

bool PCB3DView::intersectBoardPlane(const QPoint& pos, QVector3D& out) const {
    const QVector3D ro = unprojectToWorld(pos, -1.0f);
    const QVector3D rf = unprojectToWorld(pos, 1.0f);
    const QVector3D rd = (rf - ro).normalized();
    if (std::abs(rd.z()) < 1e-8f) return false;

    const float zPlane = kCopperZTop + kCopperThickness;
    const float t = (zPlane - ro.z()) / rd.z();
    if (t < 0.0f) return false;
    out = ro + rd * t;
    return true;
}

void PCB3DView::ensurePickFbo() {
    const int pw = std::max(1, int(width() * devicePixelRatioF()));
    const int ph = std::max(1, int(height() * devicePixelRatioF()));
    if (m_pickFbo && m_pickFbo->size() == QSize(pw, ph)) return;

    delete m_pickFbo;
    m_pickFbo = nullptr;
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::Depth);
    fmt.setTextureTarget(GL_TEXTURE_2D);
    m_pickFbo = new QOpenGLFramebufferObject(pw, ph, fmt);
}

QMatrix4x4 PCB3DView::currentViewMatrix() const {
    QMatrix4x4 view;
    view.translate(m_pan.x(), m_pan.y(), m_pan.z());
    view.translate(0.0f, 0.0f, m_zoom);
    view.rotate(m_rotation);
    return view;
}

bool PCB3DView::gpuPickAt(const QPoint& pos, QUuid& outId) {
    if (!context() || !isValid()) return false;
    if (m_sceneDirty) rebuildSceneCache();
    if (m_componentDraws.isEmpty()) return false;

    makeCurrent();
    ensurePickFbo();
    if (!m_pickFbo) {
        doneCurrent();
        return false;
    }

    m_pickFbo->bind();
    const int pw = m_pickFbo->width();
    const int ph = m_pickFbo->height();
    glViewport(0, 0, pw, ph);
    glDisable(GL_BLEND);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_pickIdToUuid.clear();
    m_pickShader.bind();
    m_pickShader.setUniformValue("uView", currentViewMatrix());
    m_pickShader.setUniformValue("uProj", m_projection);
    QMatrix4x4 model;
    m_pickShader.setUniformValue("uModel", model);

    m_streamVbo.bind();
    m_pickShader.enableAttributeArray(0);
    m_pickShader.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, pos), 3, sizeof(Vertex));

    int id = 1;
    for (const ComponentDraw& draw : m_componentDraws) {
        if (draw.vertices.isEmpty()) continue;
        const int rid = id & 0xFF;
        const int gid = (id >> 8) & 0xFF;
        const int bid = (id >> 16) & 0xFF;
        m_pickShader.setUniformValue("uIdColor", QVector3D(rid / 255.0f, gid / 255.0f, bid / 255.0f));
        m_pickIdToUuid[id] = draw.id;

        m_streamVbo.allocate(draw.vertices.constData(), int(draw.vertices.size() * sizeof(Vertex)));
        glDrawArrays(GL_TRIANGLES, 0, draw.vertices.size());
        ++id;
    }

    m_pickShader.disableAttributeArray(0);
    m_streamVbo.release();
    m_pickShader.release();

    const int px = std::clamp(int(pos.x() * devicePixelRatioF()), 0, pw - 1);
    const int py = std::clamp(int(pos.y() * devicePixelRatioF()), 0, ph - 1);
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(px, ph - 1 - py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    m_pickFbo->release();
    glViewport(0, 0, std::max(1, int(width() * devicePixelRatioF())), std::max(1, int(height() * devicePixelRatioF())));
    glEnable(GL_BLEND);
    doneCurrent();

    const int pickedId = int(rgba[0]) | (int(rgba[1]) << 8) | (int(rgba[2]) << 16);
    if (pickedId <= 0 || !m_pickIdToUuid.contains(pickedId)) return false;
    outId = m_pickIdToUuid.value(pickedId);
    return !outId.isNull();
}

void PCB3DView::drawAxisTriadOverlay() {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPoint c(width() - 54, 54);
    const float scale = 24.0f;

    auto proj = [&](const QVector3D& axis) {
        QVector3D v = m_rotation.rotatedVector(axis);
        return QPointF(c.x() + v.x() * scale, c.y() - v.y() * scale);
    };

    p.setPen(QPen(QColor(245, 70, 70), 2));
    p.drawLine(c, proj(QVector3D(1, 0, 0)));
    p.drawText(proj(QVector3D(1, 0, 0)) + QPointF(2, -2), "X");

    p.setPen(QPen(QColor(70, 220, 100), 2));
    p.drawLine(c, proj(QVector3D(0, 1, 0)));
    p.drawText(proj(QVector3D(0, 1, 0)) + QPointF(2, -2), "Y");

    p.setPen(QPen(QColor(90, 140, 255), 2));
    p.drawLine(c, proj(QVector3D(0, 0, 1)));
    p.drawText(proj(QVector3D(0, 0, 1)) + QPointF(2, -2), "Z");

    p.setPen(QPen(QColor(180, 190, 210, 180), 1));
    p.setBrush(QColor(25, 30, 38, 170));
    p.drawEllipse(c, 4, 4);
}

void PCB3DView::drawGridOverlay() {
    if (!m_showGrid || m_substrateVerts.isEmpty()) return;

    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    bool hasBounds = false;
    for (const Vertex& v : m_substrateVerts) {
        const float x = v.pos.x();
        const float y = v.pos.y();
        if (!hasBounds) {
            minX = maxX = x;
            minY = maxY = y;
            hasBounds = true;
            continue;
        }
        minX = std::min(minX, x);
        minY = std::min(minY, y);
        maxX = std::max(maxX, x);
        maxY = std::max(maxY, y);
    }
    if (!hasBounds || maxX <= minX || maxY <= minY) return;

    auto chooseStep = [](float span) -> float {
        if (span <= 0.0f) return 1.0f;
        const float raw = span / 20.0f;
        const float p10 = std::pow(10.0f, std::floor(std::log10(raw)));
        const float n = raw / p10;
        float m = 1.0f;
        if (n > 5.0f) m = 10.0f;
        else if (n > 2.0f) m = 5.0f;
        else if (n > 1.0f) m = 2.0f;
        return m * p10;
    };

    const float span = std::max(maxX - minX, maxY - minY);
    const float step = chooseStep(span);
    const float zPlane = kCopperZTop + kCopperThickness + 0.002f;

    auto project = [&](const QVector3D& w, QPointF& outPt) -> bool {
        const QMatrix4x4 view = currentViewMatrix();
        const QVector4D clip = m_projection * view * QVector4D(w, 1.0f);
        if (std::abs(clip.w()) < 1e-8f) return false;
        const QVector3D ndc = (clip / clip.w()).toVector3D();
        outPt.setX((ndc.x() * 0.5f + 0.5f) * width());
        outPt.setY((1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        return true;
    };

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(160, 175, 196, 70), 1.0));

    const float startX = std::floor(minX / step) * step;
    const float endX = std::ceil(maxX / step) * step;
    const float startY = std::floor(minY / step) * step;
    const float endY = std::ceil(maxY / step) * step;

    for (float x = startX; x <= endX + 1e-6f; x += step) {
        QPointF a, b;
        if (project(QVector3D(x, minY, zPlane), a) && project(QVector3D(x, maxY, zPlane), b)) {
            p.drawLine(a, b);
        }
    }
    for (float y = startY; y <= endY + 1e-6f; y += step) {
        QPointF a, b;
        if (project(QVector3D(minX, y, zPlane), a) && project(QVector3D(maxX, y, zPlane), b)) {
            p.drawLine(a, b);
        }
    }
}

void PCB3DView::drawMeasurementOverlay() {
    if (!m_measureMode || !m_measureHasFirst) return;

    auto project = [&](const QVector3D& w, QPointF& outPt) -> bool {
        const QMatrix4x4 view = currentViewMatrix();
        const QVector4D clip = m_projection * view * QVector4D(w, 1.0f);
        if (std::abs(clip.w()) < 1e-8f) return false;
        const QVector3D ndc = (clip / clip.w()).toVector3D();
        outPt.setX((ndc.x() * 0.5f + 0.5f) * width());
        outPt.setY((1.0f - (ndc.y() * 0.5f + 0.5f)) * height());
        return true;
    };

    QPointF p1;
    if (!project(m_measureP1, p1)) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(255, 212, 59), 2.0));
    p.setBrush(QBrush(QColor(255, 212, 59, 140)));
    p.drawEllipse(p1, 4, 4);

    if (!m_measureHasSecond) {
        p.setPen(QPen(QColor(255, 212, 59, 180), 1.0, Qt::DashLine));
        p.drawText(p1 + QPointF(8, -8), "Pick second point");
        return;
    }

    QPointF p2;
    if (!project(m_measureP2, p2)) return;
    p.drawEllipse(p2, 4, 4);
    p.setPen(QPen(QColor(255, 212, 59), 1.5));
    p.drawLine(p1, p2);

    const double dist = QLineF(
        QPointF(m_measureP1.x(), m_measureP1.y()),
        QPointF(m_measureP2.x(), m_measureP2.y())).length();
    const QString label = QString("%1 mm").arg(dist, 0, 'f', 3);
    const QPointF mid = (p1 + p2) * 0.5;
    QRectF textRect = p.fontMetrics().boundingRect(label).adjusted(-6, -3, 6, 3);
    textRect.moveCenter(mid + QPointF(0, -12));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 24, 31, 210));
    p.drawRoundedRect(textRect, 4, 4);
    p.setPen(QPen(QColor(255, 232, 140), 1.0));
    p.drawText(textRect, Qt::AlignCenter, label);
}
