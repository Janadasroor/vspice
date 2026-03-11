#ifndef PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_H

#include <QObject>
#include <QList>
#include <QPluginLoader>
#include <QSet>
#include <QString>
#include <QStringList>
#include "pcb_plugin.h"

class PluginManager : public QObject {
    Q_OBJECT

public:
    struct PluginLoadResult {
        enum class Status {
            Loaded,
            Skipped,
            Failed
        };
        enum class LifecycleStatus {
            Installed,
            Enabled,
            Disabled,
            LoadFailed,
            Incompatible,
            UpdateAvailable
        };

        QString filePath;
        QString pluginName;
        QString version;
        QString author;
        QString description;
        Status status = Status::Failed;
        LifecycleStatus lifecycleStatus = LifecycleStatus::Installed;
        QString reason;
    };

    static PluginManager& instance();

    void discoverPlugins();
    void loadEnabledPlugins();
    void loadPlugins();
    void unloadPlugins();
    bool setPluginEnabledByPath(const QString& filePath, bool enabled);
    bool isPluginEnabledByPath(const QString& filePath) const;
    bool uninstallPluginByPath(const QString& filePath);
    bool reloadPluginByPath(const QString& filePath);
    
    QList<FluxPlugin*> activePlugins() const { return m_plugins; }
    QList<PluginLoadResult> lastLoadResults() const { return m_lastLoadResults; }

signals:
    void pluginLoaded(const QString& name);
    void pluginUnloaded(const QString& name);

private:
    PluginManager();
    ~PluginManager();
    QString normalizePluginPath(const QString& filePath) const;
    QStringList pluginSearchDirectories() const;
    int findLoadResultIndexByPath(const QString& normalizedPath) const;
    void loadDisabledPluginPaths();
    void saveDisabledPluginPaths() const;

    QList<FluxPlugin*> m_plugins;
    QList<QPluginLoader*> m_loaders;
    QList<PluginLoadResult> m_lastLoadResults;
    QStringList m_discoveredPluginPaths;
    QSet<QString> m_disabledPluginPaths;
};

#endif // PLUGIN_MANAGER_H
