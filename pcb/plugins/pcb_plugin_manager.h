#ifndef PCBPLUGINMANAGER_H
#define PCBPLUGINMANAGER_H

#include "pcb_plugin.h"
#include <QObject>
#include <QPluginLoader>
#include <QMap>
#include <QString>

class PCBPluginManager : public QObject {
    Q_OBJECT

public:
    static PCBPluginManager& instance();

    // Plugin management
    bool loadPlugin(const QString& pluginPath);
    bool unloadPlugin(const QString& pluginName);
    void unloadAllPlugins();

    // Plugin information
    QStringList loadedPlugins() const;
    PCBPlugin* getPlugin(const QString& pluginName) const;

    // Plugin directories
    void addPluginDirectory(const QString& directory);
    QStringList pluginDirectories() const;

signals:
    void pluginLoaded(const QString& pluginName);
    void pluginUnloaded(const QString& pluginName);
    void pluginError(const QString& pluginName, const QString& error);

private:
    PCBPluginManager(QObject* parent = nullptr);
    ~PCBPluginManager();

    PCBPluginManager(const PCBPluginManager&) = delete;
    PCBPluginManager& operator=(const PCBPluginManager&) = delete;

    QMap<QString, PCBPlugin*> m_plugins;
    QMap<QString, QPluginLoader*> m_loaders;
    QStringList m_pluginDirectories;
};

#endif // PCBPLUGINMANAGER_H
