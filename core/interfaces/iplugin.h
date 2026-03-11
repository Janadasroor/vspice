#pragma once

#include <QString>
#include <QVariantMap>
#include "plugin_sdk_version.h"

class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual QString pluginId() const = 0;
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;
    virtual int sdkVersionMajor() const { return flux::plugin::kSdkVersionMajor; }
    virtual int sdkVersionMinor() const { return flux::plugin::kSdkVersionMinor; }

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    virtual void onProjectOpened(const QString& path) { Q_UNUSED(path) }
    virtual void onCommandExecuted(const QString& command, const QVariantMap& params) {
        Q_UNUSED(command)
        Q_UNUSED(params)
    }
};
