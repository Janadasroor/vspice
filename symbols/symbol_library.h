#ifndef SYMBOL_LIBRARY_H
#define SYMBOL_LIBRARY_H

#include "models/symbol_definition.h"
#include "models/symbol_primitive.h"
#include <QString>
#include <QMap>
#include <QStringList>

using Flux::Model::SymbolDefinition;
using Flux::Model::SymbolPrimitive;

/**
 * @brief A library containing multiple symbol definitions
 */
class SymbolLibrary {
public:
    SymbolLibrary();
    SymbolLibrary(const QString& name, bool builtIn = false);
    
    // Properties
    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    
    QString path() const { return m_path; }
    void setPath(const QString& path) { m_path = path; }
    
    bool isBuiltIn() const { return m_builtIn; }
    void setBuiltIn(bool builtIn) { m_builtIn = builtIn; }
    
    // Symbol management
    void addSymbol(const SymbolDefinition& symbol);
    void removeSymbol(const QString& name);
    SymbolDefinition* findSymbol(const QString& name);
    const SymbolDefinition* findSymbol(const QString& name) const;
    QStringList symbolNames() const;
    int symbolCount() const { return m_symbols.size(); }
    QList<SymbolDefinition> allSymbols() const { return m_symbols.values(); }
    
    // Categories
    QStringList categories() const;
    QList<SymbolDefinition*> symbolsInCategory(const QString& category);
    
    // File I/O
    bool load(const QString& filePath);
    bool save(const QString& filePath = QString());
    
    // Serialization
    QJsonObject toJson() const;
    static SymbolLibrary fromJson(const QJsonObject& json);
    
private:
    QString m_name;
    QString m_path;
    bool m_builtIn;
    QMap<QString, SymbolDefinition> m_symbols;
};

/**
 * @brief Singleton manager for all symbol libraries
 */
class SymbolLibraryManager {
public:
    static SymbolLibraryManager& instance();
    
    // Library management
    void addLibrary(SymbolLibrary* library);
    void removeLibrary(const QString& name);
    SymbolLibrary* findLibrary(const QString& name);
    QList<SymbolLibrary*> libraries() const { return m_libraries; }
    
    // Symbol lookup across all libraries
    SymbolDefinition* findSymbol(const QString& name);
    SymbolDefinition* findSymbol(const QString& name, const QString& libraryName);
    QList<SymbolDefinition*> search(const QString& query);
    
    // Loading
    void loadBuiltInLibrary();
    void loadUserLibraries(const QString& userLibPath);
    void reloadUserLibraries();
    
    // Categories across all libraries
    QStringList allCategories() const;
    
private:
    SymbolLibraryManager();
    ~SymbolLibraryManager();
    SymbolLibraryManager(const SymbolLibraryManager&) = delete;
    SymbolLibraryManager& operator=(const SymbolLibraryManager&) = delete;
    
    void createDefaultBuiltInLibrary();
    
    QList<SymbolLibrary*> m_libraries;
};

#endif // SYMBOL_LIBRARY_H
