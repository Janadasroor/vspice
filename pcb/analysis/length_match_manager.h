#ifndef LENGTH_MATCH_MANAGER_H
#define LENGTH_MATCH_MANAGER_H

#include <QObject>
#include <QMap>
#include <QString>
#include "../models/length_match_types.h"

class QGraphicsScene;

/**
 * @brief Singleton manager for length match groups.
 * 
 * Maintains the list of length matching groups, validates them,
 * and triggers serpentine generation when needed.
 */
class LengthMatchManager : public QObject {
    Q_OBJECT

public:
    static LengthMatchManager& instance();

    // Group management
    QString createGroup(const QString& name);
    bool deleteGroup(const QString& groupId);
    bool addNetToGroup(const QString& groupId, const QString& netName);
    bool removeNetFromGroup(const QString& groupId, const QString& netName);
    void setGroupTolerance(const QString& groupId, double toleranceMm);
    void setGroupTarget(const QString& groupId, double targetMm, bool autoCompute = false);
    void setIntraPairTolerance(const QString& groupId, double toleranceMm);

    // Access
    LengthMatchGroup* getGroup(const QString& groupId);
    const QList<LengthMatchGroup>& groups() const { return m_groups; }
    LengthMatchGroup* findGroupForNet(const QString& netName) const;

    // Validation and measurement
    void measureAll(QGraphicsScene* scene);
    void validateAll();
    bool allGroupsPass() const;

    // Serpentine auto-tuning
    int autoTuneGroup(const QString& groupId, QGraphicsScene* scene);
    int autoTuneAll(QGraphicsScene* scene);

    // Export/import (JSON serialization)
    QString toJson() const;
    bool fromJson(const QString& json);

    // Clear all
    void clear();

signals:
    void groupsChanged();
    void measurementUpdated(const QString& groupId);
    void tuningApplied(const QString& groupId, int netCount);

private:
    explicit LengthMatchManager(QObject* parent = nullptr);
    ~LengthMatchManager();
    LengthMatchManager(const LengthMatchManager&) = delete;
    LengthMatchManager& operator=(const LengthMatchManager&) = delete;

    QList<LengthMatchGroup> m_groups;
    int m_nextGroupId = 1;
};

#endif // LENGTH_MATCH_MANAGER_H
