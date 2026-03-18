#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

class ConfigManager : public QObject {
    Q_OBJECT

public:
    static ConfigManager& instance();

    // Auto-Save
    bool autoSaveEnabled() const;
    void setAutoSaveEnabled(bool enabled);
    int autoSaveInterval() const; // minutes
    void setAutoSaveInterval(int minutes);

    // Theme
    QString currentTheme() const;
    void setCurrentTheme(const QString& themeName);

    QString geminiApiKey() const;
    void setGeminiApiKey(const QString& key);

    QString octopartApiKey() const;
    void setOctopartApiKey(const QString& key);

    QStringList symbolPaths() const;
    QStringList rawSymbolPaths() const;
    void setSymbolPaths(const QStringList& paths);

    QStringList modelPaths() const;
    QStringList rawModelPaths() const;
    void setModelPaths(const QStringList& paths);
    QStringList libraryRoots() const;
    void setLibraryRoots(const QStringList& roots);

    // Simulator Settings
    QString defaultSolver() const;
    void setDefaultSolver(const QString& solver);
    QString integrationMethod() const;
    void setIntegrationMethod(const QString& method);
    double reltol() const;
    void setReltol(double val);
    double abstol() const;
    void setAbstol(double val);
    double vntol() const;
    void setVntol(double val);
    double gmin() const;
    void setGmin(double val);
    int maxIterations() const;
    void setMaxIterations(int val);

    // Geometry and State
    void saveWindowState(const QString& name, const QByteArray& geometry, const QByteArray& state);
    QByteArray windowGeometry(const QString& name) const;
    QByteArray windowState(const QString& name) const;

    // Grid
    bool snapToGrid() const;
    void setSnapToGrid(bool enabled);
    bool autoFocusOnCrossProbe() const;
    void setAutoFocusOnCrossProbe(bool enabled);
    bool isRealtimeWireUpdateEnabled() const { return m_realtimeWireUpdate; }
    void setRealtimeWireUpdateEnabled(bool enabled) { m_realtimeWireUpdate = enabled; }

    // Tool Properties
    void setToolProperty(const QString& toolName, const QString& propName, const QVariant& value);
    QVariant toolProperty(const QString& toolName, const QString& propName, const QVariant& defaultValue = QVariant()) const;

    // Feature Flags
    bool isFeatureEnabled(const QString& name, bool defaultVal = false) const;
    void setFeatureEnabled(const QString& name, bool enabled);

    void save();
    void load();

private:
    ConfigManager();
    QSettings m_settings;
    
    bool m_autoSaveEnabled;
    int m_autoSaveInterval;
    QString m_currentTheme;
    QString m_geminiApiKey;
    QStringList m_symbolPaths;
    QStringList m_modelPaths;
    QStringList m_libraryRoots;

    // Simulator
    QString m_defaultSolver;
    QString m_integrationMethod;
    double m_reltol;
    double m_abstol;
    double m_vntol;
    double m_gmin;
    int m_maxIterations;

    bool m_snapToGrid;
    bool m_autoFocusOnCrossProbe;
    bool m_realtimeWireUpdate;
};

#endif // CONFIG_MANAGER_H
