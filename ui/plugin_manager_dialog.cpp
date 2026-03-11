#include "plugin_manager_dialog.h"
#include "../core/interfaces/plugin_sdk_version.h"
#include "../core/plugins/plugin_package_installer.h"

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QSysInfo>
#include <QStandardPaths>

namespace {
QString statusToString(PluginManager::PluginLoadResult::Status status) {
    switch (status) {
        case PluginManager::PluginLoadResult::Status::Loaded:
            return "Loaded";
        case PluginManager::PluginLoadResult::Status::Skipped:
            return "Skipped";
        case PluginManager::PluginLoadResult::Status::Failed:
        default:
            return "Failed";
    }
}

QString lifecycleStatusToString(PluginManager::PluginLoadResult::LifecycleStatus status) {
    switch (status) {
        case PluginManager::PluginLoadResult::LifecycleStatus::Installed:
            return "Installed";
        case PluginManager::PluginLoadResult::LifecycleStatus::Enabled:
            return "Enabled";
        case PluginManager::PluginLoadResult::LifecycleStatus::Disabled:
            return "Disabled";
        case PluginManager::PluginLoadResult::LifecycleStatus::LoadFailed:
            return "LoadFailed";
        case PluginManager::PluginLoadResult::LifecycleStatus::Incompatible:
            return "Incompatible";
        case PluginManager::PluginLoadResult::LifecycleStatus::UpdateAvailable:
            return "UpdateAvailable";
        default:
            return "Installed";
    }
}

QString computeFileSha256(const QString& filePath, QString* error) {
    if (error) {
        error->clear();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QString("Cannot open downloaded file for checksum: %1").arg(filePath);
        }
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error) {
            *error = QString("Failed to compute checksum for: %1").arg(filePath);
        }
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}
}

PluginManagerDialog::PluginManagerDialog(QWidget* parent)
    : QDialog(parent)
    , m_tabs(nullptr)
    , m_installedTab(nullptr)
    , m_onlineTab(nullptr)
    , m_updatesTab(nullptr)
    , m_pluginList(nullptr)
    , m_detailsLabel(nullptr)
    , m_refreshBtn(nullptr)
    , m_toggleEnabledBtn(nullptr)
    , m_uninstallBtn(nullptr)
    , m_backendUrlEdit(nullptr)
    , m_onlineSearchEdit(nullptr)
    , m_onlineRefreshBtn(nullptr)
    , m_onlineDownloadBtn(nullptr)
    , m_onlineRetryBtn(nullptr)
    , m_onlinePluginList(nullptr)
    , m_onlineDetailsLabel(nullptr)
    , m_onlineStatusLabel(nullptr)
    , m_updatesRefreshBtn(nullptr)
    , m_updateSelectedBtn(nullptr)
    , m_updateAllBtn(nullptr)
    , m_updatesList(nullptr)
    , m_updatesDetailsLabel(nullptr)
    , m_updatesStatusLabel(nullptr)
    , m_downloadNetwork(new QNetworkAccessManager(this))
    , m_activeDownloadReply(nullptr)
    , m_downloadProgressDialog(nullptr)
    , m_activeDownloadFile(nullptr) {
    setupUI();

    refreshPluginList();
    refreshOnlinePluginList();
    refreshUpdatesList();
}

PluginManagerDialog::~PluginManagerDialog() {}

void PluginManagerDialog::setupUI() {
    setWindowTitle("Plugin Manager");
    resize(900, 520);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    m_tabs = new QTabWidget(this);
    m_installedTab = new QWidget(m_tabs);
    m_onlineTab = new QWidget(m_tabs);
    m_updatesTab = new QWidget(m_tabs);

    setupInstalledTab();
    setupOnlineTab();
    setupUpdatesTab();

    m_tabs->addTab(m_installedTab, "Installed");
    m_tabs->addTab(m_onlineTab, "Online");
    m_tabs->addTab(m_updatesTab, "Updates");
    mainLayout->addWidget(m_tabs);

    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch();
    QPushButton* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(closeBtn);
    mainLayout->addLayout(bottomLayout);

    setStyleSheet(
        "QDialog { background-color: #1e1e1e; color: #d4d4d4; font-family: 'Inter', sans-serif; }"
        "QTabWidget::pane { border: 1px solid #3e3e42; border-top: none; background: #1e1e1e; border-bottom-left-radius: 4px; border-bottom-right-radius: 4px; }"
        "QTabBar::tab { background: #2d2d30; color: #969696; padding: 10px 20px; border: 1px solid #3e3e42; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; margin-right: 2px; }"
        "QTabBar::tab:hover { background: #3e3e42; color: #cccccc; }"
        "QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; border-bottom: 2px solid #007acc; }"
        "QGroupBox { font-weight: bold; color: #888; border: 1px solid #3e3e42; border-radius: 6px; margin-top: 15px; padding-top: 15px; font-size: 10px; text-transform: uppercase; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        "QListWidget { background-color: #252526; border: 1px solid #3e3e42; border-radius: 4px; color: #cccccc; outline: none; padding: 5px; }"
        "QListWidget::item { padding: 8px 10px; border-radius: 3px; margin-bottom: 2px; }"
        "QListWidget::item:hover { background-color: #2a2d2e; }"
        "QListWidget::item:selected { background-color: #094771; color: white; }"
        "QLabel { color: #cccccc; line-height: 1.4; }"
        "QLineEdit { background: #2d2d30; color: #cccccc; border: 1px solid #3e3e42; border-radius: 4px; padding: 6px 10px; selection-background-color: #094771; }"
        "QLineEdit:focus { border: 1px solid #007acc; }"
        "QPushButton { background-color: #333333; border: 1px solid #454545; border-radius: 4px; padding: 6px 16px; color: #cccccc; font-weight: 500; }"
        "QPushButton:hover { background-color: #454545; border: 1px solid #555555; }"
        "QPushButton:pressed { background-color: #252526; }"
        "QPushButton:disabled { background-color: #252526; color: #666666; border: 1px solid #333333; }"
        "QPushButton#closeBtn { background-color: #0e639c; color: white; border: none; font-weight: bold; }"
        "QPushButton#closeBtn:hover { background-color: #1177bb; }"
    );
    closeBtn->setObjectName("closeBtn");
}

void PluginManagerDialog::setupInstalledTab() {
    QVBoxLayout* tabLayout = new QVBoxLayout(m_installedTab);
    tabLayout->setSpacing(12);

    QHBoxLayout* contentLayout = new QHBoxLayout();

    QGroupBox* listGroup = new QGroupBox("Installed Plugins");
    QVBoxLayout* listLayout = new QVBoxLayout(listGroup);
    m_pluginList = new QListWidget();
    listLayout->addWidget(m_pluginList);
    contentLayout->addWidget(listGroup, 1);

    QGroupBox* detailsGroup = new QGroupBox("Plugin Details");
    QVBoxLayout* detailsLayout = new QVBoxLayout(detailsGroup);
    m_detailsLabel = new QLabel("Select a plugin to see details.");
    m_detailsLabel->setWordWrap(true);
    m_detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailsLayout->addWidget(m_detailsLabel);
    contentLayout->addWidget(detailsGroup, 1);

    tabLayout->addLayout(contentLayout);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton("Scan for Plugins");
    m_toggleEnabledBtn = new QPushButton("Disable");
    m_uninstallBtn = new QPushButton("Uninstall");
    m_toggleEnabledBtn->setEnabled(false);
    m_uninstallBtn->setEnabled(false);
    connect(m_refreshBtn, &QPushButton::clicked, this, &PluginManagerDialog::refreshPluginList);
    connect(m_pluginList, &QListWidget::itemClicked, this, &PluginManagerDialog::onPluginSelected);
    connect(m_toggleEnabledBtn, &QPushButton::clicked, this, &PluginManagerDialog::onToggleSelectedPluginEnabled);
    connect(m_uninstallBtn, &QPushButton::clicked, this, &PluginManagerDialog::onUninstallSelectedPlugin);

    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addWidget(m_toggleEnabledBtn);
    btnLayout->addWidget(m_uninstallBtn);
    btnLayout->addStretch();
    tabLayout->addLayout(btnLayout);
}

void PluginManagerDialog::setupOnlineTab() {
    QVBoxLayout* tabLayout = new QVBoxLayout(m_onlineTab);
    tabLayout->setSpacing(12);

    QGroupBox* connectionGroup = new QGroupBox("Catalog Backend");
    QHBoxLayout* connectionLayout = new QHBoxLayout(connectionGroup);

    m_backendUrlEdit = new QLineEdit("http://127.0.0.1:4080");
    m_onlineSearchEdit = new QLineEdit();
    m_onlineSearchEdit->setPlaceholderText("Search plugins...");
    m_onlineRefreshBtn = new QPushButton("Browse Online");
    m_onlineDownloadBtn = new QPushButton("Download");
    m_onlineRetryBtn = new QPushButton("Retry Download");
    m_onlineDownloadBtn->setEnabled(false);
    m_onlineRetryBtn->setEnabled(false);

    connectionLayout->addWidget(new QLabel("URL:"));
    connectionLayout->addWidget(m_backendUrlEdit, 2);
    connectionLayout->addWidget(new QLabel("Search:"));
    connectionLayout->addWidget(m_onlineSearchEdit, 1);
    connectionLayout->addWidget(m_onlineRefreshBtn);
    connectionLayout->addWidget(m_onlineDownloadBtn);
    connectionLayout->addWidget(m_onlineRetryBtn);

    tabLayout->addWidget(connectionGroup);

    QHBoxLayout* contentLayout = new QHBoxLayout();

    QGroupBox* listGroup = new QGroupBox("Online Plugins");
    QVBoxLayout* listLayout = new QVBoxLayout(listGroup);
    m_onlinePluginList = new QListWidget();
    listLayout->addWidget(m_onlinePluginList);
    contentLayout->addWidget(listGroup, 1);

    QGroupBox* detailsGroup = new QGroupBox("Online Plugin Details");
    QVBoxLayout* detailsLayout = new QVBoxLayout(detailsGroup);
    m_onlineDetailsLabel = new QLabel("Select an online plugin to see details.");
    m_onlineDetailsLabel->setWordWrap(true);
    m_onlineDetailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailsLayout->addWidget(m_onlineDetailsLabel);
    contentLayout->addWidget(detailsGroup, 1);

    tabLayout->addLayout(contentLayout);

    m_onlineStatusLabel = new QLabel("Ready.");
    tabLayout->addWidget(m_onlineStatusLabel);

    connect(m_onlineRefreshBtn, &QPushButton::clicked,
            this, &PluginManagerDialog::refreshOnlinePluginList);
    connect(m_onlineSearchEdit, &QLineEdit::returnPressed,
            this, &PluginManagerDialog::refreshOnlinePluginList);
    connect(m_onlinePluginList, &QListWidget::itemClicked,
            this, &PluginManagerDialog::onOnlinePluginSelected);
    connect(m_onlineDownloadBtn, &QPushButton::clicked,
            this, &PluginManagerDialog::onDownloadSelectedPlugin);
    connect(m_onlineRetryBtn, &QPushButton::clicked,
            this, &PluginManagerDialog::onRetryDownload);
}

void PluginManagerDialog::setupUpdatesTab() {
    QVBoxLayout* tabLayout = new QVBoxLayout(m_updatesTab);
    tabLayout->setSpacing(12);

    QGroupBox* controlGroup = new QGroupBox("Available Updates");
    QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
    m_updatesRefreshBtn = new QPushButton("Check Updates");
    m_updateSelectedBtn = new QPushButton("Update Selected");
    m_updateAllBtn = new QPushButton("Update All");
    m_updateSelectedBtn->setEnabled(false);
    m_updateAllBtn->setEnabled(false);

    controlLayout->addWidget(m_updatesRefreshBtn);
    controlLayout->addWidget(m_updateSelectedBtn);
    controlLayout->addWidget(m_updateAllBtn);
    controlLayout->addStretch();

    tabLayout->addWidget(controlGroup);

    QHBoxLayout* contentLayout = new QHBoxLayout();
    QGroupBox* listGroup = new QGroupBox("Update Candidates");
    QVBoxLayout* listLayout = new QVBoxLayout(listGroup);
    m_updatesList = new QListWidget();
    listLayout->addWidget(m_updatesList);
    contentLayout->addWidget(listGroup, 1);

    QGroupBox* detailsGroup = new QGroupBox("Update Details");
    QVBoxLayout* detailsLayout = new QVBoxLayout(detailsGroup);
    m_updatesDetailsLabel = new QLabel("Select an update to see details.");
    m_updatesDetailsLabel->setWordWrap(true);
    m_updatesDetailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailsLayout->addWidget(m_updatesDetailsLabel);
    contentLayout->addWidget(detailsGroup, 1);

    tabLayout->addLayout(contentLayout);

    m_updatesStatusLabel = new QLabel("Ready.");
    tabLayout->addWidget(m_updatesStatusLabel);

    connect(m_updatesRefreshBtn, &QPushButton::clicked,
            this, &PluginManagerDialog::refreshUpdatesList);
    connect(m_updateSelectedBtn, &QPushButton::clicked,
            this, &PluginManagerDialog::onUpdateSelectedPlugin);
    connect(m_updateAllBtn, &QPushButton::clicked,
            this, &PluginManagerDialog::onUpdateAllPlugins);
    connect(m_updatesList, &QListWidget::itemClicked,
            this, &PluginManagerDialog::onUpdateItemSelected);
}

void PluginManagerDialog::refreshPluginList() {
    PluginManager::instance().loadPlugins();
    m_pluginList->clear();
    m_detailsLabel->setText("Select a plugin to see details.");
    m_results = PluginManager::instance().lastLoadResults();
    m_toggleEnabledBtn->setEnabled(false);
    m_uninstallBtn->setEnabled(false);

    for (int i = 0; i < m_results.size(); ++i) {
        const auto& result = m_results[i];
        const QString pluginLabel = result.pluginName.isEmpty()
                                        ? QFileInfo(result.filePath).fileName()
                                        : result.pluginName;
        
        QListWidgetItem* item = new QListWidgetItem(pluginLabel);
        item->setData(Qt::UserRole, i);
        // Use icons/badges conceptually in text for now as we are limited to QListWidgetItem
        QString statusText;
        QColor color = QColor("#cccccc");

        if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::Enabled) {
            color = QColor("#4ec9b0");
            statusText = " [Enabled]";
        } else if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::Disabled) {
            color = QColor("#808080");
            statusText = " [Disabled]";
        } else if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::Incompatible) {
            color = QColor("#dcdcaa");
            statusText = " [Incompatible]";
        } else if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::UpdateAvailable) {
            color = QColor("#569cd6");
            statusText = " [Update Available]";
        } else {
            color = QColor("#f44747");
            statusText = " [Error]";
        }
        
        item->setText(pluginLabel + statusText);
        item->setForeground(QBrush(color));
        m_pluginList->addItem(item);
    }

    if (m_pluginList->count() == 0) {
        m_pluginList->addItem("(No plugins found)");
        m_pluginList->item(0)->setFlags(Qt::NoItemFlags);
    }
}

void PluginManagerDialog::onPluginSelected(QListWidgetItem* item) {
    const QVariant indexData = item->data(Qt::UserRole);
    if (!indexData.isValid()) {
        m_detailsLabel->setText("Select a plugin to see details.");
        return;
    }

    const int index = indexData.toInt();
    if (index < 0 || index >= m_results.size()) {
        m_detailsLabel->setText("Select a plugin to see details.");
        return;
    }

    const auto& result = m_results[index];
    const QString status = statusToString(result.status);
    const QString lifecycleStatus = lifecycleStatusToString(result.lifecycleStatus);
    const bool fileExists = QFileInfo::exists(result.filePath);
    
    if (fileExists) {
        const bool enabled = PluginManager::instance().isPluginEnabledByPath(result.filePath);
        m_toggleEnabledBtn->setText(enabled ? "Disable" : "Enable");
    }
    m_toggleEnabledBtn->setEnabled(fileExists);
    m_uninstallBtn->setEnabled(fileExists);

    QString statusColor = "#aaaaaa";
    if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::Enabled) statusColor = "#4ec9b0";
    else if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::Disabled) statusColor = "#808080";
    else if (result.lifecycleStatus == PluginManager::PluginLoadResult::LifecycleStatus::Incompatible) statusColor = "#dcdcaa";
    else statusColor = "#f44747";

    const QString details = QString(
        "<div style='background-color: #2d2d30; padding: 15px; border-radius: 8px; border: 1px solid #3e3e42;'>"
        "  <div style='font-size: 16px; font-weight: bold; color: #ffffff; margin-bottom: 5px;'>%1</div>"
        "  <div style='color: %2; font-weight: bold; font-size: 11px; margin-bottom: 12px; text-transform: uppercase;'>&nbsp;● %3</div>"
        "  <hr style='border: none; border-top: 1px solid #3e3e42; margin-bottom: 12px;'>"
        "  <table width='100%' style='color: #cccccc; font-size: 12px;'>"
        "    <tr><td width='80'><b>Version:</b></td><td>%4</td></tr>"
        "    <tr><td><b>Author:</b></td><td>%5</td></tr>"
        "    <tr><td><b>Status:</b></td><td>%6</td></tr>"
        "  </table>"
        "  <div style='margin-top: 15px; background: #1e1e1e; padding: 10px; border-radius: 4px; color: #bbbbbb; font-style: italic;'>"
        "    %7"
        "  </div>"
        "  <div style='margin-top: 10px; font-size: 10px; color: #666666;'>"
        "    <b>Path:</b> %8"
        "  </div>"
        "  %9"
        "</div>"
    ).arg(
        result.pluginName.isEmpty() ? QFileInfo(result.filePath).fileName() : result.pluginName,
        statusColor,
        lifecycleStatus,
        result.version.isEmpty() ? "0.0.0" : result.version,
        result.author.isEmpty() ? "Community" : result.author,
        status,
        result.description.isEmpty() ? "No description provided." : result.description,
        result.filePath,
        result.reason.isEmpty() ? "" : QString("<div style='margin-top: 8px; color: #ce9178; font-family: monospace;'>%1</div>").arg(result.reason)
    );
    m_detailsLabel->setText(details);
}

void PluginManagerDialog::onToggleSelectedPluginEnabled() {
    QListWidgetItem* currentItem = m_pluginList->currentItem();
    if (!currentItem) {
        return;
    }

    const QVariant indexData = currentItem->data(Qt::UserRole);
    if (!indexData.isValid()) {
        return;
    }

    const int index = indexData.toInt();
    if (index < 0 || index >= m_results.size()) {
        return;
    }

    const QString path = m_results[index].filePath;
    const bool currentlyEnabled = PluginManager::instance().isPluginEnabledByPath(path);
    PluginManager::instance().setPluginEnabledByPath(path, !currentlyEnabled);
    refreshPluginList();
}

void PluginManagerDialog::onUninstallSelectedPlugin() {
    QListWidgetItem* currentItem = m_pluginList->currentItem();
    if (!currentItem) {
        return;
    }

    const QVariant indexData = currentItem->data(Qt::UserRole);
    if (!indexData.isValid()) {
        return;
    }

    const int index = indexData.toInt();
    if (index < 0 || index >= m_results.size()) {
        return;
    }

    const QString path = m_results[index].filePath;
    const bool removed = PluginManager::instance().uninstallPluginByPath(path);
    if (!removed) {
        m_detailsLabel->setText(QString("Failed to uninstall plugin file:<br>%1").arg(path));
    }
    refreshPluginList();
}

void PluginManagerDialog::refreshOnlinePluginList() {
    m_onlineRefreshBtn->setEnabled(false);
    updateOnlineStatus("Loading online catalog...");
    QJsonArray items;
    QString error;
    const bool ok = m_catalogClient.fetchPlugins(
        m_backendUrlEdit->text(),
        m_onlineSearchEdit->text(),
        &items,
        &error
    );

    m_onlineRefreshBtn->setEnabled(true);
    m_onlinePluginList->clear();

    if (!ok) {
        m_onlineResults = QJsonArray();
        m_onlineDetailsLabel->setText("Could not load online plugins.");
        m_onlineDownloadBtn->setEnabled(false);
        updateOnlineStatus(QString("Catalog request failed: %1").arg(error));
        return;
    }

    m_onlineResults = items;
    for (int i = 0; i < m_onlineResults.size(); ++i) {
        const QJsonObject plugin = m_onlineResults.at(i).toObject();
        const QString name = plugin.value("name").toString(plugin.value("id").toString("(unknown)"));
        const QString owner = plugin.value("owner").toString();
        const QString category = plugin.value("category").toString();

        QString label = name;
        if (!category.isEmpty()) {
            label = QString("[%1] %2").arg(category, name);
        }
        if (!owner.isEmpty()) {
            label += QString(" by %1").arg(owner);
        }

        QListWidgetItem* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, i);
        item->setForeground(QBrush(QColor("#569cd6"))); // Blue for online items
        m_onlinePluginList->addItem(item);
    }

    if (m_onlinePluginList->count() == 0) {
        m_onlinePluginList->addItem("(No online plugins found)");
        m_onlinePluginList->item(0)->setFlags(Qt::NoItemFlags);
    }

    m_onlineDetailsLabel->setText("Select an online plugin to see details.");
    m_onlineDownloadBtn->setEnabled(false);
    updateOnlineStatus(QString("Loaded %1 online plugin(s)").arg(m_onlineResults.size()));
}

void PluginManagerDialog::onOnlinePluginSelected(QListWidgetItem* item) {
    const QVariant indexData = item->data(Qt::UserRole);
    if (!indexData.isValid()) {
        m_onlineDetailsLabel->setText("Select an online plugin to see details.");
        return;
    }

    const int index = indexData.toInt();
    if (index < 0 || index >= m_onlineResults.size()) {
        m_onlineDetailsLabel->setText("Select an online plugin to see details.");
        return;
    }

    const QJsonObject plugin = m_onlineResults.at(index).toObject();
    m_onlineDetailsLabel->setText(formatOnlinePluginDetails(plugin));
    m_onlineDownloadBtn->setEnabled(true);
}

QString PluginManagerDialog::detectedPlatformTag() const {
    const QString os = QSysInfo::productType().toLower();
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();

    QString osTag = os;
    if (os.contains("windows")) {
        osTag = "windows";
    } else if (os.contains("linux")) {
        osTag = "linux";
    } else if (os.contains("mac") || os.contains("osx")) {
        osTag = "macos";
    }
    QString archTag = arch;
    if (arch.contains("x86_64") || arch.contains("amd64")) {
        archTag = "x64";
    } else if (arch.contains("aarch64") || arch.contains("arm64")) {
        archTag = "arm64";
    }
    return QString("%1-%2").arg(osTag, archTag);
}

QString PluginManagerDialog::detectedAppVersion() const {
    const QString version = QCoreApplication::applicationVersion().trimmed();
    if (!version.isEmpty()) {
        return version;
    }
    return "0.2.0";
}

QStringList PluginManagerDialog::installedPluginIdCandidates() const {
    QStringList ids;
    for (const auto& result : m_results) {
        if (result.pluginName == "Plugin directories") {
            continue;
        }
        const QFileInfo fi(result.filePath);
        if (!result.pluginName.trimmed().isEmpty()) {
            ids.append(result.pluginName.trimmed());
        }
        const QString baseName = fi.completeBaseName().trimmed();
        if (!baseName.isEmpty()) {
            ids.append(baseName);
        }
    }
    ids.removeDuplicates();
    return ids;
}

void PluginManagerDialog::refreshUpdatesList() {
    m_updatesRefreshBtn->setEnabled(false);
    m_updateSelectedBtn->setEnabled(false);
    m_updateAllBtn->setEnabled(false);
    m_updatesList->clear();
    m_updatesDetailsLabel->setText("Select an update to see details.");

    if (m_results.isEmpty()) {
        refreshPluginList();
    }

    const QStringList pluginIds = installedPluginIdCandidates();
    if (pluginIds.isEmpty()) {
        updateUpdatesStatus("No installed plugins available for update check.");
        m_updatesRefreshBtn->setEnabled(true);
        return;
    }

    QJsonArray updates;
    QString error;
    const bool ok = m_catalogClient.fetchUpdates(
        m_backendUrlEdit->text(),
        detectedAppVersion(),
        flux::plugin::kSdkVersionMajor,
        detectedPlatformTag(),
        pluginIds,
        &updates,
        &error
    );

    m_updatesRefreshBtn->setEnabled(true);
    if (!ok) {
        m_updatesResults = QJsonArray();
        updateUpdatesStatus(QString("Update check failed: %1").arg(error));
        return;
    }

    m_updatesResults = updates;
    for (int i = 0; i < m_updatesResults.size(); ++i) {
        const QJsonObject upd = m_updatesResults.at(i).toObject();
        const QString pluginId = upd.value("pluginId").toString("(unknown)");
        const QString version = upd.value("version").toString("(unknown)");
        QListWidgetItem* item = new QListWidgetItem(QString("%1 -> %2").arg(pluginId, version));
        item->setData(Qt::UserRole, i);
        item->setForeground(QBrush(QColor("#7fc7ff")));
        m_updatesList->addItem(item);
    }

    if (m_updatesList->count() == 0) {
        m_updatesList->addItem("(No updates available)");
        m_updatesList->item(0)->setFlags(Qt::NoItemFlags);
        updateUpdatesStatus("No updates available.");
    } else {
        m_updateAllBtn->setEnabled(true);
        updateUpdatesStatus(QString("Found %1 update(s)").arg(m_updatesResults.size()));
    }
}

QString PluginManagerDialog::formatUpdateDetails(const QJsonObject& update) const {
    const QString compatibility = compatibilitySummary(update);
    const QString verification = verificationSummary(update);
    return QString(
        "<div style='background-color: #2d2d30; padding: 15px; border-radius: 8px; border: 1px solid #3e3e42;'>"
        "  <div style='font-size: 16px; font-weight: bold; color: #ffffff; margin-bottom: 5px;'>%1 Update</div>"
        "  <div style='color: #7fc7ff; font-weight: bold; font-size: 11px; margin-bottom: 12px; text-transform: uppercase;'>&nbsp;● NEW VERSION AVAILABLE</div>"
        "  <hr style='border: none; border-top: 1px solid #3e3e42; margin-bottom: 12px;'>"
        "  <table width='100%' style='color: #cccccc; font-size: 12px; line-height: 1.6;'>"
        "    <tr><td width='85'><b>Version:</b></td><td><span style='color: #4ec9b0;'>%2</span></td></tr>"
        "    <tr><td><b>Platform:</b></td><td>%3</td></tr>"
        "    <tr><td><b>Published:</b></td><td>%4</td></tr>"
        "    <tr><td><b>Size:</b></td><td>%5 KB</td></tr>"
        "  </table>"
        "  <div style='margin-top: 12px; padding: 10px; background: #2a2d2e; border-radius: 4px; border-left: 3px solid #007acc;'>"
        "    <div style='color: #cccccc; font-size: 12px;'><b>Compatibility:</b> %6</div>"
        "    <div style='color: #cccccc; font-size: 12px; margin-top: 4px;'><b>Verification:</b> %7</div>"
        "  </div>"
        "  <div style='margin-top: 12px; font-size: 10px; color: #666666;'>"
        "    <b>SHA-256:</b> %8"
        "  </div>"
        "</div>"
    ).arg(
        update.value("pluginId").toString("(unknown)"),
        update.value("version").toString("(unknown)"),
        update.value("platform").toString("(unknown)"),
        update.value("publishedAt").toString("(unknown)"),
        QString::number(static_cast<qint64>(update.value("sizeBytes").toDouble(0.0) / 1024.0)),
        compatibility,
        verification,
        update.value("checksumSha256").toString("(unknown)")
    );
}

void PluginManagerDialog::onUpdateItemSelected(QListWidgetItem* item) {
    const QVariant indexData = item->data(Qt::UserRole);
    if (!indexData.isValid()) {
        m_updatesDetailsLabel->setText("Select an update to see details.");
        m_updateSelectedBtn->setEnabled(false);
        return;
    }
    const int index = indexData.toInt();
    if (index < 0 || index >= m_updatesResults.size()) {
        m_updatesDetailsLabel->setText("Select an update to see details.");
        m_updateSelectedBtn->setEnabled(false);
        return;
    }
    const QJsonObject update = m_updatesResults.at(index).toObject();
    m_updatesDetailsLabel->setText(formatUpdateDetails(update));
    m_updateSelectedBtn->setEnabled(true);
}

void PluginManagerDialog::onUpdateSelectedPlugin() {
    QListWidgetItem* current = m_updatesList->currentItem();
    if (!current) {
        updateUpdatesStatus("Select an update first.");
        return;
    }
    const QVariant indexData = current->data(Qt::UserRole);
    if (!indexData.isValid()) {
        updateUpdatesStatus("Select a valid update item.");
        return;
    }
    const int index = indexData.toInt();
    if (index < 0 || index >= m_updatesResults.size()) {
        updateUpdatesStatus("Select a valid update item.");
        return;
    }

    const QJsonObject update = m_updatesResults.at(index).toObject();
    const QString pluginId = update.value("pluginId").toString();
    const QString version = update.value("version").toString();
    const QString checksum = update.value("checksumSha256").toString(
        update.value("checksum_sha256").toString()).trimmed();
    const QString downloadUrl = update.value("downloadUrl").toString().trimmed();
    if (!startDownloadForPluginVersion(pluginId, version, false, checksum, downloadUrl)) {
        return;
    }
    updateUpdatesStatus(QString("Downloading update %1 -> %2 ...").arg(pluginId, version));
}

void PluginManagerDialog::onUpdateAllPlugins() {
    if (m_updatesResults.isEmpty()) {
        updateUpdatesStatus("No updates available.");
        return;
    }

    m_pendingUpdates.clear();
    for (int i = 0; i < m_updatesResults.size(); ++i) {
        const QJsonObject update = m_updatesResults.at(i).toObject();
        const QString pluginId = update.value("pluginId").toString();
        const QString version = update.value("version").toString();
        if (!pluginId.isEmpty() && !version.isEmpty()) {
            QJsonObject queued;
            queued["pluginId"] = pluginId;
            queued["version"] = version;
            queued["checksumSha256"] = update.value("checksumSha256").toString(
                update.value("checksum_sha256").toString()).trimmed();
            queued["downloadUrl"] = update.value("downloadUrl").toString().trimmed();
            m_pendingUpdates.append(queued);
        }
    }
    if (m_pendingUpdates.isEmpty()) {
        updateUpdatesStatus("No valid update entries.");
        return;
    }
    m_processingUpdateQueue = true;
    updateUpdatesStatus(QString("Starting update queue (%1 items)...").arg(m_pendingUpdates.size()));
    processNextQueuedUpdate();
}

void PluginManagerDialog::processNextQueuedUpdate() {
    if (!m_processingUpdateQueue) {
        return;
    }
    if (m_activeDownloadReply) {
        return;
    }
    if (m_pendingUpdates.isEmpty()) {
        m_processingUpdateQueue = false;
        updateUpdatesStatus("Update queue finished.");
        return;
    }

    const QJsonObject next = m_pendingUpdates.takeFirst();
    const QString pluginId = next.value("pluginId").toString();
    const QString version = next.value("version").toString();
    const QString checksum = next.value("checksumSha256").toString(
        next.value("checksum_sha256").toString()).trimmed();
    const QString downloadUrl = next.value("downloadUrl").toString().trimmed();
    if (startDownloadForPluginVersion(pluginId, version, false, checksum, downloadUrl)) {
        updateUpdatesStatus(QString("Updating %1 -> %2 (%3 remaining)")
                                .arg(pluginId, version)
                                .arg(m_pendingUpdates.size()));
    } else {
        m_processingUpdateQueue = false;
        m_pendingUpdates.clear();
        updateUpdatesStatus("Update queue stopped.");
    }
}

void PluginManagerDialog::updateOnlineStatus(const QString& text) {
    if (m_onlineStatusLabel) {
        m_onlineStatusLabel->setText(text);
    }
}

void PluginManagerDialog::updateUpdatesStatus(const QString& text) {
    if (m_updatesStatusLabel) {
        m_updatesStatusLabel->setText(text);
    }
}

QString PluginManagerDialog::formatOnlinePluginDetails(const QJsonObject& plugin) const {
    const QString name = plugin.value("name").toString(plugin.value("id").toString("(unknown)"));
    const QString id = plugin.value("id").toString("(unknown)");
    const QString description = plugin.value("description").toString("(No description)");
    const QString owner = plugin.value("owner").toString("(Unknown)");
    const QString category = plugin.value("category").toString("(Uncategorized)");
    const QString homepage = plugin.value("homepage").toString("-");

    QString latestVersion = "(Unknown)";
    QJsonObject latestVersionObj;
    if (plugin.value("latestVersion").isObject()) {
        latestVersionObj = plugin.value("latestVersion").toObject();
        latestVersion = latestVersionObj.value("version").toString("(Unknown)");
    }
    const QString compatibility = compatibilitySummary(latestVersionObj);
    const QString verification = verificationSummary(latestVersionObj);

    return QString(
        "<div style='background-color: #2d2d30; padding: 15px; border-radius: 8px; border: 1px solid #3e3e42;'>"
        "  <div style='font-size: 16px; font-weight: bold; color: #ffffff; margin-bottom: 5px;'>%1</div>"
        "  <div style='color: #007acc; font-weight: bold; font-size: 11px; margin-bottom: 12px; text-transform: uppercase;'>&nbsp;● ONLINE CATALOG</div>"
        "  <hr style='border: none; border-top: 1px solid #3e3e42; margin-bottom: 12px;'>"
        "  <table width='100%' style='color: #cccccc; font-size: 12px; line-height: 1.6;'>"
        "    <tr><td width='85'><b>ID:</b></td><td>%2</td></tr>"
        "    <tr><td><b>Latest:</b></td><td><span style='color: #4ec9b0; font-weight: bold;'>%3</span></td></tr>"
        "    <tr><td><b>Publisher:</b></td><td>%4</td></tr>"
        "    <tr><td><b>Category:</b></td><td>%5</td></tr>"
        "    <tr><td><b>Homepage:</b></td><td><a href='%6' style='color: #007acc; text-decoration: none;'>%6</a></td></tr>"
        "  </table>"
        "  <div style='margin-top: 15px; background: #1e1e1e; padding: 12px; border-radius: 4px; border: 1px solid #333333;'>"
        "    <div style='color: #888888; font-size: 10px; font-weight: bold; text-transform: uppercase; margin-bottom: 5px;'>Description</div>"
        "    <div style='color: #cccccc; font-size: 12px;'>%9</div>"
        "  </div>"
        "  <div style='margin-top: 12px; padding: 8px; background: #2a2d2e; border-radius: 4px; font-size: 11px; color: #999999;'>"
        "    <b>Compatibility:</b> %7<br>"
        "    <b>Security:</b> %8"
        "  </div>"
        "</div>"
    ).arg(name, id, latestVersion, owner, category, homepage, compatibility, verification, description);
}

QString PluginManagerDialog::compatibilitySummary(const QJsonObject& versionObj) const {
    if (versionObj.isEmpty()) {
        return "Unknown (missing version metadata)";
    }

    const int sdkMin = versionObj.value("sdk_major_min").toInt(
        versionObj.value("sdkMajorMin").toInt(flux::plugin::kSdkVersionMajor));
    const int sdkMax = versionObj.value("sdk_major_max").toInt(
        versionObj.value("sdkMajorMax").toInt(sdkMin));
    const int hostSdk = flux::plugin::kSdkVersionMajor;
    const bool sdkOk = hostSdk >= sdkMin && hostSdk <= sdkMax;

    const QString platform = versionObj.value("platform").toString("any").trimmed().toLower();
    const QString hostPlatform = detectedPlatformTag().toLower();
    const bool platformOk = platform == "any" || platform == hostPlatform;

    const QString hostApp = detectedAppVersion();
    const QVersionNumber hostVersion = QVersionNumber::fromString(hostApp);
    const QString minApp = versionObj.value("app_min_version").toString(
        versionObj.value("appMinVersion").toString());
    const QString maxApp = versionObj.value("app_max_version").toString(
        versionObj.value("appMaxVersion").toString());

    bool appMinOk = true;
    bool appMaxOk = true;
    const QVersionNumber minVersion = QVersionNumber::fromString(minApp);
    const QVersionNumber maxVersion = QVersionNumber::fromString(maxApp);
    if (!minVersion.isNull() && !hostVersion.isNull()) {
        appMinOk = QVersionNumber::compare(hostVersion, minVersion) >= 0;
    }
    if (!maxVersion.isNull() && !hostVersion.isNull()) {
        appMaxOk = QVersionNumber::compare(hostVersion, maxVersion) <= 0;
    }

    if (sdkOk && platformOk && appMinOk && appMaxOk) {
        return "Compatible";
    }

    QStringList reasons;
    if (!sdkOk) {
        reasons.append(QString("SDK host=%1 expected=%2..%3").arg(hostSdk).arg(sdkMin).arg(sdkMax));
    }
    if (!platformOk) {
        reasons.append(QString("Platform host=%1 expected=%2").arg(hostPlatform, platform));
    }
    if (!appMinOk || !appMaxOk) {
        reasons.append(QString("App host=%1 range=%2..%3").arg(
            hostApp,
            minApp.isEmpty() ? "-" : minApp,
            maxApp.isEmpty() ? "-" : maxApp));
    }
    return "Incompatible (" + reasons.join("; ") + ")";
}

QString PluginManagerDialog::verificationSummary(const QJsonObject& versionObj) const {
    if (versionObj.isEmpty()) {
        return "Unknown (no metadata)";
    }

    const QString checksum = versionObj.value("checksum_sha256").toString(
        versionObj.value("checksumSha256").toString()).trimmed().toLower();
    if (checksum.isEmpty()) {
        return "Not verifiable (checksum missing)";
    }
    if (checksum.size() == 64) {
        return "Verifiable (SHA-256 present)";
    }
    return "Suspicious metadata (invalid checksum format)";
}

QString PluginManagerDialog::defaultDownloadPath(const QString& pluginId, const QString& version) const {
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath();
    }
    return QString("%1/%2-%3.fluxplugin").arg(baseDir, pluginId, version);
}

void PluginManagerDialog::onDownloadSelectedPlugin() {
    QListWidgetItem* currentItem = m_onlinePluginList->currentItem();
    if (!currentItem) {
        updateOnlineStatus("Select an online plugin first.");
        return;
    }

    const QVariant indexData = currentItem->data(Qt::UserRole);
    if (!indexData.isValid()) {
        updateOnlineStatus("Select a valid online plugin.");
        return;
    }

    const int index = indexData.toInt();
    if (index < 0 || index >= m_onlineResults.size()) {
        updateOnlineStatus("Select a valid online plugin.");
        return;
    }

    const QJsonObject plugin = m_onlineResults.at(index).toObject();
    startDownloadFromSelection(plugin, false);
}

void PluginManagerDialog::onRetryDownload() {
    if (m_lastDownloadUrl.isEmpty() || m_lastDownloadPath.isEmpty()) {
        updateOnlineStatus("No failed download available for retry.");
        return;
    }

    if (m_activeDownloadReply) {
        updateOnlineStatus("Download already in progress.");
        return;
    }

    m_activeDownloadFile = new QFile(m_lastDownloadPath, this);
    if (!m_activeDownloadFile->open(QIODevice::WriteOnly)) {
        updateOnlineStatus(QString("Retry failed: cannot write %1").arg(m_lastDownloadPath));
        m_activeDownloadFile->deleteLater();
        m_activeDownloadFile = nullptr;
        return;
    }

    QNetworkRequest request(m_lastDownloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_activeDownloadReply = m_downloadNetwork->get(request);
    connect(m_activeDownloadReply, &QNetworkReply::downloadProgress,
            this, &PluginManagerDialog::onDownloadProgress);
    connect(m_activeDownloadReply, &QNetworkReply::finished,
            this, &PluginManagerDialog::onDownloadFinished);
    connect(m_activeDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_activeDownloadReply && m_activeDownloadFile) {
            m_activeDownloadFile->write(m_activeDownloadReply->readAll());
        }
    });

    m_downloadProgressDialog = new QProgressDialog("Downloading plugin...", "Cancel", 0, 100, this);
    m_downloadProgressDialog->setWindowModality(Qt::WindowModal);
    m_downloadProgressDialog->setAutoClose(false);
    m_downloadProgressDialog->setAutoReset(false);
    connect(m_downloadProgressDialog, &QProgressDialog::canceled,
            this, &PluginManagerDialog::onDownloadCanceled);
    m_downloadProgressDialog->show();

    m_onlineDownloadBtn->setEnabled(false);
    m_onlineRetryBtn->setEnabled(false);
    updateOnlineStatus("Retrying plugin download...");
}

bool PluginManagerDialog::startDownloadForPluginVersion(const QString& pluginId,
                                                        const QString& version,
                                                        bool isRetry,
                                                        const QString& expectedChecksum,
                                                        const QString& downloadUrl) {
    if (m_activeDownloadReply) {
        updateOnlineStatus("Download already in progress.");
        return false;
    }

    if (pluginId.trimmed().isEmpty() || version.trimmed().isEmpty()) {
        updateOnlineStatus("Plugin update metadata is missing id/version.");
        return false;
    }

    const QString baseUrl = m_backendUrlEdit->text().trimmed();
    QUrl resolvedDownloadUrl(downloadUrl.trimmed());
    if (resolvedDownloadUrl.isEmpty()) {
        resolvedDownloadUrl = QUrl(QString("%1/api/plugins/%2/versions/%3/download")
                                       .arg(baseUrl, pluginId.trimmed(), version.trimmed()));
    }
    if (!resolvedDownloadUrl.isValid()) {
        updateOnlineStatus("Backend URL is invalid.");
        return false;
    }

    QString savePath = m_lastDownloadPath;
    if (!isRetry || savePath.isEmpty()) {
        savePath = QFileDialog::getSaveFileName(
            this,
            "Save Plugin Package",
            defaultDownloadPath(pluginId.trimmed(), version.trimmed()),
            "VioraEDA Plugin (*.fluxplugin);;All Files (*)");
    }

    if (savePath.isEmpty()) {
        updateOnlineStatus("Download canceled.");
        return false;
    }

    m_activeDownloadFile = new QFile(savePath, this);
    if (!m_activeDownloadFile->open(QIODevice::WriteOnly)) {
        updateOnlineStatus(QString("Cannot write file: %1").arg(savePath));
        m_activeDownloadFile->deleteLater();
        m_activeDownloadFile = nullptr;
        return false;
    }

    m_lastDownloadUrl = resolvedDownloadUrl;
    m_lastDownloadPath = savePath;
    m_lastExpectedChecksum = expectedChecksum.trimmed().toLower();

    QNetworkRequest request(resolvedDownloadUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    m_activeDownloadReply = m_downloadNetwork->get(request);

    connect(m_activeDownloadReply, &QNetworkReply::downloadProgress,
            this, &PluginManagerDialog::onDownloadProgress);
    connect(m_activeDownloadReply, &QNetworkReply::finished,
            this, &PluginManagerDialog::onDownloadFinished);
    connect(m_activeDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_activeDownloadReply && m_activeDownloadFile) {
            m_activeDownloadFile->write(m_activeDownloadReply->readAll());
        }
    });

    m_downloadProgressDialog = new QProgressDialog("Downloading plugin...", "Cancel", 0, 100, this);
    m_downloadProgressDialog->setWindowModality(Qt::WindowModal);
    m_downloadProgressDialog->setAutoClose(false);
    m_downloadProgressDialog->setAutoReset(false);
    connect(m_downloadProgressDialog, &QProgressDialog::canceled,
            this, &PluginManagerDialog::onDownloadCanceled);
    m_downloadProgressDialog->show();

    m_onlineDownloadBtn->setEnabled(false);
    m_onlineRetryBtn->setEnabled(false);
    updateOnlineStatus(QString("Downloading %1 %2...").arg(pluginId, version));
    return true;
}

void PluginManagerDialog::startDownloadFromSelection(const QJsonObject& plugin, bool isRetry) {
    const QString pluginId = plugin.value("id").toString();
    const QJsonObject latestVersionObj = plugin.value("latestVersion").toObject();
    const QString version = latestVersionObj.value("version").toString();
    const QString checksum = latestVersionObj.value("checksum_sha256").toString(
        latestVersionObj.value("checksumSha256").toString()).trimmed();
    const QString downloadUrl = latestVersionObj.value("downloadUrl").toString().trimmed();
    if (pluginId.isEmpty() || version.isEmpty()) {
        updateOnlineStatus("Selected plugin is missing id/version.");
        return;
    }
    startDownloadForPluginVersion(pluginId, version, isRetry, checksum, downloadUrl);
}

void PluginManagerDialog::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (!m_downloadProgressDialog) {
        return;
    }

    if (bytesTotal <= 0) {
        m_downloadProgressDialog->setMaximum(0);
        m_downloadProgressDialog->setValue(0);
        return;
    }

    m_downloadProgressDialog->setMaximum(100);
    const int percent = static_cast<int>((100.0 * bytesReceived) / bytesTotal);
    m_downloadProgressDialog->setValue(percent);
}

void PluginManagerDialog::onDownloadFinished() {
    if (!m_activeDownloadReply) {
        return;
    }

    if (m_activeDownloadReply->error() != QNetworkReply::NoError) {
        const QString err = m_activeDownloadReply->errorString();
        if (m_activeDownloadFile) {
            m_activeDownloadFile->close();
            QFile::remove(m_activeDownloadFile->fileName());
        }
        updateOnlineStatus(QString("Download failed: %1").arg(err));
        m_onlineRetryBtn->setEnabled(true);
        resetDownloadState();
        if (m_processingUpdateQueue) {
            m_processingUpdateQueue = false;
            m_pendingUpdates.clear();
            updateUpdatesStatus("Update queue stopped after a download failure.");
        }
        return;
    }

    if (m_activeDownloadFile) {
        m_activeDownloadFile->write(m_activeDownloadReply->readAll());
        m_activeDownloadFile->close();
    }

    const QString headerChecksum =
        QString::fromLatin1(m_activeDownloadReply->rawHeader("x-checksum-sha256")).trimmed().toLower();
    const QString metadataChecksum = m_lastExpectedChecksum.trimmed().toLower();
    if (!headerChecksum.isEmpty() && !metadataChecksum.isEmpty() && headerChecksum != metadataChecksum) {
        updateOnlineStatus(QString("Download failed: checksum header mismatch with metadata (%1 vs %2).")
                               .arg(headerChecksum, metadataChecksum));
        if (m_activeDownloadFile) {
            QFile::remove(m_activeDownloadFile->fileName());
        }
        m_onlineRetryBtn->setEnabled(true);
        resetDownloadState();
        if (m_processingUpdateQueue) {
            m_processingUpdateQueue = false;
            m_pendingUpdates.clear();
            updateUpdatesStatus("Update queue stopped: inconsistent checksum metadata.");
        }
        return;
    }
    const QString expectedChecksum = !headerChecksum.isEmpty() ? headerChecksum : metadataChecksum;
    if (expectedChecksum.isEmpty()) {
        updateOnlineStatus("Download failed: checksum is missing from both response header and metadata.");
        if (m_activeDownloadFile) {
            QFile::remove(m_activeDownloadFile->fileName());
        }
        m_onlineRetryBtn->setEnabled(true);
        resetDownloadState();
        if (m_processingUpdateQueue) {
            m_processingUpdateQueue = false;
            m_pendingUpdates.clear();
            updateUpdatesStatus("Update queue stopped: missing checksum.");
        }
        return;
    }

    QString checksumError;
    const QString actualChecksum = computeFileSha256(m_lastDownloadPath, &checksumError).toLower();
    if (actualChecksum.isEmpty()) {
        updateOnlineStatus(QString("Download failed: %1").arg(checksumError));
        if (m_activeDownloadFile) {
            QFile::remove(m_activeDownloadFile->fileName());
        }
        m_onlineRetryBtn->setEnabled(true);
        resetDownloadState();
        if (m_processingUpdateQueue) {
            m_processingUpdateQueue = false;
            m_pendingUpdates.clear();
            updateUpdatesStatus("Update queue stopped: checksum compute failure.");
        }
        return;
    }

    if (actualChecksum != expectedChecksum) {
        if (m_activeDownloadFile) {
            QFile::remove(m_activeDownloadFile->fileName());
        }
        updateOnlineStatus(QString("Checksum mismatch. Expected %1, got %2. File removed.")
                               .arg(expectedChecksum, actualChecksum));
        m_onlineRetryBtn->setEnabled(true);
        resetDownloadState();
        if (m_processingUpdateQueue) {
            m_processingUpdateQueue = false;
            m_pendingUpdates.clear();
            updateUpdatesStatus("Update queue stopped: checksum mismatch.");
        }
        return;
    }

    QString installedPath;
    QString installError;
    const bool installed = PluginPackageInstaller::installPackage(
        m_lastDownloadPath, &installedPath, &installError);
    if (installed) {
        updateOnlineStatus(QString("Installed safely: %1").arg(installedPath));
        updateUpdatesStatus(QString("Installed safely: %1").arg(installedPath));
        refreshPluginList();
        refreshUpdatesList();
    } else {
        updateOnlineStatus(QString("Downloaded and verified, but install failed: %1").arg(installError));
        updateUpdatesStatus(QString("Install failed: %1").arg(installError));
    }
    m_onlineRetryBtn->setEnabled(false);
    resetDownloadState();
    if (m_processingUpdateQueue) {
        processNextQueuedUpdate();
    }
}

void PluginManagerDialog::onDownloadCanceled() {
    if (m_activeDownloadReply) {
        m_activeDownloadReply->abort();
    }
    if (m_processingUpdateQueue) {
        m_processingUpdateQueue = false;
        m_pendingUpdates.clear();
        updateUpdatesStatus("Update queue canceled.");
    }
}

void PluginManagerDialog::resetDownloadState() {
    if (m_downloadProgressDialog) {
        m_downloadProgressDialog->close();
        m_downloadProgressDialog->deleteLater();
        m_downloadProgressDialog = nullptr;
    }

    if (m_activeDownloadReply) {
        m_activeDownloadReply->deleteLater();
        m_activeDownloadReply = nullptr;
    }

    if (m_activeDownloadFile) {
        if (m_activeDownloadFile->isOpen()) {
            m_activeDownloadFile->close();
        }
        m_activeDownloadFile->deleteLater();
        m_activeDownloadFile = nullptr;
    }
    m_lastExpectedChecksum.clear();

    m_onlineDownloadBtn->setEnabled(m_onlinePluginList && m_onlinePluginList->currentItem());
}
