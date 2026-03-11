#include "config_manager.h"

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager()
    : m_settings("viospice", "Settings") {
    load();
}

bool ConfigManager::autoSaveEnabled() const { return m_autoSaveEnabled; }
void ConfigManager::setAutoSaveEnabled(bool enabled) { m_autoSaveEnabled = enabled; }

int ConfigManager::autoSaveInterval() const { return m_autoSaveInterval; }
void ConfigManager::setAutoSaveInterval(int minutes) { m_autoSaveInterval = minutes; }

QString ConfigManager::currentTheme() const { return m_currentTheme; }
void ConfigManager::setCurrentTheme(const QString& themeName) { m_currentTheme = themeName; }

QString ConfigManager::geminiApiKey() const { return m_geminiApiKey; }
void ConfigManager::setGeminiApiKey(const QString& key) { 
    m_geminiApiKey = key; 
    m_settings.setValue("api/geminiKey", m_geminiApiKey);
    m_settings.sync();
}

QStringList ConfigManager::symbolPaths() const { return m_symbolPaths; }
void ConfigManager::setSymbolPaths(const QStringList& paths) { m_symbolPaths = paths; }

QStringList ConfigManager::modelPaths() const { return m_modelPaths; }
void ConfigManager::setModelPaths(const QStringList& paths) { m_modelPaths = paths; }

// Simulator Settings
QString ConfigManager::defaultSolver() const { return m_defaultSolver; }
void ConfigManager::setDefaultSolver(const QString& solver) { m_defaultSolver = solver; }
QString ConfigManager::integrationMethod() const { return m_integrationMethod; }
void ConfigManager::setIntegrationMethod(const QString& method) { m_integrationMethod = method; }
double ConfigManager::reltol() const { return m_reltol; }
void ConfigManager::setReltol(double val) { m_reltol = val; }
double ConfigManager::abstol() const { return m_abstol; }
void ConfigManager::setAbstol(double val) { m_abstol = val; }
double ConfigManager::vntol() const { return m_vntol; }
void ConfigManager::setVntol(double val) { m_vntol = val; }
double ConfigManager::gmin() const { return m_gmin; }
void ConfigManager::setGmin(double val) { m_gmin = val; }
int ConfigManager::maxIterations() const { return m_maxIterations; }
void ConfigManager::setMaxIterations(int val) { m_maxIterations = val; }

void ConfigManager::saveWindowState(const QString& name, const QByteArray& geometry, const QByteArray& state) {
    m_settings.setValue("ui/" + name + "/geometry", geometry);
    m_settings.setValue("ui/" + name + "/state", state);
    m_settings.sync();
}

QByteArray ConfigManager::windowGeometry(const QString& name) const {
    return m_settings.value("ui/" + name + "/geometry").toByteArray();
}

QByteArray ConfigManager::windowState(const QString& name) const {
    return m_settings.value("ui/" + name + "/state").toByteArray();
}

bool ConfigManager::snapToGrid() const { return m_snapToGrid; }
void ConfigManager::setSnapToGrid(bool enabled) { m_snapToGrid = enabled; }
bool ConfigManager::autoFocusOnCrossProbe() const { return m_autoFocusOnCrossProbe; }
void ConfigManager::setAutoFocusOnCrossProbe(bool enabled) { m_autoFocusOnCrossProbe = enabled; }

void ConfigManager::setToolProperty(const QString& toolName, const QString& propName, const QVariant& value) {
    m_settings.setValue(QString("tools/%1/%2").arg(toolName, propName), value);
    m_settings.sync();
}

QVariant ConfigManager::toolProperty(const QString& toolName, const QString& propName, const QVariant& defaultValue) const {
    return m_settings.value(QString("tools/%1/%2").arg(toolName, propName), defaultValue);
}

bool ConfigManager::isFeatureEnabled(const QString& name, bool defaultVal) const {
    return m_settings.value("features/" + name, defaultVal).toBool();
}

void ConfigManager::setFeatureEnabled(const QString& name, bool enabled) {
    m_settings.setValue("features/" + name, enabled);
    m_settings.sync();
}

void ConfigManager::save() {
    m_settings.setValue("autoSave/enabled", m_autoSaveEnabled);
    m_settings.setValue("autoSave/interval", m_autoSaveInterval);
    m_settings.setValue("appearance/theme", m_currentTheme);
    m_settings.setValue("api/geminiKey", m_geminiApiKey);
    m_settings.setValue("libraries/symbols", m_symbolPaths);
    m_settings.setValue("libraries/models", m_modelPaths);
    m_settings.setValue("editor/snapToGrid", m_snapToGrid);
    m_settings.setValue("editor/autoFocusOnCrossProbe", m_autoFocusOnCrossProbe);
    m_settings.setValue("editor/realtimeWireUpdate", m_realtimeWireUpdate);
    
    m_settings.setValue("simulator/defaultSolver", m_defaultSolver);
    m_settings.setValue("simulator/integrationMethod", m_integrationMethod);
    m_settings.setValue("simulator/reltol", m_reltol);
    m_settings.setValue("simulator/abstol", m_abstol);
    m_settings.setValue("simulator/vntol", m_vntol);
    m_settings.setValue("simulator/gmin", m_gmin);
    m_settings.setValue("simulator/maxIterations", m_maxIterations);
    
    m_settings.sync();
}

void ConfigManager::load() {
    m_autoSaveEnabled = m_settings.value("autoSave/enabled", true).toBool();
    m_autoSaveInterval = m_settings.value("autoSave/interval", 5).toInt();
    m_currentTheme = m_settings.value("appearance/theme", "Dark").toString();
    m_geminiApiKey = m_settings.value("api/geminiKey", "").toString();
    m_symbolPaths = m_settings.value("libraries/symbols").toStringList();
    m_modelPaths = m_settings.value("libraries/models").toStringList();
    m_snapToGrid = m_settings.value("editor/snapToGrid", true).toBool();
    m_autoFocusOnCrossProbe = m_settings.value("editor/autoFocusOnCrossProbe", false).toBool();
    m_realtimeWireUpdate = m_settings.value("editor/realtimeWireUpdate", true).toBool();

    m_defaultSolver = m_settings.value("simulator/defaultSolver", "SparseLU").toString();
    m_integrationMethod = m_settings.value("simulator/integrationMethod", "Trapezoidal").toString();
    m_reltol = m_settings.value("simulator/reltol", 1e-3).toDouble();
    m_abstol = m_settings.value("simulator/abstol", 1e-12).toDouble();
    m_vntol = m_settings.value("simulator/vntol", 1e-6).toDouble();
    m_gmin = m_settings.value("simulator/gmin", 1e-12).toDouble();
    m_maxIterations = m_settings.value("simulator/maxIterations", 100).toInt();
}
