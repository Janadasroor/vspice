#ifndef RECENTPROJECTS_H
#define RECENTPROJECTS_H

#include <QObject>
#include <QStringList>
#include <QJsonArray>

class RecentProjects : public QObject {
    Q_OBJECT

public:
    static RecentProjects& instance();

    // Project management
    void addProject(const QString& projectPath);
    void removeProject(const QString& projectPath);
    void clear();

    // Access
    QStringList projects() const;
    QString mostRecent() const;

    // Persistence
    void load();
    void save();

    // Settings
    int maxProjects() const { return m_maxProjects; }
    void setMaxProjects(int max);

signals:
    void projectsChanged();

private:
    RecentProjects(QObject* parent = nullptr);
    ~RecentProjects();

    QString settingsFilePath() const;
    void trimToMax();

    QStringList m_projects;
    int m_maxProjects;
};

#endif // RECENTPROJECTS_H
