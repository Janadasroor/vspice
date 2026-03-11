#ifndef PCB_PLUGIN_H
#define PCB_PLUGIN_H

#include <QString>
#include <QVariantMap>
#include <QtPlugin>
#include "../interfaces/plugin_sdk_version.h"

/**
 * @brief Base class for all Viora EDA plugins.
 */
class FluxPlugin {
public:
    virtual ~FluxPlugin() = default;

    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;
    virtual QString author() const = 0;
    virtual int sdkVersionMajor() const { return flux::plugin::kSdkVersionMajor; }
    virtual int sdkVersionMinor() const { return flux::plugin::kSdkVersionMinor; }

    // Lifecycle
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // Interaction
    virtual void onProjectOpened(const QString& path) { Q_UNUSED(path) }
    virtual void onToolExecuted(const QString& toolName, const QVariantMap& params) { Q_UNUSED(toolName) Q_UNUSED(params) }
};

#define FluxPlugin_IID "com.viora_eda.Plugin.1.0"
Q_DECLARE_INTERFACE(FluxPlugin, FluxPlugin_IID)

#endif // PCB_PLUGIN_H
