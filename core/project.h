#ifndef PROJECT_H
#define PROJECT_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QUuid>

class Project : public QObject {
    Q_OBJECT

public:
    enum ProjectType {
        Schematic
    };

    Project(QObject* parent = nullptr);
    Project(const QString& name, const QString& path, QObject* parent = nullptr);
    ~Project();

    // Project properties
    QUuid id() const { return m_id; }
    QString name() const { return m_name; }
    void setName(const QString& name);

    QString path() const { return m_path; }
    void setPath(const QString& path);

    QString description() const { return m_description; }
    void setDescription(const QString& description);

    ProjectType type() const { return m_type; }
    void setType(ProjectType type);

    QString author() const { return m_author; }
    void setAuthor(const QString& author);

    QDateTime created() const { return m_created; }
    QDateTime modified() const { return m_modified; }
    void updateModified();

    // File paths
    QString projectFilePath() const;
    QString schematicFilePath() const;

    // Serialization
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);

    // Project operations
    bool save();
    bool load();
    bool createNew();
    bool isModified() const { return m_modifiedFlag; }
    void setModified(bool modified = true);

signals:
    void projectModified();
    void projectSaved();

private:
    QUuid m_id;
    QString m_name;
    QString m_path;
    QString m_description;
    ProjectType m_type;
    QString m_author;
    QDateTime m_created;
    QDateTime m_modified;
    bool m_modifiedFlag;
};

#endif // PROJECT_H
