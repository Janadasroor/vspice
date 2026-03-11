#include "net_manager.h"
#include "wire_item.h"
#include "schematic_item.h"
#include "net_class.h"
#include "net_label_item.h"
#include <QDebug>
#include <QGraphicsScene>
#include <cmath>
#include <QRegularExpression>

NetManager::NetManager(QObject* parent)
    : QObject(parent)
    , m_nextNetId(1) {
}

NetManager::~NetManager() {
}

QString NetManager::createNet(const QString& name) {
    QString netName = name.isEmpty() ? generateNetName() : name;

    if (!m_nets.contains(netName)) {
        m_nets[netName] = QList<NetConnection>();
        m_netWires[netName] = QList<WireItem*>();
        emit netAdded(netName);
        qDebug() << "Created net:" << netName;
    }

    return netName;
}

void NetManager::removeNet(const QString& netName) {
    if (m_nets.contains(netName)) {
        m_nets.remove(netName);
        m_netWires.remove(netName);
        emit netRemoved(netName);
        qDebug() << "Removed net:" << netName;
    }
}

QStringList NetManager::netNames() const {
    return m_nets.keys();
}

void NetManager::setBusAliases(const QMap<QString, QList<QString>>& aliases) {
    m_busAliases = aliases;
}

void NetManager::addConnection(const QString& netName, SchematicItem* item, const QPointF& point, const QString& pinName) {
    if (!m_nets.contains(netName)) {
        createNet(netName);
    }

    NetConnection connection = {item, point, pinName};
    if (!m_nets[netName].contains(connection)) {
        m_nets[netName].append(connection);
        emit connectionAdded(netName, item, point);
        qDebug() << "Added connection to net" << netName << "at" << point;
    }
}

void NetManager::removeConnection(const QString& netName, SchematicItem* item, const QPointF& point) {
    if (!m_nets.contains(netName)) return;

    NetConnection connection = {item, point, QString()};
    m_nets[netName].removeAll(connection);
    emit connectionRemoved(netName, item, point);

    // Remove net if it becomes empty
    if (m_nets[netName].isEmpty() && m_netWires[netName].isEmpty()) {
        removeNet(netName);
    }
}

QList<NetConnection> NetManager::getConnections(const QString& netName) const {
    return m_nets.value(netName, QList<NetConnection>());
}

void NetManager::addWireToNet(const QString& netName, WireItem* wire) {
    if (!m_netWires.contains(netName)) {
        createNet(netName);
    }

    if (!m_netWires[netName].contains(wire)) {
        m_netWires[netName].append(wire);
        qDebug() << "Added wire to net" << netName;
    }
}

void NetManager::removeWireFromNet(const QString& netName, WireItem* wire) {
    if (m_netWires.contains(netName)) {
        m_netWires[netName].removeAll(wire);

        // Remove net if it becomes empty
        if (m_nets[netName].isEmpty() && m_netWires[netName].isEmpty()) {
            removeNet(netName);
        }
    }
}

QList<WireItem*> NetManager::getWiresInNet(const QString& netName) const {
    return m_netWires.value(netName, QList<WireItem*>());
}

void NetManager::updateConnections() {
    // This is called to rebuild the entire net list from physical layout
    // Usually triggered after layout changes or on periodic refresh
}

void NetManager::updateNets(QGraphicsScene* scene) {
    if (!scene) return;

    m_nets.clear();
    m_netWires.clear();
    m_nextNetId = 1;

    struct DSU {
        QMap<int, int> parent;
        int find(int i) {
            if (!parent.contains(i)) parent[i] = i;
            if (parent[i] == i) return i;
            return parent[i] = find(parent[i]);
        }
        void unite(int i, int j) {
            int root_i = find(i);
            int root_j = find(j);
            if (root_i != root_j) parent[root_i] = root_j;
        }
    } dsu;

    struct Node {
        QPointF pos;
        int id;
    };
    QList<Node> nodes;
    auto getNodeId = [&](const QPointF& p) -> int {
        for (const auto& node : nodes) {
            if (pointsAreClose(p, node.pos, 5.0)) return node.id;
        }
        int id = nodes.size();
        nodes.append({p, id});
        return id;
    };

    QList<QGraphicsItem*> allItems = scene->items();
    QList<SchematicItem*> schItems;
    for (QGraphicsItem* gi : allItems) {
        if (auto* si = dynamic_cast<SchematicItem*>(gi)) {
            schItems.append(si);
            // 0. Pre-register ALL connection points in the nodes pool
            // This is critical for T-junction detection!
            for (const QPointF& p : si->connectionPoints()) {
                getNodeId(si->mapToScene(p));
            }
        }
    }

    auto isConductiveSegment = [](SchematicItem* item) -> bool {
        if (!item) return false;
        return item->itemType() == SchematicItem::WireType ||
               item->itemType() == SchematicItem::BusType ||
               item->itemTypeName() == "BusEntry";
    };

    // 1. Group by physical connection along wires/buses/bus-entries
    for (SchematicItem* item : schItems) {
        if (isConductiveSegment(item)) {
            QList<QPointF> pts = item->connectionPoints();
            if (pts.isEmpty()) continue;
            int lastId = getNodeId(item->mapToScene(pts[0]));
            for (int i = 1; i < pts.size(); ++i) {
                int id = getNodeId(item->mapToScene(pts[i]));
                dsu.unite(lastId, id);
                lastId = id;
            }
        }
    }

    // 2. Handle T-junctions: any node (pin, end, or junction) on any wire segment unites them
    for (SchematicItem* item : schItems) {
        if (isConductiveSegment(item)) {
            QList<QPointF> pts = item->connectionPoints();
            for (int i = 0; i < pts.size() - 1; ++i) {
                QPointF p1 = item->mapToScene(pts[i]);
                QPointF p2 = item->mapToScene(pts[i+1]);
                for (const auto& node : nodes) {
                    // Check if node lies on the wire segment p1-p2
                    if (isPointOnItem(item, node.pos, 5.0)) {
                        dsu.unite(node.id, getNodeId(p1));
                    }
                }
            }
        }
    }

    // 4. Materialize Nets
    QMap<int, QString> rootToName;
    
    // Priority structure for finding the best name of a connected net
    struct NamedNode {
        QString name;
        int priority; // 5: Power, 4: Global, 3: Hierarchical, 2: Local, 1: Label
        
        bool operator<(const NamedNode& other) const {
            if (priority != other.priority) return priority < other.priority;
            return name > other.name; // Alphabetical tie-breaker (lesser char value is "higher" thus >)
        }
    };
    QMap<int, NamedNode> rootToBestName;

    QMap<QString, QString> logicalNetClasses;

    auto getPriority = [](SchematicItem* item) -> int {
        if (item->itemTypeName() == "Power") return 5;
        if (item->itemType() == SchematicItem::HierarchicalPortType) return 3;
        if (item->itemType() == SchematicItem::LabelType) return 1;
        if (item->itemType() == SchematicItem::NetLabelType) {
            auto* netLabel = dynamic_cast<NetLabelItem*>(item);
            if (netLabel && netLabel->labelScope() == NetLabelItem::Global) {
                return 4;
            }
            return 2;
        }
        return 0;
    };

    // Prioritize names from logical groups and physical connections
    for (SchematicItem* item : schItems) {
        int prio = getPriority(item);
        if (prio > 0 && !item->connectionPoints().isEmpty()) {
            QString name = (item->itemTypeName() == "Power") ? item->name() : item->value().trimmed();
            name = resolveBusAliasNetName(name);

            if (!name.isEmpty()) {
                int root = dsu.find(getNodeId(item->mapToScene(item->connectionPoints().first())));
                NamedNode candidate{name, prio};
                if (!rootToBestName.contains(root) || rootToBestName[root] < candidate) {
                    rootToBestName[root] = candidate;
                }
                if (item->itemType() == SchematicItem::NetLabelType) {
                    if (auto* netLabel = dynamic_cast<NetLabelItem*>(item)) {
                        const QString cls = netLabel->netClassName().trimmed();
                        if (!cls.isEmpty()) logicalNetClasses[name] = cls;
                    }
                }
            }
        }
    }

    // Populate m_nets and m_netWires
    for (SchematicItem* item : schItems) {
        if (item->itemType() == SchematicItem::WireType) {
            item->clearPinNets();
            QList<QPointF> pts = item->connectionPoints();
            if (!pts.isEmpty()) {
                int root = dsu.find(getNodeId(item->mapToScene(pts[0])));
                if (!rootToName.contains(root)) {
                    rootToName[root] = rootToBestName.contains(root) ? rootToBestName[root].name : generateNetName();
                }
                const QString netName = rootToName[root];
                m_netWires[netName].append(static_cast<WireItem*>(item));
                item->setPinNet(0, netName);
            }
        } else {
            // Component pins, Power ports, Labels, Junctions
            item->clearPinNets();
            QList<QPointF> pts = item->connectionPoints();
            for (int i = 0; i < pts.size(); ++i) {
                QPointF scenePt = item->mapToScene(pts[i]);
                int root = dsu.find(getNodeId(scenePt));
                if (!rootToName.contains(root)) {
                    rootToName[root] = rootToBestName.contains(root) ? rootToBestName[root].name : generateNetName();
                }
                const QString netName = rootToName[root];
                addConnection(netName, item, scenePt, item->pinName(i));
                item->setPinNet(i, netName);
            }
        }
    }

    // 5. Apply schematic net-class hints (from Net Label properties) to named nets.
    for (auto it = logicalNetClasses.begin(); it != logicalNetClasses.end(); ++it) {
        if (it.key().isEmpty() || it.value().isEmpty()) continue;
        NetClassManager::instance().assignNetToClass(it.key(), it.value());
    }

    qDebug() << "Rebuilt nets via DSU. Count:" << m_nets.size();
}

QString NetManager::findNetAtPoint(const QPointF& point) const {
    auto pointToSegmentDistance = [](const QPointF& p, const QPointF& a, const QPointF& b) -> qreal {
        const qreal vx = b.x() - a.x();
        const qreal vy = b.y() - a.y();
        const qreal wx = p.x() - a.x();
        const qreal wy = p.y() - a.y();
        const qreal vv = vx * vx + vy * vy;
        if (vv <= 1e-9) {
            const qreal dx = p.x() - a.x();
            const qreal dy = p.y() - a.y();
            return std::sqrt(dx * dx + dy * dy);
        }
        qreal t = (wx * vx + wy * vy) / vv;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
        const qreal px = a.x() + t * vx;
        const qreal py = a.y() + t * vy;
        const qreal dx = p.x() - px;
        const qreal dy = p.y() - py;
        return std::sqrt(dx * dx + dy * dy);
    };

    // 1) Fast exact/near pin and junction test.
    for (auto it = m_nets.constBegin(); it != m_nets.constEnd(); ++it) {
        for (const NetConnection& connection : it.value()) {
            if (pointsAreClose(point, connection.connectionPoint)) {
                return it.key();
            }
        }
    }

    // 2) Fallback: allow probing on wire segments (not only endpoints/junctions).
    const qreal wireHitThreshold = 8.0;
    for (auto it = m_netWires.constBegin(); it != m_netWires.constEnd(); ++it) {
        for (WireItem* wire : it.value()) {
            if (!wire) continue;
            const QList<QPointF> pts = wire->points();
            for (int i = 0; i + 1 < pts.size(); ++i) {
                const QPointF a = wire->mapToScene(pts[i]);
                const QPointF b = wire->mapToScene(pts[i + 1]);
                if (pointToSegmentDistance(point, a, b) <= wireHitThreshold) {
                    return it.key();
                }
            }
        }
    }
    return QString();
}

bool NetManager::arePointsConnected(const QPointF& point1, const QPointF& point2) const {
    QString net1 = findNetAtPoint(point1);
    QString net2 = findNetAtPoint(point2);

    return !net1.isEmpty() && !net2.isEmpty() && net1 == net2;
}

QString NetManager::getCommonNet(const QPointF& point1, const QPointF& point2) const {
    QString net1 = findNetAtPoint(point1);
    QString net2 = findNetAtPoint(point2);

    return (net1 == net2 && !net1.isEmpty()) ? net1 : QString();
}

QList<SchematicItem*> NetManager::traceNet(SchematicItem* startItem) const {
    QMap<SchematicItem*, QSet<int>> map = traceNetWithPins(startItem);
    return map.keys();
}

QMap<SchematicItem*, QSet<int>> NetManager::traceNetWithPins(SchematicItem* startItem) const {
    if (!startItem) return {};

    QMap<SchematicItem*, QSet<int>> result;
    QStringList targetNets;

    // 1. Identify which net(s) the startItem belongs to
    // For Wires, it's usually just one. For components, it could be one per pin.
    for (auto it = m_nets.constBegin(); it != m_nets.constEnd(); ++it) {
        for (const auto& conn : it.value()) {
            if (conn.item == startItem) {
                targetNets.append(it.key());
                break;
            }
        }
    }
    for (auto it = m_netWires.constBegin(); it != m_netWires.constEnd(); ++it) {
        if (it.value().contains(static_cast<WireItem*>(startItem))) {
            targetNets.append(it.key());
        }
    }

    // 2. Collate all members of those nets
    for (const QString& netName : targetNets) {
        for (const auto& conn : m_nets.value(netName)) {
            // My updateNets stores pin index as 1-based string
            int pinIdx = conn.pinName.toInt() - 1;
            result[conn.item].insert(qMax(0, pinIdx));
        }
        for (WireItem* wire : m_netWires.value(netName)) {
            result[wire].insert(0);
        }
    }

    return result;
}

bool NetManager::isPointOnItem(SchematicItem* item, const QPointF& scenePoint, qreal threshold) const {
    if (!item) return false;

    // 1. Check direct connection points (vertices) first
    QList<QPointF> points = item->connectionPoints();
    for (const QPointF& p : points) {
        if (pointsAreClose(scenePoint, item->mapToScene(p), threshold)) return true;
    }

    // 2. For conductive line-like items, check segments for T-junctions.
    if (item->itemType() == SchematicItem::WireType ||
        item->itemType() == SchematicItem::BusType ||
        item->itemTypeName() == "BusEntry") {
        // We need the actual path points. Wires/Buses use connectionPoints for segments too.
        for (int i = 0; i < points.size() - 1; ++i) {
            QPointF p1 = item->mapToScene(points[i]);
            QPointF p2 = item->mapToScene(points[i+1]);
            
            QLineF segment(p1, p2);
            QPointF vec = p2 - p1;
            qreal lenSq = vec.x()*vec.x() + vec.y()*vec.y();
            
            if (lenSq > 0) {
                qreal u = ((scenePoint.x() - p1.x()) * vec.x() + (scenePoint.y() - p1.y()) * vec.y()) / lenSq;
                if (u >= -0.001 && u <= 1.001) {
                    QPointF proj = p1 + u * vec;
                    if (QLineF(scenePoint, proj).length() < threshold) return true;
                }
            }
        }
    }

    return false;
}

void NetManager::clearAllHighlights(QGraphicsScene* scene) const {
    if (!scene) return;
    for (QGraphicsItem* item : scene->items()) {
        if (SchematicItem* sItem = dynamic_cast<SchematicItem*>(item)) {
            sItem->setHighlighted(false);
        }
    }
}

QList<SchematicItem*> NetManager::getItemsForNet(const QString& netName) const {
    // This is a bridge between the name-based system and the physical crawler
    // For now, return a list of wires associated with this net
    QList<SchematicItem*> items;
    for (WireItem* wire : m_netWires.value(netName)) {
        items.append(wire);
    }
    // And items with connections
    for (const auto& conn : m_nets.value(netName)) {
        if (!items.contains(conn.item)) {
            items.append(conn.item);
        }
    }
    return items;
}

QString NetManager::generateNetName() {
    return QString("Net%1").arg(m_nextNetId++);
}

QString NetManager::resolveBusAliasNetName(const QString& rawNetName) const {
    const QString netName = rawNetName.trimmed();
    if (netName.isEmpty()) return netName;

    // ALIAS[index] -> member by index
    static const QRegularExpression indexedRe("^([A-Za-z_][A-Za-z0-9_]*)\\[(\\d+)\\]$");
    QRegularExpressionMatch m = indexedRe.match(netName);
    if (m.hasMatch()) {
        const QString alias = m.captured(1);
        const int idx = m.captured(2).toInt();
        const QStringList members = m_busAliases.value(alias);
        if (idx >= 0 && idx < members.size()) {
            const QString member = members[idx].trimmed();
            if (!member.isEmpty()) return member;
        }
    }

    // ALIAS.MEMBER -> MEMBER if MEMBER exists in alias list
    static const QRegularExpression memberRe("^([A-Za-z_][A-Za-z0-9_]*)\\.([A-Za-z_][A-Za-z0-9_]*)$");
    m = memberRe.match(netName);
    if (m.hasMatch()) {
        const QString alias = m.captured(1);
        const QString member = m.captured(2);
        const QStringList members = m_busAliases.value(alias);
        for (const QString& candidate : members) {
            if (candidate.trimmed().compare(member, Qt::CaseInsensitive) == 0) {
                return candidate.trimmed();
            }
        }
    }

    return netName;
}

bool NetManager::pointsAreClose(const QPointF& p1, const QPointF& p2, qreal threshold) const {
    qreal dx = p1.x() - p2.x();
    qreal dy = p1.y() - p2.y();
    return (dx*dx + dy*dy) < (threshold*threshold);
}
