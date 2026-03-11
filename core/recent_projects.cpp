#include "recent_projects.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDebug>

RecentProjects& RecentProjects::instance() {
    static RecentProjects instance;
    return instance;
}

RecentProjects::RecentProjects(QObject* parent)
    : QObject(parent)
    , m_maxProjects(10) {
    load();
}

RecentProjects::~RecentProjects() {
    save();
}

void RecentProjects::addProject(const QString& projectPath) {
    // Remove if already exists
    m_projects.removeAll(projectPath);

    // Add to front
    m_projects.prepend(projectPath);

    // Trim to max
    trimToMax();

    save();
    emit projectsChanged();

    qDebug() << "Added recent project:" << projectPath;
}

void RecentProjects::removeProject(const QString& projectPath) {
    if (m_projects.removeAll(projectPath) > 0) {
        save();
        emit projectsChanged();
        qDebug() << "Removed recent project:" << projectPath;
    }
}

void RecentProjects::clear() {
    m_projects.clear();
    save();
    emit projectsChanged();
    qDebug() << "Cleared all recent projects";
}

QStringList RecentProjects::projects() const {
    return m_projects;
}

QString RecentProjects::mostRecent() const {
    return m_projects.isEmpty() ? QString() : m_projects.first();
}

void RecentProjects::load() {
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "No recent projects file found, starting fresh";
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qWarning() << "Invalid JSON in recent projects file";
        return;
    }

    QJsonArray array = doc.array();
    m_projects.clear();

    for (const QJsonValue& value : array) {
        QString path = value.toString();
        if (!path.isEmpty() && QFile::exists(path)) {
            m_projects.append(path);
        }
    }

    trimToMax();
    qDebug() << "Loaded" << m_projects.size() << "recent projects";
}

void RecentProjects::save() {
    QJsonArray array;
    for (const QString& path : m_projects) {
        array.append(path);
    }

    QJsonDocument doc(array);

    QFile file(settingsFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save recent projects file:" << settingsFilePath();
        return;
    }

    file.write(doc.toJson());
    file.close();

    qDebug() << "Saved" << m_projects.size() << "recent projects";
}

void RecentProjects::setMaxProjects(int max) {
    if (m_maxProjects != max) {
        m_maxProjects = max;
        trimToMax();
        save();
    }
}

QString RecentProjects::settingsFilePath() const {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return configDir + "/recent_projects.json";
}

void RecentProjects::trimToMax() {
    while (m_projects.size() > m_maxProjects) {
        m_projects.removeLast();
    }
}
