#include "plugin_manager.h"
#include "../interfaces/plugin_sdk_version.h"
#include "../config_manager.h"
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QStandardPaths>

PluginManager& PluginManager::instance() {
    static PluginManager instance;
    return instance;
}

PluginManager::PluginManager() {
    loadDisabledPluginPaths();
}

PluginManager::~PluginManager() {
    unloadPlugins();
}

void PluginManager::discoverPlugins() {
    // Start from a clean runtime state before rediscovery.
    unloadPlugins();
    m_lastLoadResults.clear();
    m_discoveredPluginPaths.clear();

    QSet<QString> scannedPaths;
    int discoveredPluginFiles = 0;

    const QStringList searchDirs = pluginSearchDirectories();
    for (const QString& dirPath : searchDirs) {
        QDir pluginsDir(dirPath);
        if (!pluginsDir.exists()) {
            continue;
        }

        for (QString fileName : pluginsDir.entryList(QDir::Files)) {
            const QString pluginPath = normalizePluginPath(pluginsDir.absoluteFilePath(fileName));
            if (pluginPath.isEmpty()) {
                continue;
            }
            if (scannedPaths.contains(pluginPath)) {
                continue;
            }
            scannedPaths.insert(pluginPath);
            m_discoveredPluginPaths.append(pluginPath);
            discoveredPluginFiles++;

            PluginLoadResult result;
            result.filePath = pluginPath;
            result.pluginName = QFileInfo(fileName).completeBaseName();
            result.status = PluginLoadResult::Status::Skipped;
            result.lifecycleStatus = PluginLoadResult::LifecycleStatus::Installed;
            result.reason = "Discovered";

            if (m_disabledPluginPaths.contains(pluginPath)) {
                result.lifecycleStatus = PluginLoadResult::LifecycleStatus::Disabled;
                result.reason = "Plugin disabled";
            }
            m_lastLoadResults.append(result);
        }
    }

    if (discoveredPluginFiles == 0) {
        PluginLoadResult result;
        result.filePath = searchDirs.join(", ");
        result.pluginName = "Plugin directories";
        result.status = PluginLoadResult::Status::Skipped;
        result.lifecycleStatus = PluginLoadResult::LifecycleStatus::Installed;
        result.reason = "No plugin files found in search directories";
        m_lastLoadResults.append(result);
    }
}

void PluginManager::loadEnabledPlugins() {
    if (m_discoveredPluginPaths.isEmpty()) {
        discoverPlugins();
    }

    for (const QString& pluginPath : m_discoveredPluginPaths) {
        if (m_disabledPluginPaths.contains(pluginPath)) {
            continue;
        }

        QPluginLoader* loader = new QPluginLoader(pluginPath);
        QObject* pluginObj = loader->instance();
        const int resultIndex = findLoadResultIndexByPath(pluginPath);
        if (resultIndex < 0 || resultIndex >= m_lastLoadResults.size()) {
            delete loader;
            continue;
        }
        PluginLoadResult& result = m_lastLoadResults[resultIndex];

        if (pluginObj) {
            FluxPlugin* fluxPlugin = qobject_cast<FluxPlugin*>(pluginObj);
            if (!fluxPlugin) {
                result.status = PluginLoadResult::Status::Failed;
                result.lifecycleStatus = PluginLoadResult::LifecycleStatus::LoadFailed;
                result.reason = "Not a FluxPlugin implementation";
                loader->unload();
                delete loader;
                continue;
            }

            result.pluginName = fluxPlugin->name();
            result.version = fluxPlugin->version();
            result.author = fluxPlugin->author();
            result.description = fluxPlugin->description();

            if (fluxPlugin->sdkVersionMajor() != flux::plugin::kSdkVersionMajor) {
                result.status = PluginLoadResult::Status::Skipped;
                result.lifecycleStatus = PluginLoadResult::LifecycleStatus::Incompatible;
                result.reason = QString("Incompatible SDK major version (plugin: %1, host: %2)")
                                    .arg(fluxPlugin->sdkVersionMajor())
                                    .arg(flux::plugin::kSdkVersionMajor);
                qWarning() << "Skipping plugin due to incompatible SDK major version:"
                           << fluxPlugin->name()
                           << "(plugin:" << fluxPlugin->sdkVersionMajor()
                           << "host:" << flux::plugin::kSdkVersionMajor << ")";
                loader->unload();
                delete loader;
                continue;
            }

            if (!fluxPlugin->initialize()) {
                result.status = PluginLoadResult::Status::Failed;
                result.lifecycleStatus = PluginLoadResult::LifecycleStatus::LoadFailed;
                result.reason = "Plugin initialize() returned false";
                loader->unload();
                delete loader;
                continue;
            }

            m_plugins.append(fluxPlugin);
            m_loaders.append(loader);
            emit pluginLoaded(fluxPlugin->name());
            qDebug() << "Loaded plugin:" << fluxPlugin->name();
            result.status = PluginLoadResult::Status::Loaded;
            result.lifecycleStatus = PluginLoadResult::LifecycleStatus::Enabled;
            result.reason = "Loaded successfully";
        } else {
            result.status = PluginLoadResult::Status::Failed;
            result.lifecycleStatus = PluginLoadResult::LifecycleStatus::LoadFailed;
            result.reason = loader->errorString();
            delete loader;
        }
    }
}

void PluginManager::loadPlugins() {
    discoverPlugins();
    loadEnabledPlugins();
}

bool PluginManager::setPluginEnabledByPath(const QString& filePath, bool enabled) {
    const QString normalizedPath = normalizePluginPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    if (enabled) {
        m_disabledPluginPaths.remove(normalizedPath);
    } else {
        m_disabledPluginPaths.insert(normalizedPath);
    }
    saveDisabledPluginPaths();
    return true;
}

bool PluginManager::isPluginEnabledByPath(const QString& filePath) const {
    const QString normalizedPath = normalizePluginPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }
    return !m_disabledPluginPaths.contains(normalizedPath);
}

bool PluginManager::uninstallPluginByPath(const QString& filePath) {
    const QString normalizedPath = normalizePluginPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    unloadPlugins();
    m_discoveredPluginPaths.removeAll(normalizedPath);
    m_disabledPluginPaths.remove(normalizedPath);
    saveDisabledPluginPaths();

    QFile file(normalizedPath);
    if (!file.exists()) {
        return false;
    }
    return file.remove();
}

bool PluginManager::reloadPluginByPath(const QString& filePath) {
    const QString normalizedPath = normalizePluginPath(filePath);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    loadPlugins();
    const int resultIndex = findLoadResultIndexByPath(normalizedPath);
    if (resultIndex < 0 || resultIndex >= m_lastLoadResults.size()) {
        return false;
    }
    return m_lastLoadResults[resultIndex].lifecycleStatus ==
           PluginLoadResult::LifecycleStatus::Enabled;
}

QString PluginManager::normalizePluginPath(const QString& filePath) const {
    const QString cleaned = QDir::cleanPath(filePath.trimmed());
    if (cleaned.isEmpty()) {
        return QString();
    }
    QFileInfo info(cleaned);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return canonical;
    }
    return info.absoluteFilePath();
}

void PluginManager::loadDisabledPluginPaths() {
    const QVariant stored = ConfigManager::instance().toolProperty(
        "plugin_manager",
        "disabled_paths",
        QStringList());
    const QStringList paths = stored.toStringList();
    m_disabledPluginPaths.clear();
    for (const QString& path : paths) {
        const QString normalized = normalizePluginPath(path);
        if (!normalized.isEmpty()) {
            m_disabledPluginPaths.insert(normalized);
        }
    }
}

void PluginManager::saveDisabledPluginPaths() const {
    QStringList paths = m_disabledPluginPaths.values();
    paths.sort();
    ConfigManager::instance().setToolProperty("plugin_manager", "disabled_paths", paths);
}

QStringList PluginManager::pluginSearchDirectories() const {
    QStringList dirs;

    QDir appDir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    if (appDir.dirName().toLower() == "debug" || appDir.dirName().toLower() == "release")
        appDir.cdUp();
#endif
    dirs.append(QDir(appDir.absoluteFilePath("plugins")).absolutePath());

    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataRoot.isEmpty()) {
        dirs.append(QDir(appDataRoot).absoluteFilePath("plugins"));
    }

    dirs.append(QDir::home().absoluteFilePath(".vioraeda/plugins"));
    dirs.removeDuplicates();
    return dirs;
}

int PluginManager::findLoadResultIndexByPath(const QString& normalizedPath) const {
    for (int i = 0; i < m_lastLoadResults.size(); ++i) {
        if (m_lastLoadResults[i].filePath == normalizedPath) {
            return i;
        }
    }
    return -1;
}

void PluginManager::unloadPlugins() {
    for (FluxPlugin* plugin : m_plugins) {
        plugin->shutdown();
        emit pluginUnloaded(plugin->name());
    }
    m_plugins.clear();

    for (QPluginLoader* loader : m_loaders) {
        loader->unload();
        delete loader;
    }
    m_loaders.clear();
}
