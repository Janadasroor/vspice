#ifndef NETMANAGER_H
#define NETMANAGER_H

#include <QObject>
#include <QSet>
#include <QMap>
#include <QString>
#include <QPointF>
#include <QStringList>

class WireItem;
class SchematicItem;

struct NetConnection {
    SchematicItem* item;
    QPointF connectionPoint;
    QString pinName;

    bool operator==(const NetConnection& other) const {
        return item == other.item && connectionPoint == other.connectionPoint;
    }
};

class NetManager : public QObject {
    Q_OBJECT

public:
    explicit NetManager(QObject* parent = nullptr);
    ~NetManager();

    // Net management
    QString createNet(const QString& name = QString());
    void removeNet(const QString& netName);
    QStringList netNames() const;

    // Connection management
    void addConnection(const QString& netName, SchematicItem* item, const QPointF& point, const QString& pinName = QString());
    void removeConnection(const QString& netName, SchematicItem* item, const QPointF& point);
    QList<NetConnection> getConnections(const QString& netName) const;

    // Wire management
    void addWireToNet(const QString& netName, WireItem* wire);
    void removeWireFromNet(const QString& netName, WireItem* wire);
    QList<WireItem*> getWiresInNet(const QString& netName) const;

    // Auto-connection detection
    void updateConnections();
    void updateNets(class QGraphicsScene* scene);
    QString findNetAtPoint(const QPointF& point) const;

    // Net analysis
    bool arePointsConnected(const QPointF& point1, const QPointF& point2) const;
    QString getCommonNet(const QPointF& point1, const QPointF& point2) const;
    
    // Advanced Net Analysis
    QList<SchematicItem*> traceNet(SchematicItem* startItem) const;
    QMap<SchematicItem*, QSet<int>> traceNetWithPins(SchematicItem* startItem) const;
    void clearAllHighlights(QGraphicsScene* scene) const;
    QList<SchematicItem*> getItemsForNet(const QString& netName) const;

    void setBusAliases(const QMap<QString, QList<QString>>& aliases);
    QMap<QString, QList<QString>> busAliases() const { return m_busAliases; }

signals:
    void netAdded(const QString& netName);
    void netRemoved(const QString& netName);
    void connectionAdded(const QString& netName, SchematicItem* item, const QPointF& point);
    void connectionRemoved(const QString& netName, SchematicItem* item, const QPointF& point);

private:
    QMap<QString, QList<NetConnection>> m_nets;
    QMap<QString, QList<WireItem*>> m_netWires;
    int m_nextNetId;
    QMap<QString, QList<QString>> m_busAliases;

    QString generateNetName();
    QString resolveBusAliasNetName(const QString& rawNetName) const;
    bool pointsAreClose(const QPointF& p1, const QPointF& p2, qreal threshold = 5.0) const;
    bool isPointOnItem(SchematicItem* item, const QPointF& scenePoint, qreal threshold = 3.0) const;
};

#endif // NETMANAGER_H
