#ifndef PLUGIN_CATALOG_CLIENT_H
#define PLUGIN_CATALOG_CLIENT_H

#include <QJsonArray>
#include <QString>
#include <QStringList>

class PluginCatalogClient {
public:
    bool fetchPlugins(const QString& baseUrl,
                      const QString& query,
                      QJsonArray* outItems,
                      QString* outError) const;
    bool fetchUpdates(const QString& baseUrl,
                      const QString& appVersion,
                      int sdkMajor,
                      const QString& platform,
                      const QStringList& pluginIds,
                      QJsonArray* outItems,
                      QString* outError) const;

private:
    QString normalizeBaseUrl(const QString& baseUrl) const;
};

#endif // PLUGIN_CATALOG_CLIENT_H
