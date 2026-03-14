#include "project.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDebug>

Project::Project(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_type(Schematic)
    , m_created(QDateTime::currentDateTime())
    , m_modified(m_created)
    , m_modifiedFlag(false) {
}

Project::Project(const QString& name, const QString& path, QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid())
    , m_name(name)
    , m_path(path)
    , m_type(Schematic)
    , m_created(QDateTime::currentDateTime())
    , m_modified(m_created)
    , m_modifiedFlag(false) {
}

Project::~Project() {
}

void Project::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        setModified(true);
    }
}

void Project::setPath(const QString& path) {
    if (m_path != path) {
        m_path = path;
        setModified(true);
    }
}

void Project::setDescription(const QString& description) {
    if (m_description != description) {
        m_description = description;
        setModified(true);
    }
}

void Project::setType(ProjectType type) {
    if (m_type != type) {
        m_type = type;
        setModified(true);
    }
}

void Project::setAuthor(const QString& author) {
    if (m_author != author) {
        m_author = author;
        setModified(true);
    }
}

void Project::updateModified() {
    m_modified = QDateTime::currentDateTime();
    setModified(true);
}

QString Project::projectFilePath() const {
    if (m_path.isEmpty()) return QString();
    return m_path + "/" + m_name + ".viospice";
}

QString Project::schematicFilePath() const {
    if (m_path.isEmpty()) return QString();
    return m_path + "/" + m_name + ".flxsch";
}

QJsonObject Project::toJson() const {
    QJsonObject json;
    json["id"] = m_id.toString();
    json["name"] = m_name;
    json["path"] = m_path;
    json["description"] = m_description;
    json["type"] = static_cast<int>(m_type);
    json["author"] = m_author;
    json["created"] = m_created.toString(Qt::ISODate);
    json["modified"] = m_modified.toString(Qt::ISODate);
    json["schematicFile"] = m_name + ".flxsch";
    return json;
}

bool Project::fromJson(const QJsonObject& json) {
    m_id = QUuid(json["id"].toString());
    m_name = json["name"].toString();
    m_path = json["path"].toString();
    m_description = json["description"].toString();
    m_type = static_cast<ProjectType>(json["type"].toInt());
    m_author = json["author"].toString();
    m_created = QDateTime::fromString(json["created"].toString(), Qt::ISODate);
    m_modified = QDateTime::fromString(json["modified"].toString(), Qt::ISODate);
    m_modifiedFlag = false;
    return true;
}

bool Project::save() {
    QJsonObject json = toJson();
    QJsonDocument doc(json);

    QFile file(projectFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot open project file for writing:" << projectFilePath();
        return false;
    }

    file.write(doc.toJson());
    file.close();

    m_modifiedFlag = false;
    emit projectSaved();
    qDebug() << "Project saved:" << projectFilePath();

    return true;
}

bool Project::load() {
    QFile file(projectFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open project file for reading:" << projectFilePath();
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qWarning() << "Invalid JSON in project file:" << projectFilePath();
        return false;
    }

    bool success = fromJson(doc.object());
    if (success) {
        qDebug() << "Project loaded:" << projectFilePath();
    }

    return success;
}

bool Project::createNew() {
    // Create project directory if it doesn't exist
    QDir dir(m_path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Cannot create project directory:" << m_path;
            return false;
        }
    }

    // Create initial project file (.viospice)
    updateModified();
    if (!save()) return false;

    // Create empty schematic file if not exists
    QString schFile = schematicFilePath();
    if (!QFile::exists(schFile)) {
        QFile f(schFile);
        if (f.open(QIODevice::WriteOnly)) {
             f.write("{}"); 
             f.close();
        }
    }
    
    return true;
}

void Project::setModified(bool modified) {
    if (m_modifiedFlag != modified) {
        m_modifiedFlag = modified;
        if (modified) {
            emit projectModified();
        }
    }
}
