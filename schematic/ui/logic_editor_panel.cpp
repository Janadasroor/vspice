#include "logic_editor_panel.h"
#include <QGraphicsScene>
#include "flux_code_editor.h"
#include "mini_scope_widget.h"
#include "../items/smart_signal_item.h"
#include "config_manager.h"
#include "flux_python.h"
#include "../../python/cpp/core/flux_script_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QGroupBox>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QListWidget>
#include <QTextEdit>
#include <QDockWidget>
#include <QSplitter>
#include <QShortcut>
#include <QMenuBar>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include "jit_context_manager.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextBrowser>
#include <QVariantMap>
#include "diagnostics/debugger.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QElapsedTimer>
#include "visual_pin_mapper.h"
#include "../../python/cpp/gemini/gemini_panel.h"
#include <QFileSystemWatcher>

LogicEditorPanel::LogicEditorPanel(QGraphicsScene* scene, NetManager* netManager, QWidget* parent)
    : QMainWindow(parent, Qt::Window), m_scene(scene), m_netManager(netManager) {
    qDebug() << "[LogicEditorPanel] Initializing...";
    
    setWindowTitle("viospice Logic IDE");
    resize(1100, 700);
    
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(500);
    connect(m_previewTimer, &QTimer::timeout, this, &LogicEditorPanel::updatePreview);

    qDebug() << "[LogicEditorPanel] Setting up UI...";
    setupUi();
    qDebug() << "[LogicEditorPanel] Setting up menus...";
    setupMenus();
    qDebug() << "[LogicEditorPanel] Creating shortcuts...";
    createShortcuts();
    qDebug() << "[LogicEditorPanel] Refreshing templates...";
    refreshTemplates();

    // Setup Template Watcher
    qDebug() << "[LogicEditorPanel] Setting up template watcher...";
    m_templateWatcher = new QFileSystemWatcher(this);
    QString templatesPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../python/templates");
    if (!QFile::exists(templatesPath)) {
        templatesPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("python/templates");
    }
    QDir().mkpath(templatesPath); // Ensure it exists before watching
    m_templateWatcher->addPath(templatesPath);
    connect(m_templateWatcher, &QFileSystemWatcher::directoryChanged, this, &LogicEditorPanel::refreshTemplates);
}

void LogicEditorPanel::setScene(QGraphicsScene* scene, NetManager* netManager) {
    m_scene = scene;
    m_netManager = netManager;
    
    if (m_editor) m_editor->setScene(scene, netManager);
    if (m_geminiPanel) m_geminiPanel->setNetManager(netManager);
}

void LogicEditorPanel::refreshTemplates() {
    m_templateList->clear();
    
    // Look for templates in python/templates
    QString templatesPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../python/templates");
    if (!QFile::exists(templatesPath)) {
        templatesPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("python/templates");
    }

    QDir dir(templatesPath);
    QStringList files = dir.entryList({"*.py"}, QDir::Files);
    
    for (const QString& file : files) {
        QString displayName = file.section('.', 0, 0).replace('_', ' ').toUpper();
        auto* item = new QListWidgetItem(displayName);
        item->setData(Qt::UserRole, dir.absoluteFilePath(file));
        item->setToolTip("Double-click to insert: " + file);
        m_templateList->addItem(item);
    }
}

void LogicEditorPanel::onTemplateDoubleClicked(QListWidgetItem* item) {
    QString path = item->data(Qt::UserRole).toString();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QTextStream(&file).readAll();
        m_editor->setPlainText(content);
        updatePreview();
        m_statusLabel->setText("Template loaded: " + item->text());
    }
}

void LogicEditorPanel::setupUi() {
    // 2. Central Widget (Splitter for Editor and Console)
    auto* central = new QWidget();
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_tabs = new QTabWidget();
    m_tabs->setStyleSheet(
        "QTabWidget::pane { border-top: 1px solid #3e3e42; background: #1e1e1e; }"
        "QTabBar::tab { background: #2d2d2d; color: #888888; padding: 8px 20px; border-right: 1px solid #3e3e42; }"
        "QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; }"
    );

    // 1. Sidebar (Project Explorer)
    auto* explorerDock = new QDockWidget("Block Explorer", this);
    explorerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_explorerList = new QListWidget();
    m_explorerList->setStyleSheet("background: #252526; color: #cccccc; border: none; font-size: 12px;");
    explorerDock->setWidget(m_explorerList);
    addDockWidget(Qt::LeftDockWidgetArea, explorerDock);
    connect(m_explorerList, &QListWidget::itemClicked, this, &LogicEditorPanel::onExplorerItemClicked);

    // Top Toolbar (IDE Style)
    auto* toolbar = new QFrame();
    toolbar->setFixedHeight(45);
    toolbar->setStyleSheet("background: #333333; border-bottom: 1px solid #3e3e42;");
    auto* toolLayout = new QHBoxLayout(toolbar);
    
    m_applyBtn = new QPushButton("RUN & DEPLOY");
    m_applyBtn->setStyleSheet("background: #0e639c; color: white; padding: 6px 15px; border-radius: 4px; font-weight: bold;");
    toolLayout->addWidget(m_applyBtn);

    m_bakeBtn = new QPushButton("BAKE TO SPICE");
    m_bakeBtn->setStyleSheet("background: #68217a; color: white; padding: 6px 15px; border-radius: 4px; font-weight: bold;");
    toolLayout->addWidget(m_bakeBtn);

    toolLayout->addSpacing(10);
    m_stepBtn = new QPushButton("STEP");
    m_stepBtn->setStyleSheet("background: #3c3c3c; color: #cccccc; padding: 6px 12px; border-radius: 4px;");
    toolLayout->addWidget(m_stepBtn);

    m_resumeBtn = new QPushButton("RESUME");
    m_resumeBtn->setStyleSheet("background: #3c3c3c; color: #cccccc; padding: 6px 12px; border-radius: 4px;");
    toolLayout->addWidget(m_resumeBtn);

    m_stopBtn = new QPushButton("STOP");
    m_stopBtn->setStyleSheet("background: #3c3c3c; color: #cccccc; padding: 6px 12px; border-radius: 4px;");
    toolLayout->addWidget(m_stopBtn);

    toolLayout->addStretch();
    m_engineLabel = new QLabel("Engine:");
    m_engineLabel->setStyleSheet("color: #888; font-weight: bold; margin-left: 10px;");
    toolLayout->addWidget(m_engineLabel);

    m_engineCombo = new QComboBox();
    m_engineCombo->addItem("FluxScript JIT", static_cast<int>(SmartSignalItem::EngineType::FluxScript));
    m_engineCombo->setEnabled(false);  // Only one engine — no choice to make
    toolLayout->addWidget(m_engineCombo);

    connect(m_engineCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LogicEditorPanel::onEngineChanged);
    
    mainLayout->addWidget(toolbar);

    // Tab 1: Logic Editor
    auto* logicTab = new QWidget();
    auto* logicLayout = new QVBoxLayout(logicTab);
    logicLayout->setContentsMargins(0, 0, 0, 0);
    logicLayout->setSpacing(0);

    // Vertical Splitter for Editor area and Console
    QSplitter* vSplitter = new QSplitter(Qt::Vertical);
    
    // Top Half: Editor + MiniScope
    auto* topHalf = new QWidget();
    auto* topLayout = new QHBoxLayout(topHalf);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(1);

    m_editor = new Flux::CodeEditor(m_scene, m_netManager, this);
    topLayout->addWidget(m_editor, 3);

    m_scope = new MiniScopeWidget(this);
    topLayout->addWidget(m_scope, 1);
    
    vSplitter->addWidget(topHalf);

    // 1b. Right Sidebar (Template Library & AI Copilot)
    auto* rightDockArea = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightDockArea);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_templateDock = new QDockWidget("Logic Templates", this);
    m_templateDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_templateList = new QListWidget();
    m_templateList->setStyleSheet("background: #252526; color: #4ec9b0; border: none; font-size: 12px; font-weight: bold;");
    m_templateDock->setWidget(m_templateList);
    addDockWidget(Qt::RightDockWidgetArea, m_templateDock);
    connect(m_templateList, &QListWidget::itemDoubleClicked, this, &LogicEditorPanel::onTemplateDoubleClicked);

    m_aiDock = new QDockWidget("AI Copilot", this);
    m_aiDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    m_geminiPanel = new GeminiPanel(m_scene, this);
    m_geminiPanel->setNetManager(m_netManager);
    m_geminiPanel->setMode("logic");
    
    // Provide live code context to Gemini
    m_geminiPanel->setContextProvider([this]() -> QString {
        return m_editor->toPlainText();
    });
    
    connect(m_geminiPanel, &GeminiPanel::pythonScriptGenerated, this, &LogicEditorPanel::onPythonGenerated);

    m_aiDock->setWidget(m_geminiPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_aiDock);

    // Bottom Half: Console
    m_console = new QTextEdit();
    m_console->setReadOnly(true);
    m_console->setPlaceholderText("Output Console...");
    m_console->setStyleSheet("background: #1e1e1e; color: #858585; font-family: 'Consolas', 'Courier New', monospace; font-size: 13px; border-top: 1px solid #333333; padding: 8px;");
    vSplitter->addWidget(m_console);
    
    vSplitter->setStretchFactor(0, 4);
    vSplitter->setStretchFactor(1, 1);
    logicLayout->addWidget(vSplitter);

    m_tabs->addTab(logicTab, "CODE EDITOR");

    // Tab 2: Pin Manager (Visual)
    auto* pinsTab = new QWidget();
    auto* pinsLayout = new QVBoxLayout(pinsTab);
    
    m_pinMapper = new VisualPinMapper();
    pinsLayout->addWidget(m_pinMapper, 1);

    auto* pinBtnLayout = new QHBoxLayout();
    auto* addInBtn = new QPushButton("+ Add Input");
    auto* addOutBtn = new QPushButton("+ Add Output");
    auto* remPinBtn = new QPushButton("- Remove Selected");
    
    addInBtn->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 6px;");
    addOutBtn->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 6px;");
    remPinBtn->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 6px;");
    
    pinBtnLayout->addWidget(addInBtn);
    pinBtnLayout->addWidget(addOutBtn);
    pinBtnLayout->addWidget(remPinBtn);
    pinBtnLayout->addStretch();
    pinsLayout->addLayout(pinBtnLayout);

    connect(addInBtn, &QPushButton::clicked, this, &LogicEditorPanel::addInputPin);
    connect(addOutBtn, &QPushButton::clicked, this, &LogicEditorPanel::addOutputPin);
    connect(remPinBtn, &QPushButton::clicked, this, &LogicEditorPanel::removeSelectedPin);
    connect(m_pinMapper, &VisualPinMapper::pinsChanged, this, &LogicEditorPanel::onVisualPinsChanged);

    m_tabs->addTab(pinsTab, "INTERFACE (PINS)");

    // Tab 3: Parameters
    m_paramsTab = new QWidget();
    m_paramsLayout = new QVBoxLayout(m_paramsTab);
    m_paramsLayout->setAlignment(Qt::AlignTop);
    m_tabs->addTab(m_paramsTab, "PARAMETERS");

    // Tab 4: Automated Testing
    auto* testsTab = new QWidget();
    auto* testsLayout = new QVBoxLayout(testsTab);
    
    m_testTable = new QTableWidget(0, 5);
    m_testTable->setHorizontalHeaderLabels({"Case Name", "Time (s)", "Inputs (JSON)", "Expected (JSON)", "Result"});
    m_testTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_testTable->setStyleSheet("background: #1e1e1e; color: #fff; gridline-color: #3e3e42;");
    testsLayout->addWidget(m_testTable);

    auto* testBtnLayout = new QHBoxLayout();
    auto* addTestBtn = new QPushButton("+ Add Test Case");
    auto* remTestBtn = new QPushButton("- Remove Selected");
    m_runTestsBtn = new QPushButton("RUN ALL TESTS");
    
    addTestBtn->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 6px;");
    remTestBtn->setStyleSheet("background: #3c3c3c; color: #ccc; padding: 6px;");
    m_runTestsBtn->setStyleSheet("background: #22c55e; color: #fff; font-weight: bold; padding: 6px 15px;");
    
    testBtnLayout->addWidget(addTestBtn);
    testBtnLayout->addWidget(remTestBtn);
    testBtnLayout->addStretch();
    testBtnLayout->addWidget(m_runTestsBtn);
    testsLayout->addLayout(testBtnLayout);

    connect(addTestBtn, &QPushButton::clicked, this, &LogicEditorPanel::addTestCase);
    connect(remTestBtn, &QPushButton::clicked, this, &LogicEditorPanel::removeTestCase);
    connect(m_runTestsBtn, &QPushButton::clicked, this, &LogicEditorPanel::runTests);

    m_tabs->addTab(testsTab, "TESTING");

    // Tab 5: Snapshots Gallery
    auto* snapTab = new QWidget();
    auto* snapLayout = new QVBoxLayout(snapTab);
    
    m_snapBtn = new QPushButton("CAPTURE SNAPSHOT");
    m_snapBtn->setStyleSheet("background: #007acc; color: #fff; font-weight: bold; padding: 10px; border-radius: 4px;");
    snapLayout->addWidget(m_snapBtn);
    
    m_snapList = new QListWidget();
    m_snapList->setStyleSheet("background: #1e1e1e; color: #ccc; border: 1px solid #3e3e42;");
    snapLayout->addWidget(m_snapList);
    
    QLabel* snapHint = new QLabel("Double-click to restore code");
    snapHint->setStyleSheet("color: #888; font-style: italic;");
    snapLayout->addWidget(snapHint);

    connect(m_snapBtn, &QPushButton::clicked, this, &LogicEditorPanel::onCaptureSnapshot);
    connect(m_snapList, &QListWidget::itemDoubleClicked, this, &LogicEditorPanel::onSnapshotDoubleClicked);

    m_tabs->addTab(snapTab, "SNAPSHOTS");

    mainLayout->addWidget(m_tabs);

    // Footer
    auto* footer = new QFrame();
    footer->setFixedHeight(22);
    footer->setStyleSheet("background: #007acc;");
    auto* footLayout = new QHBoxLayout(footer);
    footLayout->setContentsMargins(10, 0, 10, 0);
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setStyleSheet("color: white; font-size: 10px;");
    footLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(footer);

    connect(m_applyBtn, &QPushButton::clicked, this, &LogicEditorPanel::onApplyClicked);
    connect(m_bakeBtn, &QPushButton::clicked, this, &LogicEditorPanel::onBakeClicked);
    connect(m_stepBtn, &QPushButton::clicked, this, &LogicEditorPanel::onDebugStep);
    connect(m_resumeBtn, &QPushButton::clicked, this, &LogicEditorPanel::onDebugResume);
    connect(m_stopBtn, &QPushButton::clicked, this, &LogicEditorPanel::onDebugStop);

    connect(&Debugger::instance(), &Debugger::stateChanged, this, &LogicEditorPanel::onDebuggerStateChanged);
    connect(&Debugger::instance(), &Debugger::activeLineChanged, this, &LogicEditorPanel::onDebuggerActiveLineChanged);

    connect(m_editor, &Flux::CodeEditor::textChanged, [this]() { m_previewTimer->start(); });
}

void LogicEditorPanel::onDebugStep() {
    Debugger::instance().step();
    updatePreview(); // Trigger a one-step re-sim
}

void LogicEditorPanel::onDebugResume() {
    Debugger::instance().resume();
    updatePreview();
}

void LogicEditorPanel::onDebugStop() {
    Debugger::instance().stop();
    m_editor->setActiveDebugLine(-1);
}

void LogicEditorPanel::onDebuggerStateChanged(DebugState state) {
    bool paused = (state == DebugState::Paused);
    m_stepBtn->setEnabled(paused);
    m_resumeBtn->setEnabled(paused);
    m_stopBtn->setEnabled(state != DebugState::Stopped);
    
    if (paused) m_statusLabel->setText("Debugger: Paused");
    else if (state == DebugState::Running) m_statusLabel->setText("Debugger: Running");
    else m_statusLabel->setText("Debugger: Stopped");
}

void LogicEditorPanel::onDebuggerActiveLineChanged(int line) {
    m_editor->setActiveDebugLine(line);
}

void LogicEditorPanel::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("Deploy to Block", QKeySequence(QKeySequence::Save), this, &LogicEditorPanel::onApplyClicked);
    fileMenu->addAction("Close IDE", QKeySequence(), this, &LogicEditorPanel::closed);

    auto* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("Refresh Explorer", QKeySequence(QKeySequence::Refresh), this, &LogicEditorPanel::refreshExplorer);
}

void LogicEditorPanel::createShortcuts() {
    new QShortcut(QKeySequence("Ctrl+R"), this, SLOT(updatePreview()));
}

void LogicEditorPanel::refreshExplorer() {
    m_explorerList->clear();
    if (!m_scene) return;

    for (auto* item : m_scene->items()) {
        if (auto* smart = dynamic_cast<SmartSignalItem*>(item)) {
            auto* listItem = new QListWidgetItem(smart->reference() + " (" + smart->name() + ")");
            listItem->setData(Qt::UserRole, smart->id().toString());
            m_explorerList->addItem(listItem);
        }
    }
}

void LogicEditorPanel::runLinter() {
    QString code = m_editor->toPlainText();
    QString scriptPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../python/scripts/script_linter.py");
    if (!QFile::exists(scriptPath)) {
        scriptPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("python/scripts/script_linter.py");
    }

    // Use a temporary connection for the linter result
    auto* conn = new QMetaObject::Connection();
    *conn = connect(&FluxScriptManager::instance(), &FluxScriptManager::scriptOutput, this, [this, conn](const QString& output) {
        this->onLinterResult(output);
        disconnect(*conn);
        delete conn;
    });

    FluxScriptManager::instance().runScript(scriptPath, {code});
}

void LogicEditorPanel::onLinterResult(const QString& output) {
    QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
    if (!doc.isArray()) return;

    QMap<int, QString> errors;
    QJsonArray arr = doc.array();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        errors[obj["line"].toInt()] = obj["msg"].toString();
    }

    m_editor->setErrorLines(errors);
}

void LogicEditorPanel::onCodeChanged() {
    if (m_previewTimer) {
        m_previewTimer->start();
    }
}

void LogicEditorPanel::onExplorerItemClicked(QListWidgetItem* item) {
    if (!item || !m_scene) return;

    const QUuid targetId = QUuid::fromString(item->data(Qt::UserRole).toString());
    if (targetId.isNull()) return;

    for (auto* sceneItem : m_scene->items()) {
        if (auto* smart = dynamic_cast<SmartSignalItem*>(sceneItem)) {
            if (smart->id() == targetId) {
                setTargetBlock(smart);
                return;
            }
        }
    }
}

void LogicEditorPanel::onEngineChanged(int /* index */) {
    // Only FluxScript is available — no engine switching.
    // This slot exists for UI signal compatibility.
}

void LogicEditorPanel::setTargetBlock(SmartSignalItem* item) {
    if (m_targetBlock == item) {
        show(); raise(); activateWindow();
        return;
    }

    // --- Auto-Save ---
    saveCurrentToBlock();

    m_targetBlock = item;
    refreshExplorer();

    if (m_targetBlock) {
        setWindowTitle("viospice Logic IDE - [" + m_targetBlock->reference() + "]");

        m_engineCombo->blockSignals(true);
        m_engineCombo->setCurrentIndex(0);  // Always FluxScript
        m_engineCombo->blockSignals(false);

        m_editor->setPlainText(m_targetBlock->fluxCode());
        
        // Populate Pins
        m_pinMapper->setPins(m_targetBlock->inputPins(), m_targetBlock->outputPins());

        m_statusLabel->setText("Editing: " + m_targetBlock->reference());
        updateEditorKeywords();
        updateParametersTab();
        
        // Populate Test Cases
        m_testTable->setRowCount(0);
        for (const auto& tc : m_targetBlock->testCases()) {
            int row = m_testTable->rowCount();
            m_testTable->insertRow(row);
            m_testTable->setItem(row, 0, new QTableWidgetItem(tc.name));
            m_testTable->setItem(row, 1, new QTableWidgetItem(QString::number(tc.time)));
            
            QJsonObject inObj;
            for (auto it = tc.inputs.begin(); it != tc.inputs.end(); ++it) inObj[it.key()] = it.value();
            m_testTable->setItem(row, 2, new QTableWidgetItem(QJsonDocument(inObj).toJson(QJsonDocument::Compact)));
            
            QJsonObject outObj;
            for (auto it = tc.expectedOutputs.begin(); it != tc.expectedOutputs.end(); ++it) outObj[it.key()] = it.value();
            m_testTable->setItem(row, 3, new QTableWidgetItem(QJsonDocument(outObj).toJson(QJsonDocument::Compact)));
            
            auto* resultItem = new QTableWidgetItem("Untested");
            resultItem->setFlags(resultItem->flags() & ~Qt::ItemIsEditable);
            m_testTable->setItem(row, 4, resultItem);
        }
        
        refreshSnapshots();

        updatePreview();
        show();
        raise();
        activateWindow();
    } else {
        m_editor->clear();
        m_pinMapper->setPins({}, {});
        m_testTable->setRowCount(0);
        m_scope->clear();
        m_statusLabel->setText("No block selected");
    }
}

void LogicEditorPanel::updateEditorKeywords() {
    if (!m_targetBlock || !m_editor) return;

    QStringList pinKeywords;
    for (const QString& pin : m_targetBlock->inputPins()) {
        pinKeywords << "inputs['" + pin + "']";
        pinKeywords << "inputs.get('" + pin + "', 0.0)";
    }
    for (const QString& pin : m_targetBlock->outputPins()) {
        pinKeywords << "'" + pin + "'";
    }
    
    // Add common signal processing library functions
    pinKeywords << "np.sin" << "np.cos" << "np.pi" << "np.exp" << "np.linspace";
    pinKeywords << "scipy.signal" << "fft" << "ifft";

    m_editor->updateCompletionKeywords(pinKeywords);
}

void LogicEditorPanel::updateParametersTab() {
    if (!m_targetBlock) return;

    // Clear existing widgets
    QLayoutItem* item;
    while ((item = m_paramsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) delete item->widget();
        delete item;
    }

    auto params = m_targetBlock->parameters();
    
    // If no params, show a helpful message
    if (params.isEmpty()) {
        auto* label = new QLabel("No parameters detected in script.\nUse 'self.params = {\"freq\": 1000}' in your init() method.");
        label->setStyleSheet("color: #888; font-style: italic; padding: 20px;");
        m_paramsLayout->addWidget(label);
        return;
    }

    for (auto it = params.begin(); it != params.end(); ++it) {
        auto* row = new QWidget();
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 5, 0, 5);
        
        auto* label = new QLabel(it.key());
        label->setFixedWidth(120);
        label->setStyleSheet("color: #ccc; font-weight: bold;");
        
        auto* spin = new QDoubleSpinBox();
        spin->setRange(-1e12, 1e12);
        spin->setValue(it.value());
        spin->setStyleSheet("background: #1e1e1e; color: #fff; border: 1px solid #444; padding: 4px;");
        
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this, key = it.key()](double val) {
            if (m_targetBlock) {
                m_targetBlock->setParameter(key, val);
                updatePreview();
            }
        });
        
        rowLayout->addWidget(label);
        rowLayout->addWidget(spin, 1);
        m_paramsLayout->addWidget(row);
    }
    
    m_paramsLayout->addStretch();
}

void LogicEditorPanel::addTestCase() {
    int row = m_testTable->rowCount();
    m_testTable->insertRow(row);
    m_testTable->setItem(row, 0, new QTableWidgetItem(QString("Test Case %1").arg(row + 1)));
    m_testTable->setItem(row, 1, new QTableWidgetItem("0.0"));
    m_testTable->setItem(row, 2, new QTableWidgetItem("{}"));
    m_testTable->setItem(row, 3, new QTableWidgetItem("{\"out\": 1.0}"));
    auto* resultItem = new QTableWidgetItem("Untested");
    resultItem->setFlags(resultItem->flags() & ~Qt::ItemIsEditable);
    m_testTable->setItem(row, 4, resultItem);
}

void LogicEditorPanel::removeTestCase() {
    int row = m_testTable->currentRow();
    if (row >= 0) {
        m_testTable->removeRow(row);
    }
}

void LogicEditorPanel::runTests() {
    if (!m_targetBlock) return;
    m_console->append("<br><font color='#007acc'>[Tester] Starting Test Suite...</font>");
    QString code = m_editor->toPlainText();
    QMap<int, QString> errors;
    if (!Flux::JITContextManager::instance().compileAndLoad(m_targetBlock->reference(), code, errors)) {
        m_console->append("<font color='#f44747'><b>Linter:</b> Code has syntax errors, skipping tests.</font>");
        return;
    }
    m_console->append("<font color='#f44747'><b>Tester:</b> Automated testing not yet migrated to new JIT engine.</font>");
}

void LogicEditorPanel::onCaptureSnapshot() {
    if (!m_targetBlock) return;
    
    bool ok;
    QString name = QInputDialog::getText(this, "Capture Snapshot", "Enter version name:", QLineEdit::Normal, 
                                          QString("Revision %1").arg(m_targetBlock->snapshots().size() + 1), &ok);
    
    if (ok && !name.isEmpty()) {
        SmartSignalItem::Snapshot s;
        s.name = name;
        s.code = m_editor->toPlainText();
        s.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        
        auto snaps = m_targetBlock->snapshots();
        snaps.insert(0, s); // Add to front
        m_targetBlock->setSnapshots(snaps);
        
        refreshSnapshots();
        m_statusLabel->setText("Snapshot captured: " + name);
    }
}

void LogicEditorPanel::onSnapshotDoubleClicked(QListWidgetItem* item) {
    if (!m_targetBlock) return;
    
    int idx = m_snapList->row(item);
    auto snaps = m_targetBlock->snapshots();
    if (idx >= 0 && idx < snaps.size()) {
        if (QMessageBox::question(this, "Restore Snapshot", 
                                  "This will overwrite your current code with '" + snaps[idx].name + "'. Continue?") == QMessageBox::Yes) {
            m_editor->setPlainText(snaps[idx].code);
            updatePreview();
            m_statusLabel->setText("Restored from: " + snaps[idx].name);
        }
    }
}

void LogicEditorPanel::refreshSnapshots() {
    m_snapList->clear();
    if (!m_targetBlock) return;
    
    for (const auto& s : m_targetBlock->snapshots()) {
        auto* item = new QListWidgetItem(QString("%1 [%2]").arg(s.name, s.timestamp));
        item->setToolTip(s.code.left(200) + "...");
        m_snapList->addItem(item);
    }
}

void LogicEditorPanel::saveCurrentToBlock() {
    if (m_targetBlock && m_editor) {
        bool changed = false;

        // Save Code — always to FluxScript (Python is external orchestration)
        QString currentCode = m_editor->toPlainText();
        if (currentCode != m_targetBlock->fluxCode()) {
            m_targetBlock->setFluxCode(currentCode);
            changed = true;
        }

        // Save Pins
        if (m_pinMapper->inputPins() != m_targetBlock->inputPins()) {
            m_targetBlock->setInputPins(m_pinMapper->inputPins());
            changed = true;
        }

        if (m_pinMapper->outputPins() != m_targetBlock->outputPins()) {
            m_targetBlock->setOutputPins(m_pinMapper->outputPins());
            changed = true;
        }

        // Save Test Cases
        QList<SmartSignalItem::TestCase> testCases;
        for (int i = 0; i < m_testTable->rowCount(); ++i) {
            SmartSignalItem::TestCase tc;
            tc.name = m_testTable->item(i, 0)->text();
            tc.time = m_testTable->item(i, 1)->text().toDouble();
            
            QJsonDocument inDoc = QJsonDocument::fromJson(m_testTable->item(i, 2)->text().toUtf8());
            if (inDoc.isObject()) {
                QJsonObject obj = inDoc.object();
                for (const QString& k : obj.keys()) tc.inputs[k] = obj[k].toDouble();
            }
            
            QJsonDocument outDoc = QJsonDocument::fromJson(m_testTable->item(i, 3)->text().toUtf8());
            if (outDoc.isObject()) {
                QJsonObject obj = outDoc.object();
                for (const QString& k : obj.keys()) tc.expectedOutputs[k] = obj[k].toDouble();
            }
            testCases.append(tc);
        }
        m_targetBlock->setTestCases(testCases);

        if (changed) {
            m_statusLabel->setText("Auto-saved " + m_targetBlock->reference());
        }
    }
}

void LogicEditorPanel::flushEdits() {
    saveCurrentToBlock();
}

void LogicEditorPanel::addInputPin() {
    if (!m_targetBlock) return;
    QStringList pins = m_targetBlock->inputPins();
    pins << QString("In%1").arg(pins.count() + 1);
    m_targetBlock->setInputPins(pins);
    m_pinMapper->setPins(pins, m_targetBlock->outputPins());
    onPinsUpdated();
}

void LogicEditorPanel::addOutputPin() {
    if (!m_targetBlock) return;
    QStringList pins = m_targetBlock->outputPins();
    pins << QString("Out%1").arg(pins.count() + 1);
    m_targetBlock->setOutputPins(pins);
    m_pinMapper->setPins(m_targetBlock->inputPins(), pins);
    onPinsUpdated();
}

void LogicEditorPanel::removeSelectedPin() {
    // This is now harder since we don't have a "selection" in the mapper easily
    // We'll just remove the last pin of the active type for now or could enhance mapper
}

void LogicEditorPanel::onVisualPinsChanged() {
    if (m_targetBlock) {
        m_targetBlock->setInputPins(m_pinMapper->inputPins());
        m_targetBlock->setOutputPins(m_pinMapper->outputPins());
        onPinsUpdated();
    }
}

void LogicEditorPanel::onPinsUpdated() {
    if (m_targetBlock) {
        updateEditorKeywords();
        updatePreview();
    }
}

void LogicEditorPanel::closeEvent(QCloseEvent* event) {
    saveCurrentToBlock();
    Q_EMIT closed();
    QMainWindow::closeEvent(event);
}

void LogicEditorPanel::updatePreview() {
    QString code = m_editor->toPlainText();
    if (code.trimmed().isEmpty()) {
        m_scope->clear();
        m_editor->setErrorLines({});
        return;
    }

    if (m_targetBlock && m_targetBlock->engineType() == SmartSignalItem::EngineType::FluxScript) {
        m_console->clear();
        m_console->append("<font color='#569cd6'>[FluxScript] Starting JIT-optimized preview simulation...</font>");
        
        QElapsedTimer timer;
        timer.start();
        
        QMap<int, QString> errors;
        QString id = m_targetBlock ? m_targetBlock->reference() : "preview_block";
        if (Flux::JITContextManager::instance().compileAndLoad(id, code, errors)) {
            qint64 elapsed = timer.elapsed();
            m_console->append("<font color='#4ec9b0'>[JIT] Compilation successful in " + QString::number(elapsed) + "ms.</font>");
            m_statusLabel->setText("JIT Ready. Compilation: " + QString::number(elapsed) + "ms");
        } else {
            m_console->append("<font color='#f44747'>[JIT ERROR] Compilation failed.</font>");
            m_editor->setErrorLines(errors);
        }
    }
}

void LogicEditorPanel::onPythonGenerated(const QString& code) {
    if (!code.isEmpty()) {
        m_editor->setPlainText(code);
        updatePreview();
        m_statusLabel->setText("Code updated by AI Copilot.");
    }
}

void LogicEditorPanel::onApplyClicked() {
    if (!m_targetBlock) return;
    
    saveCurrentToBlock();
    
    if (m_targetBlock->engineType() == SmartSignalItem::EngineType::FluxScript) {
        m_console->append("<font color='#007acc'>[FluxScript] Compiling JIT Module for deployment...</font>");
        QMap<int, QString> errors;
        if (Flux::JITContextManager::instance().compileAndLoad(m_targetBlock->reference(), m_targetBlock->fluxCode(), errors)) {
            m_console->append("<font color='#22c55e'>[JIT] Deployment Success! Current engine: FluxScript.</font>");
            m_statusLabel->setText("Deployed FluxScript to " + m_targetBlock->reference());
        } else {
            const QString error = errors.isEmpty() ? QStringLiteral("Compilation failed.") : errors.constBegin().value();
            m_console->append("<font color='#f44747'>[JIT ERROR] " + error + "</font>");
        }
    } else {
        m_console->append("<font color='#4ec9b0'>[System] Successfully deployed logic to " + m_targetBlock->reference() + "</font>");
        m_statusLabel->setText("Deployed Python to " + m_targetBlock->reference());
    }
    
    m_targetBlock->update();
}

void LogicEditorPanel::onBakeClicked() {
    if (!m_targetBlock) return;
    
    saveCurrentToBlock();
    m_statusLabel->setText("Baking to SPICE...");
    m_console->append("<font color='#68217a'>[Baker] Analyzing Python logic for SPICE translation...</font>");

    QString code = m_editor->toPlainText();
    QString blockName = m_targetBlock->name().replace(" ", "_");
    
    QJsonArray inArr, outArr;
    for(const auto& p : m_targetBlock->inputPins()) inArr.append(p);
    for(const auto& p : m_targetBlock->outputPins()) outArr.append(p);
    
    QString inJson = QJsonDocument(inArr).toJson(QJsonDocument::Compact);
    QString outJson = QJsonDocument(outArr).toJson(QJsonDocument::Compact);

    QString scriptPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../python/scripts/spice_baker.py");
    if (!QFile::exists(scriptPath)) {
        scriptPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("python/scripts/spice_baker.py");
    }

    QStringList args;
    args << code << blockName << inJson << outJson;

    auto* conn = new QMetaObject::Connection();
    *conn = connect(&FluxScriptManager::instance(), &FluxScriptManager::scriptOutput, this, [this, conn, blockName](const QString& output) {
        disconnect(*conn);
        delete conn;
        
        if (output.startsWith("*")) {
            QString path = QFileDialog::getSaveFileName(this, "Save Baked SPICE Model", blockName.toLower() + ".sub", "SPICE Models (*.sub *.lib *.txt)");
            if (!path.isEmpty()) {
                QFile file(path);
                if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream(&file) << output;
                    m_console->append("<font color='#4ec9b0'>[Baker] Successfully baked to: " + path + "</font>");
                    m_statusLabel->setText("Bake Successful.");
                }
            }
        } else {
            m_console->append("<font color='#f44747'><b>Bake Error:</b> " + output + "</font>");
            m_statusLabel->setText("Bake Failed.");
        }
    });

    FluxScriptManager::instance().runScript(scriptPath, args);
}
void LogicEditorPanel::onAiPromptReturn() {}
