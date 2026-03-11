#include "plugin_catalog_client.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

QString PluginCatalogClient::normalizeBaseUrl(const QString& baseUrl) const {
    QString normalized = baseUrl.trimmed();
    if (normalized.endsWith('/')) {
        normalized.chop(1);
    }
    return normalized;
}

bool PluginCatalogClient::fetchPlugins(const QString& baseUrl,
                                       const QString& query,
                                       QJsonArray* outItems,
                                       QString* outError) const {
    if (!outItems || !outError) {
        return false;
    }

    *outItems = QJsonArray();
    outError->clear();

    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty()) {
        *outError = "Backend URL is empty";
        return false;
    }

    QUrl url(normalized + "/api/plugins");
    if (!url.isValid()) {
        *outError = "Backend URL is invalid";
        return false;
    }

    QUrlQuery urlQuery;
    if (!query.trimmed().isEmpty()) {
        urlQuery.addQueryItem("q", query.trimmed());
    }
    url.setQuery(urlQuery);

    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = network.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(10000);
    loop.exec();

    if (timeout.isActive() == false && reply->isFinished() == false) {
        reply->abort();
        *outError = "Catalog request timed out";
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        *outError = reply->errorString();
        reply->deleteLater();
        return false;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        *outError = "Invalid catalog response (expected JSON object)";
        reply->deleteLater();
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonValue itemsValue = root.value("items");
    if (!itemsValue.isArray()) {
        *outError = "Invalid catalog response (missing items array)";
        reply->deleteLater();
        return false;
    }

    *outItems = itemsValue.toArray();
    reply->deleteLater();
    return true;
}

bool PluginCatalogClient::fetchUpdates(const QString& baseUrl,
                                       const QString& appVersion,
                                       int sdkMajor,
                                       const QString& platform,
                                       const QStringList& pluginIds,
                                       QJsonArray* outItems,
                                       QString* outError) const {
    if (!outItems || !outError) {
        return false;
    }

    *outItems = QJsonArray();
    outError->clear();

    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty()) {
        *outError = "Backend URL is empty";
        return false;
    }

    QUrl url(normalized + "/api/plugins/updates");
    if (!url.isValid()) {
        *outError = "Backend URL is invalid";
        return false;
    }

    QUrlQuery urlQuery;
    urlQuery.addQueryItem("appVersion", appVersion.trimmed());
    urlQuery.addQueryItem("sdkMajor", QString::number(sdkMajor));
    urlQuery.addQueryItem("platform", platform.trimmed());
    if (!pluginIds.isEmpty()) {
        urlQuery.addQueryItem("pluginIds", pluginIds.join(','));
    }
    url.setQuery(urlQuery);

    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = network.get(request);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeout.start(10000);
    loop.exec();

    if (timeout.isActive() == false && reply->isFinished() == false) {
        reply->abort();
        *outError = "Updates request timed out";
        reply->deleteLater();
        return false;
    }

    if (reply->error() != QNetworkReply::NoError) {
        *outError = reply->errorString();
        reply->deleteLater();
        return false;
    }

    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        *outError = "Invalid updates response (expected JSON object)";
        reply->deleteLater();
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonValue itemsValue = root.value("items");
    if (!itemsValue.isArray()) {
        *outError = "Invalid updates response (missing items array)";
        reply->deleteLater();
        return false;
    }

    *outItems = itemsValue.toArray();
    reply->deleteLater();
    return true;
}
