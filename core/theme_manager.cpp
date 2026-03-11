#include "theme_manager.h"

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

PCBTheme* ThemeManager::theme() {
    return instance().currentTheme();
}

ThemeManager::ThemeManager()
    : m_theme(new PCBTheme(PCBTheme::Light)) {
}

ThemeManager::~ThemeManager() {
    delete m_theme;
}

void ThemeManager::setTheme(PCBTheme::ThemeType type) {
    delete m_theme;
    m_theme = new PCBTheme(type);
    emit themeChanged();
}

void ThemeManager::setTheme(PCBTheme* theme) {
    if (theme != m_theme) {
        delete m_theme;
        m_theme = theme;
        emit themeChanged();
    }
}
