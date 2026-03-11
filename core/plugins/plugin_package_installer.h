#ifndef PLUGIN_PACKAGE_INSTALLER_H
#define PLUGIN_PACKAGE_INSTALLER_H

#include <QString>

class PluginPackageInstaller {
public:
    static QString userPluginDirectory();
    static bool installPackage(const QString& packagePath,
                               QString* outInstalledPath,
                               QString* outError);
};

#endif // PLUGIN_PACKAGE_INSTALLER_H
