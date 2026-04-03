#ifndef FOOTPRINT_LIBRARY_H
#define FOOTPRINT_LIBRARY_H

#include <QObject>
#include <QMap>
#include <QStringList>
#include <QReadWriteLock>
#include "models/footprint_definition.h"
#include "models/footprint_primitive.h"

using Flux::Model::FootprintDefinition;
using Flux::Model::FootprintPrimitive;

class FootprintLibrary {
public:
    FootprintLibrary(const QString& name, const QString& path, bool builtIn = false);
    
    QString name() const { return m_name; }
    QString path() const { return m_path; }
    bool isBuiltIn() const { return m_builtIn; }
    
    // Footprint management
    bool hasFootprint(const QString& name) const;
    FootprintDefinition getFootprint(const QString& name) const;
    QStringList getFootprintNames() const;
    
    // IO
    void load(); // Load all from dir
    void addFootprint(const FootprintDefinition& footprint); // Add to memory only
    bool saveFootprint(const FootprintDefinition& footprint); // Save to this lib (dir)

private:
    QString m_name;
    QString m_path;
    bool m_builtIn;
    QMap<QString, FootprintDefinition> m_footprints;
};

class FootprintLibraryManager : public QObject {
    Q_OBJECT

public:
    static FootprintLibraryManager& instance();

    // Init
    void initialize();
    void loadBuiltInLibrary(); // Added

    // Librariy Management
    QList<FootprintLibrary*> libraries() const { return m_libraries; }
    FootprintLibrary* createLibrary(const QString& name);
    void addLibrary(const QString& path);
    FootprintLibrary* findLibrary(const QString& name);

    // Loading
    void loadUserLibraries(const QString& userLibPath);
    void reloadUserLibraries();

    // Global Access (search all)
    FootprintDefinition findFootprint(const QString& name) const;
    bool hasFootprint(const QString& name) const;

    void createDefaultBuiltInLibrary();

Q_SIGNALS:
    void progressUpdated(const QString& status, int progress, int total);
    void loadingFinished();

private:
    explicit FootprintLibraryManager(QObject* parent = nullptr);
    ~FootprintLibraryManager();
    FootprintLibraryManager(const FootprintLibraryManager&) = delete;
    FootprintLibraryManager& operator=(const FootprintLibraryManager&) = delete;

    QList<FootprintLibrary*> m_libraries;
    mutable QReadWriteLock m_lock;
};

#endif // FOOTPRINT_LIBRARY_H
