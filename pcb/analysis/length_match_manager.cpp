#include "length_match_manager.h"
#include "length_measurement_engine.h"
#include "serpentine_generator.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

LengthMatchManager::LengthMatchManager(QObject* parent)
    : QObject(parent)
{
}

LengthMatchManager::~LengthMatchManager() = default;

LengthMatchManager& LengthMatchManager::instance() {
    static LengthMatchManager instance;
    return instance;
}

// ============================================================================
// Group Management
// ============================================================================

QString LengthMatchManager::createGroup(const QString& name) {
    LengthMatchGroup group;
    group.id = QString("group_%1").arg(m_nextGroupId++);
    group.name = name.isEmpty() ? QString("Group %1").arg(m_nextGroupId - 1) : name;
    group.tolerance = 2.0;          // Default ±2mm
    group.intraPairTolerance = 0.1; // Default ±0.1mm for diff pairs
    group.autoComputeTarget = true;
    group.enableSerpentine = true;
    group.serpentineAmplitude = 1.5;
    group.serpentineSpacing = 0.3;

    m_groups.append(group);
    emit groupsChanged();
    return group.id;
}

bool LengthMatchManager::deleteGroup(const QString& groupId) {
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].id == groupId) {
            m_groups.removeAt(i);
            emit groupsChanged();
            return true;
        }
    }
    return false;
}

bool LengthMatchManager::addNetToGroup(const QString& groupId, const QString& netName) {
    for (auto& group : m_groups) {
        if (group.id == groupId) {
            group.netNames.insert(netName);
            emit groupsChanged();
            return true;
        }
    }
    return false;
}

bool LengthMatchManager::removeNetFromGroup(const QString& groupId, const QString& netName) {
    for (auto& group : m_groups) {
        if (group.id == groupId) {
            group.netNames.remove(netName);
            emit groupsChanged();
            return true;
        }
    }
    return false;
}

void LengthMatchManager::setGroupTolerance(const QString& groupId, double toleranceMm) {
    for (auto& group : m_groups) {
        if (group.id == groupId) {
            group.tolerance = toleranceMm;
            emit groupsChanged();
            return;
        }
    }
}

void LengthMatchManager::setGroupTarget(const QString& groupId, double targetMm, bool autoCompute) {
    for (auto& group : m_groups) {
        if (group.id == groupId) {
            group.targetLength = targetMm;
            group.autoComputeTarget = autoCompute;
            emit groupsChanged();
            return;
        }
    }
}

void LengthMatchManager::setIntraPairTolerance(const QString& groupId, double toleranceMm) {
    for (auto& group : m_groups) {
        if (group.id == groupId) {
            group.intraPairTolerance = toleranceMm;
            emit groupsChanged();
            return;
        }
    }
}

// ============================================================================
// Access
// ============================================================================

LengthMatchGroup* LengthMatchManager::getGroup(const QString& groupId) {
    for (auto& group : m_groups) {
        if (group.id == groupId) return &group;
    }
    return nullptr;
}

LengthMatchGroup* LengthMatchManager::findGroupForNet(const QString& netName) const {
    for (auto& group : m_groups) {
        if (group.netNames.contains(netName)) return const_cast<LengthMatchGroup*>(&group);
    }
    return nullptr;
}

// ============================================================================
// Validation and Measurement
// ============================================================================

void LengthMatchManager::measureAll(QGraphicsScene* scene) {
    if (!scene) return;

    auto allNetLengths = LengthMeasurementEngine::measureAllNets(scene);

    for (auto& group : m_groups) {
        group.entries.clear();
        group.maxLength = 0.0;
        group.minLength = 999999.0;

        for (const QString& netName : group.netNames) {
            LengthMatchEntry entry;
            entry.netName = netName;

            if (allNetLengths.contains(netName)) {
                const auto& data = allNetLengths[netName];
                entry.length = data.totalLength;
                entry.viaLength = data.viaLength;
                entry.viaCount = data.viaCount;
                entry.delayPs = data.propagationDelayPs;
                entry.isDiffPairP = data.isDiffPairP;
                entry.isDiffPairN = data.isDiffPairN;
                entry.diffPairName = data.diffPairName;
            } else {
                entry.length = 0.0;
            }

            group.maxLength = qMax(group.maxLength, entry.length);
            group.minLength = qMin(group.minLength, entry.length);
            group.entries.append(entry);
        }

        // Auto-compute target (longest net)
        if (group.autoComputeTarget) {
            group.targetLength = group.maxLength;
        }

        // Compute delta and tolerance status
        for (auto& entry : group.entries) {
            entry.deltaFromTarget = entry.length - group.targetLength;
            entry.withinTolerance = qAbs(entry.deltaFromTarget) <= group.tolerance;
        }

        // Check differential pair skew
        group.diffPairInfo.clear();
        auto diffPairs = LengthMeasurementEngine::detectDiffPairs(scene);
        for (auto it = diffPairs.begin(); it != diffPairs.end(); ++it) {
            const QString& pairName = it.key();
            const auto& nets = it.value();

            // Check if either net is in this group
            if (!group.netNames.contains(nets.first) && !group.netNames.contains(nets.second))
                continue;

            DiffPairSkewInfo skewInfo;
            skewInfo.pairName = pairName;
            skewInfo.pNet = nets.first;
            skewInfo.nNet = nets.second;

            if (allNetLengths.contains(nets.first))
                skewInfo.pLength = allNetLengths[nets.first].totalLength;
            if (allNetLengths.contains(nets.second))
                skewInfo.nLength = allNetLengths[nets.second].totalLength;

            skewInfo.intraPairSkew = qAbs(skewInfo.pLength - skewInfo.nLength);
            skewInfo.withinTolerance = skewInfo.intraPairSkew <= group.intraPairTolerance;

            group.diffPairInfo.append(skewInfo);
        }

        // Overall group status
        group.allWithinTolerance = true;
        for (const auto& entry : group.entries) {
            if (!entry.withinTolerance) {
                group.allWithinTolerance = false;
                break;
            }
        }
        for (const auto& skew : group.diffPairInfo) {
            if (!skew.withinTolerance) {
                group.allWithinTolerance = false;
                break;
            }
        }

        // Validation
        group.errors.clear();
        group.hasErrors = false;
        if (group.netNames.isEmpty()) {
            group.errors.append("No nets in group");
            group.hasErrors = true;
        }
        if (group.tolerance <= 0) {
            group.errors.append("Tolerance must be positive");
            group.hasErrors = true;
        }

        emit measurementUpdated(group.id);
    }
}

void LengthMatchManager::validateAll() {
    for (auto& group : m_groups) {
        group.errors.clear();
        group.hasErrors = false;

        if (group.netNames.isEmpty()) {
            group.errors.append("No nets in group");
            group.hasErrors = true;
        }
        if (group.tolerance <= 0) {
            group.errors.append("Tolerance must be positive");
            group.hasErrors = true;
        }
        if (group.serpentineAmplitude <= 0) {
            group.errors.append("Serpentine amplitude must be positive");
            group.hasErrors = true;
        }
        if (group.serpentineSpacing <= 0) {
            group.errors.append("Serpentine spacing must be positive");
            group.hasErrors = true;
        }
    }
}

bool LengthMatchManager::allGroupsPass() const {
    for (const auto& group : m_groups) {
        if (!group.allWithinTolerance) return false;
    }
    return true;
}

// ============================================================================
// Serpentine Auto-Tuning
// ============================================================================

int LengthMatchManager::autoTuneGroup(const QString& groupId, QGraphicsScene* scene) {
    if (!scene) return 0;

    auto* group = getGroup(groupId);
    if (!group) return 0;

    int tunedCount = 0;
    for (auto& entry : group->entries) {
        if (!entry.withinTolerance && entry.deltaFromTarget < 0) {
            // Net is too short - needs serpentine
            double neededLength = qAbs(entry.deltaFromTarget);

            SerpentineGenerator::SerpentineConfig config;
            config.netName = entry.netName;
            config.extraLength = neededLength;
            config.amplitude = group->serpentineAmplitude;
            config.spacing = group->serpentineSpacing;
            config.traceWidth = 0.25; // Default, could come from net class
            config.clearance = group->serpentineSpacing;

            SerpentineGenerator generator(scene);
            bool ok = generator.generateSerpentine(config);
            if (ok) {
                tunedCount++;
                entry.length += config.extraLength; // Update measured length
            }
        }
    }

    if (tunedCount > 0) {
        emit tuningApplied(groupId, tunedCount);
    }

    return tunedCount;
}

int LengthMatchManager::autoTuneAll(QGraphicsScene* scene) {
    int totalTuned = 0;
    for (const auto& group : m_groups) {
        totalTuned += const_cast<LengthMatchManager*>(this)->autoTuneGroup(group.id, scene);
    }
    return totalTuned;
}

// ============================================================================
// JSON Serialization
// ============================================================================

QString LengthMatchManager::toJson() const {
    QJsonArray groupsArray;
    for (const auto& group : m_groups) {
        QJsonObject obj;
        obj["id"] = group.id;
        obj["name"] = group.name;
        obj["targetLength"] = group.targetLength;
        obj["tolerance"] = group.tolerance;
        obj["intraPairTolerance"] = group.intraPairTolerance;
        obj["autoComputeTarget"] = group.autoComputeTarget;
        obj["enableSerpentine"] = group.enableSerpentine;
        obj["serpentineAmplitude"] = group.serpentineAmplitude;
        obj["serpentineSpacing"] = group.serpentineSpacing;

        QJsonArray netsArray;
        for (const auto& net : group.netNames) {
            netsArray.append(net);
        }
        obj["netNames"] = netsArray;

        groupsArray.append(obj);
    }

    QJsonDocument doc(groupsArray);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

bool LengthMatchManager::fromJson(const QString& json) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return false;

    QJsonArray groupsArray = doc.array();
    m_groups.clear();

    for (const QJsonValue& val : groupsArray) {
        QJsonObject obj = val.toObject();
        LengthMatchGroup group;
        group.id = obj["id"].toString();
        group.name = obj["name"].toString();
        group.targetLength = obj["targetLength"].toDouble(0.0);
        group.tolerance = obj["tolerance"].toDouble(2.0);
        group.intraPairTolerance = obj["intraPairTolerance"].toDouble(0.1);
        group.autoComputeTarget = obj["autoComputeTarget"].toBool(true);
        group.enableSerpentine = obj["enableSerpentine"].toBool(true);
        group.serpentineAmplitude = obj["serpentineAmplitude"].toDouble(1.5);
        group.serpentineSpacing = obj["serpentineSpacing"].toDouble(0.3);

        QJsonArray netsArray = obj["netNames"].toArray();
        for (const QJsonValue& netVal : netsArray) {
            group.netNames.insert(netVal.toString());
        }

        m_groups.append(group);
    }

    emit groupsChanged();
    return true;
}

void LengthMatchManager::clear() {
    m_groups.clear();
    m_nextGroupId = 1;
    emit groupsChanged();
}
