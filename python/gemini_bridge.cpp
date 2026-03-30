#include "gemini_bridge.h"
#include "../core/theme_manager.h"
#include <QColor>
#include <QPalette>
#include <QDebug>

GeminiBridge::GeminiBridge(QObject* parent) : QObject(parent) {
    m_availableModels << m_currentModel;
}

QString GeminiBridge::textColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->textColor().name() : "#000000";
}

QString GeminiBridge::secondaryColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->textSecondary().name() : "#888888";
}

QString GeminiBridge::accentColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->accentColor().name() : "#3b82f6";
}

QString GeminiBridge::backgroundColor() const {
    PCBTheme* theme = ThemeManager::theme();
    return theme ? theme->windowBackground().name() : "#ffffff";
}

QString GeminiBridge::glassBackground() const {
    PCBTheme* theme = ThemeManager::theme();
    if (!theme) return "rgba(255, 255, 255, 0.95)";
    
    if (theme->type() == PCBTheme::Light) {
        return "rgba(255, 255, 255, 0.95)";
    } else {
        return "rgba(13, 17, 23, 0.92)";
    }
}

void GeminiBridge::setCurrentModel(const QString& model) {
    if (m_currentModel != model) {
        m_currentModel = model;
        emit currentModelChanged();
    }
}

void GeminiBridge::setCurrentMode(const QString& mode) {
    if (m_currentMode != mode) {
        m_currentMode = mode;
        emit currentModeChanged();
    }
}

void GeminiBridge::sendMessage(const QString& text) {
    qDebug() << "[GeminiBridge] sendMessage:" << text;
    emit sendMessageRequested(text);
}

void GeminiBridge::clearHistory() {
    emit clearHistoryRequested();
}

void GeminiBridge::stopRun() {
    emit stopRequested();
}

void GeminiBridge::refreshModels() {
    emit refreshModelsRequested();
}

void GeminiBridge::updateMessages(const QVariantList& msgs) {
    m_messages = msgs;
    emit messagesChanged();
}

void GeminiBridge::setWorking(bool working, const QString& thinking) {
    if (m_isWorking != working || m_thinkingText != thinking) {
        m_isWorking = working;
        m_thinkingText = thinking;
        emit isWorkingChanged();
        emit thinkingTextChanged();
    }
}

void GeminiBridge::updateStatus(const QString& status) {
    if (m_thinkingText != status) {
        m_thinkingText = status;
        emit thinkingTextChanged();
    }
}

void GeminiBridge::updateAvailableModels(const QStringList& models) {
    if (m_availableModels != models) {
        m_availableModels = models;
        emit availableModelsChanged();
    }
}

void GeminiBridge::updateTitle(const QString& title) {
    if (m_conversationTitle != title) {
        m_conversationTitle = title;
        emit conversationTitleChanged();
    }
}

void GeminiBridge::closePanel() {
    emit closeRequested();
}

void GeminiBridge::showHistory() {
    emit showHistoryRequested();
}
