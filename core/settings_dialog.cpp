#include "settings_dialog.h"
#include "config_manager.h"
#include "theme_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QIcon>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    setupUI();
    loadSettings();
}

SettingsDialog::~SettingsDialog() {}

void SettingsDialog::setupUI() {
    setWindowTitle("viospice Settings");
    resize(750, 550);

    PCBTheme* theme = ThemeManager::theme();
    QString bg = theme ? theme->windowBackground().name() : "#121215";
    QString panelBg = theme ? theme->panelBackground().name() : "#1e1e23";
    QString border = theme ? theme->panelBorder().name() : "#2d2d32";
    QString text = theme ? theme->textColor().name() : "#dcdce6";
    QString textSec = theme ? theme->textSecondary().name() : "#8c8c96";
    QString accent = theme ? theme->accentColor().name() : "#3b82f6";
    QString itemHoverBg = theme ? (theme->type() == PCBTheme::Light ? "#f1f5f9" : "#1e1e23") : "#1e1e23";
    QString inputFocusBg = theme ? (theme->type() == PCBTheme::Light ? "#ffffff" : "#25252b") : "#25252b";
    QString accentRgba = theme ? QString("rgba(%1, %2, %3, 0.1)").arg(theme->accentColor().red()).arg(theme->accentColor().green()).arg(theme->accentColor().blue()) : "rgba(59, 130, 246, 0.1)";

    if (theme) {
        theme->applyToWidget(this);
    }

    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Sidebar Navigation ---
    QWidget* sidebar = new QWidget(this);
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet(QString(
        "QWidget { background-color: %1; border-right: 1px solid %2; }"
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { padding: 10px 10px; margin: 4px 10px; color: %3; font-weight: 600; border-radius: 6px; border-left: 3px solid transparent; }"
        "QListWidget::item:hover { background-color: %4; color: %5; }"
        "QListWidget::item:selected { background-color: %6; color: %7; border-left-color: %7; font-weight: bold; }"
    ).arg(bg, border, textSec, itemHoverBg, text, accentRgba, accent));

    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 20, 0, 20);

    QLabel* titleLabel = new QLabel("PREFERENCES", sidebar);
    titleLabel->setStyleSheet(QString("color: %1; font-weight: 800; font-size: 10px; letter-spacing: 2px; padding-left: 20px; margin-bottom: 10px; border: none;").arg(textSec));
    sidebarLayout->addWidget(titleLabel);

    m_navMenu = new QListWidget(sidebar);
    m_navMenu->setIconSize(QSize(18, 18));
    m_navMenu->setSpacing(10);
    
    auto addNavItem = [&](const QString& txt, const QString& iconPath) {
        QListWidgetItem* item = new QListWidgetItem(QIcon(iconPath), txt);
        m_navMenu->addItem(item);
    };

    addNavItem("General", ":/icons/tool_gear.svg");
    addNavItem("Simulator", ":/icons/tool_run.svg");
    addNavItem("Libraries", ":/icons/folder_open.svg");
    addNavItem("AI Assistant", ":/icons/tool_search.svg");
    
    sidebarLayout->addWidget(m_navMenu);
    mainLayout->addWidget(sidebar);

    // --- Content Area ---
    QWidget* contentArea = new QWidget(this);
    contentArea->setStyleSheet(QString(
        "QWidget { background-color: %1; color: %2; }"
        "QGroupBox { font-weight: bold; padding-top: 15px; margin-top: 10px; border: 1px solid %3; border-radius: 4px; color: %2; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 5px; color: %4; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTextEdit { background: %5; border: 1px solid %3; border-radius: 4px; padding: 6px; color: %2; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QTextEdit:focus { border: 1px solid %4; background: %6; }"
        "QLabel { color: %2; border: none; }"
        "QCheckBox { color: %2; border: none; }"
    ).arg(bg, text, border, accent, panelBg, inputFocusBg));
    QVBoxLayout* contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(20, 20, 20, 20);

    m_pagesStack = new QStackedWidget(contentArea);

    // Page 1: General
    QWidget* pageGeneral = new QWidget();
    QVBoxLayout* layGeneral = new QVBoxLayout(pageGeneral);
    layGeneral->setSpacing(15);
    
    QGroupBox* grpAppearance = new QGroupBox("Appearance");
    QFormLayout* formApp = new QFormLayout(grpAppearance);
    m_themeCombo = new QComboBox();
    m_themeCombo->addItems({"Dark", "Engineering", "Light"});
    formApp->addRow("Application Theme:", m_themeCombo);
    layGeneral->addWidget(grpAppearance);

    QGroupBox* grpBehavior = new QGroupBox("Editor Behavior");
    QVBoxLayout* layBeh = new QVBoxLayout(grpBehavior);
    layBeh->setSpacing(10);

    m_autoSaveCheck = new QCheckBox("Enable Auto-Save Background Tasks");
    QHBoxLayout* saveIntervalLayout = new QHBoxLayout();
    saveIntervalLayout->addWidget(new QLabel("Auto-Save Interval:"));
    m_autoSaveSpin = new QSpinBox();
    m_autoSaveSpin->setRange(1, 60);
    m_autoSaveSpin->setSuffix(" min");
    saveIntervalLayout->addWidget(m_autoSaveSpin);
    saveIntervalLayout->addStretch();

    m_snapGridCheck = new QCheckBox("Snap to Grid by Default");
    m_autoFocusCrossProbeCheck = new QCheckBox("Auto-focus viewport on cross-probe clicks");
    m_realtimeWireUpdateCheck = new QCheckBox("Real-time Wire Updates (High Performance)");

    layBeh->addWidget(m_autoSaveCheck);
    layBeh->addLayout(saveIntervalLayout);
    layBeh->addWidget(m_snapGridCheck);
    layBeh->addWidget(m_autoFocusCrossProbeCheck);
    layBeh->addWidget(m_realtimeWireUpdateCheck);
    layGeneral->addWidget(grpBehavior);
    layGeneral->addStretch();
    m_pagesStack->addWidget(pageGeneral);

    // Page 2: Simulator
    QWidget* pageSim = new QWidget();
    QVBoxLayout* laySim = new QVBoxLayout(pageSim);
    laySim->setSpacing(15);

    QGroupBox* grpSolver = new QGroupBox("Solver & Algorithms");
    QFormLayout* formSolver = new QFormLayout(grpSolver);
    m_solverCombo = new QComboBox();
    m_solverCombo->addItems({"SparseLU", "KLU (SuiteSparse)", "Dense"});
    m_integrationCombo = new QComboBox();
    m_integrationCombo->addItems({"Trapezoidal", "Gear (2nd Order)"});
    m_maxIterSpin = new QSpinBox();
    m_maxIterSpin->setRange(10, 1000);
    formSolver->addRow("Linear Solver:", m_solverCombo);
    formSolver->addRow("Integration Method:", m_integrationCombo);
    formSolver->addRow("Max NR Iterations:", m_maxIterSpin);
    laySim->addWidget(grpSolver);

    QGroupBox* grpTols = new QGroupBox("Tolerances & Accuracy");
    QFormLayout* formTols = new QFormLayout(grpTols);
    auto setupDoubleSpin = [](QDoubleSpinBox* s) {
        s->setRange(1e-20, 1.0);
        s->setDecimals(15);
        s->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    };
    m_reltolSpin = new QDoubleSpinBox(); setupDoubleSpin(m_reltolSpin);
    m_abstolSpin = new QDoubleSpinBox(); setupDoubleSpin(m_abstolSpin);
    m_vntolSpin = new QDoubleSpinBox();  setupDoubleSpin(m_vntolSpin);
    m_gminSpin = new QDoubleSpinBox();   setupDoubleSpin(m_gminSpin);
    formTols->addRow("RELTOL (Relative Tolerance):", m_reltolSpin);
    formTols->addRow("ABSTOL (Absolute Current):", m_abstolSpin);
    formTols->addRow("VNTOL (Absolute Voltage):", m_vntolSpin);
    formTols->addRow("GMIN (Shunt Conductance):", m_gminSpin);
    laySim->addWidget(grpTols);
    laySim->addStretch();
    m_pagesStack->addWidget(pageSim);

    // Page 3: Libraries
    QWidget* pageLibs = new QWidget();
    QVBoxLayout* layLibs = new QVBoxLayout(pageLibs);
    QGroupBox* grpSyms = new QGroupBox("Symbol Library Paths");
    QVBoxLayout* laySyms = new QVBoxLayout(grpSyms);
    m_symbolPathsEdit = new QTextEdit();
    laySyms->addWidget(m_symbolPathsEdit);
    layLibs->addWidget(grpSyms);

    QGroupBox* grpModels = new QGroupBox("SPICE Model Paths");
    QVBoxLayout* layGrpModels = new QVBoxLayout(grpModels);
    m_modelPathsEdit = new QTextEdit();
    layGrpModels->addWidget(m_modelPathsEdit);
    layLibs->addWidget(grpModels);
    layLibs->addStretch();
    m_pagesStack->addWidget(pageLibs);

    // Page 4: AI Assistant
    QWidget* pageAI = new QWidget();
    QVBoxLayout* layAI = new QVBoxLayout(pageAI);
    QGroupBox* grpAPI = new QGroupBox("Gemini API Configuration");
    QFormLayout* formAPI = new QFormLayout(grpAPI);
    m_geminiKeyEdit = new QLineEdit();
    m_geminiKeyEdit->setEchoMode(QLineEdit::Password);
    formAPI->addRow("API Key:", m_geminiKeyEdit);
    layAI->addWidget(grpAPI);
    
    QLabel* aiDesc = new QLabel(
        "viospice uses Gemini 2.5 Flash Lite to provide AI assistance for circuit generation, "
        "ERC debugging, and FluxScript authoring."
    );
    aiDesc->setWordWrap(true);
    aiDesc->setStyleSheet("color: #a1a1aa; margin-top: 10px;");
    layAI->addWidget(aiDesc);
    layAI->addStretch();
    m_pagesStack->addWidget(pageAI);

    contentLayout->addWidget(m_pagesStack);

    // Bottom Action Buttons
    QFrame* actionFrame = new QFrame();
    actionFrame->setStyleSheet(QString("border-top: 1px solid %1; background: transparent; padding-top: 10px;").arg(border));
    QHBoxLayout* btnLayout = new QHBoxLayout(actionFrame);
    
    QPushButton* okBtn = new QPushButton("Save Preferences");
    okBtn->setStyleSheet(QString("background-color: %1; color: white; border-radius: 4px; padding: 8px 18px; font-weight: bold;").arg(accent));
    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet(QString("background-color: %1; color: %2; border-radius: 4px; padding: 8px 18px;").arg(border, text));
    QPushButton* resetBtn = new QPushButton("Reset to Defaults");
    resetBtn->setStyleSheet(QString("background-color: transparent; color: %1; border: 1px solid %2; border-radius: 4px; padding: 8px 18px;").arg(textSec, border));
    
    connect(okBtn, &QPushButton::clicked, this, &SettingsDialog::onAccept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    
    btnLayout->addWidget(resetBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    contentLayout->addWidget(actionFrame);
    
    mainLayout->addWidget(contentArea);
    connect(m_navMenu, &QListWidget::currentRowChanged, m_pagesStack, &QStackedWidget::setCurrentIndex);
    m_navMenu->setCurrentRow(0);
}

void SettingsDialog::loadSettings() {
    auto& config = ConfigManager::instance();
    m_themeCombo->setCurrentText(config.currentTheme());
    m_autoSaveCheck->setChecked(config.autoSaveEnabled());
    m_autoSaveSpin->setValue(config.autoSaveInterval());
    m_snapGridCheck->setChecked(config.snapToGrid());
    m_autoFocusCrossProbeCheck->setChecked(config.autoFocusOnCrossProbe());
    m_realtimeWireUpdateCheck->setChecked(config.isRealtimeWireUpdateEnabled());
    
    m_solverCombo->setCurrentText(config.defaultSolver());
    m_integrationCombo->setCurrentText(config.integrationMethod());
    m_reltolSpin->setValue(config.reltol());
    m_abstolSpin->setValue(config.abstol());
    m_vntolSpin->setValue(config.vntol());
    m_gminSpin->setValue(config.gmin());
    m_maxIterSpin->setValue(config.maxIterations());

    m_geminiKeyEdit->setText(config.geminiApiKey());
    m_symbolPathsEdit->setPlainText(config.symbolPaths().join("\n"));
    m_modelPathsEdit->setPlainText(config.modelPaths().join("\n"));
}

void SettingsDialog::onAccept() {
    auto& config = ConfigManager::instance();
    config.setCurrentTheme(m_themeCombo->currentText());
    config.setAutoSaveEnabled(m_autoSaveCheck->isChecked());
    config.setAutoSaveInterval(m_autoSaveSpin->value());
    config.setSnapToGrid(m_snapGridCheck->isChecked());
    config.setAutoFocusOnCrossProbe(m_autoFocusCrossProbeCheck->isChecked());
    config.setRealtimeWireUpdateEnabled(m_realtimeWireUpdateCheck->isChecked());
    
    config.setDefaultSolver(m_solverCombo->currentText());
    config.setIntegrationMethod(m_integrationCombo->currentText());
    config.setReltol(m_reltolSpin->value());
    config.setAbstol(m_abstolSpin->value());
    config.setVntol(m_vntolSpin->value());
    config.setGmin(m_gminSpin->value());
    config.setMaxIterations(m_maxIterSpin->value());

    config.setGeminiApiKey(m_geminiKeyEdit->text());
    config.setSymbolPaths(m_symbolPathsEdit->toPlainText().split('\n', Qt::SkipEmptyParts));
    config.setModelPaths(m_modelPathsEdit->toPlainText().split('\n', Qt::SkipEmptyParts));
    
    config.save();
    accept();
}
