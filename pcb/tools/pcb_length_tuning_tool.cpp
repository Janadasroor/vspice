#include "pcb_length_tuning_tool.h"
#include "pcb_view.h"
#include "trace_item.h"
#include "pcb_commands.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsScene>
#include <QPainterPath>
#include <cmath>
#include <QDebug>

PCBLengthTuningTool::PCBLengthTuningTool(QObject* parent)
    : PCBTool("Length Tuning", parent)
    , m_isActive(false)
    , m_targetTrace(nullptr)
    , m_targetLength(50.0)
    , m_amplitude(2.0)
    , m_spacing(1.0)
    , m_previewPath(nullptr) {
}

PCBLengthTuningTool::~PCBLengthTuningTool() {
}

void PCBLengthTuningTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;

    QPointF scenePos = view()->mapToScene(event->pos());
    QGraphicsItem* item = view()->itemAt(event->pos());
    m_targetTrace = dynamic_cast<TraceItem*>(item);

    if (m_targetTrace) {
        m_isActive = true;
        if (!m_previewPath) {
            m_previewPath = new QGraphicsPathItem();
            m_previewPath->setPen(QPen(Qt::yellow, m_targetTrace->width(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            m_previewPath->setZValue(2000);
            view()->scene()->addItem(m_previewPath);
        }
        updateMeanderPreview(scenePos);
        if (view()) {
            view()->statusMessage(QString("Length tuning: target=%1mm amp=%2mm spacing=%3mm | [U]/[I] amplitude")
                                  .arg(m_targetLength).arg(m_amplitude).arg(m_spacing));
        }
    }
    
    event->accept();
}

void PCBLengthTuningTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isActive && m_targetTrace) {
        QPointF scenePos = view()->mapToScene(event->pos());
        updateMeanderPreview(scenePos);
    }
    event->accept();
}

void PCBLengthTuningTool::mouseReleaseEvent(QMouseEvent* event) {
    if (m_isActive && m_targetTrace && view() && view()->scene()) {
        const QPointF start = m_targetTrace->mapToScene(m_targetTrace->startPoint());
        const QPointF end = m_targetTrace->mapToScene(m_targetTrace->endPoint());
        const QList<QPointF> points = generateMeanderPoints(start, end, m_targetLength);

        QList<PCBItem*> replacementSegments;
        replacementSegments.reserve(points.size());
        for (int i = 0; i + 1 < points.size(); ++i) {
            if (QLineF(points[i], points[i + 1]).length() < 0.01) continue;
            TraceItem* seg = new TraceItem(points[i], points[i + 1], m_targetTrace->width());
            seg->setLayer(m_targetTrace->layer());
            seg->setNetName(m_targetTrace->netName());
            replacementSegments.append(seg);
        }

        if (replacementSegments.size() > 1 && view()->undoStack()) {
            QList<PCBItem*> original;
            original.append(m_targetTrace);
            view()->undoStack()->beginMacro("Length Tune Trace");
            view()->undoStack()->push(new PCBRemoveItemCommand(view()->scene(), original));
            view()->undoStack()->push(new PCBAddItemsCommand(view()->scene(), replacementSegments));
            view()->undoStack()->endMacro();
        } else {
            qDeleteAll(replacementSegments);
        }

        m_isActive = false;
        if (m_previewPath) {
            view()->scene()->removeItem(m_previewPath);
            delete m_previewPath;
            m_previewPath = nullptr;
        }
        m_targetTrace = nullptr;
    }
    event->accept();
}

void PCBLengthTuningTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_U) {
        m_amplitude += 0.2;
    } else if (event->key() == Qt::Key_I) {
        m_amplitude = std::max(0.2, m_amplitude - 0.2);
    }
    PCBTool::keyPressEvent(event);
}

void PCBLengthTuningTool::updateMeanderPreview(QPointF pos) {
    Q_UNUSED(pos)
    if (!m_targetTrace || !m_previewPath) return;

    const QPointF start = m_targetTrace->mapToScene(m_targetTrace->startPoint());
    const QPointF end = m_targetTrace->mapToScene(m_targetTrace->endPoint());
    QPainterPath path = generateMeander(start, end, m_targetLength);
    m_previewPath->setPath(path);
}

QPainterPath PCBLengthTuningTool::generateMeander(QPointF start, QPointF end, double targetLen) {
    const QList<QPointF> points = generateMeanderPoints(start, end, targetLen);
    QPainterPath path;
    if (points.isEmpty()) return path;
    path.moveTo(points.first());
    for (int i = 1; i < points.size(); ++i) {
        path.lineTo(points[i]);
    }
    return path;
}

QList<QPointF> PCBLengthTuningTool::generateMeanderPoints(QPointF start, QPointF end, double targetLen) const {
    QList<QPointF> points;
    points.append(start);
    QLineF baseLine(start, end);
    double currentLen = baseLine.length();
    if (currentLen >= targetLen) {
        points.append(end);
        return points;
    }

    // Serpentine algorithm
    double neededExtra = targetLen - currentLen;
    QPointF dir = (end - start) / currentLen;
    QPointF normal(-dir.y(), dir.x());

    int numHumps = static_cast<int>(currentLen / std::max(0.2, m_spacing));
    if (numHumps < 2) {
        points.append(end);
        return points;
    }

    const double pitch = currentLen / numHumps;
    double extraPerHump = neededExtra / numHumps;
    double h = std::sqrt(std::max(0.0, std::pow((extraPerHump + pitch) / 2.0, 2) - std::pow(pitch / 2.0, 2)));
    h = std::min(h, std::max(0.2, m_amplitude)); // Cap at configured amplitude

    for (int i = 0; i < numHumps; ++i) {
        QPointF p1 = start + dir * (i * pitch + pitch * 0.25) + normal * h;
        QPointF p2 = start + dir * (i * pitch + pitch * 0.75) - normal * h;
        points.append(p1);
        points.append(p2);
    }

    points.append(end);
    return points;
}

QMap<QString, QVariant> PCBLengthTuningTool::toolProperties() const {
    QMap<QString, QVariant> props;
    props["Target Length (mm)"] = m_targetLength;
    props["Amplitude (mm)"] = m_amplitude;
    props["Spacing (mm)"] = m_spacing;
    return props;
}

void PCBLengthTuningTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Target Length (mm)") m_targetLength = value.toDouble();
    else if (name == "Amplitude (mm)") m_amplitude = value.toDouble();
    else if (name == "Spacing (mm)") m_spacing = std::max(0.2, value.toDouble());
}
