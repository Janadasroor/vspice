#ifndef ECO_TYPES_H
#define ECO_TYPES_H

#include <QString>
#include <QList>
#include <QPointF>
#include <QMap>

struct ECOPin {
    QString componentRef;
    QString pinName; // e.g. "1", "2", "GND"
    
    bool operator==(const ECOPin& other) const {
        return componentRef == other.componentRef && pinName == other.pinName;
    }
};

struct ECONet {
    QString name;
    QList<ECOPin> pins;
};

struct ECOComponent {
    QString reference;
    QString footprint;
    QString spiceModel;
    int symbolPinCount = 0;
    QString value;
    QString manufacturer;
    QString mpn;
    int type; // SchematicItem::ItemType
    QString typeName;
    bool excludeFromSim = false;
    bool excludeFromPcb = false;

    // Advanced Simulation Metadata
    QMap<QString, QString> paramExpressions;
    QMap<QString, QString> tolerances; // paramName -> string value (e.g. "0.05" or "5%")
    QMap<QString, QString> pinPadMapping; // pinName -> padName
};

struct ECOPackage {
    QList<ECOComponent> components;
    QList<ECONet> nets;
};

#endif // ECO_TYPES_H
