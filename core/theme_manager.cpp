#include "theme_manager.h"
#include <QApplication>

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    static bool firstCall = true;
    if (firstCall && qApp) {
        instance.m_theme->applyToApplication();
        firstCall = false;
    }
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
    
    // Apply globally
    m_theme->applyToApplication();
    
    emit themeChanged();
}

void ThemeManager::setTheme(PCBTheme* theme) {
    if (theme != m_theme) {
        delete m_theme;
        m_theme = theme;
        
        // Apply globally
        m_theme->applyToApplication();
        
        emit themeChanged();
    }
}
