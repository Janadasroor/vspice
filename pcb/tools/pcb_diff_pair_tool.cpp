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
constexpr double kPairSpacingEps = 1e-6;

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

bool pointsNearPair(const QPointF& a, const QPointF& b, double eps = 1e-3) {
    return QLineF(a, b).length() <= eps;
}

QPointF unitLeftNormal(const QPointF& from, const QPointF& to) {
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < kPairSpacingEps) return QPointF(0.0, 0.0);
    return QPointF(-d.y() / len, d.x() / len);
}

bool intersectInfiniteLines(const QPointF& a1, const QPointF& a2,
                            const QPointF& b1, const QPointF& b2,
                            QPointF& out) {
    const QPointF r = a2 - a1;
    const QPointF s = b2 - b1;
    const double det = r.x() * s.y() - r.y() * s.x();
    if (std::abs(det) < kPairSpacingEps) return false;

    const QPointF qp = b1 - a1;
    const double t = (qp.x() * s.y() - qp.y() * s.x()) / det;
    out = a1 + r * t;
    return true;
}

QVector<QPointF> dedupePolyline(const QVector<QPointF>& points) {
    QVector<QPointF> out;
    for (const QPointF& p : points) {
        if (out.isEmpty() || !pointsNearPair(out.last(), p)) out.append(p);
    }
    return out;
}

void setLinePreviewSegment(QGraphicsLineItem* item, const QPointF& a, const QPointF& b) {
    if (!item) return;
    if (QLineF(a, b).length() < 0.01) {
        item->hide();
        return;
    }
    item->setLine(QLineF(a, b));
    item->show();
}

void setPathPreviewSegment(QGraphicsPathItem* item, const QPainterPath& path) {
    if (!item) return;
    item->setPath(path);
    item->setVisible(!path.isEmpty());
}
}

PCBDiffPairTool::PCBDiffPairTool(QObject* parent)
    : PCBTraceTool(parent) {
}

void PCBDiffPairTool::deactivate() {
    if (m_isDiffRouting || m_isRouting) {
        cancelTrace();
    }
    cleanupDiffPreview();
    m_isDiffRouting = false;
    m_lastP = QPointF();
    m_lastN = QPointF();
    PCBTool::deactivate();
}

void PCBDiffPairTool::mousePressEvent(QMouseEvent* event) {
    if (!view()) return;
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    
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
        m_lastP = QPointF();
        m_lastN = QPointF();
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

QVector<QPointF> PCBDiffPairTool::primaryRoutePolyline(const QPointF& from, const QPointF& to) {
    QVector<QPointF> polyline;
    polyline.append(from);

    if (m_routingMode == Hugging) {
        const QPainterPath path = calculateHuggingPath(from, to);
        for (int i = 1; i < path.elementCount(); ++i) {
            polyline.append(QPointF(path.elementAt(i).x, path.elementAt(i).y));
        }
        if (polyline.size() == 1) polyline.append(to);
        return dedupePolyline(polyline);
    }

    QPointF elbow;
    if (m_routingMode == WalkAround) {
        elbow = chooseWalkAroundElbow(from, to);
    } else {
        const double dx = to.x() - from.x();
        const double dy = to.y() - from.y();
        if (std::abs(dx) > std::abs(dy)) {
            elbow = QPointF(from.x() + std::copysign(std::abs(dy), dx), to.y());
        } else {
            elbow = QPointF(to.x(), from.y() + std::copysign(std::abs(dx), dy));
        }
    }

    if (!pointsNearPair(from, elbow)) polyline.append(elbow);
    if (!pointsNearPair(polyline.last(), to)) polyline.append(to);
    return dedupePolyline(polyline);
}

double PCBDiffPairTool::choosePairSideSign(const QVector<QPointF>& primaryPolyline, const QPointF& mateStart) const {
    const QVector<QPointF> primary = dedupePolyline(primaryPolyline);
    if (primary.size() < 2) return 1.0;

    int firstSeg = -1;
    for (int i = 0; i + 1 < primary.size(); ++i) {
        if (!pointsNearPair(primary[i], primary[i + 1])) {
            firstSeg = i;
            break;
        }
    }
    if (firstSeg < 0) return 1.0;

    const QPointF firstNormal = unitLeftNormal(primary[firstSeg], primary[firstSeg + 1]);
    if (std::abs(firstNormal.x()) < kPairSpacingEps && std::abs(firstNormal.y()) < kPairSpacingEps) {
        return 1.0;
    }

    const double offsetDistance = m_pairGap + m_traceWidth;
    const QPointF shiftedA = primary.first() + firstNormal * offsetDistance;
    const QPointF shiftedB = primary.first() - firstNormal * offsetDistance;
    return (QLineF(shiftedA, mateStart).length() <= QLineF(shiftedB, mateStart).length()) ? 1.0 : -1.0;
}

QVector<QPointF> PCBDiffPairTool::pairedRoutePolyline(const QVector<QPointF>& primaryPolyline, double offsetDistance, double sideSign) const {
    const QVector<QPointF> primary = dedupePolyline(primaryPolyline);
    QVector<QPointF> secondary;
    if (primary.size() < 2) return secondary;
    if (offsetDistance < kPairSpacingEps) return secondary;

    int firstSeg = -1;
    for (int i = 0; i + 1 < primary.size(); ++i) {
        if (!pointsNearPair(primary[i], primary[i + 1])) {
            firstSeg = i;
            break;
        }
    }
    if (firstSeg < 0) return secondary;

    const QPointF firstNormal = unitLeftNormal(primary[firstSeg], primary[firstSeg + 1]);
    if (std::abs(firstNormal.x()) < kPairSpacingEps && std::abs(firstNormal.y()) < kPairSpacingEps) {
        return secondary;
    }

    secondary.append(primary.first() + firstNormal * (offsetDistance * sideSign));

    for (int i = 1; i + 1 < primary.size(); ++i) {
        const QPointF prevNormal = unitLeftNormal(primary[i - 1], primary[i]);
        const QPointF nextNormal = unitLeftNormal(primary[i], primary[i + 1]);

        const QPointF prevA = primary[i - 1] + prevNormal * (offsetDistance * sideSign);
        const QPointF prevB = primary[i] + prevNormal * (offsetDistance * sideSign);
        const QPointF nextA = primary[i] + nextNormal * (offsetDistance * sideSign);
        const QPointF nextB = primary[i + 1] + nextNormal * (offsetDistance * sideSign);

        QPointF corner;
        const bool hasIntersection = intersectInfiniteLines(prevA, prevB, nextA, nextB, corner);
        if (!hasIntersection || QLineF(corner, (prevB + nextA) * 0.5).length() > offsetDistance * 4.0) {
            corner = (prevB + nextA) * 0.5;
        }

        if (secondary.isEmpty() || !pointsNearPair(secondary.last(), corner)) {
            secondary.append(corner);
        }
    }

    const QPointF lastNormal = unitLeftNormal(primary[primary.size() - 2], primary.last());
    const QPointF lastPoint = primary.last() + lastNormal * (offsetDistance * sideSign);
    if (secondary.isEmpty() || !pointsNearPair(secondary.last(), lastPoint)) {
        secondary.append(lastPoint);
    }

    return dedupePolyline(secondary);
}

bool PCBDiffPairTool::polylineBlockedForNet(const QVector<QPointF>& polyline, const QString& netName) const {
    if (polyline.size() < 2) return true;

    for (int i = 0; i + 1 < polyline.size(); ++i) {
        if (QLineF(polyline[i], polyline[i + 1]).length() < 0.01) continue;
        TraceItem probe(polyline[i], polyline[i + 1], m_traceWidth);
        probe.setLayer(m_currentLayer);
        probe.setNetName(netName);
        if (!collectLiveClearanceViolations(probe).isEmpty()) return true;
    }

    return false;
}

QVector<QPointF> PCBDiffPairTool::adaptivePairedRoutePolyline(const QVector<QPointF>& primaryPolyline, const QPointF& mateStart, const QString& netName) const {
    const QVector<QPointF> primary = dedupePolyline(primaryPolyline);
    if (primary.size() < 2) return {};

    const double sideSign = choosePairSideSign(primary, mateStart);
    const double preferredOffset = m_pairGap + m_traceWidth;
    const double step = 0.05;
    const double maxExtra = 2.0;

    QVector<QPointF> fallback = pairedRoutePolyline(primary, preferredOffset, sideSign);
    if (!fallback.isEmpty() && !polylineBlockedForNet(fallback, netName)) {
        return fallback;
    }

    for (double extra = step; extra <= maxExtra + 1e-9; extra += step) {
        QVector<QPointF> candidate = pairedRoutePolyline(primary, preferredOffset + extra, sideSign);
        if (!candidate.isEmpty() && !polylineBlockedForNet(candidate, netName)) {
            return candidate;
        }
    }

    return fallback;
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
    
    const QVector<QPointF> pPolyline = primaryRoutePolyline(m_lastP, p_pos);
    const QString pairBase = m_currentNet.isEmpty() ? "DIFF" : m_currentNet;
    const QString nNet = makeDiffNet(pairBase, false);
    const QVector<QPointF> nPolyline = adaptivePairedRoutePolyline(pPolyline, m_lastN, nNet);

    const QPointF n1Start = nPolyline.size() > 0 ? nPolyline[0] : m_lastN;
    const QPointF n1End = nPolyline.size() > 1 ? nPolyline[1] : n1Start;
    const QPointF n2Start = nPolyline.size() > 1 ? nPolyline[1] : n1End;
    const QPointF n2End = nPolyline.size() > 2 ? nPolyline[2] : n2Start;

    setLinePreviewSegment(m_previewN1, n1Start, n1End);
    setLinePreviewSegment(m_previewN2, n2Start, n2End);
    
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

    QPainterPath h1 = createHalo(n1Start, n1End);
    QPainterPath h2 = createHalo(n2Start, n2End);
    setPathPreviewSegment(m_clearanceHaloN1, h1);
    setPathPreviewSegment(m_clearanceHaloN2, h2);

    bool coll1 = polylineBlockedForNet({n1Start, n1End}, nNet);
    bool coll2 = polylineBlockedForNet({n2Start, n2End}, nNet);
    
    QColor safeColor(0, 240, 255, 30);
    QColor errorColor(255, 50, 50, 90);
    
    m_clearanceHaloN1->setBrush(coll1 ? errorColor : safeColor);
    m_clearanceHaloN1->setPen(QPen(coll1 ? QColor(255, 0, 0, 150) : QColor(0, 200, 255, 60), 0.05));
    m_clearanceHaloN2->setBrush(coll2 ? errorColor : safeColor);
    m_clearanceHaloN2->setPen(QPen(coll2 ? QColor(255, 0, 0, 150) : QColor(0, 200, 255, 60), 0.05));
}

void PCBDiffPairTool::addDiffSegment(QPointF p_pos) {
    if (!m_isDiffRouting) return;

    const QVector<QPointF> pPolyline = primaryRoutePolyline(m_lastP, p_pos);
    if (pPolyline.size() < 2) return;

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
    const QVector<QPointF> nPolyline = adaptivePairedRoutePolyline(pPolyline, m_lastN, nNet);
    if (nPolyline.size() < 2) return;
    if (polylineBlockedForNet(pPolyline, pNet) || polylineBlockedForNet(nPolyline, nNet)) return;

    for (int i = 0; i + 1 < pPolyline.size(); ++i) addSeg(pPolyline[i], pPolyline[i + 1], pNet);
    for (int i = 0; i + 1 < nPolyline.size(); ++i) addSeg(nPolyline[i], nPolyline[i + 1], nNet);

    m_lastP = pPolyline.last();
    m_lastN = nPolyline.last();
    
    // Update base class points so guidelines/snapping work
    m_lastPoint = m_lastP;
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
