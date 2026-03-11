#ifndef SCHEMATICCONNECTIVITY_H
#define SCHEMATICCONNECTIVITY_H

#include <QGraphicsScene>
#include <QList>
#include <QString>

class WireItem;
class NetManager;

struct SchematicConnectivityPin {
    QString componentRef;
    QString pinName;
};

struct SchematicConnectivityNet {
    QString name;
    QList<SchematicConnectivityPin> pins;
};

class SchematicConnectivity {
public:
    static void updateVisualConnections(QGraphicsScene* scene);
    static QList<SchematicConnectivityNet> buildConnectivity(QGraphicsScene* scene, NetManager* netManager = nullptr);
    
private:
    static void detectJunctions(WireItem* wire, const QList<WireItem*>& allWires);
    static void detectJumpOvers(WireItem* wire, const QList<WireItem*>& allWires);
};

#endif // SCHEMATICCONNECTIVITY_H
