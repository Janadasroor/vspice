#include "pcb_diff_pair_tool.h"
#include "pcb_view.h"
#include "pcb_item.h"
#include "trace_item.h"
#include "theme_manager.h"
#include "pcb_drc.h"
#include <QMouseEvent>
#include <QGraphicsScene>
#include <QPainterPathStroker>
#include <cmath>
#include <algorithm>

namespace {
bool parseDiffNet(const QString& net, QString& base, bool& isP) {
    if (net.isEmpty() || net == "No Net") return false;
    const QString upper = net.toUpper();
    const QStringList pSuffixes = {"_P", "-P", "+", ".P"};
    const QStringList nSuffixes = {"_N", "-N", "-", ".N"};
    for (const QString& s : pSuffixes) {
        if (upper.endsWith(s)) {
            base = net.left(net.size() - s.size());
            isP = true;
            return !base.isEmpty();
        }
    }
    for (const QString& s : nSuffixes) {
        if (upper.endsWith(s)) {
            base = net.left(net.size() - s.size());
            isP = false;
            return !base.isEmpty();
        }
    }
    return false;
}

QString makeDiffNet(const QString& base, bool pSide) {
    return base + (pSide ? "_P" : "_N");
}
}

PCBDiffPairTool::PCBDiffPairTool(QObject* parent)
    : PCBTraceTool(parent) {
}

void PCBDiffPairTool::deactivate() {
    cleanupDiffPreview();
    PCBTraceTool::deactivate();
}

void PCBDiffPairTool::mousePressEvent(QMouseEvent* event) {
    if (!view() || event->button() != Qt::LeftButton) return;
    
    QPointF scenePos = view()->mapToScene(event->pos());
    QPointF snappedPos = view()->snapToGrid(scenePos);

    if (!m_isDiffRouting) {
        QString pNet;
        QString nNet;
        QString pairBase;
        detectPairNetsAtPoint(snappedPos, pNet, nNet, pairBase);

        if (pairBase.isEmpty()) pairBase = "DIFF";
        m_currentNet = pairBase;
        m_targetNet = pNet.isEmpty() ? makeDiffNet(pairBase, true) : pNet;

        QPointF p_pos = snappedPos;
        QPointF n_pos = p_pos + QPointF(0, m_pairGap + m_traceWidth);
        startDiffPair(p_pos, n_pos);
    } else {
        const bool constrain45 = event->modifiers() & Qt::ShiftModifier;
        if (constrain45) {
            snappedPos = constrainAngle(m_lastP, snappedPos, true);
        }
        addDiffSegment(snappedPos);
    }
    event->accept();
}

void PCBDiffPairTool::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDiffRouting) {
        QPointF scenePos = view()->mapToScene(event->pos());
        QPointF snapped = view()->snapToGrid(scenePos);
        if (event->modifiers() & Qt::ShiftModifier) {
            snapped = constrainAngle(m_lastP, snapped, true);
        }
        updateDiffPreview(snapped);
        event->accept();
    } else {
        PCBTraceTool::mouseMoveEvent(event);
    }
}

void PCBDiffPairTool::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_isDiffRouting) {
        finishDiffPair();
        event->accept();
    } else {
        PCBTraceTool::mouseDoubleClickEvent(event);
    }
}

void PCBDiffPairTool::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && m_isDiffRouting) {
        cancelTrace();
        cleanupDiffPreview();
        m_isDiffRouting = false;
        event->accept();
        return;
    }
    PCBTraceTool::keyPressEvent(event);
}

QMap<QString, QVariant> PCBDiffPairTool::toolProperties() const {
    QMap<QString, QVariant> props = PCBTraceTool::toolProperties();
    props["Pair Gap (mm)"] = m_pairGap;
    return props;
}

void PCBDiffPairTool::setToolProperty(const QString& name, const QVariant& value) {
    if (name == "Pair Gap (mm)") {
        setGap(std::max(0.01, value.toDouble()));
        return;
    }
    PCBTraceTool::setToolProperty(name, value);
}

bool PCBDiffPairTool::detectPairNetsAtPoint(const QPointF& scenePos, QString& pNet, QString& nNet, QString& pairBase) const {
    if (!view() || !view()->scene()) return false;

    QList<QGraphicsItem*> hits = view()->scene()->items(QRectF(scenePos.x() - 0.6, scenePos.y() - 0.6, 1.2, 1.2));
    QString seed;
    for (QGraphicsItem* g : hits) {
        QGraphicsItem* cur = g;
        while (cur) {
            if (PCBItem* item = dynamic_cast<PCBItem*>(cur)) {
                if (!item->netName().isEmpty() && item->netName() != "No Net") {
                    seed = item->netName();
                    break;
                }
            }
            cur = cur->parentItem();
        }
        if (!seed.isEmpty()) break;
    }

    if (seed.isEmpty()) return false;

    bool seedIsP = true;
    QString base;
    if (!parseDiffNet(seed, base, seedIsP)) {
        return false;
    }
    pairBase = base;

    const QString candidateP = makeDiffNet(base, true);
    const QString candidateN = makeDiffNet(base, false);
    if (seedIsP) {
        pNet = seed;
        nNet = candidateN;
    } else {
        nNet = seed;
        pNet = candidateP;
    }
    return true;
}

QPointF PCBDiffPairTool::pairOffsetForTarget(const QPointF& pFrom, const QPointF& pTo, const QPointF& nFrom) const {
    QPointF d = pTo - pFrom;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-9) {
        return nFrom - pFrom;
    }

    QPointF normal(-d.y() / len, d.x() / len);
    QPointF a = normal * (m_pairGap + m_traceWidth);
    QPointF b = -a;
    const double da = QLineF((pFrom + a), nFrom).length();
    const double db = QLineF((pFrom + b), nFrom).length();
    return (da <= db) ? a : b;
}

void PCBDiffPairTool::startDiffPair(QPointF p_pos, QPointF n_pos) {
    m_isDiffRouting = true;
    m_lastP = p_pos;
    m_lastN = n_pos;
    
    // Use base class startTrace for the P signal and guides.
    startTrace(p_pos);
    m_lastP = p_pos;
    m_lastN = n_pos;
    
    if (!m_previewN1) {
        m_previewN1 = new QGraphicsLineItem();
        m_previewN2 = new QGraphicsLineItem();
        m_clearanceHaloN1 = new QGraphicsPathItem();
        m_clearanceHaloN2 = new QGraphicsPathItem();
        
        view()->scene()->addItem(m_previewN1);
        view()->scene()->addItem(m_previewN2);
        view()->scene()->addItem(m_clearanceHaloN1);
        view()->scene()->addItem(m_clearanceHaloN2);
        
        m_previewN1->setZValue(2000);
        m_previewN2->setZValue(2000);
        m_clearanceHaloN1->setZValue(1999);
        m_clearanceHaloN2->setZValue(1999);
    }

    if (view()) {
        const QString pNet = makeDiffNet(m_currentNet.isEmpty() ? "DIFF" : m_currentNet, true);
        const QString nNet = makeDiffNet(m_currentNet.isEmpty() ? "DIFF" : m_currentNet, false);
        view()->statusMessage(QString("Diff Pair: %1 / %2 | Width: %3mm Gap: %4mm | [Shift] 45deg")
                              .arg(pNet).arg(nNet).arg(m_traceWidth).arg(m_pairGap));
    }
}

void PCBDiffPairTool::updateDiffPreview(QPointF p_pos) {
    if (!m_isDiffRouting) return;
    
    // Update P preview using base class
    updatePreview(p_pos);
    
    // N preview follows P with perpendicular offset to routing direction.
    const QPointF offsetDir = pairOffsetForTarget(m_lastP, p_pos, m_lastN);
    
    // Recalculate P segments (using same logic as base class)
    double dx = p_pos.x() - m_lastP.x();
    double dy = p_pos.y() - m_lastP.y();
    QPointF p_elbow;
    if (std::abs(dx) > std::abs(dy)) {
        p_elbow = QPointF(m_lastP.x() + std::copysign(std::abs(dy), dx), p_pos.y());
    } else {
        p_elbow = QPointF(p_pos.x(), m_lastP.y() + std::copysign(std::abs(dx), dy));
    }
    
    QPointF n_elbow = p_elbow + offsetDir;
    QPointF n_end = p_pos + offsetDir;
    
    m_previewN1->setLine(m_lastN.x(), m_lastN.y(), n_elbow.x(), n_elbow.y());
    m_previewN2->setLine(n_elbow.x(), n_elbow.y(), n_end.x(), n_end.y());
    
    // Theme and Clearance Halos for N
    PCBTheme* theme = ThemeManager::theme();
    QColor copper = (m_currentLayer == 0) ? theme->topCopper() : theme->bottomCopper();
    copper.setAlpha(180);
    QPen penN(copper, m_traceWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    m_previewN1->setPen(penN);
    m_previewN2->setPen(penN);

    // Clearance logic
    PCBDRC drc;
    double clearance = drc.rules().minClearance();
    double haloWidth = m_traceWidth + (clearance * 2.0);
    
    auto createHalo = [&](QPointF s, QPointF e) {
        QPainterPath path;
        if (QLineF(s,e).length() < 0.01) return path;
        QPainterPath lp; lp.moveTo(s); lp.lineTo(e);
        QPainterPathStroker st; st.setWidth(haloWidth);
        st.setCapStyle(Qt::RoundCap); st.setJoinStyle(Qt::RoundJoin);
        return st.createStroke(lp);
    };

    QPainterPath h1 = createHalo(m_lastN, n_elbow);
    QPainterPath h2 = createHalo(n_elbow, n_end);
    m_clearanceHaloN1->setPath(h1);
    m_clearanceHaloN2->setPath(h2);

    bool coll1 = checkClearance(h1);
    bool coll2 = checkClearance(h2);
    
    QColor safeColor(0, 240, 255, 30);
    QColor errorColor(255, 50, 50, 90);
    
    m_clearanceHaloN1->setBrush(coll1 ? errorColor : safeColor);
    m_clearanceHaloN1->setPen(QPen(coll1 ? QColor(255, 0, 0, 150) : QColor(0, 200, 255, 60), 0.05));
    m_clearanceHaloN2->setBrush(coll2 ? errorColor : safeColor);
    m_clearanceHaloN2->setPen(QPen(coll2 ? QColor(255, 0, 0, 150) : QColor(0, 200, 255, 60), 0.05));
}

void PCBDiffPairTool::addDiffSegment(QPointF p_pos) {
    if (!m_isDiffRouting) return;
    
    const QPointF offsetDir = pairOffsetForTarget(m_lastP, p_pos, m_lastN);
    
    double dx = p_pos.x() - m_lastP.x();
    double dy = p_pos.y() - m_lastP.y();
    QPointF p_elbow;
    if (std::abs(dx) > std::abs(dy)) {
        p_elbow = QPointF(m_lastP.x() + std::copysign(std::abs(dy), dx), p_pos.y());
    } else {
        p_elbow = QPointF(p_pos.x(), m_lastP.y() + std::copysign(std::abs(dx), dy));
    }
    
    QPointF n_elbow = p_elbow + offsetDir;
    QPointF n_end = p_pos + offsetDir;
    
    auto addSeg = [&](QPointF s, QPointF e, const QString& net) {
        if (QLineF(s,e).length() < 0.01) return;
        TraceItem* t = new TraceItem(s, e, m_traceWidth);
        t->setLayer(m_currentLayer);
        t->setNetName(net);
        view()->scene()->addItem(t);
        m_currentTraceItems.append(t);
    };
    
    const QString pairBase = m_currentNet.isEmpty() ? "DIFF" : m_currentNet;
    const QString pNet = makeDiffNet(pairBase, true);
    const QString nNet = makeDiffNet(pairBase, false);
    addSeg(m_lastP, p_elbow, pNet);
    addSeg(p_elbow, p_pos, pNet);
    addSeg(m_lastN, n_elbow, nNet);
    addSeg(n_elbow, n_end, nNet);
    
    m_lastP = p_pos;
    m_lastN = n_end;
    
    // Update base class points so guidelines/snapping work
    m_lastPoint = p_pos;
}

void PCBDiffPairTool::finishDiffPair() {
    finishTrace();
    cleanupDiffPreview();
    m_isDiffRouting = false;
}

void PCBDiffPairTool::cleanupDiffPreview() {
    if (m_previewN1 && view() && view()->scene()) {
        view()->scene()->removeItem(m_previewN1);
        view()->scene()->removeItem(m_previewN2);
        view()->scene()->removeItem(m_clearanceHaloN1);
        view()->scene()->removeItem(m_clearanceHaloN2);
        delete m_previewN1;
        delete m_previewN2;
        delete m_clearanceHaloN1;
        delete m_clearanceHaloN2;
    }
    m_previewN1 = m_previewN2 = nullptr;
    m_clearanceHaloN1 = m_clearanceHaloN2 = nullptr;
}
