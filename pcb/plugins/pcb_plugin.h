#ifndef PCBPLUGIN_H
#define PCBPLUGIN_H

#include <QtPlugin>
#include <QString>

class PCBPlugin {
public:
    virtual ~PCBPlugin() = default;

    // Plugin information
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    virtual QString description() const = 0;

    // Plugin lifecycle
    virtual bool initialize() = 0;
    virtual bool shutdown() = 0;

    // Optional: register custom items/tools
    virtual void registerItems() {}
    virtual void registerTools() {}
};

Q_DECLARE_INTERFACE(PCBPlugin, "org.viora_eda.PCBPlugin/1.0")

#endif // PCBPLUGIN_H
