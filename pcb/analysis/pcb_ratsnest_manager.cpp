#include "pcb_ratsnest_manager.h"
#include "ratsnest_item.h"
#include "pad_item.h"
#include "trace_item.h"
#include "via_item.h"
#include <QSet>
#include <limits>
#include <QHash>
#include <cmath>

namespace {
struct DisjointSet {
    QVector<int> p;
    QVector<int> r;
    explicit DisjointSet(int n = 0) : p(n), r(n, 0) {
        for (int i = 0; i < n; ++i) p[i] = i;
    }
    int find(int x) {
        if (p[x] == x) return x;
        p[x] = find(p[x]);
        return p[x];
    }
    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (r[a] < r[b]) std::swap(a, b);
        p[b] = a;
        if (r[a] == r[b]) r[a]++;
    }
};

QString pointKey(const QPointF& p, double eps = 0.01) {
    const qint64 x = static_cast<qint64>(std::llround(p.x() / eps));
    const qint64 y = static_cast<qint64>(std::llround(p.y() / eps));
    return QString::number(x) + ":" + QString::number(y);
}
}

PCBRatsnestManager& PCBRatsnestManager::instance() {
    static PCBRatsnestManager instance;
    return instance;
}

PCBRatsnestManager::PCBRatsnestManager(QObject* parent)
    : QObject(parent), m_scene(nullptr), m_visible(true) {
}

PCBRatsnestManager::~PCBRatsnestManager() {
    clearRatsnest();
}

void PCBRatsnestManager::setScene(QGraphicsScene* scene) {
    if (m_scene != scene) {
        clearRatsnest();
        m_scene = scene;
        update();
    }
}

void PCBRatsnestManager::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        for (auto it = m_netItems.begin(); it != m_netItems.end(); ++it) {
            for (auto* item : it.value()) {
                item->setVisible(visible);
            }
        }
    }
}

void PCBRatsnestManager::clearRatsnest() {
    for (auto it = m_netItems.begin(); it != m_netItems.end(); ++it) {
        for (auto* item : it.value()) {
            if (m_scene) m_scene->removeItem(item);
            delete item;
        }
    }
    m_netItems.clear();
}

void PCBRatsnestManager::update() {
    if (!m_scene) return;

    // 1. Group all copper items (Pads, Vias, Traces) by net
    QMap<QString, QList<PadItem*>> netPads;
    QMap<QString, QList<TraceItem*>> netTraces;
    QMap<QString, QList<ViaItem*>> netVias;
    
    for (auto* item : m_scene->items()) {
        PCBItem* pcbItem = dynamic_cast<PCBItem*>(item);
        if (!pcbItem) continue;
        
        QString net = pcbItem->netName();
        if (net.isEmpty() || net == "No Net") continue;

        if (PadItem* pad = dynamic_cast<PadItem*>(pcbItem)) netPads[net].append(pad);
        else if (TraceItem* trace = dynamic_cast<TraceItem*>(pcbItem)) netTraces[net].append(trace);
        else if (ViaItem* via = dynamic_cast<ViaItem*>(pcbItem)) netVias[net].append(via);
    }

    clearRatsnest();
    
    // 2. For each net, find unrouted clusters
    QSet<QString> allNets;
    for (const QString& net : netPads.keys()) allNets.insert(net);
    for (const QString& net : netVias.keys()) allNets.insert(net);

    for (const QString& netName : allNets) {
        QList<PCBItem*> targets;
        if (netPads.contains(netName)) {
            for (auto* p : netPads[netName]) targets.append(p);
        }
        if (netVias.contains(netName)) {
            for (auto* v : netVias[netName]) targets.append(v);
        }

        if (targets.size() < 2) continue;
        calculateMST(netName, targets);
    }
}

void PCBRatsnestManager::updateNet(const QString& netName) {
    if (!m_scene || netName.isEmpty() || netName == "No Net") return;

    // Remove old lines for this net
    if (m_netItems.contains(netName)) {
        for (auto* item : m_netItems[netName]) {
            m_scene->removeItem(item);
            delete item;
        }
        m_netItems.remove(netName);
    }

    // Find all pads/vias for this net
    QList<PCBItem*> targets;
    for (auto* item : m_scene->items()) {
        if (PCBItem* pcbItem = dynamic_cast<PCBItem*>(item)) {
            if (pcbItem->netName() == netName && 
               (pcbItem->itemType() == PCBItem::PadType || pcbItem->itemType() == PCBItem::ViaType)) {
                targets.append(pcbItem);
            }
        }
    }

    if (targets.size() > 1) {
        calculateMST(netName, targets);
    }
}

void PCBRatsnestManager::calculateMST(const QString& netName, const QList<PCBItem*>& targets) {
    if (targets.isEmpty()) return;

    // Build endpoint graph from already-routed traces on this net.
    QHash<QString, int> nodeIndex;
    QVector<QString> nodes;
    auto ensureNode = [&](const QString& key) -> int {
        if (nodeIndex.contains(key)) return nodeIndex.value(key);
        const int idx = nodes.size();
        nodeIndex.insert(key, idx);
        nodes.append(key);
        return idx;
    };

    QList<TraceItem*> netTraces;
    if (m_scene) {
        for (QGraphicsItem* item : m_scene->items()) {
            if (TraceItem* trace = dynamic_cast<TraceItem*>(item)) {
                if (trace->netName() == netName) {
                    netTraces.append(trace);
                    const QString ks = pointKey(trace->mapToScene(trace->startPoint()));
                    const QString ke = pointKey(trace->mapToScene(trace->endPoint()));
                    ensureNode(ks);
                    ensureNode(ke);
                }
            }
        }
    }

    // Each target is attached to nearest endpoint-key by its center.
    QVector<QString> targetKeys;
    targetKeys.reserve(targets.size());
    for (PCBItem* t : targets) {
        const QString k = pointKey(t->scenePos());
        ensureNode(k);
        targetKeys.append(k);
    }

    DisjointSet dsu(nodes.size());
    for (TraceItem* trace : netTraces) {
        const int a = nodeIndex.value(pointKey(trace->mapToScene(trace->startPoint())), -1);
        const int b = nodeIndex.value(pointKey(trace->mapToScene(trace->endPoint())), -1);
        if (a >= 0 && b >= 0) dsu.unite(a, b);
    }

    // Cluster pads/vias by routed connectivity (through traces).
    QHash<int, QList<PCBItem*>> clustersByRoot;
    for (int i = 0; i < targets.size(); ++i) {
        const int keyIdx = nodeIndex.value(targetKeys[i], -1);
        const int root = (keyIdx >= 0) ? dsu.find(keyIdx) : i;
        clustersByRoot[root].append(targets[i]);
    }

    QList<QList<PCBItem*>> clusters;
    clusters.reserve(clustersByRoot.size());
    for (auto it = clustersByRoot.begin(); it != clustersByRoot.end(); ++it) {
        clusters.append(it.value());
    }

    // Already fully connected: no airwires needed.
    if (clusters.size() < 2) return;

    // Build MST across disconnected clusters only.
    const int cN = clusters.size();
    QVector<double> minDist(cN, std::numeric_limits<double>::max());
    QVector<int> parent(cN, -1);
    QVector<QPointF> edgeP1(cN), edgeP2(cN);
    QVector<bool> visited(cN, false);
    minDist[0] = 0.0;

    for (int i = 0; i < cN; ++i) {
        int u = -1;
        for (int j = 0; j < cN; ++j) {
            if (!visited[j] && (u == -1 || minDist[j] < minDist[u])) u = j;
        }
        if (u < 0 || minDist[u] == std::numeric_limits<double>::max()) break;
        visited[u] = true;

        if (parent[u] != -1) {
            RatsnestItem* item = new RatsnestItem(edgeP1[u], edgeP2[u]);
            item->setVisible(m_visible);
            m_scene->addItem(item);
            m_netItems[netName].append(item);
        }

        for (int v = 0; v < cN; ++v) {
            if (visited[v] || u == v) continue;

            double best = std::numeric_limits<double>::max();
            QPointF bestA, bestB;
            for (PCBItem* a : clusters[u]) {
                for (PCBItem* b : clusters[v]) {
                    const QPointF pa = a->scenePos();
                    const QPointF pb = b->scenePos();
                    const double d = QLineF(pa, pb).length();
                    if (d < best) {
                        best = d;
                        bestA = pa;
                        bestB = pb;
                    }
                }
            }

            if (best < minDist[v]) {
                minDist[v] = best;
                parent[v] = u;
                edgeP1[v] = bestA;
                edgeP2[v] = bestB;
            }
        }
    }
}
