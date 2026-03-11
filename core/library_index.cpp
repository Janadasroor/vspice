#include "library_index.h"
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

LibraryIndex& LibraryIndex::instance() {
    static LibraryIndex inst;
    return inst;
}

LibraryIndex::LibraryIndex(QObject* parent) : QObject(parent) {
}

LibraryIndex::~LibraryIndex() {
    if (m_db.isOpen()) m_db.close();
}

bool LibraryIndex::initialize() {
    QString dbPath = QDir::homePath() + "/.viora_eda/library_index.db";
    QDir().mkpath(QDir::homePath() + "/.viora_eda");

    if (QSqlDatabase::contains("qt_sql_default_connection")) {
        m_db = QSqlDatabase::database("qt_sql_default_connection");
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE");
    }
    
    m_db.setDatabaseName(dbPath);

    if (!m_db.isOpen() && !m_db.open()) {
        qWarning() << "Failed to open library index database:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q;
    // Create tables if they don't exist
    bool ok = q.exec("CREATE TABLE IF NOT EXISTS components ("
                     "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                     "name TEXT NOT NULL,"
                     "library TEXT,"
                     "category TEXT,"
                     "type TEXT NOT NULL,"
                     "tags TEXT,"
                     "UNIQUE(name, type, library))");
    
    if (ok) {
        // Create an index on name for fast prefix/substring matching
        q.exec("CREATE INDEX IF NOT EXISTS idx_comp_name ON components(name)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_comp_type ON components(type)");
    }

    return ok;
}

void LibraryIndex::addFootprint(const QString& name, const QString& library, const QString& category, const QString& tags) {
    QSqlQuery q;
    q.prepare("INSERT OR REPLACE INTO components (name, library, category, type, tags) VALUES (?, ?, ?, 'Footprint', ?)");
    q.addBindValue(name);
    q.addBindValue(library);
    q.addBindValue(category);
    q.addBindValue(tags);
    q.exec();
}

void LibraryIndex::addSymbol(const QString& name, const QString& library, const QString& category, const QString& tags) {
    QSqlQuery q;
    q.prepare("INSERT OR REPLACE INTO components (name, library, category, type, tags) VALUES (?, ?, ?, 'Symbol', ?)");
    q.addBindValue(name);
    q.addBindValue(library);
    q.addBindValue(category);
    q.addBindValue(tags);
    q.exec();
}

void LibraryIndex::clearIndex() {
    QSqlQuery q;
    q.exec("DELETE FROM components");
}

QList<SearchResult> LibraryIndex::search(const QString& query, const QString& typeFilter) {
    QList<SearchResult> results;
    QSqlQuery q;
    
    QString sql = "SELECT name, library, category, type FROM components WHERE 1=1";
    if (!query.isEmpty()) {
        sql += " AND (name LIKE ? OR category LIKE ? OR tags LIKE ?)";
    }
    if (!typeFilter.isEmpty()) {
        sql += " AND type = ?";
    }
    sql += " LIMIT 500"; // Performance safeguard for UI

    q.prepare(sql);
    if (!query.isEmpty()) {
        QString pattern = "%" + query + "%";
        q.addBindValue(pattern);
        q.addBindValue(pattern);
        q.addBindValue(pattern);
    }
    if (!typeFilter.isEmpty()) {
        q.addBindValue(typeFilter);
    }

    if (q.exec()) {
        while (q.next()) {
            results.append({
                q.value(0).toString(),
                q.value(1).toString(),
                q.value(2).toString(),
                q.value(3).toString()
            });
        }
    }
    return results;
}

QStringList LibraryIndex::getCategories(const QString& type) {
    QStringList cats;
    QSqlQuery q;
    q.prepare("SELECT DISTINCT category FROM components WHERE type = ? ORDER BY category");
    q.addBindValue(type);
    if (q.exec()) {
        while (q.next()) cats << q.value(0).toString();
    }
    return cats;
}
