#include "net_class.h"
#include <QJsonArray>
#include <algorithm>

QJsonObject NetClass::toJson() const {
    QJsonObject json;
    json["name"] = name;
    json["traceWidth"] = traceWidth;
    json["clearance"] = clearance;
    json["viaDiameter"] = viaDiameter;
    json["viaDrill"] = viaDrill;
    return json;
}

NetClass NetClass::fromJson(const QJsonObject& json) {
    return NetClass(
        json["name"].toString(),
        json["traceWidth"].toDouble(0.25),
        json["clearance"].toDouble(0.2),
        json["viaDiameter"].toDouble(0.6),
        json["viaDrill"].toDouble(0.3)
    );
}

QJsonObject ClearanceRule::toJson() const {
    QJsonObject json;
    json["lhs"] = lhs;
    json["rhs"] = rhs;
    json["clearance"] = clearance;
    return json;
}

ClearanceRule ClearanceRule::fromJson(const QJsonObject& json) {
    ClearanceRule r;
    r.lhs = json["lhs"].toString();
    r.rhs = json["rhs"].toString();
    r.clearance = json["clearance"].toDouble(0.2);
    return r;
}

NetClassManager& NetClassManager::instance() {
    static NetClassManager instance;
    return instance;
}

NetClassManager::NetClassManager() {
    // Ensure Default class always exists
    addClass(NetClass("Default", 0.25, 0.2, 0.6, 0.3));
    
    // Add a Power class example
    addClass(NetClass("Power", 0.5, 0.25, 0.8, 0.4));
}

void NetClassManager::addClass(const NetClass& netClass) {
    m_classes[netClass.name] = netClass;
}

void NetClassManager::removeClass(const QString& name) {
    if (name != "Default") {
        m_classes.remove(name);
        // Reset assigned nets to Default
        QMutableMapIterator<QString, QString> it(m_net_assignments);
        while (it.hasNext()) {
            it.next();
            if (it.value() == name) it.setValue("Default");
        }
    }
}

NetClass NetClassManager::getClass(const QString& name) const {
    return m_classes.value(name, m_classes.value("Default"));
}

QList<NetClass> NetClassManager::classes() const {
    return m_classes.values();
}

void NetClassManager::assignNetToClass(const QString& netName, const QString& className) {
    if (m_classes.contains(className)) {
        m_net_assignments[netName] = className;
    }
}

QString NetClassManager::getClassName(const QString& netName) const {
    return m_net_assignments.value(netName, "Default");
}

NetClass NetClassManager::getClassForNet(const QString& netName) const {
    return getClass(getClassName(netName));
}

double NetClassManager::getCustomClearanceForNets(const QString& netA, const QString& netB, bool* matched) const {
    if (matched) *matched = false;
    if (netA.isEmpty() || netB.isEmpty()) return 0.0;

    const QString classA = getClassName(netA);
    const QString classB = getClassName(netB);

    auto tokenMatches = [&](const QString& token, const QString& net, const QString& cls) {
        return token.compare("ANY", Qt::CaseInsensitive) == 0 || token == net || token == cls;
    };
    auto tokenScore = [&](const QString& token, const QString& net, const QString& cls) {
        if (token.compare("ANY", Qt::CaseInsensitive) == 0) return 1;
        if (token == cls) return 2;
        if (token == net) return 3;
        return 0;
    };

    int bestScore = -1;
    double bestClearance = 0.0;

    for (const ClearanceRule& rule : m_clearanceRules) {
        const bool direct = tokenMatches(rule.lhs, netA, classA) && tokenMatches(rule.rhs, netB, classB);
        const bool swapped = tokenMatches(rule.lhs, netB, classB) && tokenMatches(rule.rhs, netA, classA);
        if (!direct && !swapped) continue;

        int score = 0;
        if (direct) {
            score = tokenScore(rule.lhs, netA, classA) + tokenScore(rule.rhs, netB, classB);
        } else {
            score = tokenScore(rule.lhs, netB, classB) + tokenScore(rule.rhs, netA, classA);
        }
        if (score > bestScore) {
            bestScore = score;
            bestClearance = rule.clearance;
        }
    }

    if (bestScore >= 0) {
        if (matched) *matched = true;
        return bestClearance;
    }
    return 0.0;
}

QList<ClearanceRule> NetClassManager::clearanceRules() const {
    return m_clearanceRules;
}

void NetClassManager::setClearanceRules(const QList<ClearanceRule>& rules) {
    m_clearanceRules.clear();
    for (const ClearanceRule& r : rules) {
        if (r.lhs.trimmed().isEmpty() || r.rhs.trimmed().isEmpty()) continue;
        if (r.clearance <= 0.0) continue;
        m_clearanceRules.append(r);
    }
}

QJsonObject NetClassManager::toJson() const {
    QJsonObject json;
    
    QJsonArray classesArray;
    for (const auto& cls : m_classes) {
        classesArray.append(cls.toJson());
    }
    json["classes"] = classesArray;

    QJsonObject assignmentsObj;
    for (auto it = m_net_assignments.begin(); it != m_net_assignments.end(); ++it) {
        assignmentsObj[it.key()] = it.value();
    }
    json["assignments"] = assignmentsObj;

    QJsonArray clearanceRulesArray;
    for (const ClearanceRule& r : m_clearanceRules) {
        clearanceRulesArray.append(r.toJson());
    }
    json["clearanceRules"] = clearanceRulesArray;

    return json;
}

void NetClassManager::fromJson(const QJsonObject& json) {
    clear(); // Reset to defaults first
    
    QJsonArray classesArray = json["classes"].toArray();
    for (const QJsonValue& val : classesArray) {
        addClass(NetClass::fromJson(val.toObject()));
    }

    QJsonObject assignmentsObj = json["assignments"].toObject();
    for (auto it = assignmentsObj.begin(); it != assignmentsObj.end(); ++it) {
        assignNetToClass(it.key(), it.value().toString());
    }

    m_clearanceRules.clear();
    QJsonArray rulesArray = json["clearanceRules"].toArray();
    for (const QJsonValue& v : rulesArray) {
        m_clearanceRules.append(ClearanceRule::fromJson(v.toObject()));
    }
}

void NetClassManager::clear() {
    m_classes.clear();
    m_net_assignments.clear();
    m_clearanceRules.clear();
    addClass(NetClass("Default", 0.25, 0.2, 0.6, 0.3));
}
