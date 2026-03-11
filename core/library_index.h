#ifndef LIBRARY_INDEX_H
#define LIBRARY_INDEX_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

struct SearchResult {
    QString name;
    QString library;
    QString category;
    QString type; // "Footprint" or "Symbol"
};

/**
 * @brief SQLite-backed index for lightning-fast search across millions of components
 */
class LibraryIndex : public QObject {
    Q_OBJECT

public:
    static LibraryIndex& instance();

    bool initialize();
    
    // Indexing
    void addFootprint(const QString& name, const QString& library, const QString& category, const QString& tags = QString());
    void addSymbol(const QString& name, const QString& library, const QString& category, const QString& tags = QString());
    void clearIndex();

    // Fast Search
    QList<SearchResult> search(const QString& query, const QString& typeFilter = "");
    QStringList getCategories(const QString& type);

private:
    explicit LibraryIndex(QObject* parent = nullptr);
    ~LibraryIndex();

    QSqlDatabase m_db;
};

#endif // LIBRARY_INDEX_H
