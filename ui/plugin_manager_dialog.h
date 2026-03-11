#ifndef PLUGIN_MANAGER_DIALOG_H
#define PLUGIN_MANAGER_DIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QListWidget>
#include <QPair>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QUrl>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVersionNumber>
#include "../core/plugins/plugin_manager.h"
#include "../core/plugins/plugin_catalog_client.h"

class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QFile;

class PluginManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit PluginManagerDialog(QWidget* parent = nullptr);
    ~PluginManagerDialog();

private slots:
    void refreshPluginList();
    void onPluginSelected(QListWidgetItem* item);
    void onToggleSelectedPluginEnabled();
    void onUninstallSelectedPlugin();
    void refreshOnlinePluginList();
    void onOnlinePluginSelected(QListWidgetItem* item);
    void refreshUpdatesList();
    void onUpdateSelectedPlugin();
    void onUpdateAllPlugins();
    void onUpdateItemSelected(QListWidgetItem* item);
    void onDownloadSelectedPlugin();
    void onRetryDownload();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onDownloadCanceled();

private:
    void setupUI();
    void setupInstalledTab();
    void setupOnlineTab();
    void setupUpdatesTab();
    void updateOnlineStatus(const QString& text);
    void updateUpdatesStatus(const QString& text);
    QString formatOnlinePluginDetails(const QJsonObject& plugin) const;
    QString formatUpdateDetails(const QJsonObject& update) const;
    QString compatibilitySummary(const QJsonObject& versionObj) const;
    QString verificationSummary(const QJsonObject& versionObj) const;
    void startDownloadFromSelection(const QJsonObject& plugin, bool isRetry);
    bool startDownloadForPluginVersion(const QString& pluginId,
                                       const QString& version,
                                       bool isRetry,
                                       const QString& expectedChecksum = QString(),
                                       const QString& downloadUrl = QString());
    QString detectedPlatformTag() const;
    QString detectedAppVersion() const;
    QStringList installedPluginIdCandidates() const;
    void processNextQueuedUpdate();
    void resetDownloadState();
    QString defaultDownloadPath(const QString& pluginId, const QString& version) const;

    QTabWidget* m_tabs;
    QWidget* m_installedTab;
    QWidget* m_onlineTab;
    QWidget* m_updatesTab;

    // Installed tab
    QListWidget* m_pluginList;
    QLabel* m_detailsLabel;
    QPushButton* m_refreshBtn;
    QPushButton* m_toggleEnabledBtn;
    QPushButton* m_uninstallBtn;
    QList<PluginManager::PluginLoadResult> m_results;

    // Online tab
    QLineEdit* m_backendUrlEdit;
    QLineEdit* m_onlineSearchEdit;
    QPushButton* m_onlineRefreshBtn;
    QPushButton* m_onlineDownloadBtn;
    QPushButton* m_onlineRetryBtn;
    QListWidget* m_onlinePluginList;
    QLabel* m_onlineDetailsLabel;
    QLabel* m_onlineStatusLabel;
    PluginCatalogClient m_catalogClient;
    QJsonArray m_onlineResults;

    // Updates tab
    QPushButton* m_updatesRefreshBtn;
    QPushButton* m_updateSelectedBtn;
    QPushButton* m_updateAllBtn;
    QListWidget* m_updatesList;
    QLabel* m_updatesDetailsLabel;
    QLabel* m_updatesStatusLabel;
    QJsonArray m_updatesResults;

    QNetworkAccessManager* m_downloadNetwork;
    QNetworkReply* m_activeDownloadReply;
    QProgressDialog* m_downloadProgressDialog;
    QFile* m_activeDownloadFile;
    QUrl m_lastDownloadUrl;
    QString m_lastDownloadPath;
    QList<QJsonObject> m_pendingUpdates;
    QString m_lastExpectedChecksum;
    bool m_processingUpdateQueue = false;
};

#endif // PLUGIN_MANAGER_DIALOG_H
