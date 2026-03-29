#include "gemini_instructions_dialog.h"
#include "../core/config_manager.h"
#include "theme_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

GeminiInstructionsDialog::GeminiInstructionsDialog(const QString& projectPath, QWidget* parent)
    : QDialog(parent), m_projectPath(projectPath) {
    
    setWindowTitle("Viora AI Custom Instructions");
    resize(600, 450);

    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->panelBackground().name() : "#ffffff";
    QString fg = theme ? theme->textColor().name() : "#000000";
    QString border = theme ? theme->panelBorder().name() : "#cccccc";

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    QLabel* title = new QLabel("Configure AI Personality & Rules", this);
    title->setStyleSheet(QString("font-weight: bold; font-size: 14px; color: %1;").arg(fg));
    layout->addWidget(title);

    QLabel* desc = new QLabel("Add custom instructions that will be appended to every AI request. Use this to set formatting rules, tone, or project-specific knowledge.", this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color: %1; font-size: 11px;").arg(theme ? theme->textSecondary().name() : "#888"));
    layout->addWidget(desc);

    QHBoxLayout* scopeLayout = new QHBoxLayout();
    QLabel* scopeLabel = new QLabel("Instruction Scope:", this);
    scopeLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(fg));
    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem("Global (All Projects)");
    m_scopeCombo->addItem("Project Specific");
    m_scopeCombo->setFixedHeight(30);
    m_scopeCombo->setStyleSheet(QString("QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 0 8px; }")
        .arg(bg, fg, border));
    
    scopeLayout->addWidget(scopeLabel);
    scopeLayout->addWidget(m_scopeCombo, 1);
    layout->addLayout(scopeLayout);

    m_editor = new QPlainTextEdit(this);
    m_editor->setPlaceholderText("Enter custom instructions here...\nExample: 'Always explain the physics of the circuit.' or 'Use technical jargon.'");
    m_editor->setStyleSheet(QString("QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 8px; font-family: 'JetBrains Mono', 'Consolas', monospace; font-size: 12px; }")
        .arg(bg, fg, border));
    layout->addWidget(m_editor, 1);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    QPushButton* cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setFixedHeight(32);
    cancelBtn->setStyleSheet(QString("QPushButton { background: transparent; color: %1; border: 1px solid %2; border-radius: 4px; padding: 0 20px; } QPushButton:hover { background: %2; }")
        .arg(fg, border));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    QPushButton* saveBtn = new QPushButton("Save Instructions", this);
    saveBtn->setFixedHeight(32);
    saveBtn->setStyleSheet("QPushButton { background: #238636; color: white; border: none; border-radius: 4px; padding: 0 24px; font-weight: bold; } QPushButton:hover { background: #2ea043; }");
    connect(saveBtn, &QPushButton::clicked, this, &GeminiInstructionsDialog::onSaveClicked);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(saveBtn);
    layout->addLayout(btnLayout);

    // Initial Load
    m_globalCache = ConfigManager::instance().geminiGlobalInstructions();
    m_projectCache = "";
    if (!m_projectPath.isEmpty()) {
        QFileInfo info(m_projectPath);
        QString pFile = info.absolutePath() + "/.gemini/custom_instructions.txt";
        QFile file(pFile);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_projectCache = QString::fromUtf8(file.readAll());
        }
    }

    if (m_projectPath.isEmpty()) {
        m_scopeCombo->removeItem(1);
    } else {
        m_scopeCombo->setCurrentIndex(1); // Default to project if available
    }

    loadInstructions();
    connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GeminiInstructionsDialog::onScopeChanged);
}

void GeminiInstructionsDialog::loadInstructions() {
    m_loading = true;
    if (m_scopeCombo->currentIndex() == 0) {
        m_editor->setPlainText(m_globalCache);
    } else {
        m_editor->setPlainText(m_projectCache);
    }
    m_loading = false;
}

void GeminiInstructionsDialog::onScopeChanged(int) {
    if (m_loading) return;
    loadInstructions();
}

void GeminiInstructionsDialog::onSaveClicked() {
    if (m_scopeCombo->currentIndex() == 0) {
        m_globalCache = m_editor->toPlainText().trimmed();
        ConfigManager::instance().setGeminiGlobalInstructions(m_globalCache);
    } else {
        m_projectCache = m_editor->toPlainText().trimmed();
        if (!m_projectPath.isEmpty()) {
            QFileInfo info(m_projectPath);
            QString pDir = info.absolutePath() + "/.gemini";
            QDir().mkpath(pDir);
            QFile file(pDir + "/custom_instructions.txt");
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << m_projectCache;
            }
        }
    }
    accept();
}
