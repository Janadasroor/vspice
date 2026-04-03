#include "pcb_plugin_manager.h"
#include <QDir>
#include <QDebug>

PCBPluginManager& PCBPluginManager::instance() {
    static PCBPluginManager instance;
    return instance;
}

PCBPluginManager::PCBPluginManager(QObject* parent)
    : QObject(parent) {
    // Add default plugin directories
    addPluginDirectory(QDir::currentPath() + "/plugins");
    addPluginDirectory(QDir::homePath() + "/.viora_eda/plugins");
}

PCBPluginManager::~PCBPluginManager() {
    unloadAllPlugins();
}

bool PCBPluginManager::loadPlugin(const QString& pluginPath) {
    QPluginLoader* loader = new QPluginLoader(pluginPath, this);

    if (!loader->load()) {
        qWarning() << "Failed to load plugin" << pluginPath << ":" << loader->errorString();
        emit pluginError(pluginPath, loader->errorString());
        delete loader;
        return false;
    }

    PCBPlugin* plugin = qobject_cast<PCBPlugin*>(loader->instance());
    if (!plugin) {
        qWarning() << "Plugin" << pluginPath << "does not implement PCBPlugin interface";
        loader->unload();
        delete loader;
        return false;
    }

    // Initialize plugin
    if (!plugin->initialize()) {
        qWarning() << "Failed to initialize plugin" << plugin->name();
        loader->unload();
        delete loader;
        return false;
    }

    // Register plugin items and tools
    plugin->registerItems();
    plugin->registerTools();

    QString pluginName = plugin->name();
    m_plugins[pluginName] = plugin;
    m_loaders[pluginName] = loader;

    qDebug() << "Loaded plugin:" << pluginName << "v" << plugin->version();
    emit pluginLoaded(pluginName);

    return true;
}

bool PCBPluginManager::unloadPlugin(const QString& pluginName) {
    if (!m_plugins.contains(pluginName)) {
        return false;
    }

    PCBPlugin* plugin = m_plugins[pluginName];
    QPluginLoader* loader = m_loaders[pluginName];

    // Shutdown plugin
    if (!plugin->shutdown()) {
        qWarning() << "Plugin" << pluginName << "failed to shutdown cleanly";
    }

    // Unload and cleanup
    loader->unload();
    delete loader;

    m_plugins.remove(pluginName);
    m_loaders.remove(pluginName);

    qDebug() << "Unloaded plugin:" << pluginName;
    emit pluginUnloaded(pluginName);

    return true;
}

void PCBPluginManager::unloadAllPlugins() {
    QStringList pluginNames = m_plugins.keys();
    for (const QString& pluginName : pluginNames) {
        unloadPlugin(pluginName);
    }
}

QStringList PCBPluginManager::loadedPlugins() const {
    return m_plugins.keys();
}

PCBPlugin* PCBPluginManager::getPlugin(const QString& pluginName) const {
    return m_plugins.value(pluginName, nullptr);
}

void PCBPluginManager::addPluginDirectory(const QString& directory) {
    if (!m_pluginDirectories.contains(directory)) {
        m_pluginDirectories.append(directory);
        qDebug() << "Added plugin directory:" << directory;
    }
}

QStringList PCBPluginManager::pluginDirectories() const {
    return m_pluginDirectories;
}
