#ifndef NETLIST_GENERATOR_H
#define NETLIST_GENERATOR_H

#include <QString>
#include <QList>
#include <QMap>
#include <QPointF>
#include "eco_types.h"

class QGraphicsScene;

struct NetlistPin {
    QString componentRef;
    QString pinName;
    
    bool operator==(const NetlistPin& other) const {
        return componentRef == other.componentRef && pinName == other.pinName;
    }
};

struct NetlistNet {
    QString name;
    QList<NetlistPin> pins;
};

class NetlistGenerator {
public:
    enum Format {
        FluxJSON,
        IPC356,
        Protel
    };

    static QList<NetlistNet> buildConnectivity(QGraphicsScene* scene, const QString& projectDir, class NetManager* netManager = nullptr);
    static QString generate(QGraphicsScene* scene, const QString& projectDir, Format format = FluxJSON, class NetManager* netManager = nullptr);
    static ECOPackage generateECOPackage(QGraphicsScene* scene, const QString& projectDir, class NetManager* netManager = nullptr);
    
private:
};

#endif // NETLIST_GENERATOR_H
