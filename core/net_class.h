#ifndef NET_CLASS_H
#define NET_CLASS_H

#include <QString>
#include <QMap>
#include <QList>
#include <QJsonObject>

struct NetClass {
    QString name;
    double traceWidth;      // mm
    double clearance;       // mm
    double viaDiameter;     // mm
    double viaDrill;        // mm
    
    // Default constructor for "Default" class
    NetClass() 
        : name("Default"), traceWidth(0.25), clearance(0.2), viaDiameter(0.6), viaDrill(0.3) {}
        
    NetClass(const QString& n, double w, double c, double vd, double vdr)
        : name(n), traceWidth(w), clearance(c), viaDiameter(vd), viaDrill(vdr) {}

    QJsonObject toJson() const;
    static NetClass fromJson(const QJsonObject& json);
};

struct ClearanceRule {
    QString lhs;      // Net name OR class name OR "ANY"
    QString rhs;      // Net name OR class name OR "ANY"
    double clearance; // mm

    QJsonObject toJson() const;
    static ClearanceRule fromJson(const QJsonObject& json);
};

class NetClassManager {
public:
    static NetClassManager& instance();

    void addClass(const NetClass& netClass);
    void removeClass(const QString& name);
    NetClass getClass(const QString& name) const;
    QList<NetClass> classes() const;

    void assignNetToClass(const QString& netName, const QString& className);
    QString getClassName(const QString& netName) const;
    NetClass getClassForNet(const QString& netName) const;
    double getCustomClearanceForNets(const QString& netA, const QString& netB, bool* matched = nullptr) const;
    QList<ClearanceRule> clearanceRules() const;
    void setClearanceRules(const QList<ClearanceRule>& rules);

    // Serialization
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);
    void clear();

private:
    NetClassManager();
    QMap<QString, NetClass> m_classes;
    QMap<QString, QString> m_net_assignments;
    QList<ClearanceRule> m_clearanceRules;
};

#endif // NET_CLASS_H
